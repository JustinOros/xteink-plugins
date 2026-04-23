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

    for include_anchor in [
        '#pragma once\n#include <HalStorage.h>',
        '#pragma once\n\n#include <HalStorage.h>',
        '#pragma once',
    ]:
        if include_anchor in content and '"SmallerFontsPlugin.h"' not in content:
            content = content.replace(
                include_anchor,
                include_anchor + '\n#include "activities/settings/SmallerFontsPlugin.h"',
                1
            )
            break

    for member_anchor in [
        '  uint8_t imageRendering = IMAGES_DISPLAY;',
        '\tuint8_t imageRendering = IMAGES_DISPLAY;',
        'uint8_t imageRendering = IMAGES_DISPLAY;',
    ]:
        if member_anchor in content:
            content = content.replace(
                member_anchor,
                member_anchor + '\n  uint8_t smallerFontsMode = 0;',
                1
            )
            break

    if "smallerFontsMode" not in content:
        sys.exit("ERROR: Could not add smallerFontsMode to CrossPointSettings.h — anchor not found")

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
    for name in ["NOTOSERIF_12", "NOTOSERIF_14", "NOTOSERIF_16", "NOTOSERIF_18",
                 "NOTOSANS_12", "NOTOSANS_14", "NOTOSANS_16", "NOTOSANS_18",
                 "OPENDYSLEXIC_8", "OPENDYSLEXIC_10", "OPENDYSLEXIC_12", "OPENDYSLEXIC_14",
                 "BOOKERLY_12"]:
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
