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
    preferred = [r for r in results if os.sep + "src" + os.sep in r and os.sep + "test" + os.sep not in r]
    return sorted(preferred or results, key=lambda r: (len(r), r))[0]


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
        include_anchor = "#include <PersistableStore.h>"
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

def _emit_for_tab_entry(e):
    hide = ".withWebHidden()" if e.hidden_from_web else ""
    obf = ".withObfuscated()" if e.obfuscated else ""
    if e.kind == "enum":
        n = len(e.option_labels) if e.option_labels else 2
        dummy_opts = ", ".join(["StrId::STR_NONE_OPT"] * n)
        return (f'  emit(SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::{e.key}, {{{dummy_opts}}}, '
                f'"{e.key}"){obf}{hide});')
    if e.kind == "toggle":
        return (f'  emit(SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::{e.key}, "{e.key}")'
                f'{obf}{hide});')
    if e.kind == "string":
        return (f'  emit(SettingInfo::String(StrId::STR_NONE_OPT, &SETTINGS.{e.key}[0], sizeof(SETTINGS.{e.key}), '
                f'"{e.key}"){obf}{hide});')
    return None


def patch_settings_list_h(repo_dir, selected: List[SelectedPlugin]):
    path = find_first("SettingsList.cpp", repo_dir)
    content = read_file(path)
    label = "SettingsList.cpp"

    lines = []
    for sp in selected:
        for e in sp.manifest.settings_list_entries:
            raw = e.cpp.strip().rstrip(",")
            lines.append(f"  emit({raw});")
        for e in sp.manifest.plugins_tab_entries:
            entry = _emit_for_tab_entry(e)
            if entry:
                lines.append(entry)

    if not lines:
        return

    anchor = "  // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---"
    contains_once(content, anchor, label)
    block = "  // --- Plugins (web-only; on-device rows live in XteinkPluginsActivity) ---\n" + "\n".join(lines) + "\n" + anchor
    content = content.replace(anchor, block, 1)

    write_file(path, content)
    print(f"  {label} patched ({len(lines)} entrie(s)).")


def patch_settings_activity_h(repo_dir, selected: List[SelectedPlugin]):
    return


