import sys
import os
import re

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from framework.manifest import (
    PluginManifest, SourceFile, Include, SettingsField, PluginsTabEntry,
)
from framework.engine import find_first, read_file, write_file, PatchError

# NOTE: the plugin's own SmallerFontsPlugin.h/.cpp currently implement only
# two real states (Off / Enabled - a single font-size step down), even
# though the settings list historically reserved a 3rd ("Smallest") slot and
# the README describes three tiers. We match what the code actually does
# (2 states) rather than exposing a 3rd option that behaves identically to
# the 2nd - see SmallerFontsPlugin.cpp: resolveReaderFontId() only special-
# cases MODE_OFF vs "anything else". Worth a real 3-tier implementation as a
# follow-up, but that's a feature gap, not part of the install/composability
# fix this framework addresses.


def _wrap_font_resolution(ctx):
    """Wrap every currently-present `return X_FONT_ID;` line in
    CrossPointSettings::getReaderFontId() with SmallerFontsPlugin's resolver.
    Regex-driven (not a hardcoded font name list) so it picks up whatever
    font plugins (e.g. bookerly) already added earlier in the same run -
    this plugin has phase=1 specifically so it runs after any font-provider
    plugin's phase=0 step."""
    path = find_first("CrossPointSettings.cpp", ctx.repo_dir)
    content = read_file(path)

    if "SmallerFontsPlugin::resolveReaderFontId" in content:
        return

    func_pattern = re.compile(r"(int CrossPointSettings::getReaderFontId\(\) const \{)")
    m = func_pattern.search(content)
    if not m:
        raise PatchError("CrossPointSettings.cpp: could not find getReaderFontId()")

    resolver = (
        "\n  auto resolve = [this](int baseId) {\n"
        "    return SmallerFontsPlugin::resolveReaderFontId(baseId, static_cast<SmallerFontsMode>(smallerFontsMode));\n"
        "  };"
    )
    content = content[:m.end()] + resolver + content[m.end():]

    # Only wrap `return X_FONT_ID;` lines that occur after the point we just
    # inserted the resolver (i.e. inside this function), up to its closing brace.
    start = content.index(resolver) + len(resolver)
    # crude but reliable: this function is the only remaining thing in the
    # file after this point that returns *_FONT_ID literals directly.
    def _wrap(match):
        return f"return resolve({match.group(1)});"

    content = content[:start] + re.sub(r"return (\w+_FONT_ID);", _wrap, content[start:])

    write_file(path, content)
    print("  CrossPointSettings.cpp: wrapped font-id returns with SmallerFontsPlugin::resolveReaderFontId.")


def get_manifest(ctx):
    return PluginManifest(
        name="smallerfonts",
        pretty_name="Smaller Fonts",
        phase=1,  # after bookerly (phase 0), so its font-id return line exists to be wrapped
        source_files=[
            SourceFile("SmallerFontsPlugin.h", "src/activities/settings"),
            SourceFile("SmallerFontsPlugin.cpp", "src/activities/settings"),
        ],
        includes=[
            Include("activities/settings/SmallerFontsPlugin.h", "cross_point_settings_h"),
        ],
        settings_fields=[
            SettingsField("uint8_t smallerFontsMode = 0;"),
        ],
        plugins_tab_entries=[
            PluginsTabEntry(
                label="Smaller Fonts",
                kind="enum",
                key="smallerFontsMode",
                option_labels=["Disabled", "Enabled"],
                value_text_expr="SmallerFontsPlugin::modeName(static_cast<SmallerFontsMode>(value))",
            ),
        ],
        post_patch=_wrap_font_resolution,
    )
