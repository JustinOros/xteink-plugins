import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="snakegame",
        pretty_name="Snake Game",
        source_files=[
            SourceFile("SnakeActivity.h", "src/activities"),
            SourceFile("SnakeActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/SnakeActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("SnakeGame"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Snake Game",
                kind="action",
                action_name="SnakeGame",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<SnakeActivity>(renderer, mappedInput)",
            ),
        ],
    )
