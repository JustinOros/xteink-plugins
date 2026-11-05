import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingsField, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="audiobookshelf",
        pretty_name="Audiobookshelf",
        source_files=[
            SourceFile("AudiobookshelfParse.h", "src/activities/settings"),
            SourceFile("AudiobookshelfPlugin.h", "src/activities/settings"),
            SourceFile("AudiobookshelfPlugin.cpp", "src/activities/settings"),
            SourceFile("AudiobookshelfActivity.h", "src/activities/settings"),
            SourceFile("AudiobookshelfActivity.cpp", "src/activities/settings"),
        ],
        includes=[
            Include("AudiobookshelfActivity.h", "settings_activity_cpp"),
        ],
        settings_fields=[
            SettingsField('char    absServer[128] = "";'),
            SettingsField('char    absUser[64] = "";'),
            SettingsField('char    absPass[128] = "";'),
            SettingsField('char    absToken[640] = "";'),
            SettingsField('char    absLibrary[48] = "Books";'),
            SettingsField('uint8_t absSavePass = 0;'),
        ],
        setting_actions=[
            SettingActionEnumValue("Audiobookshelf"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Audiobookshelf",
                kind="action",
                action_name="Audiobookshelf",
                action_value_text="Open",
                activity_launch_expr="std::make_unique<AudiobookshelfActivity>(renderer, mappedInput)",
            ),
            PluginsTabEntry(
                label="Audiobookshelf Server",
                kind="string",
                key="absServer",
                show_on_device=False,
            ),
            PluginsTabEntry(
                label="Audiobookshelf User Name",
                kind="string",
                key="absUser",
                show_on_device=False,
            ),
            PluginsTabEntry(
                label="Audiobookshelf Password",
                kind="string",
                key="absPass",
                obfuscated=True,
                show_on_device=False,
            ),
            PluginsTabEntry(
                label="Audiobookshelf Save Password to SD Card (plain text)",
                kind="toggle",
                key="absSavePass",
                show_on_device=False,
            ),
            PluginsTabEntry(
                label="Audiobookshelf Library Name",
                kind="string",
                key="absLibrary",
                show_on_device=False,
            ),
            PluginsTabEntry(
                label="Audiobookshelf Token",
                kind="string",
                key="absToken",
                obfuscated=True,
                hidden_from_web=True,
                show_on_device=False,
            ),
        ],
    )
