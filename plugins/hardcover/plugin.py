import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingsField, PluginsTabEntry, SettingActionEnumValue,
)

HARDCOVER_TOKEN_FILE = os.path.expanduser("~/.hardcover")


def _get_token(ctx):
    token = ""
    if os.path.exists(HARDCOVER_TOKEN_FILE):
        raw = open(HARDCOVER_TOKEN_FILE).read().strip()
        if raw.startswith("Bearer "):
            raw = raw[len("Bearer "):]
        token = raw

    if token:
        print(f"  [hardcover] Using API token from {HARDCOVER_TOKEN_FILE}.")
        return token

    if not ctx.prompt:
        return ""

    print("  [hardcover] Hardcover API token required.")
    print("  [hardcover] Get yours from https://hardcover.app/account/api")
    while not token:
        raw = input("  Hardcover API token: ").strip()
        if raw.startswith("Bearer "):
            raw = raw[len("Bearer "):]
        token = raw
        if not token:
            print("  Token cannot be empty.")
    with open(HARDCOVER_TOKEN_FILE, "w") as f:
        f.write(f"Bearer {token}\n")
    print(f"  [hardcover] Token saved to {HARDCOVER_TOKEN_FILE} for future installs.")
    return token


def get_manifest(ctx):
    token = _get_token(ctx)
    if len(token) >= 640:
        raise SystemExit(f"ERROR: Hardcover API token is too long ({len(token)} chars, max 639).")
    safe_token = token.replace("\\", "\\\\").replace('"', '\\"')

    return PluginManifest(
        name="hardcover",
        pretty_name="Hardcover Sync",
        source_files=[
            SourceFile("HardcoverPlugin.h", "src/activities/settings"),
            SourceFile("HardcoverPlugin.cpp", "src/activities/settings"),
            SourceFile("HardcoverSyncActivity.h", "src/activities/settings"),
            SourceFile("HardcoverSyncActivity.cpp", "src/activities/settings"),
        ],
        includes=[
            Include("HardcoverPlugin.h", "settings_activity_cpp"),
            Include("HardcoverSyncActivity.h", "settings_activity_cpp"),
        ],
        settings_fields=[
            SettingsField(f'char    hardcoverApiToken[640] = "{safe_token}";'),
        ],
        setting_actions=[
            SettingActionEnumValue("HardcoverSync"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Hardcover API Token",
                kind="string",
                key="hardcoverApiToken",
                obfuscated=True,
                show_on_device=False,
            ),
            PluginsTabEntry(
                label="Hardcover Progress",
                kind="action",
                action_name="HardcoverSync",
                action_value_text="Sync",
                activity_launch_expr="std::make_unique<HardcoverSyncActivity>(renderer, mappedInput)",
            ),
        ],
    )
