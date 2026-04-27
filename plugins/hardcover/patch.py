#!/usr/bin/env python3

import os
import sys
import glob
import shutil

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

def copy_plugin_sources(plugin_dir, repo_dir):
    dest = os.path.join(repo_dir, "src", "activities", "settings")
    os.makedirs(dest, exist_ok=True)
    for fname in os.listdir(plugin_dir):
        if fname.endswith((".h", ".cpp", ".hpp")):
            src = os.path.join(plugin_dir, fname)
            dst = os.path.join(dest, fname)
            shutil.copy2(src, dst)
            print(f"    \u2713 {fname}")

def patch_cross_point_settings_h(repo_dir, token=""):
    path    = find_first("CrossPointSettings.h", repo_dir)
    content = read_file(path)

    if "hardcoverApiToken" in content:
        print("  CrossPointSettings.h already patched, skipping.")
        return

    safe_token = token.replace('\\', '\\\\').replace('"', '\\"')

    content = content.replace(
        '  uint8_t imageRendering = IMAGES_DISPLAY;',
        f'  uint8_t imageRendering = IMAGES_DISPLAY;\n  char    hardcoverApiToken[640] = "{safe_token}";'
    )

    write_file(path, content)
    print(f"  CrossPointSettings.h patched{' (token embedded)' if token else ' (no token)'}.") 

def patch_settings_list_h(repo_dir):
    path    = find_first("SettingsList.h", repo_dir)
    content = read_file(path)

    if "hardcoverApiToken" in content:
        print("  SettingsList.h already patched, skipping.")
        return

    content = content.replace(
        '  };\n  return list;\n}\n',
        '      SettingInfo::String(StrId::STR_NONE_OPT, SETTINGS.hardcoverApiToken,\n'
        '                          sizeof(SETTINGS.hardcoverApiToken), "hardcoverApiToken")\n'
        '          .withObfuscated(),\n'
        '  };\n  return list;\n}\n'
    )

    write_file(path, content)
    print("  SettingsList.h patched.")

def patch_setting_action_enum(repo_dir):
    path    = find_first("SettingsActivity.h", repo_dir)
    content = read_file(path)

    if "HardcoverSync" in content:
        print("  SettingAction enum already patched, skipping.")
        return

    for anchor in [
        '  Language,\n};',
        '  BookerlyInstalled,\n};',
        '  GitHubSync,\n  Language,\n};',
        '  Language,\n  BookerlyInstalled,\n};',
        '  GitHubSync,\n  Language,\n  BookerlyInstalled,\n};',
    ]:
        if anchor in content:
            content = content.replace(anchor, anchor[:-2] + '  HardcoverSync,\n};')
            break
    else:
        import re
        content = re.sub(r'(\n};)', r'\n  HardcoverSync,\1', content, count=1)

    write_file(path, content)
    print("  SettingAction::HardcoverSync added.")

def patch_settings_h(repo_dir):
    path    = find_first("SettingsActivity.h", repo_dir)
    content = read_file(path)

    already_has_tab = "categoryCount = 5" in content
    already_has_vec = "pluginsSettings" in content

    if already_has_tab and already_has_vec:
        print("  SettingsActivity.h already patched, skipping.")
        return

    if not already_has_tab:
        content = content.replace(
            'static constexpr int categoryCount = 4;',
            'static constexpr int categoryCount = 5;'
        )

    if not already_has_vec:
        content = content.replace(
            '  std::vector<SettingInfo> systemSettings;',
            '  std::vector<SettingInfo> systemSettings;\n  std::vector<SettingInfo> pluginsSettings;'
        )

    write_file(path, content)
    print("  SettingsActivity.h patched.")

