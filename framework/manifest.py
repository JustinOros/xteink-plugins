"""
Declarative contribution model for xteink plugins.

The old design had every plugin's patch.py directly rewrite shared CrossPoint
source files with hand-written string/regex surgery, and each plugin's patch
script *also* hardcoded literal snippets belonging to other plugins (e.g.
darkmode's patch.py contained the exact text of the smallerfonts plugin's
settings entry, and referenced SmallerFontsPlugin/CrossPointSettings::
smallerFontsMode directly) so that things would still line up if both were
installed together. That makes every plugin's correctness depend on which
*other* plugins happen to be installed and in what order their patches ran -
which is exactly why installing a subset (or adding a new plugin) breaks
things.

The fix: plugins no longer touch shared files at all. They describe *what*
they need in a shared UI/struct/behaviour via small declarative objects
below. A single engine (framework/engine.py) collects the contributions of
whichever plugins were actually selected, and applies each shared file
exactly once, deriving the whole "Plugins" tab / settings struct / web UI
from the current selection instead of guessing at a partially-patched file's
history.
"""

from dataclasses import dataclass, field
from typing import Callable, List, Optional


@dataclass
class SourceFile:
    """A plugin-owned .h/.cpp file to copy into the firmware tree."""
    src_name: str
    dest_subdir: str  # relative to repo_dir, e.g. "src/activities/settings"


@dataclass
class Include:
    """An extra #include line needed in one of the shared files."""
    header: str
    target: str  # "cross_point_settings_h" | "settings_activity_h" | "settings_activity_cpp" | "main_cpp"


@dataclass
class SettingsField:
    """A member to add to the CrossPointSettings struct (persisted state)."""
    decl: str  # e.g. "uint8_t darkModeState = 0;"


@dataclass
class SettingsListEntry:
    """A literal SettingInfo::... initializer added to the master settings list
    (src/SettingsList.h). Used by save/load and by the web UI's full settings
    dump. Do NOT give these a StrId category other than STR_NONE_OPT - visible
    Plugins-tab rows are declared via PluginsTabEntry below instead, so they
    aren't double-counted."""
    cpp: str  # trailing comma, no trailing newline needed


@dataclass
class SettingActionEnumValue:
    """Requests a new enum class SettingAction member."""
    name: str


@dataclass
class PluginsTabEntry:
    """One row rendered in the on-device 'Plugins' settings tab (and mirrored
    into the web UI). kind is one of:
      - "enum":   cycles setting.key through option_labels; value_text_expr
                  (a C++ expression using `value`) renders the current value.
      - "toggle": on/off backed by a uint8_t field.
      - "string": free text field (e.g. an API token), usually obfuscated.
      - "action": launches an activity; doesn't read/write a settings field.
    """
    label: str
    kind: str
    key: Optional[str] = None
    option_labels: Optional[List[str]] = None       # web UI + on-device generic fallback text
    value_text_expr: Optional[str] = None           # e.g. 'DarkModePlugin::stateName(static_cast<DarkModeState>(value))'
    obfuscated: bool = False
    hidden_from_web: bool = False
    show_on_device: bool = True                      # False = web/API-only (e.g. an API token field)
    action_name: Optional[str] = None                # must match a SettingActionEnumValue.name
    action_value_text: str = "Launch"
    activity_launch_expr: Optional[str] = None        # e.g. 'std::make_unique<PongActivity>(renderer, mappedInput)'
    activity_needs_rebuild: bool = False              # result handler should call rebuildSettingsLists()


@dataclass
class EnumValueOverride:
    """Overrides the on-device valueText for one specific value of an
    EXISTING built-in enum setting (as opposed to PluginsTabEntry, which
    describes a brand new plugin-owned setting). Used by e.g. bookerly to
    make the built-in `fontFamily` setting show "Bookerly" for its new
    enum value, while every other value keeps rendering normally."""
    key: str
    condition_expr: str  # e.g. "value == CrossPointSettings::BOOKERLY"
    text: str


@dataclass
class WebOptionAppend:
    """Appends one extra dropdown option string to an EXISTING built-in
    enum setting's web-UI options array (paired with EnumValueOverride
    above for the on-device side)."""
    key: str
    label: str


@dataclass
class ToggleHook:
    """Custom logic that runs inside SettingsActivity::toggleCurrentSetting()
    right after a matching setting's value has been changed, before the
    generic save+rebuild tail. Used for things like lockscreen's "just
    enabled, no PIN set yet -> launch PIN-creation activity instead of a
    plain save" behaviour. `code` may end with `return;` to skip the
    generic tail entirely for that keypress."""
    key: str
    code: str


@dataclass
class MainHook:
    """A code block inserted into main.cpp's setup(). Multiple plugins'
    hooks at the same point are concatenated in manifest order - none of
    them need to know about each other."""
    point: str  # "early_boot" (right after RECENT_BOOKS load, before routing -
               #   display/renderer/fonts are NOT ready yet at this point, so
               #   nothing that renders to screen can run here)
               # or "post_display_setup" (right after setupDisplayAndFonts(),
               #   before the boot-resume routing/splash paints anything -
               #   display+fonts ARE ready; use this for anything that needs
               #   to draw a screen, e.g. a lock gate, before Home/Reader
               #   becomes visible)
               # or "post_boot" (once, after the boot-resume routing decision, every boot)
    code: str


@dataclass
class PlatformioFlag:
    line: str


@dataclass
class TranslationEntry:
    after_key: str  # insert immediately after this existing YAML key, in every translation file
    key: str
    value: str


@dataclass
class ReaderInvertHook:
    """Generalized version of darkmode's screen-invert behaviour: a runtime
    predicate that, when true, XORs the framebuffer right after each reader
    page paints (Epub/Txt/Xtc)."""
    predicate_expr: str
    include_header: str


@dataclass
class PluginManifest:
    name: str
    pretty_name: str
    source_files: List[SourceFile] = field(default_factory=list)
    includes: List[Include] = field(default_factory=list)
    settings_fields: List[SettingsField] = field(default_factory=list)
    settings_list_entries: List[SettingsListEntry] = field(default_factory=list)
    setting_actions: List[SettingActionEnumValue] = field(default_factory=list)
    plugins_tab_entries: List[PluginsTabEntry] = field(default_factory=list)
    enum_value_overrides: List[EnumValueOverride] = field(default_factory=list)
    web_option_appends: List[WebOptionAppend] = field(default_factory=list)
    toggle_hooks: List[ToggleHook] = field(default_factory=list)
    main_hooks: List[MainHook] = field(default_factory=list)
    platformio_flags: List[PlatformioFlag] = field(default_factory=list)
    translation_entries: List[TranslationEntry] = field(default_factory=list)
    reader_invert_hook: Optional[ReaderInvertHook] = None

    # Escape hatches for genuinely plugin-specific work that doesn't touch
    # shared files (font generation, credential prompts, copying non-source
    # assets). ctx is a framework.engine.Context.
    pre_patch: Optional[Callable] = None
    post_patch: Optional[Callable] = None

    # Ordering hint only (lower runs earlier among pre_patch/post_patch
    # callbacks - e.g. bookerly's font generation must run before
    # smallerfonts' generic font-resolution wrapping). Shared-file structural
    # patches themselves are NOT order-dependent between plugins.
    phase: int = 0
