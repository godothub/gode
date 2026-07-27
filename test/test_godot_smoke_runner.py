import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "test" / "run_godot_smoke.py"

spec = importlib.util.spec_from_file_location("run_godot_smoke", RUNNER_PATH)
run_godot_smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run_godot_smoke)


class GodotSmokeRunnerTests(unittest.TestCase):
	def test_leak_lines_are_classified_separately_from_runtime_errors(self):
		output = "\n".join(
			[
				'ERROR: 1 RID allocations of type "Viewport" were leaked at exit.',
				"WARNING: 17 ObjectDB instances were leaked at exit.",
				"Leaked instance: TypeScriptScript:123 - Reference count: 1",
				'ERROR: Condition "ret != noErr" is true. Returning: ""',
				"   at: get_system_ca_certificates (platform/macos/os_macos.mm:1035)",
				"ERROR: Script failed before running the test.",
			]
		)

		self.assertEqual(3, len(run_godot_smoke.leak_lines(output)))
		self.assertEqual(["ERROR: Script failed before running the test."], run_godot_smoke.non_leak_error_lines(output))

	def test_timeout_output_is_normalized_from_bytes(self):
		self.assertEqual(
			"Godot Engine\nSCRIPT ERROR: Parse Error\n",
			run_godot_smoke.captured_output_text(b"Godot Engine\nSCRIPT ERROR: Parse Error\n"),
		)

	def test_extension_list_is_created_for_clean_projects(self):
		with tempfile.TemporaryDirectory() as temp_dir:
			project = pathlib.Path(temp_dir)
			manifest = project / "addons/gode/binary/gode.gdextension"
			manifest.parent.mkdir(parents=True)
			manifest.write_text("[configuration]\n", encoding="utf-8")

			run_godot_smoke.ensure_extension_list(project, run_godot_smoke.DEFAULT_EXTENSION)

			self.assertEqual(
				run_godot_smoke.DEFAULT_EXTENSION + "\n",
				(project / ".godot/extension_list.cfg").read_text(encoding="utf-8"),
			)

	def test_extension_list_preserves_existing_entries_without_duplicates(self):
		with tempfile.TemporaryDirectory() as temp_dir:
			project = pathlib.Path(temp_dir)
			manifest = project / "addons/gode/binary/gode.gdextension"
			manifest.parent.mkdir(parents=True)
			manifest.write_text("[configuration]\n", encoding="utf-8")
			extension_list = project / ".godot/extension_list.cfg"
			extension_list.parent.mkdir()
			extension_list.write_text(
				"res://addons/other/other.gdextension\n"
				+ run_godot_smoke.DEFAULT_EXTENSION
				+ "\n",
				encoding="utf-8",
			)

			run_godot_smoke.ensure_extension_list(project, run_godot_smoke.DEFAULT_EXTENSION)

			self.assertEqual(
				[
					"res://addons/other/other.gdextension",
					run_godot_smoke.DEFAULT_EXTENSION,
				],
				extension_list.read_text(encoding="utf-8").splitlines(),
			)


if __name__ == "__main__":
	unittest.main()
