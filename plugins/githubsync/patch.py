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


def patch_cross_point_settings_h(repo_dir, url="", pat=""):
    path    = find_first("CrossPointSettings.h", repo_dir)
    content = read_file(path)

    if "githubSyncUrl" in content:
        print("  CrossPointSettings.h already patched, skipping.")
        return

    safe_url = url.replace('\\', '\\\\').replace('"', '\\"')
    safe_pat = pat.replace('\\', '\\\\').replace('"', '\\"')

    content = content.replace(
        '  uint8_t imageRendering = IMAGES_DISPLAY;',
        f'  uint8_t imageRendering = IMAGES_DISPLAY;\n'
        f'  char    githubSyncUrl[512] = "{safe_url}";\n'
        f'  char    githubSyncPat[256] = "{safe_pat}";'
    )

    write_file(path, content)
    print("  CrossPointSettings.h patched.")


def patch_settings_list_h(repo_dir):
    path    = find_first("SettingsList.h", repo_dir)
    content = read_file(path)

    if "githubSyncUrl" in content:
        print("  SettingsList.h already patched, skipping.")
        return

    content = content.replace(
        '  };\n  return list;\n}\n',
        '      SettingInfo::String(StrId::STR_NONE_OPT, SETTINGS.githubSyncUrl,\n'
        '                          sizeof(SETTINGS.githubSyncUrl), "githubSyncUrl"),\n'
        '      SettingInfo::String(StrId::STR_NONE_OPT, SETTINGS.githubSyncPat,\n'
        '                          sizeof(SETTINGS.githubSyncPat), "githubSyncPat")\n'
        '          .withObfuscated(),\n'
        '  };\n  return list;\n}\n'
    )

    write_file(path, content)
    print("  SettingsList.h patched.")