def patch_settings_cpp(repo_dir):
    path    = find_first("SettingsActivity.cpp", repo_dir)
    content = read_file(path)

    if "HardcoverPlugin::syncProgress" in content:
        print("  SettingsActivity.cpp already patched, skipping.")
        return

    content = content.replace(
        '#include "SettingsList.h"',
        '#include "SettingsList.h"\n#include "HardcoverPlugin.h"\n#include "HardcoverSyncActivity.h"'
    )

    if 'StrId::STR_NONE_OPT};' not in content and 'STR_NONE_OPT' not in content.split('categoryNames')[1].split(';')[0]:
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

    hardcover_push = (
        '  pluginsSettings.push_back(SettingInfo::Action(\n'
        '    StrId::STR_NONE_OPT, SettingAction::HardcoverSync\n'
        '  ));\n'
    )

    if 'SettingAction::HardcoverSync' not in content:
        anchors = [

            '  pluginsSettings.push_back(SettingInfo::Enum(\n'
            '    StrId::STR_NONE_OPT, &CrossPointSettings::smallerFontsMode,\n'
            '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "smallerFontsMode"\n'
            '  ));\n',

            '  pluginsSettings.push_back(SettingInfo::Enum(\n'
            '    StrId::STR_NONE_OPT, &CrossPointSettings::darkModeState,\n'
            '    {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "darkModeState"\n'
            '  ));\n',

            '  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));\n',
        ]
        for anchor in anchors:
            if anchor in content:
                content = content.replace(anchor, anchor + hardcover_push)
                break

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

    hardcover_label_line = (
        '        if (s.type == SettingType::ACTION &&\n'
        '            s.action == SettingAction::HardcoverSync) return "Hardcover Progress";\n'
    )

    if 'Hardcover Progress' not in content:
        if 'selectedCategoryIndex == 4' in content:

            closing = (
                '      }\n'
                '      return std::string(I18N.get(settings[index].nameId));\n'
                '    }, nullptr, nullptr,'
            )
            closing_bookerly = (
                '        }\n'
                '        return std::string(I18N.get(settings[index].nameId));\n'
                '      }, nullptr, nullptr,'
            )
            for last_key_line in [
                '        if (s.key && std::string(s.key) == "smallerFontsMode") return "Smaller Fonts";\n',
                '        if (s.key && std::string(s.key) == "darkModeState") return "Dark Mode";\n',
                '            s.action == SettingAction::GitHubSync) return "GitHub Sync";\n',
                '            s.action == SettingAction::BookerlyInstalled) return "Bookerly Font";\n',
            ]:
                for cl in [closing, closing_bookerly]:
                    anchor = last_key_line + cl
                    if anchor in content:
                        content = content.replace(anchor, last_key_line + hardcover_label_line + cl)
                        break
                else:
                    continue
                break
            else:
                for cl in [closing, closing_bookerly]:
                    if cl in content:
                        content = content.replace(cl, hardcover_label_line + cl, 1)
                        break
        else:

            old_lambda = (
                '      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,'
            )
            new_lambda = (
                '      [&settings, this](int index) -> std::string {\n'
                '      if (selectedCategoryIndex == 4) {\n'
                '        const auto& s = settings[index];\n'
                + hardcover_label_line +
                '      }\n'
                '      return std::string(I18N.get(settings[index].nameId));\n'
                '    }, nullptr, nullptr,'
            )
            content = content.replace(old_lambda, new_lambda)

    hardcover_value_block = (
        '        if (setting.type == SettingType::ACTION &&\n'
        '            setting.action == SettingAction::HardcoverSync) {\n'
        '          valueText = "Sync";\n'
        '        } else '
    )
    if 'SettingAction::HardcoverSync) {\n          valueText = "Sync"' not in content:
        for value_anchor in [
            '        if (setting.type == SettingType::ACTION &&\n'
            '            setting.action == SettingAction::GoodreadsSync) {\n'
            '          valueText = "Sync";\n'
            '        } else if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {',
            '        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {',
        ]:
            if value_anchor in content:
                content = content.replace(value_anchor, hardcover_value_block + value_anchor)
                break

    hardcover_case = (
        '      case SettingAction::HardcoverSync:\n'
        '        startActivityForResult(std::make_unique<HardcoverSyncActivity>(renderer, mappedInput), resultHandler);\n'
        '        break;\n'
    )
    if 'case SettingAction::HardcoverSync:' not in content:
        for anchor in [
            '      case SettingAction::GoodreadsSync:\n'
            '        startActivityForResult(std::make_unique<GoodreadsSyncActivity>(renderer, mappedInput), resultHandler);\n'
            '        break;\n'
            '      case SettingAction::None:',
            '      case SettingAction::Language:\n'
            '        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);\n'
            '        break;\n'
            '      case SettingAction::None:',
        ]:
            if anchor in content:
                content = content.replace(anchor, hardcover_case + anchor)
                break

    write_file(path, content)
    print("  SettingsActivity.cpp patched.")

