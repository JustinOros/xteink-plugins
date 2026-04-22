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
    styles = ["Regular"]

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
    sizes = [12, 14, 16, 18]
    for size in sizes:
        path = os.path.join(fonts_dir, f"bookerly_{size}_regular.h")
        if os.path.exists(path):
            h = hashlib.sha256(open(path, "rb").read()).hexdigest()
            ids[f"BOOKERLY_{size}_FONT_ID"] = int(h, 16) % (2 ** 32) - (2 ** 31)
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
    for size in sizes:
        key = f"BOOKERLY_{size}_FONT_ID"
        if key not in new_ids:
            continue
        fname = f"bookerly_{size}_regular"
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
            f'EpdFontFamily {varbase}FontFamily(&{varbase}RegularFont, &{varbase}RegularFont, &{varbase}RegularFont, &{varbase}RegularFont);',
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
    print(f"  main.cpp: added {len(decl_lines) // 2} font families.")


def patch_cross_point_settings_h(repo_dir):
    path    = find_first("CrossPointSettings.h", repo_dir)
    content = read_file(path)

    if "BOOKERLY = 3" in content:
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



    write_file(path, content)
    print("  CrossPointSettings.h patched.")


def patch_cross_point_settings_cpp(repo_dir, new_ids):
    path    = find_first("CrossPointSettings.cpp", repo_dir)
    content = read_file(path)

    if "BOOKERLY" in content:
        print("  CrossPointSettings.cpp already patched, skipping.")
        return


    content = content.replace(
        '    case OPENDYSLEXIC:\n      switch (lineSpacing) {\n        case TIGHT:\n          return 0.90f;\n        case NORMAL:\n        default:\n          return 0.95f;\n        case WIDE:\n          return 1.0f;\n      }\n  }\n}\n\nunsigned long',
        '    case OPENDYSLEXIC:\n      switch (lineSpacing) {\n        case TIGHT:\n          return 0.90f;\n        case NORMAL:\n        default:\n          return 0.95f;\n        case WIDE:\n          return 1.0f;\n      }\n    case BOOKERLY:\n      switch (lineSpacing) {\n        case TIGHT:\n          return 0.95f;\n        case NORMAL:\n        default:\n          return 1.0f;\n        case WIDE:\n          return 1.1f;\n      }\n  }\n}\n\nunsigned long'
    )

    bookerly_case = (
        '    case BOOKERLY:\n'
        '      return resolve(BOOKERLY_12_FONT_ID);\n'
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


def patch_settings_activity_h(repo_dir):
    path    = find_first("SettingsActivity.h", repo_dir)
    content = read_file(path)

    if 'categoryCount = 5' in content:
        print("  SettingsActivity.h already patched, skipping.")
        return

    content = content.replace(
        'static constexpr int categoryCount = 4;',
        'static constexpr int categoryCount = 5;'
    )

    if 'pluginsSettings' not in content:
        content = content.replace(
            '  std::vector<SettingInfo> systemSettings;',
            '  std::vector<SettingInfo> systemSettings;\n  std::vector<SettingInfo> pluginsSettings;'
        )

    write_file(path, content)
    print("  SettingsActivity.h patched.")


def patch_settings_activity_cpp(repo_dir):
    path    = find_first("SettingsActivity.cpp", repo_dir)
    content = read_file(path)

    if "BookerlyPlugin" in content:
        print("  SettingsActivity.cpp already patched, skipping.")
        return

    content = content.replace(
        '#include "DarkModePlugin.h"',
        '#include "DarkModePlugin.h"\n#include "BookerlyPlugin.h"'
    )

    if 'STR_NONE_OPT};' not in content:
        content = content.replace(
            'const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,\n'
            '                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};',
            'const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,\n'
            '                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM,\n'
            '                                                              StrId::STR_NONE_OPT};'
        )

    if 'pluginsSettings.clear();' not in content:
        content = content.replace(
            '  systemSettings.clear();',
            '  systemSettings.clear();\n  pluginsSettings.clear();'
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

    if '"Bookerly"' not in content:
        content = content.replace(
            '    [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); },',
            '    [&settings, this](int index) -> std::string {\n'
            '      if (settings[index].key && std::string(settings[index].key) == "fontFamily") {\n'
            '        const uint8_t val = SETTINGS.*(settings[index].valuePtr);\n'
            '        if (val == CrossPointSettings::BOOKERLY) return "Bookerly";\n'
            '      }\n'
            '      if (selectedCategoryIndex == 4) {\n'
            '        const auto& s = settings[index];\n'
            '        if (s.key && std::string(s.key) == "darkModeState") return "Dark Mode";\n'
            '        if (s.key && std::string(s.key) == "smallerFontsMode") return "Smaller Fonts";\n'
            '      }\n'
            '      return std::string(I18N.get(settings[index].nameId));\n'
            '    },'
        )

    content = content.replace(
        '          } else {\n'
        '            valueText = I18N.get(setting.enumValues[value]);\n'
        '          }',
        '          } else if (setting.key && std::string(setting.key) == "fontFamily" && value == CrossPointSettings::BOOKERLY) {\n'
        '            valueText = "Bookerly";\n'
        '          } else {\n'
        '            valueText = I18N.get(setting.enumValues[value]);\n'
        '          }'
    )

    write_file(path, content)
    print("  SettingsActivity.cpp patched.")


def patch_web_server(repo_dir):
    path    = find_first("CrossPointWebServer.cpp", repo_dir)
    content = read_file(path)

    if '"Bookerly"' in content:
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
        '      } else if (strcmp(s.key, "fontFamily") == 0) {\n'
        '        JsonArray opts = doc["options"].to<JsonArray>();\n'
        '        opts.add("Noto Serif");\n'
        '        opts.add("Noto Sans");\n'
        '        opts.add("OpenDyslexic");\n'
        '        opts.add("Bookerly");\n'
        '      }\n'
        '    }\n'
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

    print("  Patching SettingsActivity.h...")
    patch_settings_activity_h(repo_dir)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_activity_cpp(repo_dir)

    print("  Patching CrossPointWebServer.cpp (web UI)...")
    patch_web_server(repo_dir)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python patch.py <path-to-crosspoint-repo>")
    patch(sys.argv[1])
