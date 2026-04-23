#!/usr/bin/env python3

import os
import sys
import glob
import shutil
import subprocess
import getpass
import csv
import tempfile
import ssl
import urllib.request
import urllib.error
import json
from pathlib import Path


NVS_NAMESPACE      = "github_sync"
XTEINK_CONFIG_FILE = Path.home() / ".xteink"


def read_file(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()

def write_file(path, content):
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

def find_first(filename, repo_dir):
    results = glob.glob(os.path.join(repo_dir, "**", filename), recursive=True)
    if not results:
        sys.exit(f"ERROR: Could not locate {filename} in {repo_dir}")
    return results[0]


def load_xteink_config() -> dict:
    if not XTEINK_CONFIG_FILE.exists():
        return {}
    try:
        with open(XTEINK_CONFIG_FILE, "r") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}

def save_xteink_config(cfg: dict) -> None:
    try:
        saved = load_xteink_config()
        if "username" in cfg:
            saved["githubuser"] = cfg["username"]
        if "repo" in cfg:
            saved["githubrepo"] = cfg["repo"]
        if "branch" in cfg:
            saved["githubbranch"] = cfg["branch"]
        with open(XTEINK_CONFIG_FILE, "w") as f:
            json.dump(saved, f, indent=2)
        print(f"    ✓ Config saved to {XTEINK_CONFIG_FILE}")
    except Exception as e:
        print(f"  ! Could not save config: {e}")


_github_https_deps_ready = False

