#!/usr/bin/env python3
import argparse
import pathlib
import shutil
import subprocess
import tempfile

from run_godot_smoke import ROOT, ensure_extension_list, leak_lines, non_leak_error_lines, resolve_godot


def main():
	parser = argparse.ArgumentParser(description="Test TypeScript project discovery and cache invalidation with Godot.")
	parser.add_argument("--godot")
	parser.add_argument("--addon", default=str(ROOT / "example/addons/gode"))
	args = parser.parse_args()
	addon = pathlib.Path(args.addon).resolve()
	with tempfile.TemporaryDirectory(prefix="gode-project-compiler-") as temp_dir:
		project = pathlib.Path(temp_dir)
		destination = project / "addons" / addon.name
		shutil.copytree(addon, destination, symlinks=True)
		(project / "project.godot").write_text("config_version=5\n[application]\nconfig/name=\"Gode compiler tests\"\n", encoding="utf-8")
		shutil.copy2(ROOT / "test/fixtures/typescript_project/probe.gd", project / "probe.gd")
		ensure_extension_list(project, [
			f"res://addons/{addon.name}/binary/gode.gdextension", "res://.godot/gode/gode_editor.gdextension"
		], f"res://addons/{addon.name}/binary/gode_editor.gdextension.template")
		result = subprocess.run([
			str(resolve_godot(args.godot).resolve()), "--headless", "--path", str(project), "--script", "res://probe.gd"
		], text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
		print(result.stdout)
		if result.returncode or "[GodeProjectCompilerTest] passed" not in result.stdout or non_leak_error_lines(result.stdout) or leak_lines(result.stdout):
			return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
