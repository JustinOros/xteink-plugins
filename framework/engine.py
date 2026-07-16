"""
The composition engine.

Design principle: every shared file is touched EXACTLY ONCE, in one pass,
built from the full set of *currently selected* plugins. Nothing is derived
from "is some other plugin's marker text already in this file" - so the
result for {darkmode} alone, {smallerfonts} alone, or {darkmode,
smallerfonts, hardcover, lockscreen, pong, githubsync, bookerly} together is
always internally consistent, because it's always generated fresh from the
same selection instead of being poked at incrementally by N independent
scripts that each assume the others already ran (or didn't).

Anchors are matched against the *current* upstream crosspoint-reader source.
If an anchor isn't found, we raise PatchError immediately with the file and
a snippet of what we were looking for, rather than silently no-op'ing like
the old patch.py scripts did (which is how upstream drift + missing plugins
produced silently-broken installs before).
"""

import glob
import os
import re
import shutil
from dataclasses import dataclass
from typing import List

from .manifest import PluginManifest


class PatchError(Exception):
    pass


@dataclass
class SelectedPlugin:
    name: str
    plugin_dir: str
    manifest: PluginManifest


@dataclass
class Context:
    repo_dir: str
    yes_all: bool = False
    prompt: bool = True


def read_file(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def write_file(path, content):
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def find_first(filename, repo_dir):
    results = glob.glob(os.path.join(repo_dir, "**", filename), recursive=True)
    if not results:
        raise PatchError(f"Could not locate {filename} anywhere under {repo_dir}")
    return results[0]


def replace_once(content, anchor, replacement, file_label):
    count = content.count(anchor)
    if count == 0:
        raise PatchError(
            f"{file_label}: expected anchor not found (upstream source may have "
            f"changed). Anchor was:\n{anchor!r}"
        )
    if count > 1:
        raise PatchError(
            f"{file_label}: anchor matched {count} times, expected exactly 1 "
            f"(ambiguous patch target). Anchor was:\n{anchor!r}"
        )
    return content.replace(anchor, replacement, 1)


def contains_once(content, anchor, file_label):
    count = content.count(anchor)
    if count == 0:
        raise PatchError(f"{file_label}: expected anchor not found:\n{anchor!r}")
    if count > 1:
        raise PatchError(f"{file_label}: anchor matched {count} times, expected exactly 1:\n{anchor!r}")


def uses_plugins_tab(selected: List[SelectedPlugin]) -> bool:
    return any(sp.manifest.plugins_tab_entries for sp in selected)


def validate_manifests(selected: List[SelectedPlugin]):
    """Catch self-inconsistent manifests before touching any file. This is
    exactly the class of bug the old patch.py scripts were prone to
    (referencing something that was never declared) - the difference now is
    it's a single, mechanical check instead of relying on every plugin
    author to notice by eye. Currently checks: every SettingAction an
    "action"-kind PluginsTabEntry refers to must have a matching
    SettingActionEnumValue declared somewhere in the selection (usually the
    same plugin)."""
    declared_actions = {a.name for sp in selected for a in sp.manifest.setting_actions}
    errors = []
    for sp in selected:
        for e in sp.manifest.plugins_tab_entries:
            if e.kind == "action" and e.action_name and e.action_name not in declared_actions:
                errors.append(
                    f"plugin '{sp.name}' has a PluginsTabEntry with action_name="
                    f"'{e.action_name}' but no matching SettingActionEnumValue('{e.action_name}') "
                    f"is declared in its (or any selected plugin's) setting_actions - the generated "
                    f"code would reference an undeclared SettingAction enum member and fail to compile."
                )
    if errors:
        raise PatchError("Manifest validation failed:\n  - " + "\n  - ".join(errors))


# ---------------------------------------------------------------------------
# Source file copying
# ---------------------------------------------------------------------------

def copy_source_files(repo_dir, selected: List[SelectedPlugin]):
    for sp in selected:
        for sf in sp.manifest.source_files:
            src = os.path.join(sp.plugin_dir, sf.src_name)
            if not os.path.exists(src):
                raise PatchError(f"{sp.name}: declared source file not found: {src}")
            dest_dir = os.path.join(repo_dir, sf.dest_subdir)
            os.makedirs(dest_dir, exist_ok=True)
            dst = os.path.join(dest_dir, sf.src_name)
            shutil.copy2(src, dst)
            print(f"    ✓ [{sp.name}] {sf.src_name}")


# ---------------------------------------------------------------------------
# CrossPointSettings.h  (persisted struct fields + includes)
# ---------------------------------------------------------------------------

def patch_cross_point_settings_h(repo_dir, selected: List[SelectedPlugin]):
    path = find_first("CrossPointSettings.h", repo_dir)
    content = read_file(path)
    label = "CrossPointSettings.h"

    includes = [inc.header for sp in selected for inc in sp.manifest.includes
                if inc.target == "cross_point_settings_h"]
    fields = [f.decl for sp in selected for f in sp.manifest.settings_fields]

    if not includes and not fields:
        return

    if includes:
        include_anchor = "#include <HalStorage.h>"
        contains_once(content, include_anchor, label)
        include_block = include_anchor + "".join(f'\n#include "{h}"' for h in includes)
        content = content.replace(include_anchor, include_block, 1)

    if fields:
        field_anchor = "  uint8_t imageRendering = IMAGES_DISPLAY;"
        contains_once(content, field_anchor, label)
        field_block = field_anchor + "".join(f"\n  {decl}" for decl in fields)
        content = content.replace(field_anchor, field_block, 1)

    write_file(path, content)
    print(f"  {label} patched ({len(includes)} include(s), {len(fields)} field(s)).")


# ---------------------------------------------------------------------------
# SettingsList.h  (master settings list used for save/load + web API dump)
# ---------------------------------------------------------------------------

def _settings_list_entry_for_tab_entry(e):
    """Build the categoryless SettingsList.h entry for a PluginsTabEntry."""
    if e.kind == "enum":
        n = len(e.option_labels) if e.option_labels else 2
        dummy_opts = ", ".join(["StrId::STR_NONE_OPT"] * n)
        obf = ".withObfuscated()" if e.obfuscated else ""
        return (f'      SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::{e.key},\n'
                f'                        {{{dummy_opts}}}, "{e.key}"){obf},')
    if e.kind == "toggle":
        obf = ".withObfuscated()" if e.obfuscated else ""
        return f'      SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::{e.key}, "{e.key}"){obf},'
    if e.kind == "string":
        obf = ".withObfuscated()" if e.obfuscated else ""
        return (f'      SettingInfo::String(StrId::STR_NONE_OPT, SETTINGS.{e.key},\n'
                f'                          sizeof(SETTINGS.{e.key}), "{e.key}"){obf},')
    return None  # "action" kind has no persisted entry


def patch_settings_list_h(repo_dir, selected: List[SelectedPlugin]):
    path = find_first("SettingsList.h", repo_dir)
    content = read_file(path)
    label = "SettingsList.h"

    lines = []
    for sp in selected:
        for e in sp.manifest.settings_list_entries:
            lines.append(f"      {e.cpp}")
        for e in sp.manifest.plugins_tab_entries:
            entry = _settings_list_entry_for_tab_entry(e)
            if entry:
                lines.append(entry)

    if not lines:
        return

    anchor = "    };\n    // Only show tilt page turn setting when the QMI8658 IMU is present (X3)"
    contains_once(content, anchor, label)
    block = "\n".join(lines) + "\n    };\n    // Only show tilt page turn setting when the QMI8658 IMU is present (X3)"
    content = content.replace(anchor, block, 1)

    write_file(path, content)
    print(f"  {label} patched ({len(lines)} entrie(s)).")


# ---------------------------------------------------------------------------
# SettingsActivity.h  (SettingAction enum, categoryCount, pluginsSettings vec)
# ---------------------------------------------------------------------------

def patch_settings_activity_h(repo_dir, selected: List[SelectedPlugin]):
    path = find_first("SettingsActivity.h", repo_dir)
    content = read_file(path)
    label = "SettingsActivity.h"

    includes = [inc.header for sp in selected for inc in sp.manifest.includes
                if inc.target == "settings_activity_h"]
    if includes:
        include_anchor = "#include <I18n.h>"
        contains_once(content, include_anchor, label)
        content = content.replace(include_anchor,
                                   include_anchor + "".join(f'\n#include "{h}"' for h in includes), 1)

    actions = [a.name for sp in selected for a in sp.manifest.setting_actions]
    if actions:
        m = re.search(r"enum class SettingAction \{.*?\n(\};)", content, re.DOTALL)
        if not m:
            raise PatchError(f"{label}: could not locate `enum class SettingAction {{ ... }};` block")
        insertion = "".join(f"  {name},\n" for name in actions)
        content = content[:m.start(1)] + insertion + content[m.start(1):]

    if uses_plugins_tab(selected):
        cat_anchor = "static constexpr int categoryCount = 4;"
        contains_once(content, cat_anchor, label)
        content = content.replace(cat_anchor, "static constexpr int categoryCount = 5;", 1)

        vec_anchor = "  std::vector<SettingInfo> systemSettings;"
        contains_once(content, vec_anchor, label)
        content = content.replace(vec_anchor,
                                   vec_anchor + "\n  std::vector<SettingInfo> pluginsSettings;", 1)

    write_file(path, content)
    print(f"  {label} patched ({len(includes)} include(s), {len(actions)} action(s)).")


# ---------------------------------------------------------------------------
# SettingsActivity.cpp  (the big one: tab, list population, labels, values,
# switch-case action dispatch)
# ---------------------------------------------------------------------------

def patch_settings_activity_cpp(repo_dir, selected: List[SelectedPlugin]):
    path = find_first("SettingsActivity.cpp", repo_dir)
    content = read_file(path)
    label = "SettingsActivity.cpp"

    includes = [inc.header for sp in selected for inc in sp.manifest.includes
                if inc.target == "settings_activity_cpp"]
    if includes:
        anchor = '#include "SettingsList.h"'
        contains_once(content, anchor, label)
        content = content.replace(anchor, anchor + "".join(f'\n#include "{h}"' for h in includes), 1)

    tab_entries = [(sp, e) for sp in selected for e in sp.manifest.plugins_tab_entries]

    if uses_plugins_tab(selected):
        # 1) categoryNames array gains a virtual "Plugins" slot (StrId::STR_NONE_OPT).
        cat_anchor = (
            'const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,\n'
            '                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};'
        )
        cat_replacement = (
            'const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,\n'
            '                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM,\n'
            '                                                              StrId::STR_NONE_OPT};'
        )
        content = replace_once(content, cat_anchor, cat_replacement, label)

        # 2) rebuildSettingsLists(): clear the plugins vector too.
        clear_anchor = "  systemSettings.clear();"
        content = replace_once(content, clear_anchor, clear_anchor + "\n  pluginsSettings.clear();", label)

        # 3) Populate pluginsSettings once, from every selected entry that
        #    shows on-device (all "action" entries; enum/toggle entries
        #    unless explicitly hidden).
        push_lines = []
        for sp, e in tab_entries:
            if e.kind == "action":
                push_lines.append(
                    "  pluginsSettings.push_back(SettingInfo::Action(\n"
                    f"    StrId::STR_NONE_OPT, SettingAction::{e.action_name}\n"
                    "  ));"
                )
            elif e.kind in ("enum", "toggle") and e.show_on_device:
                n = len(e.option_labels) if e.option_labels else 2
                dummy_opts = ", ".join(["StrId::STR_NONE_OPT"] * n)
                if e.kind == "enum":
                    push_lines.append(
                        "  pluginsSettings.push_back(SettingInfo::Enum(\n"
                        f"    StrId::STR_NONE_OPT, &CrossPointSettings::{e.key},\n"
                        f"    {{{dummy_opts}}}, \"{e.key}\", StrId::STR_NONE_OPT\n"
                        "  ));"
                    )
                else:
                    push_lines.append(
                        "  pluginsSettings.push_back(SettingInfo::Toggle(\n"
                        f"    StrId::STR_NONE_OPT, &CrossPointSettings::{e.key}, \"{e.key}\"\n"
                        "  ));"
                    )
        populate_anchor = (
            '  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));'
        )
        content = replace_once(content, populate_anchor,
                                populate_anchor + ("\n" + "\n".join(push_lines) if push_lines else ""), label)

        # 4) The two `switch (selectedCategoryIndex) { case 0: ... case 3: ... }`
        #    blocks (rebuildSettingsLists + loop()'s hasChangedCategory branch)
        #    both need a case 4. Match structurally so indentation differences
        #    between the two occurrences don't matter.
        case_pattern = re.compile(
            r"([ \t]*)case 3:\n([ \t]*)currentSettings = &systemSettings;\n[ \t]*break;\n"
        )

        def _add_case4(m):
            case_indent, body_indent = m.group(1), m.group(2)
            return m.group(0) + f"{case_indent}case 4:\n{body_indent}currentSettings = &pluginsSettings;\n{body_indent}break;\n"

        new_content, n_subs = case_pattern.subn(_add_case4, content)
        if n_subs != 2:
            raise PatchError(f"{label}: expected 2 `case 3:` switch blocks, found {n_subs}")
        content = new_content

        # 5) Tab bar label: index 4 renders literally as "Plugins".
        tabs_anchor = (
            '  std::vector<TabInfo> tabs;\n'
            '  tabs.reserve(categoryCount);\n'
            '  for (int i = 0; i < categoryCount; i++) {\n'
            '    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});\n'
            '  }'
        )
        tabs_replacement = (
            '  std::vector<TabInfo> tabs;\n'
            '  tabs.reserve(categoryCount);\n'
            '  for (int i = 0; i < categoryCount; i++) {\n'
            '    const char* tabLabel = (i == 4) ? "Plugins" : I18N.get(categoryNames[i]);\n'
            '    tabs.push_back({tabLabel, selectedCategoryIndex == i});\n'
            '  }'
        )
        content = replace_once(content, tabs_anchor, tabs_replacement, label)

        # 6) confirmLabel / "next category" hint also needs to say "Plugins".
        confirm_anchor = (
            '  const auto confirmLabel =\n'
            '      (selectedSettingIndex == 0)\n'
            '          ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])\n'
            '          : (selectedSettingIndex > 0 && (*currentSettings)[selectedSettingIndex - 1].nameId == StrId::STR_TIME_TO_SLEEP\n'
            '                 ? tr(STR_SELECT)\n'
            '                 : tr(STR_TOGGLE));'
        )
        confirm_replacement = (
            '  const int nextCatIndex = (selectedCategoryIndex + 1) % categoryCount;\n'
            '  const char* nextCatLabel = (nextCatIndex == 4) ? "Plugins" : I18N.get(categoryNames[nextCatIndex]);\n'
            '  const auto confirmLabel =\n'
            '      (selectedSettingIndex == 0)\n'
            '          ? nextCatLabel\n'
            '          : (selectedSettingIndex > 0 && (*currentSettings)[selectedSettingIndex - 1].nameId == StrId::STR_TIME_TO_SLEEP\n'
            '                 ? tr(STR_SELECT)\n'
            '                 : tr(STR_TOGGLE));'
        )
        content = replace_once(content, confirm_anchor, confirm_replacement, label)

        # 7) Row label lambda: plugin rows have nameId == STR_NONE_OPT, so they
        #    need their real label supplied out-of-band.
        label_lines = []
        for sp, e in tab_entries:
            if e.kind == "action":
                label_lines.append(
                    f'          if (s.type == SettingType::ACTION && s.action == SettingAction::{e.action_name}) '
                    f'return "{e.label}";'
                )
            elif e.key:
                label_lines.append(
                    f'          if (s.key && std::string(s.key) == "{e.key}") return "{e.label}";'
                )
        label_anchor = (
            '      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,'
        )
        label_replacement = (
            '      [&settings, this](int index) -> std::string {\n'
            '        if (selectedCategoryIndex == 4) {\n'
            '          const auto& s = settings[index];\n'
            + "\n".join(label_lines) + "\n"
            '        }\n'
            '        return std::string(I18N.get(settings[index].nameId));\n'
            '      }, nullptr, nullptr,'
        )
        content = replace_once(content, label_anchor, label_replacement, label)

        # 8) Value text: plugin-owned enum keys and action rows get custom
        #    rendering; anything else falls through to the original chain
        #    completely unchanged.
        value_branches = []
        for sp, e in tab_entries:
            if e.kind == "enum" and e.value_text_expr:
                value_branches.append(
                    f'        if (setting.key && std::string(setting.key) == "{e.key}") {{\n'
                    f'          const uint8_t value = SETTINGS.*(setting.valuePtr);\n'
                    f'          valueText = {e.value_text_expr};\n'
                    f'        }} else '
                )
            elif e.kind == "action":
                value_branches.append(
                    f'        if (setting.type == SettingType::ACTION && setting.action == SettingAction::{e.action_name}) {{\n'
                    f'          valueText = "{e.action_value_text}";\n'
                    f'        }} else '
                )
        if value_branches:
            value_anchor = '        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {'
            value_replacement = "".join(value_branches) + value_anchor
            content = replace_once(content, value_anchor, value_replacement, label)

        # 9) switch (setting.action) { ... } dispatch: launch the activity.
        case_lines = []
        for sp, e in tab_entries:
            if e.kind == "action" and e.activity_launch_expr:
                case_lines.append(
                    f"      case SettingAction::{e.action_name}:\n"
                    f"        startActivityForResult({e.activity_launch_expr}, resultHandler);\n"
                    f"        break;"
                )
        if case_lines:
            none_anchor = "      case SettingAction::None:\n        // Do nothing\n        break;"
            content = replace_once(content, none_anchor, "\n".join(case_lines) + "\n" + none_anchor, label)

    overrides = [o for sp in selected for o in sp.manifest.enum_value_overrides]
    if overrides:
        enum_anchor = (
            '        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {\n'
            '          const uint8_t value = SETTINGS.*(setting.valuePtr);\n'
            '          valueText = I18N.get(setting.enumValues[value]);\n'
            '        } else if (setting.type == SettingType::ENUM && setting.valueGetter) {'
        )
        by_key = {}
        for o in overrides:
            by_key.setdefault(o.key, []).append(o)
        branches = ""
        for key, ovs in by_key.items():
            for i, o in enumerate(ovs):
                kw = "if" if i == 0 else "} else if"
                branches += (f'          {kw} (setting.key && std::string(setting.key) == "{key}" && {o.condition_expr}) {{\n'
                             f'            valueText = "{o.text}";\n')
            branches += "          } else {\n            valueText = I18N.get(setting.enumValues[value]);\n          }\n"
        enum_replacement = (
            '        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {\n'
            '          const uint8_t value = SETTINGS.*(setting.valuePtr);\n'
            + branches +
            '        } else if (setting.type == SettingType::ENUM && setting.valueGetter) {'
        )
        content = replace_once(content, enum_anchor, enum_replacement, label)

    toggle_hooks = [h for sp in selected for h in sp.manifest.toggle_hooks]
    if toggle_hooks:
        tail_anchor = (
            "  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);\n"
            "  SETTINGS.saveToFile();\n"
            "  rebuildSettingsLists();\n"
            "  selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);\n"
            "}"
        )
        hook_block = ""
        for h in toggle_hooks:
            hook_block += f'  if (setting.key && std::string(setting.key) == "{h.key}") {{\n{h.code}\n  }}\n'
        content = replace_once(content, tail_anchor, hook_block + tail_anchor, label)

    write_file(path, content)
    print(f"  {label} patched ({len(tab_entries)} Plugins-tab row(s)).")


# ---------------------------------------------------------------------------
# CrossPointWebServer.cpp  (web UI name/category + enum option overrides)
# ---------------------------------------------------------------------------

def patch_web_server(repo_dir, selected: List[SelectedPlugin]):
    path = find_first("CrossPointWebServer.cpp", repo_dir)
    content = read_file(path)
    label = "CrossPointWebServer.cpp"

    keyed_entries = []
    for sp in selected:
        for e in sp.manifest.plugins_tab_entries:
            if e.key:
                keyed_entries.append(e)
        # settings_list_entries with no PluginsTabEntry counterpart (e.g. a
        # plugin might declare a raw entry directly) don't get name overrides
        # automatically since we don't know their key/label here - plugins
        # should use PluginsTabEntry (with show_on_device=False) for that.

    appends = [a for sp in selected for a in sp.manifest.web_option_appends]

    if not keyed_entries and not appends:
        return

    hidden = [e.key for e in keyed_entries if e.hidden_from_web]
    named = [e for e in keyed_entries if not e.hidden_from_web]

    if hidden:
        hide_cond = " || ".join(f'strcmp(s.key, "{k}") == 0' for k in hidden)
        skip_line = f'    if (s.key && ({hide_cond})) continue;\n'
    else:
        skip_line = ""

    if named:
        name_block = "    if (s.key) {\n"
        for i, e in enumerate(named):
            kw = "if" if i == 0 else "} else if"
            name_block += f'      {kw} (strcmp(s.key, "{e.key}") == 0) {{\n'
            name_block += f'        doc["name"] = "{e.label}";\n'
            name_block += '        doc["category"] = "Plugins";\n'
        name_block += "      }\n    }\n"
    else:
        name_block = ""

    if skip_line or name_block:
        anchor = '    doc["category"] = I18N.get(s.category);\n\n    switch (s.type) {'
        contains_once(content, anchor, label)
        replacement = '    doc["category"] = I18N.get(s.category);\n\n' + skip_line + name_block + '\n    switch (s.type) {'
        content = content.replace(anchor, replacement, 1)

    enum_entries = [e for e in named if e.kind == "enum" and e.option_labels]
    if enum_entries:
        opt_block = ""
        for i, e in enumerate(enum_entries):
            kw = "if" if i == 0 else "} else if"
            opt_block += f'          {kw} (strcmp(s.key, "{e.key}") == 0) {{\n'
            opt_block += '            JsonArray opts = doc["options"].to<JsonArray>();\n'
            for lbl in e.option_labels:
                opt_block += f'            opts.add("{lbl}");\n'
        opt_block += "          }\n"
        opt_wrapped = "        if (s.key) {\n" + opt_block + "        }\n"

        enum_anchor = (
            '          for (const auto& opt : s.enumValues) {\n'
            '            options.add(I18N.get(opt));\n'
            '          }\n'
            '        }\n'
            '        break;\n'
        )
        contains_once(content, enum_anchor, label)
        content = content.replace(enum_anchor, enum_anchor[:-len('        break;\n')] + opt_wrapped + '        break;\n', 1)

    if appends:
        by_key = {}
        for a in appends:
            by_key.setdefault(a.key, []).append(a.label)
        append_block = "        if (s.key) {\n"
        for key, labels in by_key.items():
            append_block += f'          if (strcmp(s.key, "{key}") == 0) {{\n'
            for lbl in labels:
                append_block += f'            options.add("{lbl}");\n'
            append_block += "          }\n"
        append_block += "        }\n"

        loop_anchor = (
            '          for (const auto& opt : s.enumValues) {\n'
            '            options.add(I18N.get(opt));\n'
            '          }\n'
            '        }\n'
        )
        contains_once(content, loop_anchor, label)
        content = content.replace(loop_anchor, loop_anchor + append_block, 1)

    write_file(path, content)
    print(f"  {label} patched ({len(keyed_entries)} key(s), {len(hidden)} hidden, {len(appends)} option append(s)).")


# ---------------------------------------------------------------------------
# main.cpp  (early_boot / post_boot hooks, includes)
# ---------------------------------------------------------------------------

def patch_main_cpp(repo_dir, selected: List[SelectedPlugin]):
    path = find_first("main.cpp", repo_dir)
    content = read_file(path)
    label = "main.cpp"

    includes = [inc.header for sp in selected for inc in sp.manifest.includes if inc.target == "main_cpp"]
    if includes:
        anchor = '#include "CrossPointSettings.h"'
        contains_once(content, anchor, label)
        content = content.replace(anchor, anchor + "".join(f'\n#include "{h}"' for h in includes), 1)

    early_hooks = [h.code for sp in selected for h in sp.manifest.main_hooks if h.point == "early_boot"]
    if early_hooks:
        anchor = "  RECENT_BOOKS.loadFromFile();\n  I18N.setLanguage(static_cast<Language>(SETTINGS.language));"
        contains_once(content, anchor, label)
        block = "  RECENT_BOOKS.loadFromFile();\n" + "\n".join(early_hooks) + \
                "\n  I18N.setLanguage(static_cast<Language>(SETTINGS.language));"
        content = content.replace(anchor, block, 1)

    display_hooks = [h.code for sp in selected for h in sp.manifest.main_hooks if h.point == "post_display_setup"]
    if display_hooks:
        anchor = "  setupDisplayAndFonts(resume != BootResume::Splash);\n\n  switch (resume) {"
        contains_once(content, anchor, label)
        block = "  setupDisplayAndFonts(resume != BootResume::Splash);\n\n" + \
                "\n".join(display_hooks) + "\n\n  switch (resume) {"
        content = content.replace(anchor, block, 1)

    post_hooks = [h.code for sp in selected for h in sp.manifest.main_hooks if h.point == "post_boot"]
    if post_hooks:
        anchor = "  // Ensure we're not still holding the power button before leaving setup\n  waitForPowerRelease();"
        contains_once(content, anchor, label)
        block = "\n".join(post_hooks) + "\n\n" + \
                "  // Ensure we're not still holding the power button before leaving setup\n  waitForPowerRelease();"
        content = content.replace(anchor, block, 1)

    write_file(path, content)
    print(f"  {label} patched ({len(includes)} include(s), {len(early_hooks)} early hook(s), "
          f"{len(display_hooks)} post-display hook(s), {len(post_hooks)} post hook(s)).")


# ---------------------------------------------------------------------------
# Reader dark-mode-style invert hook (Epub/Txt/Xtc)
# ---------------------------------------------------------------------------

def patch_reader_invert_hooks(repo_dir, selected: List[SelectedPlugin]):
    hooks = [sp.manifest.reader_invert_hook for sp in selected if sp.manifest.reader_invert_hook]
    if not hooks:
        return
    if len(hooks) > 1:
        raise PatchError("More than one plugin declared a reader_invert_hook - only one is supported at a time.")
    hook = hooks[0]
    predicate = hook.predicate_expr

    # --- EpubReaderActivity.cpp ---
    path = find_first("EpubReaderActivity.cpp", repo_dir)
    content = read_file(path)
    label = "EpubReaderActivity.cpp"

    inc_anchor = '#include "EpubReaderActivity.h"'
    contains_once(content, inc_anchor, label)
    content = content.replace(inc_anchor, inc_anchor + f'\n#include "{hook.include_header}"', 1)

    main_anchor = (
        '  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);\n'
        '  renderStatusBar();\n'
        '  const auto tBwRender = millis();'
    )
    main_replacement = (
        '  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);\n'
        '  renderStatusBar();\n'
        f'  if ({predicate}) renderer.invertScreen();\n'
        '  const auto tBwRender = millis();'
    )
    content = replace_once(content, main_anchor, main_replacement, label)

    image_anchor = (
        '    int16_t imgX, imgY, imgW, imgH;\n'
        '    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {\n'
        '      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);\n'
        '      renderer.displayBuffer(HalDisplay::FAST_REFRESH);\n'
        '\n'
        '      // Re-render page content to restore images into the blanked area\n'
        '      // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)\n'
        '      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);\n'
        '      renderer.displayBuffer(HalDisplay::FAST_REFRESH);\n'
        '    } else {\n'
        '      renderer.displayBuffer(HalDisplay::HALF_REFRESH);\n'
        '    }'
    )
    image_replacement = (
        '    int16_t imgX, imgY, imgW, imgH;\n'
        '    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {\n'
        f'      const bool pluginDarkMode = ({predicate});\n'
        '      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, pluginDarkMode);\n'
        '      if (pluginDarkMode) renderer.invertScreen();\n'
        '      renderer.displayBuffer(HalDisplay::FAST_REFRESH);\n'
        '\n'
        '      // Re-render page content to restore images into the blanked area\n'
        '      // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)\n'
        '      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);\n'
        '      if (pluginDarkMode) renderer.invertScreen();\n'
        '      renderer.displayBuffer(HalDisplay::FAST_REFRESH);\n'
        '    } else {\n'
        '      renderer.displayBuffer(HalDisplay::HALF_REFRESH);\n'
        '    }'
    )
    content = replace_once(content, image_anchor, image_replacement, label)
    write_file(path, content)
    print(f"  {label} patched (invert hook).")

    # --- TxtReaderActivity.cpp ---
    path = find_first("TxtReaderActivity.cpp", repo_dir)
    content = read_file(path)
    label = "TxtReaderActivity.cpp"

    inc_anchor = '#include "TxtReaderActivity.h"'
    contains_once(content, inc_anchor, label)
    content = content.replace(inc_anchor, inc_anchor + f'\n#include "{hook.include_header}"', 1)

    txt_anchor = (
        '  // BW rendering\n'
        '  renderLines();\n'
        '  renderStatusBar();\n'
        '\n'
        '  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);'
    )
    txt_replacement = (
        '  // BW rendering\n'
        '  renderLines();\n'
        '  renderStatusBar();\n'
        f'  if ({predicate}) renderer.invertScreen();\n'
        '\n'
        '  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);'
    )
    content = replace_once(content, txt_anchor, txt_replacement, label)
    write_file(path, content)
    print(f"  {label} patched (invert hook).")

    # --- XtcReaderActivity.cpp (1-bit path only; see README note on the
    #     2-bit grayscale pipeline) ---
    path = find_first("XtcReaderActivity.cpp", repo_dir)
    content = read_file(path)
    label = "XtcReaderActivity.cpp"

    inc_anchor = '#include "XtcReaderActivity.h"'
    contains_once(content, inc_anchor, label)
    content = content.replace(inc_anchor, inc_anchor + f'\n#include "{hook.include_header}"', 1)

    xtc_anchor = (
        '  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);\n'
        '\n'
        '  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", currentPage + 1, xtc->getPageCount(), bitDepth);'
    )
    xtc_replacement = (
        f'  if ({predicate}) renderer.invertScreen();\n'
        '  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);\n'
        '\n'
        '  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", currentPage + 1, xtc->getPageCount(), bitDepth);'
    )
    content = replace_once(content, xtc_anchor, xtc_replacement, label)
    write_file(path, content)
    print(f"  {label} patched (invert hook, 1-bit path only).")


# ---------------------------------------------------------------------------
# platformio.ini / translation files
# ---------------------------------------------------------------------------

def patch_platformio_ini(repo_dir, selected: List[SelectedPlugin]):
    flags = []
    for sp in selected:
        for f in sp.manifest.platformio_flags:
            if f.line not in flags:
                flags.append(f.line)
    if not flags:
        return

    path = os.path.join(repo_dir, "platformio.ini")
    if not os.path.exists(path):
        raise PatchError("platformio.ini not found")
    content = read_file(path)
    if all(f in content for f in flags):
        return

    anchor = "[env:default]\nextends = base\nbuild_flags =\n"
    if anchor not in content:
        raise PatchError("platformio.ini: could not find [env:default] build_flags anchor")
    addition = "".join(f"  {f}\n" for f in flags if f not in content)
    content = content.replace(anchor, anchor + addition, 1)
    write_file(path, content)
    print(f"  platformio.ini patched ({len(flags)} flag(s)).")


def patch_translation_files(repo_dir, selected: List[SelectedPlugin]):
    entries = [e for sp in selected for e in sp.manifest.translation_entries]
    if not entries:
        return

    yaml_dir = os.path.join(repo_dir, "lib", "I18n", "translations")
    if not os.path.isdir(yaml_dir):
        raise PatchError(f"translation directory not found: {yaml_dir}")

    yaml_files = glob.glob(os.path.join(yaml_dir, "*.yaml"))
    if not yaml_files:
        raise PatchError("no translation YAML files found")

    patched = 0
    for yf in yaml_files:
        content = read_file(yf)
        lines = content.splitlines(keepends=True)
        new_lines = []
        for line in lines:
            new_lines.append(line)
            for e in entries:
                if line.startswith(e.after_key + ":") and f'{e.key}:' not in content:
                    new_lines.append(f'{e.key}: "{e.value}"\n')
        write_file(yf, "".join(new_lines))
        patched += 1
    print(f"  Translation files patched ({patched} file(s)).")


# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------

def apply(ctx: Context, selected: List[SelectedPlugin]):
    """Apply every selected plugin's contributions to repo_dir in one
    consistent pass. `selected` should already be in a stable, deterministic
    order (install.py sorts by plugin name)."""
    repo_dir = ctx.repo_dir

    validate_manifests(selected)

    ordered_pre = sorted(selected, key=lambda sp: (sp.manifest.phase, sp.name))
    for sp in ordered_pre:
        if sp.manifest.pre_patch:
            print(f"  [{sp.name}] running pre-patch step...")
            sp.manifest.pre_patch(ctx)

    print("  Copying plugin source files...")
    copy_source_files(repo_dir, selected)

    print("  Patching CrossPointSettings.h...")
    patch_cross_point_settings_h(repo_dir, selected)

    print("  Patching SettingsList.h...")
    patch_settings_list_h(repo_dir, selected)

    print("  Patching SettingsActivity.h...")
    patch_settings_activity_h(repo_dir, selected)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_activity_cpp(repo_dir, selected)

    print("  Patching CrossPointWebServer.cpp...")
    patch_web_server(repo_dir, selected)

    print("  Patching main.cpp...")
    patch_main_cpp(repo_dir, selected)

    print("  Patching reader activities (invert hook)...")
    patch_reader_invert_hooks(repo_dir, selected)

    print("  Patching platformio.ini...")
    patch_platformio_ini(repo_dir, selected)

    print("  Patching translation files...")
    patch_translation_files(repo_dir, selected)

    ordered_post = sorted(selected, key=lambda sp: (sp.manifest.phase, sp.name))
    for sp in ordered_post:
        if sp.manifest.post_patch:
            print(f"  [{sp.name}] running post-patch step...")
            sp.manifest.post_patch(ctx)
