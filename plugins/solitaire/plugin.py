import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="solitaire",
        pretty_name="Solitaire",
        source_files=[
            SourceFile("SolitaireGame.h", "src/activities"),
            SourceFile("SolitaireActivity.h", "src/activities"),
            SourceFile("SolitaireActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/SolitaireActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("Solitaire"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Solitaire Game",
                kind="action",
                action_name="Solitaire",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<SolitaireActivity>(renderer, mappedInput)",
            ),
        ],
    )
