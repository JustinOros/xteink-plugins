<table>
  <tr>
    <td><img src="screenshot1.jpg?1" height="400"/></td>
    <td><img src="screenshot2.jpg?1" height="400"/></td>
    <td><img src="screenshot3.jpg?1" height="400"/></td>
  </tr>
</table>

# xteink-plugins

A plugin system for customizing and extending https://github.com/crosspoint-reader/crosspoint-reader firmware on your xteink device. Plugins are applied as source-level patches before the firmware is compiled and flashed.

## Compatibility

The installer always builds against the latest crosspoint-reader. Plugins are opened from the Plugins row at the bottom of the main Settings screen.

Working on the latest crosspoint-reader: Audiobookshelf, Hardcover Sync, Lockscreen, Pong Game, Checkers Game, Chess Game, Dino Game, Maze Chase and Snake Game.

Skipped by the installer until they are ported: Dark Mode, Smaller Fonts, Bookerly Font and GitHub Sync. Dark Mode is not needed on the latest crosspoint-reader, which has a built-in Night Mode.

## Plugins

### Dark Mode

Adds a Dark Mode option to the Plugins screen and the web interface Settings page. When enabled, the screen is inverted after each page render, producing white-on-black text across all reader formats (EPUB, TXT, and XTC).

State | Effect
------|--------
Disabled | Normal display (default)
Enabled | Screen inverted — white text on black background

---

### Smaller Fonts

Adds a Smaller Fonts option to the Plugins screen and the web interface Settings page. When enabled, your chosen reader font is transparently substituted with a smaller variant — no need to change your font preference.

Mode | Effect
-----|--------
Disabled | No change (default)
Enabled | Drops the current font size down by one step (e.g. Bookerly 16 → 14)

Supports Noto Serif, Noto Sans, and Bookerly.

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

### Bookerly Font

Adds the Bookerly font to your xteink device, available as a reader font option alongside the built-in Noto Serif, Noto Sans, and OpenDyslexic fonts.

- Generates Bookerly at 12pt, 14pt, 16pt, and 18pt during install
- Selectable via Settings → Reader → Font
- Works with the Smaller Fonts plugin to drop one size step down (e.g. 16 → 14)

---

### Hardcover Sync

Automatically syncs your reading progress between your xteink device and https://hardcover.app.

- Extracts ISBN metadata from EPUB files to identify books, falling back to the Amazon ASIN when no ISBN is present
- Tracks reading progress (page numbers) while you read
- Automatically syncs progress for books with > 0% completion
- Marks books as "Read" when you reach 100%+ completion
- Requires a Hardcover API token configured in Settings

Books with neither an ISBN nor an ASIN, or at 0% completion are skipped. 100%+ completed books are automatically moved to your "Read" shelf on Hardcover.

Requirements:
- Active internet connection via WiFi
- Hardcover account and API token
- Books with embedded ISBN or ASIN metadata

---

### GitHub Sync

Syncs `.epub` files from a private GitHub repository to your device on boot.

- Downloads new or updated books from your configured repo automatically on startup
- Skips files already on the device that haven't changed
- Configurable via Plugins → GitHub Sync (username, personal access token, repo, branch), after flashing

Requirements:
- Active internet connection via WiFi
- GitHub account with a repository containing your `.epub` files
- Personal access token with read-only Contents access to the repo

---

### Pong Game

Adds a classic Pong game against a CPU opponent, launched from the Plugins row in Settings.

- Left/Right (or Up/Down, Back/Confirm) move your paddle in full paddle-width steps
- CPU controls the top paddle and tracks the ball
- First to 7 points wins; any button exits the win/lose screen
- Power always exits the game

---

### Checkers Game

Adds classic checkers, launched from the Plugins row in Settings.

- Choose Player vs Computer or Player vs Player when it launches, so two people can share the screen
- Against the computer, pick Beginner, Casual, Intermediate or Hard; you play white
- Tap a piece to see its legal moves, then tap a highlighted square to move; or use the cursor buttons and Select
- Captures are required, you keep jumping while more jumps are available, and a piece that reaches the far row becomes a king
- Back deselects a piece, or undoes your last move; Power exits

