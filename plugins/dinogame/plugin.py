import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="dinogame",
        pretty_name="Dino Game",
        source_files=[
            SourceFile("DinoActivity.h", "src/activities"),
            SourceFile("DinoActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/DinoActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("DinoGame"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Dino Game",
                kind="action",
                action_name="DinoGame",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<DinoActivity>(renderer, mappedInput)",
            ),
        ],
    )
