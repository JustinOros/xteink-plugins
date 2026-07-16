import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="pong",
        pretty_name="Pong",
        source_files=[
            SourceFile("PongActivity.h", "src/activities"),
            SourceFile("PongActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/PongActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("PongGame"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Pong Game",
                kind="action",
                action_name="PongGame",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<PongActivity>(renderer, mappedInput)",
            ),
        ],
    )
