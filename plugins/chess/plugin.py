import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="chess",
        pretty_name="Chess",
        source_files=[
            SourceFile("ChessEngine.h", "src/activities"),
            SourceFile("ChessActivity.h", "src/activities"),
            SourceFile("ChessActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/ChessActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("Chess"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Chess",
                kind="action",
                action_name="Chess",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<ChessActivity>(renderer, mappedInput)",
            ),
        ],
    )
