import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="minesweeper",
        pretty_name="Minesweeper",
        source_files=[
            SourceFile("MinesweeperBoard.h", "src/activities"),
            SourceFile("MinesweeperActivity.h", "src/activities"),
            SourceFile("MinesweeperActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/MinesweeperActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("Minesweeper"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Minesweeper Game",
                kind="action",
                action_name="Minesweeper",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<MinesweeperActivity>(renderer, mappedInput)",
            ),
        ],
    )
