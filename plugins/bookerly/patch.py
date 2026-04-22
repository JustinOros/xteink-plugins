#!/usr/bin/env python3

import os
import sys
import glob
import shutil
import hashlib
import subprocess


def read_file(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()

def write_file(path, content):
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

def find_first(filename, repo_dir):
    results = glob.glob(os.path.join(repo_dir, "**", filename), recursive=True)
    if not results:
        sys.exit(f"ERROR: Could not locate {filename} in {repo_dir}")
    return results[0]

def font_id_from_files(paths):
    total = 0
    for p in paths:
        h = hashlib.sha256(open(p, "rb").read()).hexdigest()
        total += int(h, 16)
    return total % (2 ** 32) - (2 ** 31)


def generate_fonts(plugin_dir, repo_dir):
    scripts_dir = os.path.join(repo_dir, "lib", "EpdFont", "scripts")
    fonts_dir   = os.path.join(repo_dir, "lib", "EpdFont", "builtinFonts")
    source_dir  = os.path.join(plugin_dir, "fonts")
    fontconvert = os.path.join(scripts_dir, "fontconvert.py")

    subprocess.run(
        [sys.executable, "-m", "pip", "install", "freetype-py==2.5.1", "fonttools", "--break-system-packages", "-q"],
        check=True
    )

    sizes  = [12, 14, 16, 18]
    styles = ["Regular", "Bold", "Italic", "BoldItalic"]

    for size in sizes:
        for style in styles:
            fname    = f"bookerly_{size}_{style.lower()}"
            out_path = os.path.join(fonts_dir, f"{fname}.h")
            if os.path.exists(out_path):
                print(f"    already exists: {fname}.h")
                continue
            src_path = os.path.join(source_dir, f"Bookerly-{style}.ttf")
            if not os.path.exists(src_path):
                sys.exit(f"ERROR: Missing font file: {src_path}")
            with open(out_path, "w") as f:
                result = subprocess.run(
                    [sys.executable, fontconvert, fname, str(size), src_path, "--2bit", "--compress"],
                    capture_output=True, text=True
                )
                if result.returncode != 0 or not result.stdout.strip():
                    print(f"    ERROR generating {fname}: {result.stderr[:200]}")
                    if os.path.exists(out_path):
                        os.remove(out_path)
                    continue
                f.write(result.stdout)
            print(f"    ✓ generated {fname}.h")

    return fonts_dir


def compute_font_ids(fonts_dir):
    ids = {}
    sizes  = [12, 14, 16, 18]
    suffixes = ["regular", "bold", "italic", "bolditalic"]
    for size in sizes:
        paths = [os.path.join(fonts_dir, f"bookerly_{size}_{s}.h") for s in suffixes]
        if all(os.path.exists(p) for p in paths):
            ids[f"BOOKERLY_{size}_FONT_ID"] = font_id_from_files(paths)
    return ids


def patch_font_ids_h(repo_dir, new_ids):
    path    = find_first("fontIds.h", repo_dir)
    content = read_file(path)

    lines = []
    for name, val in new_ids.items():
        if name not in content:
            lines.append(f"#define {name} ({val})")

    if not lines:
        print("  fontIds.h already patched, skipping.")
        return

    write_file(path, content.rstrip() + "\n" + "\n".join(lines) + "\n")
    print(f"  fontIds.h: added {len(lines)} new font IDs.")


def patch_all_h(repo_dir, new_ids):
    path    = find_first("all.h", repo_dir)
    content = read_file(path)

    added = 0
    sizes  = [12, 14, 16, 18]
    suffixes = ["bold", "bolditalic", "italic", "regular"]
    for size in sizes:
        key = f"BOOKERLY_{size}_FONT_ID"
        if key not in new_ids:
            continue
        for suffix in suffixes:
            fname = f"bookerly_{size}_{suffix}"
            inc   = f'#include <builtinFonts/{fname}.h>'
            if inc not in content:
                content += inc + "\n"
                added += 1

    if added:
        write_file(path, content)
        print(f"  all.h: added {added} includes.")
    else:
        print("  all.h already patched, skipping.")


def patch_main_cpp(repo_dir, new_ids):
    path    = find_first("main.cpp", repo_dir)
    content = read_file(path)

    sizes = [8, 10, 12, 14, 16, 18]
    decl_lines   = []
    insert_lines = []

    for size in sizes:
        key     = f"BOOKERLY_{size}_FONT_ID"
        varbase = f"bookerly{size}"
        if key not in new_ids:
            continue
        if f"EpdFontFamily {varbase}FontFamily" in content:
            continue
        decl_lines += [
            f'EpdFont {varbase}RegularFont(&bookerly_{size}_regular);',
            f'EpdFont {varbase}BoldFont(&bookerly_{size}_bold);',
            f'EpdFont {varbase}ItalicFont(&bookerly_{size}_italic);',
            f'EpdFont {varbase}BoldItalicFont(&bookerly_{size}_bolditalic);',
            f'EpdFontFamily {varbase}FontFamily(&{varbase}RegularFont, &{varbase}BoldFont, &{varbase}ItalicFont, &{varbase}BoldItalicFont);',
        ]
        insert_lines.append(f'  renderer.insertFont({key}, {varbase}FontFamily);')

    if not decl_lines:
        print("  main.cpp already patched, skipping.")
        return

    anchor_decl = '#ifndef OMIT_FONTS'
    content = content.replace(anchor_decl, "\n".join(decl_lines) + "\n" + anchor_decl, 1)

    anchor_insert = '  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);'
    content = content.replace(anchor_insert, "\n".join(insert_lines) + "\n" + anchor_insert, 1)

    write_file(path, content)
    print(f"  main.cpp: added {len(decl_lines) // 5} font families.")


def patch_cross_point_settings_h(repo_dir):
    path    = find_first("CrossPointSettings.h", repo_dir)
    content = read_file(path)

    if "bookerlyStyle" in content:
        print("  CrossPointSettings.h already patched, skipping.")
        return

    if '"activities/settings/BookerlyPlugin.h"' not in content:
        content = content.replace(
            '#include "activities/settings/SmallerFontsPlugin.h"',
            '#include "activities/settings/SmallerFontsPlugin.h"\n#include "activities/settings/BookerlyPlugin.h"'
        )

    if 'BOOKERLY = 3' not in content:
        content = content.replace(
            'enum FONT_FAMILY { NOTOSERIF = 0, NOTOSANS = 1, OPENDYSLEXIC = 2, FONT_FAMILY_COUNT };',
            'enum FONT_FAMILY { NOTOSERIF = 0, NOTOSANS = 1, OPENDYSLEXIC = 2, BOOKERLY = 3, FONT_FAMILY_COUNT };'
        )

    if 'uint8_t bookerlyStyle' not in content:
        anchor = None
        for candidate in [
            '  uint8_t smallerFontsMode = 0;',
            '  uint8_t imageRendering = IMAGES_DISPLAY;',
        ]:
            if candidate in content:
                anchor = candidate
                break
        if anchor:
            content = content.replace(anchor, anchor + '\n  uint8_t bookerlyStyle = 0;')

    write_file(path, content)
    print("  CrossPointSettings.h patched.")


def patch_cross_point_settings_cpp(repo_dir, new_ids):
    path    = find_first("CrossPointSettings.cpp", repo_dir)
    content = read_file(path)

    if "BOOKERLY" in content:
        print("  CrossPointSettings.cpp already patched, skipping.")
        return

    content = content.replace(
        'readAndValidate(inputFile, fontFamily, FONT_FAMILY_COUNT);\n    if (++settingsRead >= fileSettingsCount) break;',
        'readAndValidate(inputFile, fontFamily, FONT_FAMILY_COUNT);\n    if (++settingsRead >= fileSettingsCount) break;\n    readAndValidate(inputFile, bookerlyStyle, 4);\n    if (++settingsRead >= fileSettingsCount) break;'
    )

    content = content.replace(
        '    case OPENDYSLEXIC:\n      switch (lineSpacing) {\n        case TIGHT:\n          return 0.90f;\n        case NORMAL:\n        default:\n          return 0.95f;\n        case WIDE:\n          return 1.0f;\n      }\n  }\n}\n\nunsigned long',
        '    case OPENDYSLEXIC:\n      switch (lineSpacing) {\n        case TIGHT:\n          return 0.90f;\n        case NORMAL:\n        default:\n          return 0.95f;\n        case WIDE:\n          return 1.0f;\n      }\n    case BOOKERLY:\n      switch (lineSpacing) {\n        case TIGHT:\n          return 0.95f;\n        case NORMAL:\n        default:\n          return 1.0f;\n        case WIDE:\n          return 1.1f;\n      }\n  }\n}\n\nunsigned long'
    )

    bookerly_case = (
        '    case BOOKERLY:\n'
        '      switch (fontSize) {\n'
        '        case SMALL:\n'
        '          return resolve(BOOKERLY_12_FONT_ID);\n'
        '        case MEDIUM:\n'
        '        default:\n'
        '          return resolve(BOOKERLY_14_FONT_ID);\n'
        '        case LARGE:\n'
        '          return resolve(BOOKERLY_16_FONT_ID);\n'
        '        case EXTRA_LARGE:\n'
        '          return resolve(BOOKERLY_18_FONT_ID);\n'
        '      }\n'
    )

    content = content.replace(
        '        case EXTRA_LARGE:\n          return resolve(OPENDYSLEXIC_14_FONT_ID);\n      }\n  }\n}\n',
        '        case EXTRA_LARGE:\n          return resolve(OPENDYSLEXIC_14_FONT_ID);\n      }\n'
        + bookerly_case +
        '  }\n}\n'
    )

    write_file(path, content)
    print("  CrossPointSettings.cpp patched.")


def patch_settings_list_h(repo_dir):
    path    = find_first("SettingsList.h", repo_dir)
    content = read_file(path)

    if 'STR_NONE_OPT}, "fontFamily"' in content:
        print("  SettingsList.h already patched, skipping.")
        return

    content = content.replace(
        '{StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS, StrId::STR_OPEN_DYSLEXIC}, "fontFamily"',
        '{StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS, StrId::STR_OPEN_DYSLEXIC, StrId::STR_NONE_OPT}, "fontFamily"'
    )

    write_file(path, content)
    print("  SettingsList.h patched.")


def patch_settings_activity_cpp(repo_dir):
    path    = find_first("SettingsActivity.cpp", repo_dir)
    content = read_file(path)

    if "bookerlyStyle" in content:
        print("  SettingsActivity.cpp already patched, skipping.")
        return

    content = content.replace(
        '#include "DarkModePlugin.h"',
        '#include "DarkModePlugin.h"\n#include "BookerlyPlugin.h"'
    )

    content = content.replace(
        '  pluginsSettings.push_back(SettingInfo::Enum(\n'
        '    StrId::STR_NONE_OPT, &CrossPointSettings::smallerFontsMode,\n'
        '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "smallerFontsMode"\n'
        '  ));\n',
        '  pluginsSettings.push_back(SettingInfo::Enum(\n'
        '    StrId::STR_NONE_OPT, &CrossPointSettings::smallerFontsMode,\n'
        '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "smallerFontsMode"\n'
        '  ));\n'
        '  pluginsSettings.push_back(SettingInfo::Enum(\n'
        '    StrId::STR_NONE_OPT, &CrossPointSettings::bookerlyStyle,\n'
        '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "bookerlyStyle"\n'
        '  ));\n'
    )

    content = content.replace(
        '        if (s.key && std::string(s.key) == "smallerFontsMode") return "Smaller Fonts";\n',
        '        if (s.key && std::string(s.key) == "smallerFontsMode") return "Smaller Fonts";\n'
        '        if (s.key && std::string(s.key) == "bookerlyStyle") return "Bookerly Font";\n'
    )

    content = content.replace(
        '          } else if (setting.key && std::string(setting.key) == "smallerFontsMode") {\n'
        '            valueText = SmallerFontsPlugin::modeName(static_cast<SmallerFontsMode>(value));\n'
        '          } else {\n',
        '          } else if (setting.key && std::string(setting.key) == "smallerFontsMode") {\n'
        '            valueText = SmallerFontsPlugin::modeName(static_cast<SmallerFontsMode>(value));\n'
        '          } else if (setting.key && std::string(setting.key) == "bookerlyStyle") {\n'
        '            valueText = BookerlyPlugin::styleName(static_cast<BookerlyStyle>(value));\n'
        '          } else {\n'
    )

    content = content.replace(
        '        if (s.key && std::string(s.key) == "fontFamily") return "Bookerly";\n',
        ''
    )

    content = content.replace(
        '      return std::string(I18N.get(settings[index].nameId));\n'
        '    },',
        '      if (settings[index].key && std::string(settings[index].key) == "fontFamily") {\n'
        '        const uint8_t val = SETTINGS.*(settings[index].valuePtr);\n'
        '        if (val == CrossPointSettings::BOOKERLY) return std::string(I18N.get(settings[index].nameId)) + " (Bookerly)";\n'
        '      }\n'
        '      return std::string(I18N.get(settings[index].nameId));\n'
        '    },'
    )

    write_file(path, content)
    print("  SettingsActivity.cpp patched.")


def patch_web_server(repo_dir):
    path    = find_first("CrossPointWebServer.cpp", repo_dir)
    content = read_file(path)

    if "bookerlyStyle" in content:
        print("  CrossPointWebServer.cpp already patched, skipping.")
        return

    content = content.replace(
        '      } else if (strcmp(s.key, "smallerFontsMode") == 0) {\n'
        '        doc["name"] = "Smaller Fonts";\n'
        '        doc["category"] = "Plugins";\n'
        '      }\n'
        '    }\n',
        '      } else if (strcmp(s.key, "smallerFontsMode") == 0) {\n'
        '        doc["name"] = "Smaller Fonts";\n'
        '        doc["category"] = "Plugins";\n'
        '      } else if (strcmp(s.key, "bookerlyStyle") == 0) {\n'
        '        doc["name"] = "Bookerly Font";\n'
        '        doc["category"] = "Plugins";\n'
        '      } else if (strcmp(s.key, "fontFamily") == 0) {\n'
        '        JsonArray opts = doc["options"].to<JsonArray>();\n'
        '        opts.add("Noto Serif");\n'
        '        opts.add("Noto Sans");\n'
        '        opts.add("OpenDyslexic");\n'
        '        opts.add("Bookerly");\n'
        '      }\n'
        '    }\n'
    )

    content = content.replace(
        '          } else if (strcmp(s.key, "smallerFontsMode") == 0) {\n'
        '            JsonArray opts = doc["options"].to<JsonArray>();\n'
        '            opts.add("Disabled");\n'
        '            opts.add("Smaller");\n'
        '            opts.add("Smallest");\n'
        '          }\n'
        '        }\n',
        '          } else if (strcmp(s.key, "smallerFontsMode") == 0) {\n'
        '            JsonArray opts = doc["options"].to<JsonArray>();\n'
        '            opts.add("Disabled");\n'
        '            opts.add("Smaller");\n'
        '            opts.add("Smallest");\n'
        '          } else if (strcmp(s.key, "bookerlyStyle") == 0) {\n'
        '            JsonArray opts = doc["options"].to<JsonArray>();\n'
        '            opts.add("Regular");\n'
        '            opts.add("Italic");\n'
        '            opts.add("Bold");\n'
        '            opts.add("Bold Italic");\n'
        '          }\n'
        '        }\n'
    )

    write_file(path, content)
    print("  CrossPointWebServer.cpp patched.")


def copy_plugin_sources(plugin_dir, repo_dir):
    dest = os.path.join(repo_dir, "src", "activities", "settings")
    os.makedirs(dest, exist_ok=True)

    for fname in os.listdir(plugin_dir):
        if fname.endswith((".h", ".cpp", ".hpp")):
            src = os.path.join(plugin_dir, fname)
            dst = os.path.join(dest, fname)
            shutil.copy2(src, dst)
            print(f"    ✓ {fname}")


def patch(repo_dir: str):
    plugin_dir = os.path.dirname(os.path.abspath(__file__))

    print("  Copying plugin sources...")
    copy_plugin_sources(plugin_dir, repo_dir)

    print("  Generating Bookerly font files...")
    fonts_dir = generate_fonts(plugin_dir, repo_dir)

    print("  Computing font IDs...")
    new_ids = compute_font_ids(fonts_dir)
    for k, v in new_ids.items():
        print(f"    {k} = {v}")

    print("  Patching fontIds.h...")
    patch_font_ids_h(repo_dir, new_ids)

    print("  Patching all.h...")
    patch_all_h(repo_dir, new_ids)

    print("  Patching main.cpp...")
    patch_main_cpp(repo_dir, new_ids)

    print("  Patching CrossPointSettings.h...")
    patch_cross_point_settings_h(repo_dir)

    print("  Patching CrossPointSettings.cpp...")
    patch_cross_point_settings_cpp(repo_dir, new_ids)

    print("  Patching SettingsList.h...")
    patch_settings_list_h(repo_dir)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_activity_cpp(repo_dir)

    print("  Patching CrossPointWebServer.cpp (web UI)...")
    patch_web_server(repo_dir)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python patch.py <path-to-crosspoint-repo>")
    patch(sys.argv[1])
