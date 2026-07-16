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
    "default":    "Debug logging enabled, version from current git branch (recommended)",
    "gh_release": "Info logging only, version hardcoded to release tag",
    "slim":       "No serial logging, smallest binary size",
}


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


def restore_from_backup():
    """If any backup.*.bin files are sitting in the current directory, offer
    to write one back to the device instead of proceeding with a fresh
    install. This always runs before backup_device() and, if the user goes
    through with a restore, exits the script afterward rather than
    continuing on to clone/patch/build/flash."""
    backups = find_backup_files()
    if not backups:
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


def backup_device():
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

    run(f"git clone --recursive {REPO_URL} {REPO_DIR}")


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
    for plugin_name in sorted(available):
        print(f"\n  Plugin: {plugin_name}")
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

    return sorted(port_map.items())


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


def build_and_flash(environment: str):
    print(f"\nBuilding firmware with PlatformIO (environment: {environment})...")
    run(f"pio run --environment {environment}", cwd=REPO_DIR)

    port = prompt_for_upload_port()

    print(f"\n  Flashing to {port}...")
    run(
        f"pio run --target upload --environment {environment} --upload-port {port}",
        cwd=REPO_DIR,
    )


def parse_args():
    parser = argparse.ArgumentParser(
        description="xteink Plugin Installer",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    env_help = "\n".join(f"  {name}: {desc}" for name, desc in ENVIRONMENTS.items())
    parser.add_argument(
        "-e", "--environment",
        default="default",
        choices=ENVIRONMENTS.keys(),
        metavar="ENV",
        help=f"PlatformIO build environment (default: default)\n{env_help}",
    )
    parser.add_argument(
        "-y", "--yes",
        action="store_true",
        help="Auto-answer Y to all plugin install prompts",
    )
    parser.add_argument(
        "--no-reclone",
        action="store_true",
        dest="no_reclone",
        help="Skip re-cloning if CrossPoint repo already exists and matches remote",
    )
    return parser.parse_args()


def main():
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
    print(f"  Environment : {args.environment}")
    print(f"  {ENVIRONMENTS[args.environment]}")
    print()
    answer = input("  Do you wish to proceed? [Y/n]: ").strip().lower() if not args.yes else "y"
    if answer not in ("", "y", "yes"):
        print("  Aborted.")
        sys.exit(0)
    print()
    restore_from_backup()
    backup_device()
    clone_repo(force=not args.no_reclone)
    clear_plugin_caches()
    chosen_plugins = select_plugins(yes_all=args.yes)
    apply_plugins(chosen_plugins, yes_all=args.yes)
    build_and_flash(args.environment)
    print("\nDone.")


if __name__ == "__main__":
    main()
