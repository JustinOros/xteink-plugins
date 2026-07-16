import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingsField, PluginsTabEntry, ReaderInvertHook,
)


def get_manifest(ctx):
    return PluginManifest(
        name="darkmode",
        pretty_name="Dark Mode",
        source_files=[
            SourceFile("DarkModePlugin.h", "src/activities/settings"),
            SourceFile("DarkModePlugin.cpp", "src/activities/settings"),
        ],
        includes=[
            Include("activities/settings/DarkModePlugin.h", "cross_point_settings_h"),
        ],
        settings_fields=[
            SettingsField("uint8_t darkModeState = 0;"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Dark Mode",
                kind="enum",
                key="darkModeState",
                option_labels=["Disabled", "Enabled"],
                value_text_expr="DarkModePlugin::stateName(static_cast<DarkModeState>(value))",
            ),
        ],
        reader_invert_hook=ReaderInvertHook(
            predicate_expr="DarkModePlugin::isDarkMode(static_cast<DarkModeState>(SETTINGS.darkModeState))",
            include_header="activities/settings/DarkModePlugin.h",
        ),
    )