---

### Chess Game

Adds a full chess game against the device, launched from the Plugins row in Settings. You play white.

- Pick a difficulty when it launches: Beginner, Casual, Intermediate or Hard
- On touch devices, tap one of your pieces to see its legal moves, then tap a highlighted square to move; tapping another piece switches to it and tapping an empty square deselects
- Without touch, use the cursor buttons and Select
- Back deselects a piece, or undoes your last move when nothing is selected; Power exits
- Supports castling, en passant, promotion (always to a queen), check, checkmate, stalemate and draws by repetition, the 50 move rule and lack of material
- Higher levels think longer, so a move on Hard can take several seconds

---

### Dino Game

Adds the classic offline dinosaur runner as a full-screen game, launched from the Plugins row in Settings.

- Endless runner: jump the cacti as the game speeds up over time
- Any button except Power jumps; Power exits the game
- On Game Over, any button restarts and Power exits
- A bird flies across the top of the screen for atmosphere - it's decorative only, no need to dodge it
- Score is shown top-right and resets each session

---

### Maze Chase

Adds an original maze game where you clear every pellet while three chasers hunt you, launched from the Plugins row in Settings.

- A new random maze is built each level, and speed increases with each level
- Swipe, tap the side you want to go, or use the 4 front buttons (Left / Right / Up / Down)
- Eat a large pellet in a corner to turn the chasers hollow and eat them for bonus points
- You have 3 lives; any front button or tap restarts after game over and Power exits

---

### Snake Game

Adds a classic Snake game sized to your device's screen, launched from the Plugins row in Settings.

- The 4 front buttons are fixed to Left / Right / Up / Down for this game, with on-screen labels above each button showing the mapping
- Eat food to grow and score points; speed increases as your score climbs
- Hitting a wall or yourself ends the game; any front button restarts and Power exits
- Filling the entire board is a win

---

### Audiobookshelf

Browse and download ebooks from your Audiobookshelf server, launched from the Plugins row in Settings.

- Connects to WiFi first (connect from Settings if it cannot), then asks for Server, User Name and Password
- Choose "Configure from Phone or PC" on the login screen to show a QR code and address, then fill in Server, User Name and Password from a browser on your phone or computer, which is much faster than typing on the device (useful for devices without a touch screen)
- Optional "Save password to SD card (plain text)" checkbox; when unchecked only a login token is kept
- Shows only your ebook library (named Books by default, change it with Audiobookshelf Library Name in the web interface)
- Search, Continue Reading, Recently Added, Recent Series and Discover, with Logout at the bottom
- Selecting a book downloads the epub to the SD card root and opens it
- WiFi is disconnected when you leave the plugin or after 5 minutes without a button press
- Servers hosted under a path such as /audiobookshelf are detected automatically

Requirements:

- Active internet connection via WiFi
- Audiobookshelf account with permission to download

---

## Requirements

- Python 3.10+
- PlatformIO (pio on your PATH)
- git
- Your xteink device connected via USB

Install Python dependencies with:

pip3 install -r requirements.txt

## Windows quick install

Download `install.ps1`, right-click it and choose "Run with PowerShell". If Windows blocks it, open PowerShell and run:

powershell -ExecutionPolicy Bypass -File .\install.ps1

The script installs Git, Python and PlatformIO if they are missing, downloads the plugins, then runs the installer.

## Usage

From the root of this repository, run:

python3 install.py

To auto-accept all plugin prompts and skip the restore/backup and baud rate prompts, pass --yes (or -y):

python3 install.py --yes

By default this uses the default build environment. To use a different environment pass --environment (or -e):

python3 install.py --environment slim
python3 install.py --environment gh_release

Environment | Description
------------|-------------
default | Debug logging enabled, version from current git branch (recommended)
gh_release | Info logging only, version hardcoded to release tag
slim | No serial logging, smallest binary size

Before flashing you'll be asked to pick an upload baud rate:

Rate | Description
-----|------------
115200 | Recommended - most reliable, avoids "chip stopped responding" errors
921600 | Faster - for those feeling froggy

