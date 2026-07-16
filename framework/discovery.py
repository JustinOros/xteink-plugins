import importlib.util
import os

from .engine import SelectedPlugin


def discover_all(plugins_dir):
    """Return {name: (plugin_dir, get_manifest_callable)} for every plugin
    directory that has a plugin.py exposing get_manifest(ctx)."""
    found = {}
    for name in sorted(os.listdir(plugins_dir)):
        plugin_dir = os.path.join(plugins_dir, name)
        manifest_path = os.path.join(plugin_dir, "plugin.py")
        if not os.path.isdir(plugin_dir) or not os.path.exists(manifest_path):
            continue
        spec = importlib.util.spec_from_file_location(f"xteink_plugin_{name}", manifest_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        if not hasattr(module, "get_manifest"):
            continue
        found[name] = (plugin_dir, module.get_manifest)
    return found


def build_selected(names, plugins_dir, ctx):
    """Build a deterministic, sorted-by-name list of SelectedPlugin for the
    given plugin names. Order doesn't affect correctness (the engine derives
    shared files from the whole set at once) but keeping it stable makes
    output/diffs reproducible."""
    found = discover_all(plugins_dir)
    selected = []
    for name in sorted(names):
        if name not in found:
            raise KeyError(f"Unknown plugin: {name} (available: {', '.join(sorted(found))})")
        plugin_dir, get_manifest = found[name]
        manifest = get_manifest(ctx)
        selected.append(SelectedPlugin(name=name, plugin_dir=plugin_dir, manifest=manifest))
    return selected
