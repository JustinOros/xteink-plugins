#!/usr/bin/env python3

import os
import sys
import shutil
import subprocess
import glob
import argparse
import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from framework.discovery import discover_all, build_selected
from framework.engine import apply as apply_plugin_framework, Context, PatchError

REPO_URL    = "https://github.com/crosspoint-reader/crosspoint-reader.git"
REPO_DIR    = "crosspoint-reader"
PLUGINS_DIR = "plugins"

ENVIRONMENTS = {
    "default":          "Xteink X3/X4 (ESP32-C3): debug logging, version from current git branch",
    "gh_release":       "Xteink X3/X4 (ESP32-C3): info logging only, version hardcoded to release tag",
    "slim":             "Xteink X3/X4 (ESP32-C3): no serial logging, smallest binary size",
    "x4pro":            "Xteink X4 Pro (ESP32-S3, touchscreen and frontlight)",
    "x4pro-gh_release": "Xteink X4 Pro (ESP32-S3): info logging only",
    "x4c":              "Xteink X4 Classic (ESP32-S3, buttons only)",
}

PIO_MAX_PYTHON = (3, 13)
USER_QUIT_EXIT_CODE = 3
USE_PIO_BINARY = os.name != "nt" and shutil.which("pio") is not None
PIO_CMD = "pio" if USE_PIO_BINARY else f'"{sys.executable}" -m platformio'

AUTO_ENV_C3 = "default"
AUTO_ENV_S3 = "x4pro"

BAUD_RATES = {
    "1": ("115200", "Recommended - most reliable, avoids 'chip stopped responding'"),
    "2": ("921600", "Faster - for those feeling froggy"),
}
DEFAULT_BAUD = "115200"


def run(cmd, cwd=None, check=True):
    print(f"  $ {cmd}")
    return subprocess.run(cmd, shell=True, cwd=cwd, check=check)


def remove_readonly(func, path, _):
    os.chmod(path, 0o777)
    func(path)


def rmtree(path):
    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=remove_readonly)
    else:
        shutil.rmtree(path, onerror=remove_readonly)


def get_remote_head_sha():
    try:
        result = subprocess.run(
            f"git ls-remote {REPO_URL} HEAD",
            shell=True, capture_output=True, text=True, timeout=10
        )
        if result.returncode == 0 and result.stdout:
            return result.stdout.split()[0]
    except Exception:
        pass
    return None


