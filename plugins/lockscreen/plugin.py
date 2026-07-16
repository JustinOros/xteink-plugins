import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingsField, PluginsTabEntry, ToggleHook, MainHook,
)


def get_manifest(ctx):
    return PluginManifest(
        name="lockscreen",
        pretty_name="Lockscreen",
        source_files=[
            SourceFile("LockscreenPlugin.h", "src/activities/settings"),
            SourceFile("LockscreenPlugin.cpp", "src/activities/settings"),
            SourceFile("LockscreenActivity.h", "src/activities"),
            SourceFile("LockscreenActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/settings/LockscreenPlugin.h", "cross_point_settings_h"),
            Include("activities/settings/LockscreenPlugin.h", "settings_activity_cpp"),
            Include("activities/LockscreenActivity.h", "settings_activity_cpp"),
            Include("activities/settings/LockscreenPlugin.h", "main_cpp"),
            Include("activities/LockscreenActivity.h", "main_cpp"),
        ],
        settings_fields=[
            SettingsField("uint8_t lockscreenMode = 0;"),
            SettingsField("char    lockscreenPinHash[9] = {};"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Lockscreen",
                kind="enum",
                key="lockscreenMode",
                option_labels=["Disabled", "Enabled"],
                value_text_expr="LockscreenPlugin::modeName(static_cast<LockscreenMode>(value))",
                hidden_from_web=True,
            ),
            PluginsTabEntry(
                label="Lockscreen PIN",
                kind="string",
                key="lockscreenPinHash",
                obfuscated=True,
                show_on_device=False,
                hidden_from_web=True,
            ),
        ],
        toggle_hooks=[
            ToggleHook(
                key="lockscreenMode",
                code=(
                    "    if (SETTINGS.lockscreenMode == 0) {\n"
                    "      memset(SETTINGS.lockscreenPinHash, 0, sizeof(SETTINGS.lockscreenPinHash));\n"
                    "    } else if (SETTINGS.lockscreenPinHash[0] == '\\0') {\n"
                    "      startActivityForResult(\n"
                    "          std::make_unique<LockscreenActivity>(renderer, mappedInput, LockscreenActivity::Purpose::CREATE),\n"
                    "          [](const ActivityResult&) { SETTINGS.saveToFile(); });\n"
                    "      return;\n"
                    "    }"
                ),
            ),
        ],
        main_hooks=[
            MainHook(
                # Must run after setupDisplayAndFonts(): the lock screen calls
                # renderDirect(), and before that point display.begin()/
                # renderer.begin() haven't run and no fonts are registered, so
                # the PIN prompt would silently draw nothing while still
                # unlocking underneath - i.e. an invisible, bypassable lock.
                point="post_display_setup",
                code=(
                    "  {\n"
                    "    const auto lsMode    = static_cast<LockscreenMode>(SETTINGS.lockscreenMode);\n"
                    "    const bool hasPinSet = SETTINGS.lockscreenPinHash[0] != '\\0';\n"
                    "    const bool shouldLock = hasPinSet && LockscreenPlugin::shouldLock(lsMode);\n"
                    "    if (shouldLock) {\n"
                    "      LockscreenActivity lockAct(renderer, mappedInputManager, LockscreenActivity::Purpose::UNLOCK);\n"
                    "      lockAct.onEnter();\n"
                    "      lockAct.renderDirect();\n"
                    "      while (!lockAct.isDone()) {\n"
                    "        gpio.update();\n"
                    "        lockAct.loop();\n"
                    "        if (lockAct.needsRender()) {\n"
                    "          lockAct.renderDirect();\n"
                    "        }\n"
                    "        delay(10);\n"
                    "      }\n"
                    "      lockAct.onExit();\n"
                    "      renderer.clearScreen();\n"
                    "      renderer.displayBuffer();\n"
                    "      if (!lockAct.wasSuccessful()) {\n"
                    "        enterDeepSleep();\n"
                    "        return;\n"
                    "      }\n"
                    "    }\n"
                    "  }"
                ),
            ),
        ],
    )