def patch_web_server(repo_dir):
    path    = find_first("CrossPointWebServer.cpp", repo_dir)
    content = read_file(path)

    if '"hardcoverApiToken"' in content or '"Hardcover API Token"' in content:
        print("  CrossPointWebServer.cpp already patched, skipping.")
        return

    hardcover_block = (
        '      if (strcmp(s.key, "hardcoverApiToken") == 0) {\n'
        '        doc["name"]     = "Hardcover API Token";\n'
        '        doc["category"] = "Plugins";\n'
        '      }\n'
    )

    if 'if (s.key) {' in content and 'darkModeState' in content:
        content = content.replace(
            '      if (strcmp(s.key, "darkModeState") == 0) {\n',
            hardcover_block +
            '      if (strcmp(s.key, "darkModeState") == 0) {\n'
        )
    else:

        content = content.replace(
            '    doc["category"] = I18N.get(s.category);\n'
            '\n'
            '    switch (s.type) {',
            '    doc["category"] = I18N.get(s.category);\n'
            '\n'
            '    if (s.key) {\n'
            + hardcover_block +
            '    }\n'
            '\n'
            '    switch (s.type) {'
        )

    write_file(path, content)
    print("  CrossPointWebServer.cpp patched.")

def patch(repo_dir: str, yes_all: bool = False):
    plugin_dir = os.path.dirname(os.path.abspath(__file__))

    hardcover_file = os.path.expanduser("~/.hardcover")
    token = ""

    if os.path.exists(hardcover_file):
        raw = open(hardcover_file).read().strip()
        if raw.startswith("Bearer "):
            raw = raw[len("Bearer "):]
        token = raw

    if token:
        print(f"  Using Hardcover API token from ~/.hardcover.")
    else:
        print("  Hardcover API token required.")
        print("  Get yours from https://hardcover.app/account/api")
        print("  Paste your Hardcover API token below.")
        while not token:
            raw = input("  Hardcover API token: ").strip()
            if raw.startswith("Bearer "):
                raw = raw[len("Bearer "):]
            token = raw
            if not token:
                print("  Token cannot be empty. Please enter your Hardcover API token.")
        with open(hardcover_file, "w") as f:
            f.write(f"Bearer {token}\n")
        print(f"  Token saved to ~/.hardcover for future installs.")

    if len(token) >= 640:
        sys.exit(f"ERROR: Hardcover API token is too long ({len(token)} chars, max 639).")

    print("  Copying plugin sources...")
    copy_plugin_sources(plugin_dir, repo_dir)

    print("  Patching CrossPointSettings.h...")
    patch_cross_point_settings_h(repo_dir, token)

    print("  Patching SettingsList.h...")
    patch_settings_list_h(repo_dir)

    print("  Patching SettingAction enum...")
    patch_setting_action_enum(repo_dir)

    print("  Patching SettingsActivity.h...")
    patch_settings_h(repo_dir)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_cpp(repo_dir)

    print("  Patching CrossPointWebServer.cpp (web UI)...")
    patch_web_server(repo_dir)

    import glob as _glob
    cpp_results = _glob.glob(os.path.join(repo_dir, "**", "SettingsActivity.cpp"), recursive=True)
    if cpp_results:
        cpp_content = open(cpp_results[0]).read()
        checks = [
            ("label lambda: 'Hardcover Progress'", "Hardcover Progress" in cpp_content),
            ("switch case: HardcoverSync",          "case SettingAction::HardcoverSync:" in cpp_content),
            ("HardcoverSyncActivity launched",      "HardcoverSyncActivity" in cpp_content),
            ("pluginsSettings.clear()",             "pluginsSettings.clear();" in cpp_content),
            ("case 4 -> pluginsSettings",           "case 4:" in cpp_content),
        ]
        all_ok = True
        for label, ok in checks:
            print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
            if not ok:
                all_ok = False
        if not all_ok:
            sys.exit("ERROR: One or more Hardcover patch checks failed.")
        print("  Hardcover plugin patch verified OK.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python patch.py <path-to-crosspoint-repo>")
    patch(sys.argv[1])