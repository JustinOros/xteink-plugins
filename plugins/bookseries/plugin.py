import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry,
)


def get_manifest(ctx):
    return PluginManifest(
        name="bookseries",
        pretty_name="Book Series",
        source_files=[
            SourceFile("BookSeriesActivity.h", "src/activities/settings"),
            SourceFile("BookSeriesActivity.cpp", "src/activities/settings"),
        ],
        includes=[
            Include("BookSeriesActivity.h", "settings_activity_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("BookSeries"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Book Series",
                kind="action",
                action_name="BookSeries",
                action_value_text="Open",
                activity_launch_expr="std::make_unique<BookSeriesActivity>(renderer, mappedInput)",
            ),
        ],
    )
