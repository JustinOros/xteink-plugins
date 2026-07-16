#!/usr/bin/env python3
"""Ad-hoc test harness (not part of the shipped installer): applies various
plugin subsets against a fresh copy of the crosspoint-reader source and
reports success/failure per combination. Requires a pristine repo template
directory and a scratch working directory as arguments.
"""
import itertools
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from framework.discovery import discover_all, build_selected
from framework.engine import apply, Context, PatchError

PLUGINS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "plugins")


def run_case(template_dir, scratch_dir, names):
    if os.path.exists(scratch_dir):
        shutil.rmtree(scratch_dir)
    shutil.copytree(template_dir, scratch_dir)
    ctx = Context(repo_dir=scratch_dir, yes_all=True, prompt=False)
    selected = build_selected(names, PLUGINS_DIR, ctx)
    apply(ctx, selected)


def main():
    template_dir = sys.argv[1]
    scratch_root = sys.argv[2]
    cases_arg = sys.argv[3] if len(sys.argv) > 3 else "all"

    all_names = sorted(discover_all(PLUGINS_DIR).keys())
    print(f"Discovered plugins: {all_names}")

    if cases_arg == "all":
        cases = [[n] for n in all_names]
        cases.append(all_names)
        cases.append(["darkmode", "smallerfonts"])
        cases.append(["bookerly", "smallerfonts"])
        cases.append(["bookerly"])
        cases.append(["githubsync"])
        cases.append(["hardcover", "pong"])
        cases.append(["lockscreen", "darkmode", "pong"])
    else:
        cases = [cases_arg.split(",")]

    results = []
    for names in cases:
        label = "+".join(names)
        scratch_dir = os.path.join(scratch_root, label.replace("/", "_"))
        try:
            run_case(template_dir, scratch_dir, names)
            print(f"OK    {label}")
            results.append((label, True, ""))
        except PatchError as e:
            print(f"FAIL  {label}: {e}")
            results.append((label, False, str(e)))
        except Exception as e:
            print(f"ERROR {label}: {type(e).__name__}: {e}")
            results.append((label, False, f"{type(e).__name__}: {e}"))

    print("\n=== Summary ===")
    n_ok = sum(1 for _, ok, _ in results if ok)
    for label, ok, msg in results:
        print(f"{'OK' if ok else 'FAIL'}  {label}" + (f"  -- {msg}" if not ok else ""))
    print(f"\n{n_ok}/{len(results)} passed")
    sys.exit(0 if n_ok == len(results) else 1)


if __name__ == "__main__":
    main()
