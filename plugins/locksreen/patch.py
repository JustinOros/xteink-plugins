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
    settings_dest   = os.path.join(repo_dir, "src", "activities", "settings")
    activities_dest = os.path.join(repo_dir, "src", "activities")
    os.makedirs(settings_dest, exist_ok=True)

    settings_files = {
        "LockscreenSettingsPage.h", "LockscreenSettingsPage.cpp",
        "LockscreenPlugin.h", "LockscreenPlugin.cpp",
    }

    for fname in os.listdir(plugin_dir):
        if not fname.endswith((".h", ".cpp", ".hpp")):
            continue
        src = os.path.join(plugin_dir, fname)
        dst = os.path.join(settings_dest if fname in settings_files else activities_dest, fname)
        shutil.copy2(src, dst)
        print(f"    ✓ {fname}")


def patch_cross_point_settings_h(repo_dir):
    path    = find_first("CrossPointSettings.h", repo_dir)
    content = read_file(path)

    if "lockscreenMode" in content:
        print("  CrossPointSettings.h already patched, skipping.")
        return

    content = content.replace(
        '#pragma once\n#include <HalStorage.h>',
        '#pragma once\n#include <HalStorage.h>\n#include "activities/settings/LockscreenPlugin.h"'
    )
    content = content.replace(
        '  uint8_t imageRendering = IMAGES_DISPLAY;',
        '  uint8_t imageRendering = IMAGES_DISPLAY;\n'
        '  uint8_t lockscreenMode = 0;\n'
        '  char    lockscreenPinHash[9] = {};'
    )

    write_file(path, content)
    print("  CrossPointSettings.h patched.")