def get_local_head_sha():
    try:
        result = subprocess.run(
            "git rev-parse HEAD",
            shell=True, capture_output=True, text=True, cwd=REPO_DIR
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except Exception:
        pass
    return None


def find_backup_files():
    return sorted(glob.glob("backup.*.bin"), reverse=True)


def restore_from_backup(yes_all: bool = False):
    """If any backup.*.bin files are sitting in the current directory, offer
    to write one back to the device instead of proceeding with a fresh
    install. This always runs before backup_device() and, if the user goes
    through with a restore, exits the script afterward rather than
    continuing on to clone/patch/build/flash."""
    backups = find_backup_files()
    if not backups:
        return

    if yes_all:
        print(f"  Found {len(backups)} existing backup(s), skipping restore (--yes).")
        return

    print(f"  Found {len(backups)} existing backup{'s' if len(backups) != 1 else ''}:")
    for i, b in enumerate(backups, 1):
        tag = "  ← most recent" if i == 1 else ""
        try:
            size_mb = os.path.getsize(b) / (1024 * 1024)
            size_label = f"  ({size_mb:.1f} MB)"
        except OSError:
            size_label = ""
        print(f"    {i}. {b}{size_label}{tag}")

    answer = input("\n  Restore firmware from backup? [N/y]: ").strip().lower()
    if answer not in ("y", "yes"):
        return

    if len(backups) == 1:
        chosen = backups[0]
    else:
        choice = input(f"\n  Which backup do you want to restore? [1-{len(backups)}] (default: 1): ").strip()
        if not choice:
            chosen = backups[0]
        elif choice.isdigit() and 1 <= int(choice) <= len(backups):
            chosen = backups[int(choice) - 1]
        else:
            sys.exit("ERROR: Invalid choice — aborting restore.")

    print()
    print("  Ensure your xteink device is connected via USB and awake.")
    input("  Press Enter when ready... ")

    ports = detect_serial_ports()
    if not ports:
        sys.exit("ERROR: No USB serial ports detected — cannot restore.")

    cu_ports = [(d, desc) for d, desc in ports if d.startswith("/dev/cu.")]
    recommended = cu_ports[0][0] if cu_ports else ports[0][0]

    print(f"\n  Found {len(ports)} serial port(s):")
    for i, (device, desc) in enumerate(ports, 1):
        tag = "  ← recommended" if device == recommended else ""
        print(f"    {i}. {device}  —  {desc}{tag}")

    port_choice = input(f"\n  Press ENTER to use {recommended}, or enter 1-{len(ports)}: ").strip()
    if not port_choice:
        port = recommended
    elif port_choice.isdigit() and 0 <= int(port_choice) - 1 < len(ports):
        port = ports[int(port_choice) - 1][0]
    else:
        sys.exit("ERROR: Invalid port choice — aborting restore.")

    while True:
        print(f"\n  Writing {chosen} → {port} ...")
        result = subprocess.run(
            f"esptool --port {port} --baud 921600 write-flash 0x0 {chosen}",
            shell=True,
        )

        if result.returncode == 0:
            print(f"\n  ✓ Restored {chosen} to the device.")
            sys.exit(0)

        print("  ✗ Restore failed (USB dropout, wrong port, or device asleep are common causes).")
        retry_choice = input("  [r]etry or [A]bort? [r/A]: ").strip().lower()

        if retry_choice == "r":
            print("\n  Ensure your xteink device is still connected via USB and awake")
            print("  (try a different cable/port if it keeps dropping out).")
            input("  Press Enter to retry... ")
            continue
        else:
            sys.exit("Aborted.")


def backup_device(yes_all: bool = False):
    if yes_all:
        print("  Skipping device backup (--yes).")
        return

    print("  Backup your current firmware before flashing? [Y/n]: ", end="", flush=True)
    answer = input().strip().lower()
    if answer not in ("", "y", "yes"):
        print("  Skipping backup.")
        return

    print()
    print("  Ensure your xteink device is connected via USB and awake.")
    input("  Press Enter when ready... ")

    while True:
        ports = detect_serial_ports()
        if ports:
            break
        print("  No USB serial ports detected.")
        retry_choice = input("  Check the USB connection, then [r]etry or [C]ontinue without backup? [r/C]: ").strip().lower()
        if retry_choice == "r":
            print("\n  Ensure your xteink device is still connected via USB and awake")
            print("  (try a different cable/port if it isn't being detected).")
            input("  Press Enter to retry... ")
            continue
        else:
            print("  Continuing without a backup.")
            return

    cu_ports = [(d, desc) for d, desc in ports if d.startswith("/dev/cu.")]
    recommended = cu_ports[0][0] if cu_ports else ports[0][0]

    print(f"\n  Found {len(ports)} serial port(s):")
    for i, (device, desc) in enumerate(ports, 1):
        tag = "  ← recommended" if device == recommended else ""
        print(f"    {i}. {device}  —  {desc}{tag}")

    choice = input(f"\n  Press ENTER to use {recommended}, or enter 1-{len(ports)}: ").strip()

    if not choice:
        port = recommended
    elif choice.isdigit() and 0 <= int(choice) - 1 < len(ports):
        port = ports[int(choice) - 1][0]
    else:
        print("  Invalid choice — skipping backup.")
        return

    while True:
        timestamp = datetime.datetime.now().strftime("%Y%m%d.%H%M%S")
        filename  = f"backup.{timestamp}.bin"
        print(f"\n  Reading flash from {port} → {filename} ...")

        result = subprocess.run(
            f"esptool --port {port} --baud 921600 read-flash 0x0 ALL {filename}",
            shell=True,
        )

        if result.returncode == 0:
            print(f"  ✓ Backup saved to {filename}")
            print()
            print(f"  To restore your device to this backup, run:")
            print(f"  esptool --port {port} --baud 921600 write-flash 0x0 {filename}")
            break

        # Remove the partial/corrupt file left behind by the failed attempt
        if os.path.exists(filename):
            try:
                os.remove(filename)
            except OSError:
                pass

        print("  ✗ Backup failed (USB dropout, wrong port, or device asleep are common causes).")
        choice = input("  [r]etry, [A]bort, or [c]ontinue without backup? [r/A/c]: ").strip().lower()

        if choice == "r":
            print("\n  Ensure your xteink device is still connected via USB and awake")
            print("  (try a different cable/port if it keeps dropping out).")
            input("  Press Enter to retry... ")
            continue
        elif choice == "c":
            print("  Continuing without a backup.")
            break
        else:
            # Empty input or anything else defaults to the safe choice: abort.
            print("  Aborted.")
            sys.exit(0)
    print()


def clone_repo(force: bool = True):
    print("[1/3] Preparing CrossPoint repository...")

    if not force and os.path.exists(REPO_DIR):
        print("  Existing repo found, checking remote for updates...")
        remote_sha = get_remote_head_sha()
        local_sha  = get_local_head_sha()

        if remote_sha and local_sha and remote_sha == local_sha:
            print(f"  Already up to date ({local_sha[:8]}), skipping clone.")
            return
        elif remote_sha:
            print(f"  Remote has changed ({remote_sha[:8]} vs local {(local_sha or 'unknown')[:8]}), re-cloning...")
        else:
            print("  Could not reach remote, re-using existing repo.")
            return

    if os.path.exists(REPO_DIR):
        print(f"  Removing existing '{REPO_DIR}'...")
        rmtree(REPO_DIR)

    run(f"git -c core.autocrlf=false -c core.eol=lf clone --recursive {REPO_URL} {REPO_DIR}")
    run("git config core.autocrlf false", cwd=REPO_DIR)


def clear_plugin_caches():
    for plugin_name in os.listdir(PLUGINS_DIR):
        cache_dir = os.path.join(PLUGINS_DIR, plugin_name, "__pycache__")
        if os.path.isdir(cache_dir):
            rmtree(cache_dir)


def select_plugins(yes_all: bool = False):
    """Ask the user which plugins to install. Selection only - no file
    touched yet, so nothing here depends on which other plugins are chosen."""
    if not os.path.exists(PLUGINS_DIR):
        print(f"  No '{PLUGINS_DIR}' directory found — skipping plugin step.")
        return []

    available = discover_all(PLUGINS_DIR)
    if not available:
        print("  No plugins with a plugin.py manifest found.")
        return []

    chosen = []
    not_yet_compatible = {"bookerly", "darkmode", "githubsync", "smallerfonts"}
    for plugin_name in sorted(available):
        print(f"\n  Plugin: {plugin_name}")
        if plugin_name in not_yet_compatible:
            print(f"  Skipping {plugin_name} (not yet compatible with the latest CrossPoint).")
            continue
        if yes_all:
            answer = "y"
            print(f"  Install '{plugin_name}'? [Y/n]: Y (--yes)")
        else:
            answer = input(f"  Install '{plugin_name}'? [Y/n]: ").strip().lower()

        if answer in ("", "y", "yes"):
            chosen.append(plugin_name)
        else:
            print(f"  Skipping {plugin_name}.")

    return chosen


def apply_plugins(plugin_names, yes_all: bool = False):
    print("[2/3] Applying plugins...")

    if not plugin_names:
        print("  No plugins selected.")
        return

    print(f"  Selected: {', '.join(plugin_names)}")
    ctx = Context(repo_dir=os.path.abspath(REPO_DIR), yes_all=yes_all, prompt=not yes_all)

    try:
        selected = build_selected(plugin_names, PLUGINS_DIR, ctx)
        apply_plugin_framework(ctx, selected)
    except (PatchError, KeyError) as exc:
        sys.exit(
            f"\nERROR: {exc}\n\n"
            "This usually means the upstream crosspoint-reader source has "
            "changed in a way this plugin set doesn't recognize yet. No "
            "half-applied changes were left in place beyond this point - "
            "please report this so the anchor can be updated."
        )

    print(f"\n  ✓ {len(plugin_names)} plugin(s) installed: {', '.join(plugin_names)}")


def list_likely_serial_ports():
    patterns = [
        "/dev/cu.usb*",
        "/dev/tty.usb*",
        "/dev/cu.wchusb*",
        "/dev/tty.wchusb*",
        "/dev/cu.SLAB*",
        "/dev/tty.SLAB*",
        "/dev/ttyACM*",
        "/dev/ttyUSB*",
    ]
    ports = []
    for pattern in patterns:
        ports.extend(glob.glob(pattern))
    return sorted(set(ports))


def detect_serial_ports():
    port_map: dict[str, str] = {}

    try:
        import serial.tools.list_ports

        usb_keywords = {
            "usb", "cp210", "ftdi", "ch340", "ch9102", "esp",
            "serial", "uart", "prolific", "silabs",
        }
        skip_keywords = {"bluetooth", "bt-", "btle", "airpods", "handsfree"}

        for port in serial.tools.list_ports.comports():
            combined = (port.device + " " + (port.description or "") +
                        " " + (port.hwid or "")).lower()

            if any(s in combined for s in skip_keywords):
                continue
            if any(k in combined for k in usb_keywords):
                port_map[port.device] = port.description or "USB Serial Device"

    except ImportError:
        pass

    for device in list_likely_serial_ports():
        if device not in port_map:
            port_map[device] = "Detected via glob pattern"

    for device in list(port_map):
        if device.startswith("/dev/tty.") and ("/dev/cu." + device[len("/dev/tty."):]) in port_map:
            del port_map[device]

    return sorted(port_map.items())


def esptool_chip_name(port):
    for base in ("esptool", f"{sys.executable} -m esptool"):
        try:
            result = subprocess.run(
                f"{base} --port {port} chip-id",
                shell=True, capture_output=True, text=True, timeout=45,
            )
        except (subprocess.TimeoutExpired, OSError):
            continue
        text = (result.stdout or "") + (result.stderr or "")
        for line in text.splitlines():
            line = line.strip()
            if line.startswith("Chip type:") or line.startswith("Detecting chip type"):
                if "ESP32-S3" in line:
                    return "ESP32-S3"
                if "ESP32-C3" in line:
                    return "ESP32-C3"
            if line.startswith("Chip is "):
                if "ESP32-S3" in line:
                    return "ESP32-S3"
                if "ESP32-C3" in line:
                    return "ESP32-C3"
        if "ESP32-S3" in text:
            return "ESP32-S3"
        if "ESP32-C3" in text:
            return "ESP32-C3"
    return None


def pick_s3_environment(port_description, yes_all):
    label = (port_description or "").lower()
    if "x4_pro" in label or "x4 pro" in label:
        return "x4pro"
    if "x4_classic" in label or "x4 classic" in label:
        return "x4c"
    if yes_all:
        return AUTO_ENV_S3
    print("\n  An ESP32-S3 device was detected. Which one is it?")
    print("    1. Xteink X4 Pro (touchscreen and frontlight)  <- default")
    print("    2. Xteink X4 Classic (buttons only)")
    choice = input("\n  Press ENTER for 1, or enter 1-2: ").strip()
    return "x4c" if choice == "2" else "x4pro"


def auto_detect_environment(yes_all):
    print("Detecting connected device...")
    ports = detect_serial_ports()
    if not ports:
        print(f"  No device detected, using the {AUTO_ENV_C3} (X3/X4) build.")
        print("  If you have an X4 Pro, connect it and re-run, or use: -e x4pro")
        return AUTO_ENV_C3

    cu_ports = [(d, desc) for d, desc in ports if d.startswith("/dev/cu.")]
    ordered = cu_ports + [p for p in ports if p not in cu_ports]
    for device, description in ordered:
        chip = esptool_chip_name(device)
        if chip == "ESP32-C3":
            print(f"  Found {chip} on {device}: Xteink X3/X4")
            return AUTO_ENV_C3
        if chip == "ESP32-S3":
            env = pick_s3_environment(description, yes_all)
            print(f"  Found {chip} on {device}: building for {env}")
            return env

    print(f"  Could not identify the chip, using the {AUTO_ENV_C3} (X3/X4) build.")
    print("  If you have an X4 Pro, use: -e x4pro")
    return AUTO_ENV_C3


def prompt_for_upload_port():
    print("\n[3/3] Select serial port for flashing...")

    max_retries = 3

    for attempt in range(1, max_retries + 1):
        print(f"\n  Attempt {attempt}/{max_retries}")
        print("  Ensure your xteink device is connected via USB and awake.")
        input("  Press Enter when ready... ")

        ports = detect_serial_ports()

        if ports:
            cu_ports = [(d, desc) for d, desc in ports if d.startswith("/dev/cu.")]
            recommended = cu_ports[0][0] if cu_ports else ports[0][0]

            print(f"\n  Found {len(ports)} serial port(s):")
            for i, (device, desc) in enumerate(ports, 1):
                tag = "  ← recommended" if device == recommended else ""
                print(f"    {i}. {device}  —  {desc}{tag}")

            choice = input(f"\n  Press ENTER to use {recommended}, or enter 1-{len(ports)}: ").strip()

            if not choice:
                print(f"  Selected: {recommended}")
                return recommended

            if choice.isdigit():
                idx = int(choice) - 1
                if 0 <= idx < len(ports):
                    print(f"  Selected: {ports[idx][0]}")
                    return ports[idx][0]

            print("  Invalid choice, please try again.")

        else:
            print("  No USB serial ports detected.")
            if attempt < max_retries:
                print("  Make sure the device is connected, powered on, and awake.")

    print("\n  Could not auto-detect a port.")
    print("  Common names:  macOS: /dev/cu.usb*   Linux: /dev/ttyUSB*   Windows: COMx")

    while True:
        port = input("\n  Enter port manually (or 'quit' to exit): ").strip()
        if port.lower() == "quit":
            sys.exit(1)
        if port:
            return port
        print("  Please enter a valid port name.")


def prompt_for_baud_rate(override: str | None, yes_all: bool = False):
    if override:
        print(f"\n  Upload baud rate: {override} (--baud)")
        return override

    if yes_all:
        print(f"\n  Upload baud rate: {DEFAULT_BAUD} (--yes)")
        return DEFAULT_BAUD

    print("\n  Select upload baud rate:")
    for key, (rate, desc) in BAUD_RATES.items():
        tag = "  ← default" if rate == DEFAULT_BAUD else ""
        print(f"    {key}. {rate}  —  {desc}{tag}")

    choice = input(
        f"\n  Press ENTER for default ({DEFAULT_BAUD}), enter 1-{len(BAUD_RATES)}, "
        "or type a custom rate: "
    ).strip()

    if not choice:
        return DEFAULT_BAUD
    if choice in BAUD_RATES:
        return BAUD_RATES[choice][0]
    if choice.isdigit():
        return choice

    print(f"  Invalid input, using default ({DEFAULT_BAUD}).")
    return DEFAULT_BAUD


def build_and_flash(environment: str, baud_rate: str):
    print(f"\nBuilding firmware with PlatformIO (environment: {environment})...")
    run(f"{PIO_CMD} run --environment {environment}", cwd=REPO_DIR)

    local_ini = os.path.join(REPO_DIR, "platformio.local.ini")
    with open(local_ini, "w") as f:
        f.write(f"[env:{environment}]\nupload_speed = {baud_rate}\n")

    while True:
        port = prompt_for_upload_port()
        print(f"\n  Flashing to {port} at {baud_rate} baud...")
        result = run(
            f"{PIO_CMD} run --target upload --environment {environment} --upload-port {port}",
            cwd=REPO_DIR,
            check=False,
        )
        if result.returncode == 0:
            return

        print("\n  Flashing failed. The firmware is already built, so you only need to retry the upload.")
        print("  Try this before retrying:")
        print("    1. Close anything else using the serial port (for example a serial monitor).")
        print("    2. Unplug the device, plug it back in and turn it on.")
        print("    3. Use a data-capable USB cable plugged straight into the computer.")
        print("    4. If it still fails, put the device in flashing mode (hold BOOT, tap RESET, release BOOT).")
        answer = input("\n  Press ENTER to retry flashing, or type q to quit: ").strip().lower()
        if answer in ("q", "quit"):
            sys.exit(USER_QUIT_EXIT_CODE)


def parse_args():
    parser = argparse.ArgumentParser(
        description="xteink Plugin Installer",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    env_help = "\n".join(f"  {name}: {desc}" for name, desc in ENVIRONMENTS.items())
    parser.add_argument(
        "-e", "--environment",
        default=None,
        choices=ENVIRONMENTS.keys(),
        metavar="ENV",
        help=f"PlatformIO build environment (default: auto-detect from the connected device)\n{env_help}",
    )
    parser.add_argument(
        "-y", "--yes",
        action="store_true",
        help="Auto-answer Y to all plugin install prompts, and skip the "
             "restore/backup prompts entirely (fast install, no backup)",
    )
    parser.add_argument(
        "-b", "--baud",
        default=None,
        metavar="RATE",
        help=(
            "Upload baud rate override, skips the interactive prompt "
            f"(default: {DEFAULT_BAUD}). Lower it (e.g. 115200) if flashing "
            "fails with 'chip stopped responding'."
        ),
    )
    parser.add_argument(
        "--no-reclone",
        action="store_true",
        dest="no_reclone",
        help="Skip re-cloning if CrossPoint repo already exists and matches remote",
    )
    return parser.parse_args()


def check_python_version():
    if USE_PIO_BINARY or (3, 10) <= sys.version_info[:2] <= PIO_MAX_PYTHON:
        return
    found = f"{sys.version_info.major}.{sys.version_info.minor}"
    print(f"ERROR: PlatformIO needs Python 3.10 to 3.13, but this is Python {found}.")
    print("  Install Python 3.12, then run the installer with it:")
    print("    winget install Python.Python.3.12")
    print("    py -3.12 -m pip install -r requirements.txt esptool")
    print("    py -3.12 install.py")
    sys.exit(1)


def main():
    check_python_version()
    args = parse_args()

    print("=" * 60)
    print("  xteink Plugin Installer")
    print("=" * 60)
    print()
    print("  This script modifies and flashes custom firmware to your")
    print("  device. The author of this script (Justin Oros) accepts")
    print("  no responsibility for any damage that may occur to your")
    print("  device as a result of using this installer.")
    print()
    answer = input("  Do you wish to proceed? [Y/n]: ").strip().lower() if not args.yes else "y"
    if answer not in ("", "y", "yes"):
        print("  Aborted.")
        sys.exit(0)
    print()
    environment = args.environment or auto_detect_environment(args.yes)
    print(f"\n  Environment : {environment}")
    print(f"  {ENVIRONMENTS[environment]}")
    print()
    restore_from_backup(yes_all=args.yes)
    backup_device(yes_all=args.yes)
    clone_repo(force=not args.no_reclone)
    clear_plugin_caches()
    chosen_plugins = select_plugins(yes_all=args.yes)
    apply_plugins(chosen_plugins, yes_all=args.yes)
    baud_rate = prompt_for_baud_rate(args.baud, yes_all=args.yes)
    build_and_flash(environment, baud_rate)
    print("\nDone.")


if __name__ == "__main__":
    main()
