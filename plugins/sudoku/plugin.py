import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="sudoku",
        pretty_name="Sudoku",
        source_files=[
            SourceFile("SudokuPuzzle.h", "src/activities"),
            SourceFile("SudokuActivity.h", "src/activities"),
            SourceFile("SudokuActivity.cpp", "src/activities"),
        ],
        includes=[
            Include("activities/SudokuActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("Sudoku"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Sudoku Game",
                kind="action",
                action_name="Sudoku",
                action_value_text="Launch",
                activity_launch_expr="std::make_unique<SudokuActivity>(renderer, mappedInput)",
            ),
        ],
    )
