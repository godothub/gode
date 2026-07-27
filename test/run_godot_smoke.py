#!/usr/bin/env python3
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_MARKER = "[GodeTest] all tests passed"
DEFAULT_EXTENSION = "res://addons/gode/binary/gode.gdextension"

LEAK_RE = re.compile(
	r"(WARNING|ERROR): .*leaked|Leaked instance:|Orphan StringName: (Gode|TypeScriptScript)",
	re.IGNORECASE,
)
ERROR_RE = re.compile(r"^ERROR:", re.IGNORECASE)
MACOS_CA_ERROR_RE = re.compile(r'^ERROR: Condition "ret != noErr" is true\. Returning: ""$')


def candidate_godot_paths():
	env_path = os.environ.get("GODOT_BIN")
	if env_path:
		yield pathlib.Path(env_path).expanduser()

	for command in ("godot", "godot4", "Godot"):
		found = shutil.which(command)
		if found:
			yield pathlib.Path(found)

	home = pathlib.Path.home()
	downloads = home / "Downloads"

	if sys.platform == "darwin":
		yield downloads / "Godot.app" / "Contents" / "MacOS" / "Godot"
		yield pathlib.Path("/Applications/Godot.app/Contents/MacOS/Godot")
	elif os.name == "nt":
		for pattern in ("Godot*.exe", "Godot*/Godot*.exe"):
			yield from sorted(downloads.glob(pattern), reverse=True)
	else:
		for pattern in ("Godot*linux*.x86_64", "Godot*/Godot*linux*.x86_64", "Godot*.x86_64", "Godot*/Godot*.x86_64"):
			yield from sorted(downloads.glob(pattern), reverse=True)


def resolve_godot(explicit_path):
	if explicit_path:
		path = pathlib.Path(explicit_path).expanduser()
		if path.exists():
			return path
		found = shutil.which(explicit_path)
		if found:
			return pathlib.Path(found)
		raise FileNotFoundError(f"Godot executable was not found: {explicit_path}")

	seen = set()
	for candidate in candidate_godot_paths():
		if candidate in seen:
			continue
		seen.add(candidate)
		if candidate.exists():
			return candidate

	raise FileNotFoundError("Godot executable was not found. Set GODOT_BIN or pass --godot.")


def godot_resource_path(project, resource_path):
	if not resource_path.startswith("res://"):
		raise ValueError(f"Expected a res:// path, got: {resource_path}")
	return project / resource_path.removeprefix("res://")


def ensure_extension_list(project, extension_path):
	if not extension_path:
		return

	manifest = godot_resource_path(project, extension_path)
	if not manifest.exists():
		raise FileNotFoundError(f"GDExtension manifest was not found: {manifest}")

	godot_dir = project / ".godot"
	extension_list = godot_dir / "extension_list.cfg"
	if extension_list.exists():
		lines = extension_list.read_text(encoding="utf-8").splitlines()
		if extension_path in [line.strip() for line in lines]:
			return
	else:
		lines = []

	godot_dir.mkdir(exist_ok=True)
	lines.append(extension_path)
	extension_list.write_text("\n".join(lines) + "\n", encoding="utf-8")
	print(f"[gode-smoke] Registered GDExtension: {extension_path}")


def output_lines(output):
	return output.replace("\r\n", "\n").replace("\r", "\n").split("\n")


def captured_output_text(output):
	if output is None:
		return ""
	if isinstance(output, bytes):
		return output.decode("utf-8", errors="replace")
	return str(output)


def leak_lines(output):
	return [line for line in output_lines(output) if LEAK_RE.search(line)]


def is_known_nonfatal_error(lines, index):
	line = lines[index]
	if not MACOS_CA_ERROR_RE.search(line):
		return False
	return any("get_system_ca_certificates" in candidate for candidate in lines[index + 1:index + 4])


def non_leak_error_lines(output):
	lines = output_lines(output)
	errors = []
	for index, line in enumerate(lines):
		if ERROR_RE.search(line) and not LEAK_RE.search(line) and not is_known_nonfatal_error(lines, index):
			errors.append(line)
	return errors


def run_smoke(args):
	godot = resolve_godot(args.godot)
	project = (ROOT / args.project).resolve()
	if not project.exists():
		raise FileNotFoundError(f"Godot project directory was not found: {project}")
	ensure_extension_list(project, args.extension)

	command = [
		str(godot),
		"--headless",
		"--path",
		str(project),
		args.scene,
	]
	print("[gode-smoke] " + " ".join(command))

	result = subprocess.run(
		command,
		cwd=ROOT,
		stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT,
		text=True,
		errors="replace",
		timeout=args.timeout,
		check=False,
	)
	output = result.stdout
	print(output, end="" if output.endswith("\n") else "\n")

	failures = []
	if result.returncode != 0:
		failures.append(f"Godot exited with code {result.returncode}")
	if args.marker not in output:
		failures.append(f"Expected marker was not found: {args.marker}")

	non_leak_errors = non_leak_error_lines(output)
	if non_leak_errors:
		failures.append("Godot emitted non-leak ERROR lines:\n" + "\n".join(non_leak_errors[:20]))

	leaks = leak_lines(output)
	if leaks:
		print(f"[gode-smoke] Godot exit leak diagnostics: {len(leaks)} line(s)")
		for line in leaks[:20]:
			print(f"[gode-smoke] {line}")
		if args.strict_exit_leaks:
			failures.append("Godot emitted exit leak diagnostics")

	if failures:
		for failure in failures:
			print(f"[gode-smoke] FAIL: {failure}", file=sys.stderr)
		return 1

	print("[gode-smoke] PASS")
	return 0


def build_parser():
	parser = argparse.ArgumentParser(description="Run the Gode Godot headless smoke test scene.")
	parser.add_argument("--godot", help="Path to the Godot executable. Defaults to GODOT_BIN or common install locations.")
	parser.add_argument("--project", default="example", help="Path to the Godot project directory relative to the repo root.")
	parser.add_argument("--scene", default="res://scenes/tests_runner.tscn", help="Godot scene path to run.")
	parser.add_argument("--extension", default=DEFAULT_EXTENSION, help="GDExtension manifest path to register before running.")
	parser.add_argument("--marker", default=DEFAULT_MARKER, help="Output marker that proves the JS test completed.")
	parser.add_argument("--timeout", type=int, default=45, help="Seconds before the Godot process is terminated.")
	parser.add_argument("--strict-exit-leaks", action="store_true", help="Fail if Godot reports exit-time leaks.")
	return parser


def main():
	parser = build_parser()
	args = parser.parse_args()
	try:
		return run_smoke(args)
	except subprocess.TimeoutExpired as exc:
		print(f"[gode-smoke] FAIL: Godot timed out after {exc.timeout} seconds", file=sys.stderr)
		output = captured_output_text(exc.stdout)
		if output:
			print(output, end="" if output.endswith("\n") else "\n")
		return 1
	except Exception as exc:
		print(f"[gode-smoke] FAIL: {exc}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	sys.exit(main())