def _cpp_str(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def generate_plugins_activity(repo_dir, selected: List[SelectedPlugin]):
    menu_order = ["audiobookshelf", "bookseries", "hardcover", "lockscreen", "checkers", "chess", "dinogame", "mazechase", "minesweeper", "pong", "snakegame", "solitaire", "sudoku"]
    ordered = sorted(selected, key=lambda sp: (menu_order.index(sp.name) if sp.name in menu_order else len(menu_order), sp.name))
    entries = [e for sp in ordered for e in sp.manifest.plugins_tab_entries
               if e.kind == "action" or (e.kind in ("enum", "toggle") and e.show_on_device)]
    includes = []
    for sp in selected:
        for inc in sp.manifest.includes:
            if inc.target == "settings_activity_cpp" and inc.header not in includes:
                includes.append(inc.header)
    hooks = [h for sp in selected for h in sp.manifest.toggle_hooks]

    dest = os.path.join(repo_dir, "src", "activities", "settings")
    os.makedirs(dest, exist_ok=True)
    shutil.copy2(os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates", "PluginListDraw.h"),
                 os.path.join(dest, "PluginListDraw.h"))

    header = """#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class XteinkPluginsActivity final : public Activity {
 public:
  explicit XteinkPluginsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("XteinkPlugins", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selected = 0;

  std::string rowTitle(int index) const;
  std::string rowValue(int index) const;
  void activate(int index);
  void afterChange(const char* key);
};
"""

    n = len(entries)
    title_cases, value_cases, activate_cases = [], [], []
    for i, e in enumerate(entries):
        title_cases.append(f'    case {i}:\n      return "{_cpp_str(e.label)}";')
        if e.kind == "action":
            value_cases.append(f'    case {i}:\n      return "{_cpp_str(e.action_value_text)}";')
            launch = e.activity_launch_expr or "nullptr"
            activate_cases.append(
                f"    case {i}:\n"
                f"      startActivityForResult({launch}, [this](const ActivityResult&) {{ requestUpdate(); }});\n"
                f"      break;"
            )
        else:
            labels = e.option_labels or ["Off", "On"]
            count = len(labels)
            if e.kind == "enum" and e.value_text_expr:
                value_cases.append(
                    f"    case {i}: {{\n"
                    f"      const uint8_t value = SETTINGS.{e.key};\n"
                    f"      return {e.value_text_expr};\n"
                    f"    }}"
                )
            else:
                arr = ", ".join(f'"{_cpp_str(x)}"' for x in labels)
                value_cases.append(
                    f"    case {i}: {{\n"
                    f"      static const char* const names[] = {{{arr}}};\n"
                    f"      const uint8_t value = SETTINGS.{e.key};\n"
                    f"      return value < {count} ? names[value] : \"\";\n"
                    f"    }}"
                )
            activate_cases.append(
                f"    case {i}:\n"
                f"      SETTINGS.{e.key} = static_cast<uint8_t>((SETTINGS.{e.key} + 1) % {count});\n"
                f"      SETTINGS.saveToFile();\n"
                f'      afterChange("{e.key}");\n'
                f"      break;"
            )

    hook_code = ""
    for h in hooks:
        hook_code += f'  if (strcmp(key, "{h.key}") == 0) {{\n{h.code}\n  }}\n'

    inc_lines = "".join(f'#include "{h}"\n' for h in includes)
    source = f"""#include "XteinkPluginsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

{inc_lines}#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "PluginListDraw.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {{
constexpr int ROW_COUNT = {n};
}}

void XteinkPluginsActivity::onEnter() {{
  Activity::onEnter();
  selected = 0;
  requestUpdate();
}}

std::string XteinkPluginsActivity::rowTitle(int index) const {{
  switch (index) {{
{chr(10).join(title_cases)}
    default:
      return "";
  }}
}}

std::string XteinkPluginsActivity::rowValue(int index) const {{
  switch (index) {{
{chr(10).join(value_cases)}
    default:
      return "";
  }}
}}

void XteinkPluginsActivity::afterChange(const char* key) {{
  (void)key;
{hook_code}}}

void XteinkPluginsActivity::activate(int index) {{
  switch (index) {{
{chr(10).join(activate_cases)}
    default:
      break;
  }}
  requestUpdate();
}}

void XteinkPluginsActivity::loop() {{
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasBackGesture()) {{
    finish();
    return;
  }}
  {{
    const auto& touchMetrics = UITheme::getInstance().getMetrics();
    const int touchTop = touchMetrics.topPadding + touchMetrics.headerHeight + touchMetrics.verticalSpacing;
    const int touchHeight =
        renderer.getScreenHeight() - touchTop - touchMetrics.buttonHintsHeight - touchMetrics.verticalSpacing * 2;
    int touched = 0;
    const auto touch = pluginui::listTouch(mappedInput, renderer, Rect{{0, touchTop, renderer.getScreenWidth(), touchHeight}},
                                           ROW_COUNT, selected, touched);
    if (touch == MappedInputManager::RowTouch::Down) {{
      selected = touched;
      requestUpdate();
      return;
    }}
    if (touch == MappedInputManager::RowTouch::Tap) {{
      selected = touched;
      activate(selected);
      return;
    }}
  }}
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.wasPressed(MappedInputManager::Button::Power)) {{
    activate(selected);
    return;
  }}
  buttonNavigator.onNext([this] {{
    selected = ButtonNavigator::nextIndex(selected, ROW_COUNT);
    requestUpdate();
  }});
  buttonNavigator.onPrevious([this] {{
    selected = ButtonNavigator::previousIndex(selected, ROW_COUNT);
    requestUpdate();
  }});
}}

void XteinkPluginsActivity::render(RenderLock&&) {{
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{{0, metrics.topPadding, pageWidth, metrics.headerHeight}}, "Plugins");

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  pluginui::drawList(
      renderer, Rect{{0, contentTop, pageWidth, contentHeight}}, ROW_COUNT, selected,
      [this](int i) {{ return rowTitle(i); }}, [this](int i) {{ return rowValue(i); }}, nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}}
"""
    write_file(os.path.join(dest, "XteinkPluginsActivity.h"), header)
    write_file(os.path.join(dest, "XteinkPluginsActivity.cpp"), source)
    print(f"    ✓ XteinkPluginsActivity ({n} row(s), {len(hooks)} hook(s))")


def patch_settings_activity_cpp(repo_dir, selected: List[SelectedPlugin]):
    if not uses_plugins_tab(selected):
        return

    path = find_first("SettingsListActivity.h", repo_dir)
    content = read_file(path)
    label = "SettingsListActivity.h"
    for anchor, repl in (
        ("std::array<freeink::ui::ListItem, SETTINGS_SECTION_COUNT> rowItems_{};",
         "std::array<freeink::ui::ListItem, SETTINGS_SECTION_COUNT + 1> rowItems_{};"),
        ("int listCount() const override { return static_cast<int>(SETTINGS_SECTION_COUNT); }",
         "int listCount() const override { return static_cast<int>(SETTINGS_SECTION_COUNT) + 1; }"),
    ):
        contains_once(content, anchor, label)
        content = content.replace(anchor, repl, 1)
    write_file(path, content)

    path = find_first("SettingsListActivity.cpp", repo_dir)
    content = read_file(path)
    label = "SettingsListActivity.cpp"

    inc_anchor = '#include "SettingsActivity.h"'
    contains_once(content, inc_anchor, label)
    content = content.replace(inc_anchor, inc_anchor + '\n#include "XteinkPluginsActivity.h"', 1)

    rows_anchor = "    item.actionValue = static_cast<int16_t>(i);\n  }\n}"
    contains_once(content, rows_anchor, label)
    rows_block = (
        "    item.actionValue = static_cast<int16_t>(i);\n  }\n"
        "  auto& pluginItem = rowItems_[SETTINGS_SECTION_COUNT];\n"
        '  pluginItem.label = "Plugins";\n'
        "  pluginItem.icon = fui::bitmapFromIcon(icon_library_32);\n"
        "  pluginItem.actionValue = static_cast<int16_t>(SETTINGS_SECTION_COUNT);\n"
        "}"
    )
    content = content.replace(rows_anchor, rows_block, 1)

    act_anchor = "  app.clearTapFlash();\n  openSection(static_cast<SettingsSection>(index));\n}"
    contains_once(content, act_anchor, label)
    act_block = (
        "  app.clearTapFlash();\n"
        "  if (index == static_cast<int>(SETTINGS_SECTION_COUNT)) {\n"
        "    nav.selected = index;\n"
        "    startActivityForResult(std::make_unique<XteinkPluginsActivity>(renderer, mappedInput),\n"
        "                           [this](const ActivityResult&) { requestUpdate(); });\n"
        "    return;\n"
        "  }\n"
        "  openSection(static_cast<SettingsSection>(index));\n}"
    )
    content = content.replace(act_anchor, act_block, 1)
    write_file(path, content)

    generate_plugins_activity(repo_dir, selected)
    print(f"  {label} patched (top-level Plugins row).")


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

    named = [e for e in keyed_entries if not e.hidden_from_web]

    if named:
        name_block = "    if (s.key) {\n"
        for i, e in enumerate(named):
            kw = "if" if i == 0 else "} else if"
            name_block += f'      {kw} (strcmp(s.key, "{e.key}") == 0) {{\n'
            name_block += f'        doc["name"] = "{e.label}";\n'
            name_block += '        doc["category"] = "Plugins";\n'
        name_block += "      }\n    }\n"
        anchor = '    doc["category"] = I18N.get(s.category);\n\n    switch (s.type) {'
        contains_once(content, anchor, label)
        replacement = '    doc["category"] = I18N.get(s.category);\n\n' + name_block + '\n    switch (s.type) {'
        content = content.replace(anchor, replacement, 1)

    enum_entries = [e for e in named if e.kind == "enum" and e.option_labels]
    if enum_entries:
        opt_block = ""
        for e in enum_entries:
            opt_block += f'        if (s.key && strcmp(s.key, "{e.key}") == 0) {{\n'
            opt_block += '          options.clear();\n'
            for lbl in e.option_labels:
                opt_block += f'          options.add("{lbl}");\n'
            opt_block += '        }\n'
        enum_anchor = '        JsonArray options = doc["options"].to<JsonArray>();\n'
        contains_once(content, enum_anchor, label)
        content = content.replace(enum_anchor, enum_anchor + opt_block, 1)

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
            '          for (const auto& opt : s.enumLabels()) {\n'
            '            options.add(I18N.get(opt));\n'
            '          }\n'
            '        }\n'
        )
        contains_once(content, loop_anchor, label)
        content = content.replace(loop_anchor, loop_anchor + append_block, 1)

    write_file(path, content)
    print(f"  {label} patched ({len(keyed_entries)} key(s), {len(appends)} option append(s)).")


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
        if any("lockscreenUnlocked" in code for code in display_hooks):
            boot_anchor = "    case BootResume::Splash:\n      activityManager.goToBoot();"
            contains_once(content, boot_anchor, label)
            content = content.replace(
                boot_anchor,
                "    case BootResume::Splash:\n      if (!lockscreenUnlocked) activityManager.goToBoot();",
                1)

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
    from .manifest import TranslationEntry
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