def patch_setting_action_enum(repo_dir):
    path    = find_first("SettingsActivity.h", repo_dir)
    content = read_file(path)

    if "GitHubSync" in content:
        print("  SettingAction enum already patched, skipping.")
        return

    content = content.replace(
        '  Language,\n',
        '  GitHubSync,\n  Language,\n'
    )

    write_file(path, content)
    print("  SettingAction::GitHubSync added.")


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

    if "GitHubSyncPlugin" in content:
        print("  SettingsActivity.cpp already patched, skipping.")
        return

    content = content.replace(
        '#include "SettingsList.h"',
        '#include "SettingsList.h"\n#include "GitHubSyncPlugin.h"\n#include "GitHubSyncActivity.h"'
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

    github_push = (
        '  pluginsSettings.push_back(SettingInfo::Action(\n'
        '    StrId::STR_NONE_OPT, SettingAction::GitHubSync\n'
        '  ));\n'
    )

    hardcover_push = (
        '  pluginsSettings.push_back(SettingInfo::Action(\n'
        '    StrId::STR_NONE_OPT, SettingAction::HardcoverSync\n'
        '  ));\n'
    )

    if 'SettingAction::GitHubSync' not in content:
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
                content = content.replace(anchor, anchor + github_push + hardcover_push)
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

    github_label_line = (
        '        if (s.type == SettingType::ACTION &&\n'
        '            s.action == SettingAction::GitHubSync) return "GitHub Sync";\n'
    )

    if 'GitHub Sync' not in content:
        if 'selectedCategoryIndex == 4' in content:
            for first_key_line in [
                '        if (s.key && std::string(s.key) == "darkModeState") return "Dark Mode";\n',
                '        if (s.key && std::string(s.key) == "smallerFontsMode") return "Smaller Fonts";\n',
                '          if (s.type == SettingType::ACTION && s.action == SettingAction::BookerlyInstalled) return "Bookerly Font";\n',
                '        return std::string(I18N.get(settings[index].nameId));\n',
            ]:
                if first_key_line in content:
                    content = content.replace(
                        first_key_line,
                        github_label_line + first_key_line
                    )
                    break
        else:
            old_lambda = (
                '      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,'
            )
            new_lambda = (
                '      [&settings, this](int index) -> std::string {\n'
                '      if (selectedCategoryIndex == 4) {\n'
                '        const auto& s = settings[index];\n'
                + github_label_line +
                '        if (s.key && std::string(s.key) == "darkModeState") return "Dark Mode";\n'
                '        if (s.key && std::string(s.key) == "smallerFontsMode") return "Smaller Fonts";\n'
                '      }\n'
                '      return std::string(I18N.get(settings[index].nameId));\n'
                '    }, nullptr, nullptr,'
            )
            content = content.replace(old_lambda, new_lambda)

    if '"No WiFi"' not in content:
        content = content.replace(
            '        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {',
            '        if (setting.type == SettingType::ACTION &&\n'
            '            setting.action == SettingAction::GitHubSync) {\n'
            '          if (WiFi.status() != WL_CONNECTED) {\n'
            '            valueText = "No WiFi";\n'
            '          } else {\n'
            '            valueText = "Sync";\n'
            '          }\n'
            '        } else if (setting.type == SettingType::ACTION &&\n'
            '            setting.action == SettingAction::HardcoverSync) {\n'
            '          valueText = "Sync";\n'
            '        } else if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {'
        )

    if 'case SettingAction::GitHubSync:' not in content:
        content = content.replace(
            '      case SettingAction::Language:\n',
            '      case SettingAction::GitHubSync:\n'
            '        startActivityForResult(std::make_unique<GitHubSyncActivity>(renderer, mappedInput), resultHandler);\n'
            '        break;\n'
            '      case SettingAction::Language:\n'
        )

    write_file(path, content)
    print("  SettingsActivity.cpp patched.")


def patch_wifi_include(repo_dir):
    path    = find_first("SettingsActivity.cpp", repo_dir)
    content = read_file(path)

    if '#include <WiFi.h>' in content:
        print("  WiFi.h already included in SettingsActivity.cpp, skipping.")
        return

    content = content.replace(
        '#include "SettingsList.h"',
        '#include "SettingsList.h"\n#include <WiFi.h>'
    )
    write_file(path, content)
    print("  WiFi.h include added to SettingsActivity.cpp.")


def patch_web_server(repo_dir):
    path    = find_first("CrossPointWebServer.cpp", repo_dir)
    content = read_file(path)

    if '"githubSyncUrl"' in content or '"GitHub Repo"' in content:
        print("  CrossPointWebServer.cpp already patched, skipping.")
        return

    github_block = (
        '      if (strcmp(s.key, "githubSyncUrl") == 0) {\n'
        '        doc["name"]     = "GitHub Repo";\n'
        '        doc["category"] = "Plugins";\n'
        '      }\n'
        '      if (strcmp(s.key, "githubSyncPat") == 0) {\n'
        '        doc["name"]     = "GitHub PAT (Optional)";\n'
        '        doc["category"] = "Plugins";\n'
        '      }\n'
    )

    if 'if (s.key) {' in content and 'darkModeState' in content:
        content = content.replace(
            '      if (strcmp(s.key, "darkModeState") == 0) {\n',
            github_block +
            '      if (strcmp(s.key, "darkModeState") == 0) {\n'
        )
    elif 'if (s.key) {' in content and 'hardcoverApiToken' in content:
        content = content.replace(
            '      if (strcmp(s.key, "hardcoverApiToken") == 0) {\n',
            github_block +
            '      if (strcmp(s.key, "hardcoverApiToken") == 0) {\n'
        )
    else:
        content = content.replace(
            '    doc["category"] = I18N.get(s.category);\n'
            '\n'
            '    switch (s.type) {',
            '    doc["category"] = I18N.get(s.category);\n'
            '\n'
            '    if (s.key) {\n'
            + github_block +
            '    }\n'
            '\n'
            '    switch (s.type) {'
        )

    write_file(path, content)
    print("  CrossPointWebServer.cpp patched.")


def patch(repo_dir: str, yes_all: bool = False):
    plugin_dir = os.path.dirname(os.path.abspath(__file__))

    cache_file = os.path.expanduser("~/.githubsync")
    url = ""
    pat = ""

    if os.path.exists(cache_file):
        with open(cache_file) as f:
            for line in f:
                if line.startswith("url="):
                    url = line[4:].strip()
                elif line.startswith("pat="):
                    pat = line[4:].strip()
        print(f"  Using GitHub config from ~/.githubsync (url: {url}).")
    else:
        print("  GitHub repository URL is required.")
        print("  Example: https://github.com/yourusername/yourrepo")
        while not url:
            url = input("  GitHub URL: ").strip()
            if not url:
                print("  URL cannot be empty.")
        pat = input("  GitHub PAT (Optional for Private repo): ").strip()
        with open(cache_file, "w") as f:
            f.write(f"url={url}\n")
            f.write(f"pat={pat}\n")
        print(f"  Config saved to ~/.githubsync for future installs.")

    print("  Copying plugin sources...")
    copy_plugin_sources(plugin_dir, repo_dir)

    print("  Patching CrossPointSettings.h...")
    patch_cross_point_settings_h(repo_dir, url, pat)

    print("  Patching SettingsList.h...")
    patch_settings_list_h(repo_dir)

    print("  Patching SettingAction enum...")
    patch_setting_action_enum(repo_dir)

    print("  Patching SettingsActivity.h...")
    patch_settings_h(repo_dir)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_cpp(repo_dir)

    print("  Adding WiFi.h include...")
    patch_wifi_include(repo_dir)

    print("  Patching CrossPointWebServer.cpp (web UI)...")
    patch_web_server(repo_dir)

    import glob as _glob
    cpp_results = _glob.glob(os.path.join(repo_dir, "**", "SettingsActivity.cpp"), recursive=True)
    if cpp_results:
        cpp_content = open(cpp_results[0]).read()
        checks = [
            ("label lambda: 'GitHub Sync'",   "GitHub Sync" in cpp_content),
            ("switch case: GitHubSync",        "case SettingAction::GitHubSync:" in cpp_content),
            ("GitHubSyncActivity launched",    "GitHubSyncActivity" in cpp_content),
            ("pluginsSettings.clear()",        "pluginsSettings.clear();" in cpp_content),
            ("case 4 -> pluginsSettings",      "case 4:" in cpp_content),
            ("No WiFi button state",           "No WiFi" in cpp_content),
        ]
        all_ok = True
        for label, ok in checks:
            print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
            if not ok:
                all_ok = False
        if not all_ok:
            sys.exit("ERROR: One or more GitHub Sync patch checks failed.")
        print("  GitHub Sync plugin patch verified OK.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python patch.py <path-to-crosspoint-repo>")
    patch(sys.argv[1])