To skip the prompt, pass --baud (or -b):

python3 install.py --baud 115200

Flags can be combined:

python3 install.py -y -e gh_release -b 115200

The installer will:

1. Offer to restore from a previous backup, if one exists in the current directory
2. Offer to back up your current firmware before making any changes
3. Clone the CrossPoint Reader source repository
4. Prompt you to select which plugins to install and apply them as patches
5. Build the firmware with PlatformIO
6. Auto-detect your device's serial port, ask for an upload baud rate, and flash the firmware

Note: This script modifies and flashes custom firmware to your device. The author accepts no responsibility for any damage that may occur to your device as a result of using this installer.

## Repository Structure

```
xteink-plugins/
├── install.py              # Interactive installer: clone → select → apply → build → flash
├── framework/               # Shared plugin engine (see "Adding a Plugin" below)
│   ├── manifest.py          # Declarative contribution types plugins are built from
│   ├── engine.py             # Applies every selected plugin's contributions in one pass
│   └── discovery.py          # Finds plugin.py files under plugins/
├── test_harness.py          # Applies every plugin, alone and in combination, against a
│                             # fresh clone and reports pass/fail - run this after adding
│                             # or editing a plugin
└── plugins/
    ├── darkmode/
    │   ├── plugin.py
    │   └── DarkModePlugin.h/.cpp
    ├── smallerfonts/
    │   ├── plugin.py
    │   └── SmallerFontsPlugin.h/.cpp
    ├── lockscreen/
    │   ├── plugin.py
    │   ├── LockscreenPlugin.h/.cpp
    │   └── LockscreenActivity.h/.cpp
    ├── bookerly/
    │   ├── plugin.py
    │   └── BookerlyPlugin.h/.cpp
    ├── hardcover/
    │   ├── plugin.py
    │   ├── HardcoverPlugin.h/.cpp
    │   └── HardcoverSyncActivity.h/.cpp
    ├── githubsync/
    │   ├── plugin.py
    │   ├── GitHubSync.h/.cpp
    │   └── GitHubSyncSettingsActivity.h/.cpp
    ├── pong/
    │   ├── plugin.py
    │   └── PongActivity.h/.cpp
    ├── checkers/
    │   ├── plugin.py
    │   ├── CheckersEngine.h
    │   └── CheckersActivity.h/.cpp
    ├── chess/
    │   ├── plugin.py
    │   ├── ChessEngine.h
    │   └── ChessActivity.h/.cpp
    ├── dinogame/
    │   ├── plugin.py
    │   └── DinoActivity.h/.cpp
    ├── mazechase/
    │   ├── plugin.py
    │   └── MazeChaseActivity.h/.cpp
    ├── snakegame/
    │   ├── plugin.py
    │   └── SnakeActivity.h/.cpp
    └── audiobookshelf/
        ├── plugin.py
        ├── AudiobookshelfParse.h
        ├── AudiobookshelfPlugin.h/.cpp
        └── AudiobookshelfActivity.h/.cpp
```

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

1. Create a new directory under `plugins/` with your plugin's name.
2. Add a `plugin.py` with a `get_manifest(ctx) -> PluginManifest` function. Use an existing plugin (e.g. `plugins/pong/plugin.py` for a simple action-row example, or `plugins/darkmode/plugin.py` for a settings-backed one) as a template. `framework/manifest.py` documents every contribution type: `SettingsField`, `PluginsTabEntry`, `SettingActionEnumValue`, `MainHook`, `ToggleHook`, `TranslationEntry`, and so on.
3. List any plugin-owned `.h`/`.cpp` files under `source_files` so the installer copies them into the firmware tree - you don't touch any shared CrossPoint file directly.
4. Run `python3 test_harness.py <path-to-a-crosspoint-reader-clone> /tmp/scratch <your-plugin-name>` (or `all` to check every plugin and combination) to confirm it applies cleanly alone and alongside everything else, before shipping it.

The installer automatically discovers and offers to install any directory under `plugins/` that contains a `plugin.py` exposing `get_manifest`.