def ensure_python_module(module_name: str, pip_package: str | None = None, *, description: str = "") -> None:
    pip_package = pip_package or module_name
    try:
        __import__(module_name)
        return
    except ImportError:
        pass
    desc = f" — {description}" if description else ""
    print(f"  ! Python module '{module_name}' is not installed{desc}.")
    input(f"  Press Enter to install {pip_package}... ")
    result = subprocess.run(
        [sys.executable, "-m", "pip", "install", pip_package],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        sys.exit(f"ERROR: Failed to install {pip_package}:\n{result.stderr.strip()}")
    print(f"    ✓ {pip_package} installed")
    try:
        __import__(module_name)
    except ImportError:
        sys.exit(f"ERROR: '{module_name}' still not importable after install")

def ensure_github_https_dependencies() -> None:
    global _github_https_deps_ready
    if _github_https_deps_ready:
        return
    if os.environ.get("GITHUB_SYNC_SSL_NO_VERIFY", "").strip().lower() in ("1", "true", "yes", "on"):
        _github_https_deps_ready = True
        return
    cf = os.environ.get("SSL_CERT_FILE", "").strip()
    if cf and os.path.isfile(cf):
        _github_https_deps_ready = True
        return
    try:
        import certifi  # noqa: F401
        _github_https_deps_ready = True
        return
    except ImportError:
        pass
    ensure_python_module(
        "certifi",
        description="recommended for GitHub HTTPS (fixes macOS SSL certificate errors)",
    )
    _github_https_deps_ready = True

def get_github_ssl_context():
    flag = os.environ.get("GITHUB_SYNC_SSL_NO_VERIFY", "").strip().lower()
    if flag in ("1", "true", "yes", "on"):
        print("  ! SSL verification is OFF (GITHUB_SYNC_SSL_NO_VERIFY). Only use if you trust this network.")
        return ssl._create_unverified_context()
    cert_file = os.environ.get("SSL_CERT_FILE", "").strip()
    if cert_file and os.path.isfile(cert_file):
        return ssl.create_default_context(cafile=cert_file)
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        pass
    return ssl.create_default_context()

def ssl_troubleshoot_hint(err_txt: str | None) -> str:
    if not err_txt:
        return ""
    if "CERTIFICATE_VERIFY_FAILED" in err_txt or "SSL" in err_txt:
        return (
            " Try: pip install certifi (then re-run), or run macOS "
            "'Install Certificates.command' for your Python, or set SSL_CERT_FILE to a CA bundle. "
            "Last resort: GITHUB_SYNC_SSL_NO_VERIFY=1 (insecure)."
        )
    return ""

def github_api_get(path: str, token: str | None = None, timeout: int = 10):
    url = f"https://api.github.com{path}"
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "xteink-github-sync-patcher",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    ctx = get_github_ssl_context()
    try:
        with urllib.request.urlopen(req, timeout=timeout, context=ctx) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            data = json.loads(body) if body else {}
            return resp.status, data, None
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        try:
            data = json.loads(body) if body else {}
        except Exception:
            data = {"message": body.strip()} if body else {}
        return e.code, data, None
    except Exception as e:
        return None, None, str(e)

def validate_github_username(username: str) -> tuple[bool, str]:
    status, data, err_txt = github_api_get(f"/users/{username}")
    if err_txt:
        return False, f"Could not reach GitHub API: {err_txt}{ssl_troubleshoot_hint(err_txt)}"
    if status == 200:
        return True, "GitHub username is reachable."
    if status == 404:
        return False, "Username not found on GitHub."
    msg = data.get("message", "Unknown API error") if isinstance(data, dict) else "Unknown API error"
    return False, f"GitHub API error ({status}): {msg}"

def validate_pat(token: str) -> tuple[bool, str, str | None]:
    status, data, err_txt = github_api_get("/user", token=token)
    if err_txt:
        return False, f"Could not validate PAT: {err_txt}{ssl_troubleshoot_hint(err_txt)}", None
    if status == 200 and isinstance(data, dict):
        login = data.get("login")
        return True, f"PAT is valid (authenticated as {login}).", login
    if status in (401, 403):
        msg = data.get("message", "Unauthorized") if isinstance(data, dict) else "Unauthorized"
        return False, f"PAT rejected: {msg}", None
    msg = data.get("message", "Unknown API error") if isinstance(data, dict) else "Unknown API error"
    return False, f"PAT validation failed ({status}): {msg}", None

def validate_repo_access(owner: str, repo: str, token: str) -> tuple[bool, str]:
    status, data, err_txt = github_api_get(f"/repos/{owner}/{repo}", token=token)
    if err_txt:
        return False, f"Could not validate repo access: {err_txt}{ssl_troubleshoot_hint(err_txt)}"
    if status == 200:
        return True, "Repo is reachable with this PAT."
    if status == 404:
        return False, "Repo not found, or PAT cannot access it."
    if status in (401, 403):
        msg = data.get("message", "Unauthorized") if isinstance(data, dict) else "Unauthorized"
        return False, f"Access denied: {msg}"
    msg = data.get("message", "Unknown API error") if isinstance(data, dict) else "Unknown API error"
    return False, f"Repo validation failed ({status}): {msg}"

def prompt_github_config() -> dict:
    print("\n  GitHub Sync Configuration by Justin Oros")
    print("  Press Enter on any field to use the saved value, or type a new one.")
    print("  Leave GitHub username blank to skip and configure on-device later.\n")

    saved = load_xteink_config()

    saved_user   = saved.get("githubuser", "")
    saved_repo   = saved.get("githubrepo", "xteink")
    saved_branch = saved.get("githubbranch", "main")

    while True:
        if saved_user:
            raw = input(f"  GitHub username ({saved_user}): ").strip()
            username = raw or saved_user
        else:
            username = input("  GitHub username: ").strip()
        if not username:
            return {}
        ensure_github_https_dependencies()
        ok_user, msg = validate_github_username(username)
        if ok_user:
            print(f"    ✓ {msg}")
            break
        print(f"  ! {msg}")
        retry = input("  Try another username? [Y/n]: ").strip().lower()
        if retry == "n":
            return {}

    print("\n  To generate a Personal Access Token (PAT):")
    print("    1. Go to github.com -> Settings -> Developer settings")
    print("    2. Personal access tokens -> Fine-grained tokens")
    print("    3. Click 'Generate new token'")
    print("    4. Token name: xteink")
    print("    5. Expiration: No expiration")
    print("    6. Repository access: Only selected repositories -> select 'xteink'")
    print("    7. Permissions -> Add permissions -> Contents: Read-only")
    print("    8. Click 'Generate token' then copy it - GitHub only shows it once\n")

    authenticated_login = None
    while True:
        pat = getpass.getpass("  Personal Access Token (PAT): ").strip()
        if not pat:
            return {}
        ok_pat, msg, authenticated_login = validate_pat(pat)
        if ok_pat:
            print(f"    ✓ {msg}")
            break
        print(f"  ! {msg}")
        retry = input("  Try entering PAT again? [Y/n]: ").strip().lower()
        if retry == "n":
            return {}

    while True:
        raw = input(f"  Repo name ({saved_repo}): ").strip()
        repo = raw or saved_repo
        owner_for_repo = authenticated_login or username
        ok_repo, msg = validate_repo_access(owner_for_repo, repo, pat)
        if ok_repo:
            print(f"    ✓ {msg}")
            if authenticated_login and authenticated_login != username:
                print(f"  ! Username '{username}' differs from PAT owner '{authenticated_login}'. Repo was validated under '{owner_for_repo}'.")
            break
        print(f"  ! {msg}")
        retry = input("  Try another repo name? [Y/n]: ").strip().lower()
        if retry == "n":
            return {}

    raw = input(f"  Branch ({saved_branch}): ").strip()
    branch = raw or saved_branch

    cfg = {"username": username, "pat": pat, "repo": repo, "branch": branch}
    save_xteink_config(cfg)
    return cfg

def check_nvs_gen() -> bool:
    result = subprocess.run(
        ["python3", "-m", "esp_idf_nvs_partition_gen", "--help"],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        return True
    result2 = subprocess.run(
        [sys.executable, "-m", "pip", "install", "esp-idf-nvs-partition-gen"],
        capture_output=True, text=True
    )
    result3 = subprocess.run(
        ["python3", "-m", "esp_idf_nvs_partition_gen", "--help"],
        capture_output=True, text=True
    )
    return result3.returncode == 0

def write_nvs_partition(cfg: dict) -> Path | None:
    if not check_nvs_gen():
        print("  ! esp-idf-nvs-partition-gen unavailable — enter credentials on-device via Settings -> GitHub Sync.")
        return None

    nvs_csv = Path(tempfile.mkdtemp()) / "github_sync_nvs.csv"
    rows = [
        ["key", "type", "encoding", "value"],
        [NVS_NAMESPACE, "namespace", "", ""],
        ["username", "data", "string", cfg["username"]],
        ["pat",      "data", "string", cfg["pat"]],
        ["repo",     "data", "string", cfg["repo"]],
        ["branch",   "data", "string", cfg["branch"]],
    ]
    with open(nvs_csv, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerows(rows)

    nvs_bin = nvs_csv.with_suffix(".bin")
    result = subprocess.run(
        ["python3", "-m", "esp_idf_nvs_partition_gen", "generate",
         str(nvs_csv), str(nvs_bin), "0x3000"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"  ! nvs_partition_gen failed — enter credentials on-device via Settings -> GitHub Sync.\n{result.stderr.strip()}")
        return None

    print(f"    ✓ NVS partition written to {nvs_bin}")
    return nvs_bin


def copy_plugin_sources(plugin_dir, repo_dir):
    copies = [
        (os.path.join(plugin_dir, "GitHubSync.h"),
         os.path.join(repo_dir, "include", "GitHubSync.h")),
        (os.path.join(plugin_dir, "GitHubSyncSettingsActivity.h"),
         os.path.join(repo_dir, "include", "GitHubSyncSettingsActivity.h")),
        (os.path.join(plugin_dir, "GitHubSync.cpp"),
         os.path.join(repo_dir, "src", "github_sync", "GitHubSync.cpp")),
        (os.path.join(plugin_dir, "GitHubSyncSettingsActivity.cpp"),
         os.path.join(repo_dir, "src", "activities", "settings", "GitHubSyncSettingsActivity.cpp")),
    ]
    for src, dst in copies:
        if not os.path.exists(src):
            sys.exit(f"ERROR: Patch file missing: {src}")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        print(f"    ✓ {os.path.basename(dst)}")


def patch_platformio_ini(repo_dir):
    ini = os.path.join(repo_dir, "platformio.ini")
    if not os.path.exists(ini):
        print("  platformio.ini not found, skipping.")
        return
    content = read_file(ini)
    if "ArduinoJson" in content:
        print("  platformio.ini already patched, skipping.")
        return
    content = content.replace(
        "lib_deps",
        "lib_deps\n\tbblanchon/ArduinoJson @ ^7",
        1
    )
    write_file(ini, content)
    print("  platformio.ini patched.")


def patch_main_cpp(repo_dir):
    candidates = [
        os.path.join(repo_dir, "src", "main.cpp"),
        os.path.join(repo_dir, "src", "Main.cpp"),
    ]
    main_path = next((p for p in candidates if os.path.exists(p)), None)
    if not main_path:
        print("  ! Could not find main.cpp — add GitHub sync call manually.")
        return

    content = read_file(main_path)

    if '#include "GitHubSync.h"' in content:
        print("  main.cpp already patched, skipping.")
        return

    content = content.replace(
        '#include "CrossPointSettings.h"',
        '#include "GitHubSync.h"\n#include "CrossPointSettings.h"',
        1
    )

    sync_call = (
        '\n  if (GitHubSync::isConfigured()) {\n'
        '    GitHubSyncResult result = GitHubSync::sync();\n'
        '    if (result != GitHubSyncResult::OK) {\n'
        '      LOG_ERR("SYNC", "%s", GitHubSync::resultMessage(result));\n'
        '    }\n'
        '  }\n'
    )

    if "GitHubSync::isConfigured" not in content:
        content = content.replace(
            "activityManager.goToBoot();",
            "activityManager.goToBoot();" + sync_call,
            1
        )

    write_file(main_path, content)
    print("  main.cpp patched.")


def patch_settings_h(repo_dir):
    path    = find_first("SettingsActivity.h", repo_dir)
    content = read_file(path)

    if "GitHubSync," in content:
        print("  SettingsActivity.h already patched, skipping.")
        return

    content = content.replace(
        "  CheckForUpdates,",
        "  CheckForUpdates,\n  GitHubSync,",
        1
    )

    write_file(path, content)
    print("  SettingsActivity.h patched.")


def patch_settings_cpp(repo_dir):
    path    = find_first("SettingsActivity.cpp", repo_dir)
    content = read_file(path)

    if '#include "GitHubSyncSettingsActivity.h"' in content and "SettingAction::GitHubSync" in content:
        print("  SettingsActivity.cpp already patched, skipping.")
        return

    if '#include "GitHubSyncSettingsActivity.h"' not in content:
        content = content.replace(
            '#include "SettingsActivity.h"',
            '#include "SettingsActivity.h"\n#include "GitHubSyncSettingsActivity.h"',
            1
        )

    if "SettingAction::GitHubSync" not in content:
        content = content.replace(
            "SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));",
            "SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));\n"
            "  pluginsSettings.push_back(SettingInfo::Action(StrId::STR_NONE_OPT, SettingAction::GitHubSync));",
            1
        )

    if "case SettingAction::GitHubSync:" not in content:
        content = content.replace(
            "case SettingAction::CheckForUpdates:",
            "case SettingAction::CheckForUpdates:\n"
            "      case SettingAction::GitHubSync:\n"
            "        startActivityForResult(std::make_unique<GitHubSyncSettingsActivity>(renderer, mappedInput), resultHandler);\n"
            "        break;",
            1
        )

    if '"GitHub Sync"' not in content:
        content = content.replace(
            '[&settings, this](int index) -> std::string {\n',
            '[&settings, this](int index) -> std::string {\n'
            '        if (selectedCategoryIndex == 4) {\n'
            '          const auto& s = settings[index];\n'
            '          if (s.type == SettingType::ACTION && s.action == SettingAction::GitHubSync) return "GitHub Sync";\n'
            '        }\n',
            1
        )

    if '"Installed"' not in content:
        content = content.replace(
            '} else if (setting.type == SettingType::ACTION && setting.action == SettingAction::BookerlyInstalled) {\n'
            '          valueText = "Installed";\n',
            '} else if (setting.type == SettingType::ACTION && setting.action == SettingAction::BookerlyInstalled) {\n'
            '          valueText = "Installed";\n'
            '        } else if (setting.type == SettingType::ACTION && setting.action == SettingAction::GitHubSync) {\n'
            '          valueText = "Sync";\n',
            1
        )
    elif "GitHubSync" not in content.split('valueText = "Installed"')[0].split('BookerlyInstalled')[-1]:
        content = content.replace(
            '} else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {\n'
            '          valueText = std::to_string(SETTINGS.*(setting.valuePtr));\n'
            '        }',
            '} else if (setting.type == SettingType::ACTION && setting.action == SettingAction::GitHubSync) {\n'
            '          valueText = "Sync";\n'
            '        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {\n'
            '          valueText = std::to_string(SETTINGS.*(setting.valuePtr));\n'
            '        }',
            1
        )

    write_file(path, content)
    print("  SettingsActivity.cpp patched.")


def patch_translation_files(repo_dir):
    yaml_dir = os.path.join(repo_dir, "lib", "I18n", "translations")
    if not os.path.isdir(yaml_dir):
        print("  ! Could not find translation YAML files — add STR_GITHUB_SYNC manually.")
        return

    yaml_files = glob.glob(os.path.join(yaml_dir, "*.yaml"))
    if not yaml_files:
        print("  ! No translation YAML files found.")
        return

    patched = 0
    for yf in yaml_files:
        content = read_file(yf)
        if "STR_GITHUB_SYNC" in content:
            patched += 1
            continue
        lines = content.splitlines(keepends=True)
        new_lines = []
        inserted = False
        for line in lines:
            new_lines.append(line)
            if line.startswith("STR_CHECK_UPDATES:"):
                new_lines.append('STR_GITHUB_SYNC: "GitHub Sync"\n')
                inserted = True
        if inserted:
            write_file(yf, "".join(new_lines))
            patched += 1
        else:
            print(f"  ! STR_CHECK_UPDATES not found in {os.path.basename(yf)} — add STR_GITHUB_SYNC manually.")

    print(f"  Translation files patched ({patched} files).")


def patch(repo_dir: str, yes_all: bool = False):
    plugin_dir = os.path.dirname(os.path.abspath(__file__))

    print("  Copying plugin sources...")
    copy_plugin_sources(plugin_dir, repo_dir)

    print("  Patching platformio.ini...")
    patch_platformio_ini(repo_dir)

    print("  Patching main.cpp...")
    patch_main_cpp(repo_dir)

    print("  Patching SettingsActivity.h...")
    patch_settings_h(repo_dir)

    print("  Patching SettingsActivity.cpp...")
    patch_settings_cpp(repo_dir)

    print("  Patching translation files...")
    patch_translation_files(repo_dir)

    if not yes_all:
        print("\n  GitHub Sync credentials (optional — can also be set on-device).")
        answer = input("  Configure GitHub credentials now? [Y/n]: ").strip().lower()
        if answer in ("", "y", "yes"):
            cfg = prompt_github_config()
            if cfg:
                nvs_bin = write_nvs_partition(cfg)
                if nvs_bin:
                    print(f"\n  NVS partition ready: {nvs_bin}")
                    print("  Flash it after firmware upload with:")
                    print(f"    esptool.py --chip esp32c3 write_flash 0x9000 {nvs_bin}")
    else:
        print("  Skipping credential prompt (--yes mode). Configure on-device via Settings -> GitHub Sync.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python patch.py <path-to-crosspoint-repo>")
    patch(sys.argv[1])
