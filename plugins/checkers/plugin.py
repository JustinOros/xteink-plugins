import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="checkers",
        pretty_name="Checkers",
        source_files=[
            SourceFile("CheckersEngine.h", "src/activities"),
            SourceFile("CheckersActivity.h", "src/activities"),
            SourceFile("CheckersActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/CheckersActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("Checkers"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Checkers Game",
                kind="action",
                action_name="Checkers",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<CheckersActivity>(renderer, mappedInput)",
            ),
        ],
    )
