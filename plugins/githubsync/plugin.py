import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingActionEnumValue, PluginsTabEntry, MainHook,
)

# NOTE on scope: the original patch.py also pre-provisioned GitHub credentials
# directly into a reserved NVS flash partition at install time (via a
# generated nvs.csv + `esptool ... write-flash 0x3000`), so users wouldn't
# have to type a PAT on-device. That's flash-time, not source-patch-time
# logic, and GitHubSyncSettingsActivity already lets you configure
# username/PAT/repo/branch on-device (or you can add web-UI editing later),
# so this rewrite drops the NVS pre-seeding step for simplicity/safety and
# has you configure GitHub Sync via Settings -> Plugins -> GitHub Sync after
# first boot instead. Ask if you want the NVS pre-seeding restored.
#
# Also fixed in this rewrite: the original main.cpp hook only ran
# GitHubSync::sync() inside one narrow boot branch (quick-resume-with-no-
# saved-frame), so on most ordinary boots it silently never synced. It's now
# a "post_boot" hook that runs exactly once, every boot, after the routing
# decision - regardless of which plugins are installed alongside it.


def get_manifest(ctx):
    return PluginManifest(
        name="githubsync",
        pretty_name="GitHub Sync",
        source_files=[
            SourceFile("GitHubSync.h", "src/activities/settings"),
            SourceFile("GitHubSync.cpp", "src/activities/settings"),
            SourceFile("GitHubSyncSettingsActivity.h", "src/activities/settings"),
            SourceFile("GitHubSyncSettingsActivity.cpp", "src/activities/settings"),
        ],
        includes=[
            Include("GitHubSync.h", "settings_activity_cpp"),
            Include("GitHubSyncSettingsActivity.h", "settings_activity_cpp"),
            Include("activities/settings/GitHubSync.h", "main_cpp"),
        ],
        setting_actions=[
            SettingActionEnumValue("GitHubSync"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="GitHub Sync",
                kind="action",
                action_name="GitHubSync",
                action_value_text="Sync",
                activity_launch_expr="std::make_unique<GitHubSyncSettingsActivity>(renderer, mappedInput)",
            ),
        ],
        main_hooks=[
            MainHook(
                point="post_boot",
                code=(
                    "  if (GitHubSync::isConfigured()) {\n"
                    "    GitHubSyncResult ghSyncResult = GitHubSync::sync();\n"
                    "    if (ghSyncResult != GitHubSyncResult::OK) {\n"
                    '      LOG_ERR("SYNC", "%s", GitHubSync::resultMessage(ghSyncResult));\n'
                    "    }\n"
                    "  }"
                ),
            ),
        ],
    )
