import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="mazechase",
        pretty_name="Maze Chase",
        source_files=[
            SourceFile("MazeChaseActivity.h", "src/activities"),
            SourceFile("MazeChaseActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/MazeChaseActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("MazeChase"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Maze Chase",
                kind="action",
                action_name="MazeChase",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<MazeChaseActivity>(renderer, mappedInput)",
            ),
        ],
    )
