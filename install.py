#!/usr/bin/env python3

import os
import sys
import shutil
import subprocess
import glob
import importlib.util
import argparse
import inspect

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


def clone_repo():
    print("[1/3] Cloning CrossPoint repository...")
    if os.path.exists(REPO_DIR):
        print(f"  Removing existing '{REPO_DIR}'...")
        rmtree(REPO_DIR)
    run(f"git clone --recursive {REPO_URL} {REPO_DIR}")


def run_plugin_patch(plugin_dir: str, yes_all: bool = False) -> bool:
    patch_path = os.path.join(plugin_dir, "patch.py")
    if not os.path.exists(patch_path):
        print(f"  WARNING: No patch.py found in {plugin_dir}, skipping.")
        return False

    spec   = importlib.util.spec_from_file_location("patch", patch_path)
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
        if hasattr(module, "patch"):
            sig = inspect.signature(module.patch)
            if "yes_all" in sig.parameters:
                module.patch(os.path.abspath(REPO_DIR), yes_all=yes_all)
            else:
                module.patch(os.path.abspath(REPO_DIR))
        return True
    except Exception as exc:
        print(f"  ERROR while running patch.py for {plugin_dir}: {exc}")
        return False


def apply_plugins(yes_all: bool = False):
    print("[2/3] Applying plugins...")

    if not os.path.exists(PLUGINS_DIR):
        print(f"  No '{PLUGINS_DIR}' directory found — skipping plugin step.")
        return

    plugin_dirs = sorted(
        d for d in os.listdir(PLUGINS_DIR)
        if os.path.isdir(os.path.join(PLUGINS_DIR, d))
    )

    if not plugin_dirs:
        print("  No plugin directories found.")
        return

    for plugin_name in plugin_dirs:
        plugin_path = os.path.join(PLUGINS_DIR, plugin_name)

        print(f"\n  Plugin: {plugin_name}")
        if yes_all:
            answer = "y"
            print(f"  Install '{plugin_name}'? [Y/n]: Y (--yes)")
        else:
            answer = input(f"  Install '{plugin_name}'? [Y/n]: ").strip().lower()

        if answer in ("", "y", "yes"):
            print(f"  Installing {plugin_name}...")
            success = run_plugin_patch(plugin_path, yes_all=yes_all)
            if success:
                print(f"  ✓ {plugin_name} installed.")
            else:
                print(f"  ✗ {plugin_name} failed or was skipped.")
        else:
            print(f"  Skipping {plugin_name}.")


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
    clone_repo()
    apply_plugins(yes_all=args.yes)
    build_and_flash(args.environment)
    print("\nDone.")


if __name__ == "__main__":
    main()
