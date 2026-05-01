#!/usr/bin/env python3

import os
import sys
import glob
import re
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
    activities_dest = os.path.join(repo_dir, "src", "activities")
    os.makedirs(activities_dest, exist_ok=True)

    for fname in os.listdir(plugin_dir):
        if not fname.endswith((".h", ".cpp", ".hpp")):
            continue
        src = os.path.join(plugin_dir, fname)
        dst = os.path.join(activities_dest, fname)
        shutil.copy2(src, dst)
        print(f"    ✓ {fname}")


def patch_setting_action_enum(repo_dir):
    path    = find_first("SettingsActivity.h", repo_dir)
    content = read_file(path)

    if "PongGame" in content:
        print("  SettingAction enum already patched, skipping.")
        return

    content = re.sub(r'(\n};)', r'\n  PongGame,\1', content, count=1)
    write_file(path, content)
    print("  SettingAction::PongGame added.")


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
            "static constexpr int categoryCount = 4;",
            "static constexpr int categoryCount = 5;"
        )

    if not already_has_vec:
        content = content.replace(
            "  std::vector<SettingInfo> systemSettings;",
            "  std::vector<SettingInfo> systemSettings;\n  std::vector<SettingInfo> pluginsSettings;"
        )

    write_file(path, content)
    print("  SettingsActivity.h patched.")


def patch_settings_cpp(repo_dir):
    path    = find_first("SettingsActivity.cpp", repo_dir)
    content = read_file(path)

    if "PongActivity" in content and "SettingAction::PongGame" in content:
        print("  SettingsActivity.cpp already patched, skipping.")
        return

    content = content.replace(
        '#include "SettingsList.h"',
        '#include "SettingsList.h"\n#include "activities/PongActivity.h"'
    )

    if "StrId::STR_NONE_OPT};" not in content:
        content = content.replace(
            "StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};",
            "StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM,\n"
            "                                                              StrId::STR_NONE_OPT};"
        )

    if "pluginsSettings.clear();" not in content:
        result = content.replace(
            "  systemSettings.clear();\n",
            "  systemSettings.clear();\n  pluginsSettings.clear();\n"
        )
        if "pluginsSettings.clear();" in result:
            content = result
            print("  pluginsSettings.clear() inserted.")
        else:
            print("  WARNING: could not insert pluginsSettings.clear().")

    pong_push = (
        "  pluginsSettings.push_back(SettingInfo::Action(\n"
        "    StrId::STR_NONE_OPT, SettingAction::PongGame\n"
        "  ));\n"
    )

    if "SettingAction::PongGame" not in content:
        inserted = False
        for anchor in [
            "  selectedCategoryIndex = 0;\n  selectedSettingIndex = 0;",
            "  selectedCategoryIndex = 0;",
            "  currentSettings = &displaySettings;",
        ]:
            if anchor in content:
                content = content.replace(anchor, pong_push + anchor, 1)
                inserted = True
                print("  PongGame push_back inserted.")
                break
        if not inserted:
            print("  WARNING: could not insert PongGame push_back.")

    if "case 4:\n        currentSettings = &pluginsSettings;" not in content:
        result = content.replace(
            "      case 3:\n        currentSettings = &systemSettings;\n        break;",
            "      case 3:\n        currentSettings = &systemSettings;\n        break;\n"
            "      case 4:\n        currentSettings = &pluginsSettings;\n        break;"
        )
        if result != content:
            content = result
            print("  case 4 inserted.")
        else:
            print("  WARNING: case 3 anchor not found.")

    if '"Plugins"' not in content:
        result = content.replace(
            "    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});\n"
            "  }",
            '    const char* tabLabel = (i == 4) ? "Plugins" : I18N.get(categoryNames[i]);\n'
            "    tabs.push_back({tabLabel, selectedCategoryIndex == i});\n"
            "  }"
        )
        if result != content:
            content = result
            print("  Plugins tab label inserted.")
        else:
            print("  WARNING: tab label anchor not found.")

    if "nextCatLabel" not in content and "nextCatIdx" not in content:
        result = content.replace(
            "  const auto confirmLabel = (selectedSettingIndex == 0)\n"
            "                                ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])\n"
            "                                : tr(STR_TOGGLE);",
            "  const int nextCatIndex = (selectedCategoryIndex + 1) % categoryCount;\n"
            '  const char* nextCatLabel = (nextCatIndex == 4) ? "Plugins" : I18N.get(categoryNames[nextCatIndex]);\n'
            "  const auto confirmLabel = (selectedSettingIndex == 0) ? nextCatLabel : tr(STR_TOGGLE);"
        )
        if result != content:
            content = result
            print("  nextCatLabel inserted.")
        else:
            print("  WARNING: confirmLabel anchor not found.")

    pong_label = (
        "        if (s.type == SettingType::ACTION &&\n"
        '            s.action == SettingAction::PongGame) return "Pong Game";\n'
    )

    if "Pong Game" not in content:
        if "selectedCategoryIndex == 4" in content:
            result = re.sub(
                r'(      if \(selectedCategoryIndex == 4\) \{.*?)(      \})',
                lambda m: m.group(1) + pong_label + m.group(2),
                content,
                count=1,
                flags=re.DOTALL,
            )
            if result != content:
                content = result
                print("  Pong Game label inserted.")
            else:
                print("  WARNING: category==4 block found but label insertion failed.")
        else:
            result = content.replace(
                "      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); },",
                "      [&settings, this](int index) -> std::string {\n"
                "      if (selectedCategoryIndex == 4) {\n"
                "        const auto& s = settings[index];\n"
                + pong_label +
                "      }\n"
                "      return std::string(I18N.get(settings[index].nameId));\n"
                "    },"
            )
            if result != content:
                content = result
                print("  Pong Game label inserted (new block).")
            else:
                print("  WARNING: label lambda anchor not found.")

    pong_value = (
        "        if (setting.type == SettingType::ACTION &&\n"
        "            setting.action == SettingAction::PongGame) {\n"
        '          valueText = "Launch";\n'
        "        } else "
    )

    if '"Launch"' not in content:
        result = re.sub(
            r'(        if \(setting\.type == SettingType::(?:ACTION|TOGGLE|ENUM)\b)',
            pong_value + r'\1',
            content,
            count=1,
        )
        if result != content:
            content = result
            print("  Launch value text inserted.")
        else:
            print("  WARNING: value text anchor not found.")

    pong_case = (
        "      case SettingAction::PongGame:\n"
        "        startActivityForResult(std::make_unique<PongActivity>(renderer, mappedInput), resultHandler);\n"
        "        break;\n"
    )

    if "case SettingAction::PongGame:" not in content:
        result = re.sub(
            r'(      case SettingAction::)',
            pong_case + r'\1',
            content,
            count=1,
        )
        if result != content:
            content = result
            print("  PongGame case inserted.")
        else:
            print("  WARNING: SettingAction case anchor not found.")

    write_file(path, content)
    print("  SettingsActivity.cpp written.")


def patch(repo_dir: str):
    plugin_dir = os.path.dirname(os.path.abspath(__file__))

    print("  Copying plugin sources...")
    copy_plugin_sources(plugin_dir, repo_dir)

    print("  Patching SettingAction enum...")
    patch_setting_action_enum(repo_dir)

    print("  Patching SettingsActivity.h...")
    patch_settings_h(repo_dir)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_cpp(repo_dir)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python patch.py <path-to-crosspoint-repo>")
    patch(sys.argv[1])
