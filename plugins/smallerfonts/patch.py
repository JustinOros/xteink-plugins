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


def generate_fonts(repo_dir):
    scripts_dir   = os.path.join(repo_dir, "lib", "EpdFont", "scripts")
    fonts_dir     = os.path.join(repo_dir, "lib", "EpdFont", "builtinFonts")
    source_dir    = os.path.join(fonts_dir, "source")
    fontconvert   = os.path.join(scripts_dir, "fontconvert.py")

    subprocess.run(
        [sys.executable, "-m", "pip", "install", "freetype-py==2.5.1", "fonttools", "--break-system-packages", "-q"],
        check=True
    )

    new_fonts = [
        ("bookerly", 8,  "Bookerly", "ttf", ["Regular", "Bold", "Italic", "BoldItalic"]),
        ("bookerly", 10, "Bookerly", "ttf", ["Regular", "Bold", "Italic", "BoldItalic"]),
    ]

    generated = []
    for family, size, folder, ext, styles in new_fonts:
        for style in styles:
            fname    = f"{family}_{size}_{style.lower()}"
            out_path = os.path.join(fonts_dir, f"{fname}.h")
            if os.path.exists(out_path):
                print(f"    already exists: {fname}.h")
                generated.append((family, size, style, out_path))
                continue
            src_path = os.path.join(source_dir, folder, f"{folder}-{style}.{ext}")
            extra = ["--2bit", "--compress"] if family != "ubuntu" else []
            with open(out_path, "w") as f:
                result = subprocess.run(
                    [sys.executable, fontconvert, fname, str(size), src_path] + extra,
                    capture_output=True, text=True
                )
                if result.returncode != 0:
                    print(f"    ERROR generating {fname}: {result.stderr[:200]}")
                    if os.path.exists(out_path):
                        os.remove(out_path)
                    continue
                if not result.stdout.strip():
                    print(f"    ERROR: {fname} generated empty output")
                    if os.path.exists(out_path):
                        os.remove(out_path)
                    continue
                f.write(result.stdout)
            print(f"    ✓ generated {fname}.h")
            generated.append((family, size, style, out_path))

    return fonts_dir


