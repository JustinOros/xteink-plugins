import sys
import os
import re
import hashlib
import subprocess

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, PluginsTabEntry, EnumValueOverride, WebOptionAppend, SettingActionEnumValue,
)
from framework.engine import find_first, read_file, write_file, PatchError, replace_once, contains_once

PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))

# NOTE: this only extends the *static* built-in font-family list
# (SettingsList.h's plain SettingInfo::Enum entry). CrossPoint also has a
# second, SD-card-font-aware variant (buildFontFamilySetting()) that swaps in
# when the user has SD-card fonts installed - Bookerly won't appear there.
# That's a narrow pre-existing edge case (SD-card fonts + Bookerly at once)
# left as a known follow-up rather than something this pass fully covers.


def _generate_fonts(ctx):
    fonts_dir = os.path.join(ctx.repo_dir, "lib", "EpdFont", "builtinFonts")
    scripts_dir = os.path.join(ctx.repo_dir, "lib", "EpdFont", "scripts")
    source_dir = os.path.join(PLUGIN_DIR, "fonts")
    fontconvert = os.path.join(scripts_dir, "fontconvert.py")

    subprocess.run(
        [sys.executable, "-m", "pip", "install", "freetype-py==2.5.1", "fonttools", "--break-system-packages", "-q"],
        check=True,
    )

    sizes = [12, 14, 16, 18]
    for size in sizes:
        fname = f"bookerly_{size}_regular"
        out_path = os.path.join(fonts_dir, f"{fname}.h")
        if os.path.exists(out_path):
            print(f"    already exists: {fname}.h")
            continue
        src_path = os.path.join(source_dir, "Bookerly-Regular.ttf")
        if not os.path.exists(src_path):
            raise PatchError(f"Missing font file: {src_path}")
        result = subprocess.run(
            [sys.executable, fontconvert, fname, str(size), src_path, "--2bit", "--compress"],
            capture_output=True, text=True,
        )
        if result.returncode != 0 or not result.stdout.strip():
            raise PatchError(f"Failed generating {fname}: {result.stderr[:300]}")
        with open(out_path, "w") as f:
            f.write(result.stdout)
        print(f"    ✓ generated {fname}.h")

    ids = {}
    for size in sizes:
        path = os.path.join(fonts_dir, f"bookerly_{size}_regular.h")
        h = hashlib.sha256(open(path, "rb").read()).hexdigest()
        ids[f"BOOKERLY_{size}_FONT_ID"] = int(h, 16) % (2 ** 32) - (2 ** 31)
    ctx.bookerly_font_ids = ids


def _patch_font_registration(ctx):
    ids = ctx.bookerly_font_ids

    # fontIds.h
    path = find_first("fontIds.h", ctx.repo_dir)
    content = read_file(path)
    lines = [f"#define {name} ({val})" for name, val in ids.items() if name not in content]
    if lines:
        write_file(path, content.rstrip() + "\n" + "\n".join(lines) + "\n")
        print(f"  fontIds.h: added {len(lines)} font ID(s).")

    # all.h
    path = find_first("all.h", ctx.repo_dir)
    content = read_file(path)
    added = 0
    for size in [12, 14, 16, 18]:
        inc = f"#include <builtinFonts/bookerly_{size}_regular.h>"
        if inc not in content:
            content += inc + "\n"
            added += 1
    if added:
        write_file(path, content)
        print(f"  all.h: added {added} include(s).")

    # main.cpp: font family declarations + registration
    path = find_first("main.cpp", ctx.repo_dir)
    content = read_file(path)
    decl_lines, insert_lines = [], []
    for size in [12, 14, 16, 18]:
        var = f"bookerly{size}"
        if f"EpdFontFamily {var}FontFamily" in content:
            continue
        decl_lines += [
            f"EpdFont {var}RegularFont(&bookerly_{size}_regular);",
            f"EpdFontFamily {var}FontFamily(&{var}RegularFont, &{var}RegularFont, &{var}RegularFont, &{var}RegularFont);",
        ]
        insert_lines.append(f"  renderer.insertFont(BOOKERLY_{size}_FONT_ID, {var}FontFamily);")
    if decl_lines:
        # "#ifndef OMIT_FONTS" legitimately appears twice (global font
        # declarations, then again inside setup() around insertFont calls) -
        # we specifically want the *first* (global declaration) occurrence,
        # which is what replace(..., count=1) targets.
        anchor_decl = "#ifndef OMIT_FONTS"
        if anchor_decl not in content:
            raise PatchError("main.cpp: could not find `#ifndef OMIT_FONTS` (font declarations section)")
        content = content.replace(anchor_decl, "\n".join(decl_lines) + "\n" + anchor_decl, 1)

        anchor_insert = "  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);"
        contains_once(content, anchor_insert, "main.cpp")
        content = content.replace(anchor_insert, "\n".join(insert_lines) + "\n" + anchor_insert, 1)

        write_file(path, content)
        print(f"  main.cpp: registered {len(decl_lines) // 2} Bookerly size(s).")


def _patch_font_family_enum(ctx):
    path = find_first("CrossPointSettings.h", ctx.repo_dir)
    content = read_file(path)
    if "BOOKERLY" in content:
        return
    m = re.search(r"enum FONT_FAMILY \{(.*?)FONT_FAMILY_COUNT\s*\};", content)
    if not m:
        raise PatchError("CrossPointSettings.h: could not find `enum FONT_FAMILY { ... FONT_FAMILY_COUNT };`")
    content = content[:m.start()] + m.group(0).replace("FONT_FAMILY_COUNT", "BOOKERLY, FONT_FAMILY_COUNT", 1) + content[m.end():]
    write_file(path, content)
    print("  CrossPointSettings.h: added FONT_FAMILY::BOOKERLY.")