def patch_settings_list_h(repo_dir):
    path    = find_first("SettingsList.h", repo_dir)
    content = read_file(path)

    if "lockscreenMode" in content and "lockscreenPinHash" in content:
        if '{StrId::STR_NONE_OPT, StrId::STR_NONE_OPT,\n                         StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}' in content:
            content = content.replace(
                '                        {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT,\n'
                '                         StrId::STR_NONE_OPT, StrId::STR_NONE_OPT},\n',
                '                        {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT},\n'
            )
            write_file(path, content)
            print("  SettingsList.h patched (reduced lockscreenMode to 2 options).")
        else:
            print("  SettingsList.h already patched, skipping.")
        return

    if "lockscreenMode" in content and "lockscreenPinHash" not in content:
        content = content.replace(
            '                        "lockscreenMode", StrId::STR_NONE_OPT),\n',
            '                        "lockscreenMode", StrId::STR_NONE_OPT),\n'
            '      SettingInfo::String(StrId::STR_NONE_OPT, SETTINGS.lockscreenPinHash,\n'
            '                          sizeof(SETTINGS.lockscreenPinHash),\n'
            '                          "lockscreenPinHash", StrId::STR_NONE_OPT),\n'
        )
        write_file(path, content)
        print("  SettingsList.h patched (added lockscreenPinHash).")
        return

    entry = (
        '\n'
        '      SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::lockscreenMode,\n'
        '                        {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT},\n'
        '                        "lockscreenMode", StrId::STR_NONE_OPT),\n'
        '      SettingInfo::String(StrId::STR_NONE_OPT, SETTINGS.lockscreenPinHash,\n'
        '                          sizeof(SETTINGS.lockscreenPinHash),\n'
        '                          "lockscreenPinHash", StrId::STR_NONE_OPT),\n'
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

    if '"LockscreenPlugin.h"' in content and "lockscreenPinHash" in content and '"lockscreenMode") return "Lockscreen"' in content:
        print("  SettingsActivity.cpp already patched, skipping.")
        return

    changed = False

    if '"LockscreenPlugin.h"' not in content:
        content = content.replace(
            '#include "SettingsList.h"',
            '#include "SettingsList.h"\n'
            '#include "activities/settings/LockscreenPlugin.h"\n'
            '#include "activities/LockscreenActivity.h"'
        )
        changed = True

    if 'STR_NONE_OPT};' not in content:
        content = content.replace(
            'StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};',
            'StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM,\n'
            '                                                              StrId::STR_NONE_OPT};'
        )
        changed = True

    if 'pluginsSettings.clear()' not in content:
        content = content.replace(
            '  systemSettings.clear();\n',
            '  systemSettings.clear();\n'
            '  pluginsSettings.clear();\n'
            '  for (const auto& setting : getSettingsList()) {\n'
            '    if (setting.key && std::string(setting.key) == "lockscreenMode") {\n'
            '      pluginsSettings.push_back(setting);\n'
            '    }\n'
            '  }\n'
        )
        changed = True
    elif 'pluginsSettings.push_back' not in content:
        content = content.replace(
            '  pluginsSettings.clear();\n',
            '  pluginsSettings.clear();\n'
            '  for (const auto& setting : getSettingsList()) {\n'
            '    if (setting.key && std::string(setting.key) == "lockscreenMode") {\n'
            '      pluginsSettings.push_back(setting);\n'
            '    }\n'
            '  }\n'
        )
        changed = True
    elif '"lockscreenMode") {\n      pluginsSettings.push_back' not in content:
        content = content.replace(
            '  for (const auto& setting : getSettingsList()) {\n'
            '    if (setting.key && std::string(setting.key) == "lockscreenMode") {\n'
            '      pluginsSettings.push_back(setting);\n'
            '    }\n'
            '  }\n'
            '\n'
            '  for (const auto& setting : getSettingsList()) {',
            '  for (const auto& setting : getSettingsList()) {\n'
            '    if (setting.key && std::string(setting.key) == "lockscreenMode") {\n'
            '      pluginsSettings.push_back(setting);\n'
            '    }\n'
            '  }\n'
            '\n'
            '  for (const auto& setting : getSettingsList()) {'
        )
        # If pluginsSettings.push_back exists from another plugin but not lockscreen,
        # append after the existing population loop
        if '"lockscreenMode") {\n      pluginsSettings.push_back' not in content:
            import re
            content = re.sub(
                r'(  pluginsSettings\.clear\(\);(\n  for.*?push_back.*?\n  \}\n)*)',
                lambda m: m.group(0) +
                    '  for (const auto& setting : getSettingsList()) {\n'
                    '    if (setting.key && std::string(setting.key) == "lockscreenMode") {\n'
                    '      pluginsSettings.push_back(setting);\n'
                    '    }\n'
                    '  }\n',
                content,
                count=1,
                flags=re.DOTALL
            )
        changed = True

    if 'case 4:' not in content:
        content = content.replace(
            '      case 3:\n'
            '        currentSettings = &systemSettings;\n'
            '        break;\n',
            '      case 3:\n'
            '        currentSettings = &systemSettings;\n'
            '        break;\n'
            '      case 4:\n'
            '        currentSettings = &pluginsSettings;\n'
            '        break;\n'
        )
        changed = True

    if '"Plugins"' not in content:
        content = content.replace(
            '    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});\n'
            '  }',
            '    const char* tabLabel = (i == 4) ? "Plugins" : I18N.get(categoryNames[i]);\n'
            '    tabs.push_back({tabLabel, selectedCategoryIndex == i});\n'
            '  }'
        )
        changed = True

    if 'nextCatIdx' not in content:
        content = content.replace(
            '  const auto confirmLabel = (selectedSettingIndex == 0)\n'
            '                                ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])\n'
            '                                : tr(STR_TOGGLE);',
            '  const int nextCatIdx = (selectedCategoryIndex + 1) % categoryCount;\n'
            '  const char* nextCatLabel = (nextCatIdx == 4) ? "Plugins" : I18N.get(categoryNames[nextCatIdx]);\n'
            '  const auto confirmLabel = (selectedSettingIndex == 0) ? nextCatLabel : tr(STR_TOGGLE);'
        )
        changed = True

    if 'selectedCategoryIndex == 4' not in content:
        content = content.replace(
            '    [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); },',
            '    [&settings, this](int index) -> std::string {\n'
            '      if (selectedCategoryIndex == 4) {\n'
            '        const auto& s = settings[index];\n'
            '        if (s.key && std::string(s.key) == "lockscreenMode") return "Lockscreen";\n'
            '      }\n'
            '      return std::string(I18N.get(settings[index].nameId));\n'
            '    },'
        )
        changed = True
    elif '"lockscreenMode") return "Lockscreen"' not in content:
        content = content.replace(
            '      if (selectedCategoryIndex == 4) {\n'
            '        const auto& s = settings[index];\n',
            '      if (selectedCategoryIndex == 4) {\n'
            '        const auto& s = settings[index];\n'
            '        if (s.key && std::string(s.key) == "lockscreenMode") return "Lockscreen";\n'
        )
        changed = True

    if 'LockscreenPlugin::modeName' not in content:
        content = content.replace(
            '          } else {\n'
            '            valueText = I18N.get(setting.enumValues[value]);\n'
            '          }',
            '          } else if (setting.key && std::string(setting.key) == "lockscreenMode") {\n'
            '            valueText = LockscreenPlugin::modeName(static_cast<LockscreenMode>(value));\n'
            '          } else {\n'
            '            valueText = I18N.get(setting.enumValues[value]);\n'
            '          }'
        )
        changed = True

    if 'lockscreenPinHash' not in content:
        content = content.replace(
            '  SETTINGS.saveToFile();\n'
            '}\n'
            '\n'
            'void SettingsActivity::render',
            '  if (setting.key && std::string(setting.key) == "lockscreenMode") {\n'
            '    if (SETTINGS.lockscreenMode == 0) {\n'
            '      memset(SETTINGS.lockscreenPinHash, 0, sizeof(SETTINGS.lockscreenPinHash));\n'
            "    } else if (SETTINGS.lockscreenPinHash[0] == '\\0') {\n"
            '      startActivityForResult(\n'
            '          std::make_unique<LockscreenActivity>(renderer, mappedInput, LockscreenActivity::Purpose::CREATE),\n'
            '          [](const ActivityResult&) { SETTINGS.saveToFile(); });\n'
            '      return;\n'
            '    }\n'
            '  }\n'
            '\n'
            '  SETTINGS.saveToFile();\n'
            '}\n'
            '\n'
            'void SettingsActivity::render'
        )
        changed = True

    if changed:
        write_file(path, content)
        print("  SettingsActivity.cpp patched.")
    else:
        print("  SettingsActivity.cpp already patched, skipping.")

def patch_web_server(repo_dir):
    path    = find_first("CrossPointWebServer.cpp", repo_dir)
    content = read_file(path)

    if 'lockscreenMode") continue' in content:
        print("  CrossPointWebServer.cpp already patched, skipping.")
        return

    target = (
        '    if (s.key && strcmp(s.key, "lockscreenMode") == 0) {\n'
        '      doc["name"] = "Lockscreen";\n'
        '      doc["category"] = "Plugins";\n'
        '    }\n'
    )

    if target in content:
        content = content.replace(
            target,
            '    if (s.key && (strcmp(s.key, "lockscreenMode") == 0 || strcmp(s.key, "lockscreenPinHash") == 0)) continue;\n'
        )
        write_file(path, content)
        print("  CrossPointWebServer.cpp patched (lockscreen hidden from WUI).")
        return

    name_override = (
        '\n'
        '    if (s.key && (strcmp(s.key, "lockscreenMode") == 0 || strcmp(s.key, "lockscreenPinHash") == 0)) continue;\n'
    )

    content = content.replace(
        '    doc["category"] = I18N.get(s.category);\n'
        '\n'
        '    switch (s.type) {',
        '    doc["category"] = I18N.get(s.category);\n'
        + name_override
        + '\n'
        '    switch (s.type) {'
    )

    write_file(path, content)
    print("  CrossPointWebServer.cpp patched (lockscreen hidden from WUI).")


def patch_main_cpp(repo_dir):
    path    = find_first("main.cpp", repo_dir)
    content = read_file(path)

    if "LockscreenPlugin" in content:
        if "shouldLockOnWake" in content or "shouldLockOnPower" in content or "wasInReader" in content:
            content = content.replace(
                '    const bool wasInReader = APP_STATE.lastSleepFromReader;\n'
                '    const bool shouldLock =\n'
                '        hasPinSet &&\n'
                '        ((lsMode == LockscreenMode::ENABLED) ||\n'
                '         (lsMode == LockscreenMode::ON_POWER && !wasInReader) ||\n'
                '         (lsMode == LockscreenMode::ON_WAKE  &&  wasInReader));\n',
                '    const bool shouldLock = hasPinSet && LockscreenPlugin::shouldLock(lsMode);\n'
            )
            write_file(path, content)
            print("  main.cpp patched (updated shouldLock call).")
        else:
            print("  main.cpp already patched, skipping.")
        return

    content = content.replace(
        '#include "CrossPointSettings.h"',
        '#include "CrossPointSettings.h"\n'
        '#include "activities/settings/LockscreenPlugin.h"\n'
        '#include "activities/LockscreenActivity.h"'
    )

    lockscreen_block = (
        '\n'
        '  {\n'
        '    const auto lsMode    = static_cast<LockscreenMode>(SETTINGS.lockscreenMode);\n'
        '    const bool hasPinSet = SETTINGS.lockscreenPinHash[0] != \'\\0\';\n'
        '    const bool shouldLock = hasPinSet && LockscreenPlugin::shouldLock(lsMode);\n'
        '\n'
        '    if (shouldLock) {\n'
        '      LockscreenActivity lockAct(renderer, mappedInputManager, LockscreenActivity::Purpose::UNLOCK);\n'
        '      lockAct.onEnter();\n'
        '      lockAct.renderDirect();\n'
        '      while (!lockAct.isDone()) {\n'
        '        gpio.update();\n'
        '        lockAct.loop();\n'
        '        if (lockAct.needsRender()) {\n'
        '          lockAct.renderDirect();\n'
        '        }\n'
        '        delay(10);\n'
        '      }\n'
        '      lockAct.onExit();\n'
        '      if (!lockAct.wasSuccessful()) {\n'
        '        enterDeepSleep();\n'
        '        return;\n'
        '      }\n'
        '    }\n'
        '  }\n'
    )

    content = content.replace(
        '  RECENT_BOOKS.loadFromFile();\n'
        '\n'
        '  // Boot to home screen if no book is open',
        '  RECENT_BOOKS.loadFromFile();\n'
        + lockscreen_block
        + '\n'
        '  // Boot to home screen if no book is open'
    )

    write_file(path, content)
    print("  main.cpp patched.")


def patch_settings_html(repo_dir):
    path = find_first("SettingsPage.html", repo_dir)
    content = read_file(path)

    if "lockscreenPin" in content:
        print("  SettingsPage.html already patched, skipping.")
        return

    lines = [
        "",
        "  <script>",
        "  (function() {",
        "    var _orig = loadSettings;",
        "    loadSettings = async function() {",
        "      await _orig.apply(this, arguments);",
        "      hideInternalRows();",
        "      injectLockscreenPin();",
        "    };",
        "",
        "    function djb2Hash(str) {",
        "      var hash = 5381;",
        "      for (var i = 0; i < str.length; i++) {",
        "        hash = ((hash << 5) + hash) ^ str.charCodeAt(i);",
        "        hash = hash >>> 0;",
        "      }",
        "      hash = (hash ^ 0xA3F1C2B4) >>> 0;",
        '      return ("00000000" + hash.toString(16).toUpperCase()).slice(-8);',
        "    }",
        "",
        "    function hideInternalRows() {",
        '      allSettings.forEach(function(s) {',
        '        if (s.hidden) {',
        '          var el = document.getElementById("setting-" + s.key);',
        '          if (el) { var row = el.closest(".setting-row"); if (row) row.style.display = "none"; }',
        '        }',
        "      });",
        "    }",
        "",
        "    function injectLockscreenPin() {",
        '      var modeEl = document.getElementById("setting-lockscreenMode");',
        "      if (!modeEl) return;",
        '      var row = modeEl.closest(".setting-row");',
        '      if (!row || document.getElementById("lockscreen-pin-row")) return;',
        '      var pinRow = document.createElement("div");',
        '      pinRow.className = "setting-row";',
        '      pinRow.id = "lockscreen-pin-row";',
        "      pinRow.innerHTML =",
        '        \'<span class="setting-name">New PIN (4 digits)</span>\' +',
        '        \'<span class="setting-control">\' +',
        '        \'<input type="password" id="lockscreenPin" maxlength="4" \' +',
        '        \'pattern="[0-9]{4}" inputmode="numeric" \' +',
        '        \'placeholder="Leave blank to keep current" \' +',
        '        \'style="width:200px;" oninput="markChanged()">\' +',
        "        '</span>';",
        "      row.parentNode.insertBefore(pinRow, row.nextSibling);",
        "      updatePinRowVisibility();",
        '      modeEl.addEventListener("change", updatePinRowVisibility);',
        "    }",
        "",
        "    function updatePinRowVisibility() {",
        '      var modeEl = document.getElementById("setting-lockscreenMode");',
        '      var pinRow = document.getElementById("lockscreen-pin-row");',
        "      if (!modeEl || !pinRow) return;",
        '      pinRow.style.display = parseInt(modeEl.value) === 1 ? "" : "none";',
        "    }",
        "",
        "    var _origSave = saveSettings;",
        "    saveSettings = async function() {",
        '      var pinEl = document.getElementById("lockscreenPin");',
        '      var modeEl = document.getElementById("setting-lockscreenMode");',
        "      if (pinEl && modeEl) {",
        "        var pin = pinEl.value.trim();",
        "        if (parseInt(modeEl.value) === 0) {",
        '          var s = allSettings.find(function(x) { return x.key === "lockscreenPinHash"; });',
        '          if (s) { s.value = ""; originalValues["lockscreenPinHash"] = "CLEAR"; }',
        "        } else if (pin.length === 4 && /^[0-9]{4}$/.test(pin)) {",
        "          var hash = djb2Hash(pin);",
        '          var s = allSettings.find(function(x) { return x.key === "lockscreenPinHash"; });',
        '          if (s) { s.value = hash; originalValues["lockscreenPinHash"] = "CHANGED"; }',
        '          pinEl.value = "";',
        "        } else if (pin.length > 0) {",
        '          document.getElementById("message").textContent = "PIN must be exactly 4 digits.";',
        '          document.getElementById("message").className = "message error";',
        '          document.getElementById("message").style.display = "block";',
        '          setTimeout(function() { document.getElementById("message").style.display = "none"; }, 4000);',
        "          return;",
        "        }",
        "      }",
        "      await _origSave.apply(this, arguments);",
        "    };",
        "  })();",
        "  </script>",
        "",
    ]

    js = "\n".join(lines)
    content = content.replace("</body>", js + "</body>")
    write_file(path, content)
    print("  SettingsPage.html patched.")


def patch(repo_dir: str):
    plugin_dir = os.path.dirname(os.path.abspath(__file__))

    print("  Copying plugin sources...")
    copy_plugin_sources(plugin_dir, repo_dir)

    print("  Patching CrossPointSettings.h...")
    patch_cross_point_settings_h(repo_dir)

    print("  Patching SettingsList.h...")
    patch_settings_list_h(repo_dir)

    print("  Patching SettingsActivity.h...")
    patch_settings_h(repo_dir)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_cpp(repo_dir)

    print("  Patching CrossPointWebServer.cpp...")
    patch_web_server(repo_dir)

    print("  Patching main.cpp...")
    patch_main_cpp(repo_dir)

    print("  Patching SettingsPage.html (WUI PIN entry)...")
    patch_settings_html(repo_dir)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python patch.py <path-to-crosspoint-repo>")
    patch(sys.argv[1])