def compute_new_font_ids(fonts_dir):
    ids = {}

    def fid(name, size, suffixes):
        paths = [os.path.join(fonts_dir, f"{name}_{size}_{s}.h") for s in suffixes]
        if all(os.path.exists(p) for p in paths):
            ids[f"{name.upper()}_{size}_FONT_ID"] = font_id_from_files(paths)

    fid("bookerly", 8,  ["regular", "bold", "italic", "bolditalic"])
    fid("bookerly", 10, ["regular", "bold", "italic", "bolditalic"])

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

    includes = {
        "BOOKERLY_8_FONT_ID":  ["bookerly_8_bold", "bookerly_8_bolditalic", "bookerly_8_italic", "bookerly_8_regular"],
        "BOOKERLY_10_FONT_ID": ["bookerly_10_bold", "bookerly_10_bolditalic", "bookerly_10_italic", "bookerly_10_regular"],
    }

    added = 0
    for key, fnames in includes.items():
        if key not in new_ids:
            continue
        for fname in fnames:
            inc = f'#include <builtinFonts/{fname}.h>'
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

    font_defs = {
        "BOOKERLY_8_FONT_ID":  ("bookerly", 8,  True),
        "BOOKERLY_10_FONT_ID": ("bookerly", 10, True),
    }

    decl_lines = []
    insert_lines = []

    for key, (family, size, has_styles) in font_defs.items():
        if key not in new_ids:
            continue
        varbase = f"{family}{size}"
        if f"EpdFontFamily {varbase}FontFamily" in content:
            continue

        decl_lines += [
            f'EpdFont {varbase}RegularFont(&{family}_{size}_regular);',
            f'EpdFont {varbase}BoldFont(&{family}_{size}_bold);',
            f'EpdFont {varbase}ItalicFont(&{family}_{size}_italic);',
            f'EpdFont {varbase}BoldItalicFont(&{family}_{size}_bolditalic);',
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
    print(f"  main.cpp: added {len(decl_lines)//5} font families.")


def copy_plugin_sources(plugin_dir, repo_dir):
    dest = os.path.join(repo_dir, "src", "activities", "settings")
    os.makedirs(dest, exist_ok=True)

    skip = {"SmallerFontsSettingsPage.h", "SmallerFontsSettingsPage.cpp"}

    for fname in os.listdir(plugin_dir):
        if fname.endswith((".h", ".cpp", ".hpp")) and fname not in skip:
            src = os.path.join(plugin_dir, fname)
            dst = os.path.join(dest, fname)
            shutil.copy2(src, dst)
            print(f"    ✓ {fname}")


def patch_cross_point_settings_h(repo_dir):
    path    = find_first("CrossPointSettings.h", repo_dir)
    content = read_file(path)

    if "smallerFontsMode" in content:
        print("  CrossPointSettings.h already patched, skipping.")
        return

    content = content.replace(
        '#pragma once\n#include <HalStorage.h>',
        '#pragma once\n#include <HalStorage.h>\n#include "activities/settings/SmallerFontsPlugin.h"'
    )
    content = content.replace(
        '  uint8_t imageRendering = IMAGES_DISPLAY;',
        '  uint8_t imageRendering = IMAGES_DISPLAY;\n  uint8_t smallerFontsMode = 0;'
    )

    write_file(path, content)
    print("  CrossPointSettings.h patched.")


def patch_cross_point_settings_cpp(repo_dir):
    path    = find_first("CrossPointSettings.cpp", repo_dir)
    content = read_file(path)

    if "resolveReaderFontId" in content:
        print("  CrossPointSettings.cpp already patched, skipping.")
        return

    content = content.replace(
        'int CrossPointSettings::getReaderFontId() const {',
        'int CrossPointSettings::getReaderFontId() const {\n  auto resolve = [this](int baseId) {\n    return SmallerFontsPlugin::resolveReaderFontId(baseId, static_cast<SmallerFontsMode>(smallerFontsMode));\n  };\n'
    )
    for name in ["BOOKERLY_12", "BOOKERLY_14", "BOOKERLY_16", "BOOKERLY_18",
                 "NOTOSANS_12", "NOTOSANS_14", "NOTOSANS_16", "NOTOSANS_18",
                 "OPENDYSLEXIC_8", "OPENDYSLEXIC_10", "OPENDYSLEXIC_12", "OPENDYSLEXIC_14"]:
        content = content.replace(f'return {name}_FONT_ID;', f'return resolve({name}_FONT_ID);')

    write_file(path, content)
    print("  CrossPointSettings.cpp patched.")


def patch_settings_list_h(repo_dir):
    path    = find_first("SettingsList.h", repo_dir)
    content = read_file(path)

    if "smallerFontsMode" in content:
        print("  SettingsList.h already patched, skipping.")
        return

    entry = (
        '      SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::smallerFontsMode,\n'
        '                        {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "smallerFontsMode"),\n'
    )

    content = content.replace(
        '  };\n  return list;\n}\n',
        entry + '  };\n  return list;\n}\n'
    )

    write_file(path, content)
    print("  SettingsList.h patched.")


def patch_settings_h(repo_dir):
    path    = find_first("SettingsActivity.h", repo_dir)
    content = read_file(path)

    already_has_plugins_tab = "categoryCount = 5" in content
    already_has_plugins_vec = "pluginsSettings" in content

    if already_has_plugins_tab and already_has_plugins_vec:
        print("  SettingsActivity.h already patched, skipping.")
        return

    if '"SmallerFontsPlugin.h"' not in content:
        content = content.replace(
            '#include <I18n.h>',
            '#include <I18n.h>\n#include "SmallerFontsPlugin.h"'
        )

    if not already_has_plugins_tab:
        content = content.replace(
            'static constexpr int categoryCount = 4;',
            'static constexpr int categoryCount = 5;'
        )

    if not already_has_plugins_vec:
        content = content.replace(
            '  std::vector<SettingInfo> systemSettings;',
            '  std::vector<SettingInfo> systemSettings;\n  std::vector<SettingInfo> pluginsSettings;'
        )

    write_file(path, content)
    print("  SettingsActivity.h patched.")


def patch_settings_cpp(repo_dir):
    path    = find_first("SettingsActivity.cpp", repo_dir)
    content = read_file(path)

    if "SmallerFontsPlugin::modeName" in content and "smallerFontsMode" in content:
        print("  SettingsActivity.cpp already patched, skipping.")
        return

    if "STR_NONE_OPT" not in content:
        content = content.replace(
            'const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,\n'
            '                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};',
            'const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,\n'
            '                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM,\n'
            '                                                              StrId::STR_NONE_OPT};'
        )

    if "pluginsSettings.clear();" not in content:
        content = content.replace(
            '  systemSettings.clear();',
            '  systemSettings.clear();\n  pluginsSettings.clear();'
        )

    plugins_block = (
        '  pluginsSettings.push_back(SettingInfo::Enum(\n'
        '    StrId::STR_NONE_OPT, &CrossPointSettings::darkModeState,\n'
        '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "darkModeState"\n'
        '  ));\n'
        '  pluginsSettings.push_back(SettingInfo::Enum(\n'
        '    StrId::STR_NONE_OPT, &CrossPointSettings::smallerFontsMode,\n'
        '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "smallerFontsMode"\n'
        '  ));\n'
    )

    sf_only_block = (
        '  pluginsSettings.push_back(SettingInfo::Enum(\n'
        '    StrId::STR_NONE_OPT, &CrossPointSettings::smallerFontsMode,\n'
        '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "smallerFontsMode"\n'
        '  ));\n'
    )

    broken_old_block = (
        '  pluginsSettings.push_back(SettingInfo::Enum(\n'
        '    StrId::STR_NONE_OPT,\n'
        '    &CrossPointSettings::smallerFontsMode,\n'
        '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT},\n'
        '    "smallerFontsMode"\n'
        '  ));\n'
    )

    if broken_old_block in content:
        content = content.replace(broken_old_block, sf_only_block)
    elif '"darkModeState"' in content and '"smallerFontsMode"' not in content:
        content = content.replace(
            '  pluginsSettings.push_back(SettingInfo::Enum(\n'
            '    StrId::STR_NONE_OPT, &CrossPointSettings::darkModeState,\n'
            '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "darkModeState"\n'
            '  ));\n',
            plugins_block
        )
    elif '"smallerFontsMode"' not in content:
        content = content.replace(
            '  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));\n',
            '  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));\n'
            + sf_only_block
        )

    if 'case 4:\n        currentSettings = &pluginsSettings;' not in content:
        content = content.replace(
            '      case 3:\n        currentSettings = &systemSettings;\n        break;',
            '      case 3:\n        currentSettings = &systemSettings;\n        break;\n'
            '      case 4:\n        currentSettings = &pluginsSettings;\n        break;'
        )

    if '"Plugins"' not in content:
        content = content.replace(
            '  std::vector<TabInfo> tabs;\n'
            '  tabs.reserve(categoryCount);\n'
            '  for (int i = 0; i < categoryCount; i++) {\n'
            '    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});\n'
            '  }',
            '  std::vector<TabInfo> tabs;\n'
            '  tabs.reserve(categoryCount);\n'
            '  for (int i = 0; i < categoryCount; i++) {\n'
            '    const char* tabLabel = (i == 4) ? "Plugins" : I18N.get(categoryNames[i]);\n'
            '    tabs.push_back({tabLabel, selectedCategoryIndex == i});\n'
            '  }'
        )

    if 'nextCatLabel' not in content:
        content = content.replace(
            '  const auto confirmLabel = (selectedSettingIndex == 0)\n'
            '                                ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])\n'
            '                                : tr(STR_TOGGLE);',
            '  const int nextCatIndex = (selectedCategoryIndex + 1) % categoryCount;\n'
            '  const char* nextCatLabel = (nextCatIndex == 4) ? "Plugins" : I18N.get(categoryNames[nextCatIndex]);\n'
            '  const auto confirmLabel = (selectedSettingIndex == 0) ? nextCatLabel : tr(STR_TOGGLE);'
        )

    if 'selectedCategoryIndex == 4' not in content:
        content = content.replace(
            '    [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); },',
            '    [&settings, this](int index) -> std::string {\n'
            '      if (selectedCategoryIndex == 4) {\n'
            '        const auto& s = settings[index];\n'
            '        if (s.key && std::string(s.key) == "darkModeState") return "Dark Mode";\n'
            '        if (s.key && std::string(s.key) == "smallerFontsMode") return "Smaller Fonts";\n'
            '      }\n'
            '      return std::string(I18N.get(settings[index].nameId));\n'
            '    },'
        )

    if "SmallerFontsPlugin::modeName" not in content:
        if "DarkModePlugin::stateName" in content:
            content = content.replace(
                '          if (setting.key && std::string(setting.key) == "darkModeState") {\n'
                '            valueText = DarkModePlugin::stateName(static_cast<DarkModeState>(value));\n'
                '          } else {\n'
                '            valueText = I18N.get(setting.enumValues[value]);\n'
                '          }',
                '          if (setting.key && std::string(setting.key) == "darkModeState") {\n'
                '            valueText = DarkModePlugin::stateName(static_cast<DarkModeState>(value));\n'
                '          } else if (setting.key && std::string(setting.key) == "smallerFontsMode") {\n'
                '            valueText = SmallerFontsPlugin::modeName(static_cast<SmallerFontsMode>(value));\n'
                '          } else {\n'
                '            valueText = I18N.get(setting.enumValues[value]);\n'
                '          }'
            )
        else:
            content = content.replace(
                '          valueText = I18N.get(setting.enumValues[value]);',
                '          if (setting.key && std::string(setting.key) == "smallerFontsMode") {\n'
                '            valueText = SmallerFontsPlugin::modeName(static_cast<SmallerFontsMode>(value));\n'
                '          } else {\n'
                '            valueText = I18N.get(setting.enumValues[value]);\n'
                '          }'
            )

    write_file(path, content)
    print("  SettingsActivity.cpp patched.")


def patch_web_server(repo_dir):
    path    = find_first("CrossPointWebServer.cpp", repo_dir)
    content = read_file(path)

    if '"Dark Mode"' in content or '"Smaller Fonts"' in content:
        print("  CrossPointWebServer.cpp already patched, skipping.")
        return

    name_overrides = (
        '\n'
        '    if (s.key) {\n'
        '      if (strcmp(s.key, "darkModeState") == 0) {\n'
        '        doc["name"] = "Dark Mode";\n'
        '        doc["category"] = "Plugins";\n'
        '      } else if (strcmp(s.key, "smallerFontsMode") == 0) {\n'
        '        doc["name"] = "Smaller Fonts";\n'
        '        doc["category"] = "Plugins";\n'
        '      }\n'
        '    }\n'
    )

    content = content.replace(
        '    doc["category"] = I18N.get(s.category);\n'
        '\n'
        '    switch (s.type) {',
        '    doc["category"] = I18N.get(s.category);\n'
        + name_overrides +
        '\n'
        '    switch (s.type) {'
    )

    enum_overrides = (
        '        if (s.key) {\n'
        '          if (strcmp(s.key, "darkModeState") == 0) {\n'
        '            JsonArray opts = doc["options"].to<JsonArray>();\n'
        '            opts.add("Disabled");\n'
        '            opts.add("Enabled");\n'
        '          } else if (strcmp(s.key, "smallerFontsMode") == 0) {\n'
        '            JsonArray opts = doc["options"].to<JsonArray>();\n'
        '            opts.add("Disabled");\n'
        '            opts.add("Smaller");\n'
        '            opts.add("Smallest");\n'
        '          }\n'
        '        }\n'
    )

    content = content.replace(
        '        for (const auto& opt : s.enumValues) {\n'
        '          options.add(I18N.get(opt));\n'
        '        }\n'
        '        break;\n',
        '        for (const auto& opt : s.enumValues) {\n'
        '          options.add(I18N.get(opt));\n'
        '        }\n'
        + enum_overrides +
        '        break;\n'
    )

    write_file(path, content)
    print("  CrossPointWebServer.cpp patched.")


def patch(repo_dir: str):
    plugin_dir = os.path.dirname(os.path.abspath(__file__))

    print("  Copying plugin sources...")
    copy_plugin_sources(plugin_dir, repo_dir)

    print("  Generating smaller font files...")
    fonts_dir = generate_fonts(repo_dir)

    print("  Computing new font IDs...")
    new_ids = compute_new_font_ids(fonts_dir)
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
    patch_cross_point_settings_cpp(repo_dir)

    print("  Patching SettingsList.h...")
    patch_settings_list_h(repo_dir)

    print("  Patching SettingsActivity.h...")
    patch_settings_h(repo_dir)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_cpp(repo_dir)

    print("  Patching CrossPointWebServer.cpp (web UI)...")
    patch_web_server(repo_dir)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python patch.py <path-to-crosspoint-repo>")
    patch(sys.argv[1])