def _patch_line_spacing_and_reader_font_id(ctx):
    path = find_first("CrossPointSettings.cpp", ctx.repo_dir)
    content = read_file(path)
    label = "CrossPointSettings.cpp"

    if "case BOOKERLY:" not in content:
        spacing_anchor = (
            "    case NOTOSANS:\n"
            "      switch (lineSpacing) {\n"
            "        case TIGHT:\n"
            "          return 0.90f;\n"
            "        case NORMAL:\n"
            "        default:\n"
            "          return 0.95f;\n"
            "        case WIDE:\n"
            "          return 1.0f;\n"
            "      }\n"
            "  }\n"
            "}\n"
        )
        spacing_replacement = (
            "    case NOTOSANS:\n"
            "      switch (lineSpacing) {\n"
            "        case TIGHT:\n"
            "          return 0.90f;\n"
            "        case NORMAL:\n"
            "        default:\n"
            "          return 0.95f;\n"
            "        case WIDE:\n"
            "          return 1.0f;\n"
            "      }\n"
            "    case BOOKERLY:\n"
            "      switch (lineSpacing) {\n"
            "        case TIGHT:\n"
            "          return 0.95f;\n"
            "        case NORMAL:\n"
            "        default:\n"
            "          return 1.0f;\n"
            "        case WIDE:\n"
            "          return 1.1f;\n"
            "      }\n"
            "  }\n"
            "}\n"
        )
        content = replace_once(content, spacing_anchor, spacing_replacement, label)

    if "BOOKERLY_12_FONT_ID" not in content:
        font_anchor = (
            "    case NOTOSANS:\n"
            "      switch (fontSize) {\n"
            "        case SMALL:\n"
            "          return NOTOSANS_12_FONT_ID;\n"
            "        case MEDIUM:\n"
            "        default:\n"
            "          return NOTOSANS_14_FONT_ID;\n"
            "        case LARGE:\n"
            "          return NOTOSANS_16_FONT_ID;\n"
            "        case EXTRA_LARGE:\n"
            "          return NOTOSANS_18_FONT_ID;\n"
            "      }\n"
            "  }\n"
            "}\n"
        )
        font_replacement = (
            "    case NOTOSANS:\n"
            "      switch (fontSize) {\n"
            "        case SMALL:\n"
            "          return NOTOSANS_12_FONT_ID;\n"
            "        case MEDIUM:\n"
            "        default:\n"
            "          return NOTOSANS_14_FONT_ID;\n"
            "        case LARGE:\n"
            "          return NOTOSANS_16_FONT_ID;\n"
            "        case EXTRA_LARGE:\n"
            "          return NOTOSANS_18_FONT_ID;\n"
            "      }\n"
            "    case BOOKERLY:\n"
            "      switch (fontSize) {\n"
            "        case SMALL:\n"
            "          return BOOKERLY_12_FONT_ID;\n"
            "        case MEDIUM:\n"
            "        default:\n"
            "          return BOOKERLY_14_FONT_ID;\n"
            "        case LARGE:\n"
            "          return BOOKERLY_16_FONT_ID;\n"
            "        case EXTRA_LARGE:\n"
            "          return BOOKERLY_18_FONT_ID;\n"
            "      }\n"
            "  }\n"
            "}\n"
        )
        content = replace_once(content, font_anchor, font_replacement, label)

    write_file(path, content)
    print("  CrossPointSettings.cpp: Bookerly line-spacing + font-id cases added.")


def _patch_settings_list_option(ctx):
    path = find_first("SettingsList.h", ctx.repo_dir)
    content = read_file(path)
    label = "SettingsList.h"
    anchor = (
        'SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,\n'
        '                          {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER),'
    )
    if "StrId::STR_NONE_OPT}, \"fontFamily\"" in content:
        return
    replacement = (
        'SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,\n'
        '                          {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS, StrId::STR_NONE_OPT}, "fontFamily", StrId::STR_CAT_READER),'
    )
    content = replace_once(content, anchor, replacement, label)
    write_file(path, content)
    print("  SettingsList.h: fontFamily gained a 3rd (Bookerly) option.")


def _pre_patch(ctx):
    print("  Generating Bookerly font files...")
    _generate_fonts(ctx)


def _post_patch(ctx):
    print("  Registering Bookerly font family...")
    _patch_font_registration(ctx)
    _patch_font_family_enum(ctx)
    _patch_line_spacing_and_reader_font_id(ctx)
    _patch_settings_list_option(ctx)


def get_manifest(ctx):
    return PluginManifest(
        name="bookerly",
        pretty_name="Bookerly Font",
        phase=0,  # must run before smallerfonts (phase 1) wraps getReaderFontId's returns
        source_files=[
            SourceFile("BookerlyPlugin.h", "src/activities/settings"),
            SourceFile("BookerlyPlugin.cpp", "src/activities/settings"),
        ],
        setting_actions=[
            SettingActionEnumValue("BookerlyInstalled"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Bookerly Font",
                kind="action",
                action_name="BookerlyInstalled",
                action_value_text="Installed",
                activity_launch_expr=None,  # informational row only, matches original behaviour
            ),
        ],
        enum_value_overrides=[
            EnumValueOverride(key="fontFamily", condition_expr="value == CrossPointSettings::BOOKERLY", text="Bookerly"),
        ],
        web_option_appends=[
            WebOptionAppend(key="fontFamily", label="Bookerly"),
        ],
        pre_patch=_pre_patch,
        post_patch=_post_patch,
    )
