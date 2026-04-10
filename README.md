<table>
  <tr>
    <td><img src="screenshot1.jpg?1" height="400"/></td>
    <td><img src="screenshot2.jpg?1" height="400"/></td>
    <td><img src="screenshot3.jpg?1" height="400"/></td>
  </tr>
</table>

# xteink-plugins

A plugin system for customizing and extending https://github.com/crosspoint-reader/crosspoint-reader firmware on your xteink device. Plugins are applied as source-level patches before the firmware is compiled and flashed.

## Plugins

### Dark Mode

Adds a Dark Mode option to the Plugins settings tab and the web interface Settings page. When enabled, the screen is inverted after each page render, producing white-on-black text across all reader formats (EPUB, TXT, and XTC).

State | Effect
------|--------
Disabled | Normal display (default)
Enabled | Screen inverted — white text on black background

---

### Smaller Fonts

Adds a Smaller Fonts option to the Plugins settings tab and the web interface Settings page. When enabled, your chosen reader font is transparently substituted with a smaller variant — no need to change your font preference.

Mode | Effect
-----|--------
Disabled | No change (default)
Smaller | Drops the current font size down by one step (e.g. Bookerly 16 → 14)
Smallest | Drops the current font size down by two steps (e.g. Bookerly 16 → 12)

Supports Bookerly, Noto Sans, and OpenDyslexic. The plugin also generates and embeds Bookerly at 8pt and 10pt — sizes not included in the stock firmware.

---

### Lockscreen

Adds a customizable Lockscreen experience to your xteink device.

This plugin introduces a dedicated lockscreen activity that is shown when the device wakes or powers on.

Features:
- Custom lockscreen activity integrated into firmware
- Four-digit PIN configurable on plugin enable
- Displays on device wake or power-on
- Replaces default wake screen behavior
- Clean UI consistent with CrossPoint Reader

---

### Hardcover Sync

Automatically syncs your reading progress between your xteink device and https://hardcover.app.

- Extracts ISBN metadata from EPUB files to identify books
- Tracks reading progress (page numbers) while you read
- Automatically syncs progress for books with > 0% completion
- Marks books as "Read" when you reach 100%+ completion
- Requires a Hardcover API token configured in Settings

Books without ISBN metadata or at 0% completion are skipped. 100%+ completed books are automatically moved to your "Read" shelf on Hardcover.

Requirements:
- Active internet connection via WiFi
- Hardcover account and API token
- Books with embedded ISBN metadata

## Requirements

- Python 3.10+
- PlatformIO (pio on your PATH)
- git
- Your xteink device connected via USB

Install Python dependencies with:

pip3 install -r requirements.txt

## Usage

From the root of this repository, run:

python3 install.py

To auto-accept all plugin prompts, pass --yes (or -y):

python3 install.py --yes

By default this uses the default build environment. To use a different environment pass --environment (or -e):

python3 install.py --environment slim
python3 install.py --environment gh_release

Flags can be combined:

python3 install.py -y -e gh_release

Environment | Description
------------|-------------
default | Debug logging enabled, version from current git branch (recommended)
gh_release | Info logging only, version hardcoded to release tag
slim | No serial logging, smallest binary size

The installer will:

1. Clone the CrossPoint Reader source repository
2. Prompt you to select which plugins to install and apply them as patches
3. Build the firmware with PlatformIO
4. Auto-detect your device's serial port and flash the firmware

Note: This script modifies and flashes custom firmware to your device. The author accepts no responsibility for any damage that may occur to your device as a result of using this installer.

## Repository Structure

xteink-plugins/
├── install.py              # Interactive installer: clone → patch → build → flash
└── plugins/
    ├── darkmode/
    │   ├── patch.py
    │   ├── DarkModePlugin.h/.cpp
    │   └── DarkModeSettingsPage.h/.cpp
    ├── smallerfonts/
    │   ├── patch.py
    │   ├── SmallerFontsPlugin.h/.cpp
    │   └── SmallerFontsSettingsPage.h/.cpp
    ├── lockscreen/
    │   ├── patch.py
    │   ├── LockscreenPlugin.h/.cpp
    │   ├── LockscreenSettingsPage.h/.cpp
    │   └── LockscreenActivity.h/.cpp
    └── hardcover/
        ├── patch.py
        ├── HardcoverPlugin.h/.cpp
        └── HardcoverSyncActivity.h/.cpp

## Troubleshooting

### Linux: Permission denied when flashing

If you see an error like:
Could not open /dev/ttyACM0, the port is busy or doesn't exist
or Permission denied

Run:

sudo usermod -aG dialout $USER

Log out and log back in for the change to take effect, then re-run install.py.

### Windows: 'pio' is not recognized

If you see:
'pio' is not recognized as an internal or external command

Run in PowerShell:

$env:PATH += ";$env:USERPROFILE\.platformio\penv\Scripts"
[Environment]::SetEnvironmentVariable("PATH", $env:PATH, "User")

If Python was installed from the Microsoft Store:

$scripts = (Get-ChildItem "$env:USERPROFILE\AppData\Local\Packages" -Filter "Scripts" -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.FullName -like "*Python*" } | Select-Object -First 1).FullName
$env:PATH += ";$scripts"
[Environment]::SetEnvironmentVariable("PATH", $env:PATH, "User")

Restart your terminal and re-run install.py.

## Adding a Plugin

1. Create a new directory under plugins/ with your plugin's name.
2. Add a patch.py file with a patch(repo_dir: str) function.

The installer will automatically discover and offer to install any directory under plugins/ that contains a patch.py.
