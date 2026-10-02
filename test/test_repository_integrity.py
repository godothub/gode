import configparser
import json
import pathlib
import re
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
EXAMPLE_ROOT = ROOT / "example"
EXTENSION_API_PATH = ROOT / "third/godot-cpp/gdextension/extension_api.json"
SKIPPED_BUILTIN_CLASSES = {"Nil", "void", "bool", "int", "float"}
JS_MAX_SAFE_INTEGER = 9007199254740991


def res_path_to_file(path: str) -> pathlib.Path:
	if not path.startswith("res://"):
		raise ValueError(f"not a Godot resource path: {path}")
	return EXAMPLE_ROOT / path.removeprefix("res://")


def load_extension_api() -> dict:
	return json.loads(EXTENSION_API_PATH.read_text(encoding="utf-8"))


def to_snake_case(name: str) -> str:
	name = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
	name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
	name = name.lower()
	name = name.replace("2_d", "2d")
	name = name.replace("3_d", "3d")
	name = name.replace("4_d", "4d")
	return name


def find_dts_class_match(dts: str, dts_name: str, exported: bool = False):
	export_prefix = r"export\s+" if exported else r"(?:export\s+)?"
	return re.search(
		rf"^(?P<indent>[ \t]*){export_prefix}(?P<abstract>abstract\s+)?class {re.escape(dts_name)}(?=[\s<])[^\n{{]*\{{\n(?P<body>.*?)^(?P=indent)\}}",
		dts,
		re.DOTALL | re.MULTILINE,
	)


def find_dts_class_body(dts: str, dts_name: str, exported: bool = False):
	match = find_dts_class_match(dts, dts_name, exported=exported)
	return match.group("body") if match else None


def gdscript_function_body(source: str, function_name: str) -> str:
	match = re.search(
		rf"^func {re.escape(function_name)}\b[^\n]*:\n(?P<body>.*?)(?=^func \w|\Z)",
		source,
		re.DOTALL | re.MULTILINE,
	)
	if not match:
		raise AssertionError(f"GDScript function was not found: {function_name}")
	return match.group("body")


class RepositoryIntegrityTests(unittest.TestCase):
	def test_scene_resource_paths_exist(self):
		missing = []
		for scene_path in sorted(EXAMPLE_ROOT.glob("scenes/**/*.tscn")):
			for match in re.finditer(r'path="(res://[^"]+)"', scene_path.read_text(encoding="utf-8")):
				resource_path = match.group(1)
				if not res_path_to_file(resource_path).exists():
					missing.append(f"{scene_path.relative_to(ROOT)} -> {resource_path}")

		self.assertEqual([], missing)

	def test_project_resource_paths_exist(self):
		project_text = (EXAMPLE_ROOT / "project.godot").read_text(encoding="utf-8")
		paths = set(re.findall(r'"(res://[^"]+)"', project_text))
		autoloads = re.findall(r'=\s*"\*?(res://[^"]+)"', project_text)
		paths.update(autoloads)

		missing = sorted(path for path in paths if not res_path_to_file(path).exists())
		self.assertEqual([], missing)

	def test_example_demo_console_is_separate_from_runtime_tests(self):
		expected_demo_files = [
			EXAMPLE_ROOT / "scripts/capability_catalog.ts",
			EXAMPLE_ROOT / "scripts/capability_selection.ts",
			EXAMPLE_ROOT / "scripts/capability_workspace.ts",
			EXAMPLE_ROOT / "scenes/capability_workspace.tscn",
		]
		missing = [str(path.relative_to(ROOT)) for path in expected_demo_files if not path.exists()]
		self.assertEqual([], missing)

		retired_demo_test_names = [
			EXAMPLE_ROOT / "scripts/test_catalog.ts",
			EXAMPLE_ROOT / "scripts/test_selection.ts",
			EXAMPLE_ROOT / "scripts/test_workspace.ts",
			EXAMPLE_ROOT / "scenes/test_workspace.tscn",
		]
		present = [str(path.relative_to(ROOT)) for path in retired_demo_test_names if path.exists()]
		self.assertEqual([], present)

		demo_text = "\n".join(
			path.read_text(encoding="utf-8")
			for path in (
				EXAMPLE_ROOT / "scripts/main_menu.ts",
				EXAMPLE_ROOT / "scripts/capability_workspace.ts",
				EXAMPLE_ROOT / "scenes/main_menu.tscn",
				EXAMPLE_ROOT / "scenes/capability_workspace.tscn",
			)
		)
		for token in ("test_catalog", "test_selection", "test_workspace", "TestGrid", "TestButton", "TestWorkspace"):
			self.assertNotIn(token, demo_text)

		self.assertTrue((EXAMPLE_ROOT / "scripts/tests").is_dir())

	def test_example_capability_menu_has_button_for_each_demo(self):
		catalog_text = (EXAMPLE_ROOT / "scripts/capability_catalog.ts").read_text(encoding="utf-8")
		scene_text = (EXAMPLE_ROOT / "scenes/main_menu.tscn").read_text(encoding="utf-8")
		demo_count = len(re.findall(r'\n\t\tid: "[^"]+"', catalog_text))
		button_count = len(re.findall(r'name="CapabilityButton[0-9]{2}"', scene_text))

		self.assertGreater(demo_count, 0)
		self.assertEqual(demo_count, button_count)

	def test_generator_sources_do_not_contain_local_machine_paths(self):
		local_path_pattern = re.compile(r"(?:[A-Za-z]:\\|/Users/|/home/[^/\s]+/)")
		offenders = []
		for path in sorted((ROOT / "generator").rglob("*.py")):
			text = path.read_text(encoding="utf-8")
			if local_path_pattern.search(text):
				offenders.append(str(path.relative_to(ROOT)))

		self.assertEqual([], offenders)

	def test_handwritten_source_comments_are_english(self):
		comment_pattern = re.compile(r"/\*.*?\*/|//[^\n]*|#[^\n]*", re.DOTALL)
		non_english_comments = []
		for root in (ROOT / "include", ROOT / "src", ROOT / "generator", ROOT / "test"):
			for path in sorted(root.rglob("*")):
				if not path.is_file() or "generated" in path.parts:
					continue
				if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".jinja2"}:
					continue
				text = path.read_text(encoding="utf-8")
				for match in comment_pattern.finditer(text):
					if re.search(r"[\u3400-\u9fff]", match.group(0)):
						non_english_comments.append(f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1}")

		self.assertEqual([], non_english_comments)

	def test_generator_source_layout_is_flat(self):
		for path in (
			ROOT / "generator/base_generator.py",
			ROOT / "generator/builtin_classes_generator.py",
			ROOT / "generator/class_generator.py",
			ROOT / "generator/register_generator.py",
			ROOT / "generator/utility_functions_generator.py",
			ROOT / "generator/dts_generator.py",
			ROOT / "generator/utils",
			ROOT / "generator/templates",
		):
			self.assertTrue(path.exists(), f"{path.relative_to(ROOT)} should exist")

		for path in (
			ROOT / "generator/builtin",
			ROOT / "generator/class",
			ROOT / "generator/core",
			ROOT / "generator/dts",
			ROOT / "generator/register",
		):
			self.assertFalse(path.exists(), f"{path.relative_to(ROOT)} should not exist")

		entrypoint = (ROOT / "generator/generator.py").read_text(encoding="utf-8")
		self.assertIn("GENERATOR_CLASSES", entrypoint)
		self.assertNotIn("pkgutil", entrypoint)
		self.assertNotIn("discover_generators", entrypoint)

		for path in (
			ROOT / "generator/base_generator.py",
			ROOT / "generator/builtin_classes_generator.py",
			ROOT / "generator/class_generator.py",
			ROOT / "generator/register_generator.py",
			ROOT / "generator/utility_functions_generator.py",
			ROOT / "generator/dts_generator.py",
		):
			text = path.read_text(encoding="utf-8")
			self.assertNotIn("sys.path.append", text, str(path.relative_to(ROOT)))
			self.assertNotIn("sys.path.insert", text, str(path.relative_to(ROOT)))

	def test_plugin_version_matches_cmake_project_version(self):
		cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
		match = re.search(r"project\s*\(\s*gode\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cmake_text)
		self.assertIsNotNone(match, "CMake project version was not found")
		cmake_version = match.group(1)

		parser = configparser.ConfigParser()
		parser.read(EXAMPLE_ROOT / "addons/gode/plugin.cfg", encoding="utf-8")
		self.assertEqual(cmake_version, parser["plugin"]["version"].strip('"'))

	def test_release_changelog_version_matches_project_version(self):
		cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
		match = re.search(r"project\s*\(\s*gode\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cmake_text)
		self.assertIsNotNone(match, "CMake project version was not found")
		project_version = match.group(1)

		for changelog_path in (ROOT / "CHANGELOG.md", ROOT / "CHANGELOG-ZH.md"):
			changelog_text = changelog_path.read_text(encoding="utf-8")
			changelog_match = re.search(r"\A##\s+([0-9]+\.[0-9]+\.[0-9]+)\s*$", changelog_text, re.MULTILINE)
			self.assertIsNotNone(changelog_match, f"{changelog_path.name} top release heading was not found")
			self.assertEqual(project_version, changelog_match.group(1), changelog_path.name)

	def test_platform_build_scripts_pin_codegen_python(self):
		scripts = [
			ROOT / "shell/build-linux.sh",
			ROOT / "shell/build-macos.sh",
			ROOT / "shell/build-ios.sh",
			ROOT / "shell/build-android.sh",
			ROOT / "shell/build-windows.ps1",
			ROOT / "shell/build-android.ps1",
		]

		missing = []
		for script in scripts:
			text = script.read_text(encoding="utf-8")
			for token in ("Python3_EXECUTABLE", "GODE_RUN_CODEGEN"):
				if token not in text:
					missing.append(f"{script.relative_to(ROOT)} missing {token}")

		self.assertEqual([], missing)

	def test_declared_ios_minimum_matches_release_build_target(self):
		ios_target = "16.0"
		build_script = (ROOT / "shell/build-ios.sh").read_text(encoding="utf-8")
		build_workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
		build_doc = (ROOT / "BUILD-ZH.md").read_text(encoding="utf-8")
		readme = (ROOT / "README.md").read_text(encoding="utf-8")
		readme_zh = (ROOT / "README-ZH.md").read_text(encoding="utf-8")
		docs_overview = (ROOT / "docs/src/content/docs/index.md").read_text(encoding="utf-8")
		docs_overview_zh = (ROOT / "docs/src/content/docs/zh/index.md").read_text(encoding="utf-8")

		self.assertIn(f'deployment_target="{ios_target}"', build_script)
		self.assertIn(
			f"./shell/build-ios.sh --arch arm64 --config Release --deployment-target {ios_target} --fresh",
			build_workflow,
		)
		self.assertIn(f"| `--deployment-target <version>` | `{ios_target}` | iOS deployment target\u3002 |", build_doc)
		self.assertIn("| Minimum Version | 10 | 9 | 10.15 | 16 | Ubuntu 22 |", readme)
		self.assertIn("| \u6700\u4f4e\u7248\u672c | 10 | 9 | 10.15 | 16 | Ubuntu 22 |", readme_zh)
		self.assertIn("| iOS | \u2705 | iOS 16 |", docs_overview)
		self.assertIn("| iOS | \u2705 | iOS 16 |", docs_overview_zh)
		self.assertNotIn('deployment_target="12.0"', build_script)
		self.assertNotIn("| `--deployment-target <version>` | `12.0` | iOS deployment target\u3002 |", build_doc)

	def test_cmake_codegen_and_generated_binding_builds_are_incremental(self):
		cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

		for token in (
			'option(GODE_USE_COMPILER_CACHE',
			'option(GODE_UNITY_GENERATED_BINDINGS',
			'set(GODE_UNITY_GENERATED_BATCH_SIZE "8" CACHE STRING',
			'if(NOT GODE_UNITY_GENERATED_BATCH_SIZE MATCHES "^[1-9][0-9]*$")',
			"GODE_UNITY_GENERATED_BATCH_SIZE must be a positive integer.",
			"find_program(GODE_COMPILER_CACHE_PROGRAM sccache)",
			"find_program(GODE_COMPILER_CACHE_PROGRAM ccache)",
			"CMAKE_C_COMPILER_LAUNCHER",
			"CMAKE_CXX_COMPILER_LAUNCHER",
			"add_library(gode_libnode STATIC IMPORTED GLOBAL)",
			'if(WIN32)',
			'set(GODE_LIBNODE_LINK_ITEMS)',
			'set(GODE_LIBNODE_LINK_OPTIONS "/WHOLEARCHIVE:${GODE_LIBNODE_FILE}")',
			'set(GODE_WINDOWS_NODE_SHIM_DEF "${CMAKE_CURRENT_BINARY_DIR}/gode_node_api_forwarders.def")',
			"string(REGEX MATCHALL [[(napi|node_api)_[A-Za-z0-9_]+\\(]]",
			"node_module_register\\(",
			'string(REPLACE "(" "" _GODE_NODE_API_SYMBOL',
			"add_custom_command(TARGET gode_runtime POST_BUILD",
			"/noentry",
			'"/out:$<TARGET_FILE_DIR:gode_runtime>/node.dll"',
			'elseif(APPLE)',
			'set(GODE_LIBNODE_LINK_ITEMS "-Wl,-force_load,${GODE_LIBNODE_FILE}")',
			"set(GODE_LIBNODE_LINK_OPTIONS)",
			'else()',
			'set(GODE_LIBNODE_LINK_ITEMS "-Wl,--whole-archive" "${GODE_LIBNODE_FILE}" "-Wl,--no-whole-archive")',
			"target_link_options(gode_runtime PRIVATE ${GODE_LIBNODE_LINK_OPTIONS})",
			'set_property(TARGET gode_runtime APPEND PROPERTY LINK_DEPENDS "${GODE_LIBNODE_FILE}")',
			'set(GODE_EXTENSION_API_JSON "" CACHE FILEPATH',
			"file(GLOB_RECURSE GODE_CODEGEN_PYTHON_INPUTS CONFIGURE_DEPENDS",
			"file(GLOB GODE_CODEGEN_TEMPLATE_INPUTS CONFIGURE_DEPENDS",
			"generator/requirements.txt",
			"file(SHA256",
			"string(SHA256 _GODE_CODEGEN_INPUT_HASH",
			"CMakeFiles/gode-codegen",
			"input.sha256",
			"Gode code generation inputs unchanged; skipping generator.",
			"Running Gode code generation",
			'"GODOT_EXTENSION_API_JSON=${_GODE_EXTENSION_API_JSON}"',
			"file(GLOB_RECURSE GODE_MANUAL_SOURCES CONFIGURE_DEPENDS",
			"file(GLOB_RECURSE GODE_GENERATED_SOURCES CONFIGURE_DEPENDS",
			"list(REMOVE_ITEM GODE_MANUAL_SOURCES ${GODE_GENERATED_SOURCES})",
			"set(GODE_RUNTIME_SOURCES ${GODE_MANUAL_SOURCES} ${GODE_GENERATED_SOURCES})",
			"set(GODE_EDITOR_SOURCES",
			"UNITY_BUILD ON",
			'UNITY_BUILD_BATCH_SIZE "${GODE_UNITY_GENERATED_BATCH_SIZE}"',
			"SKIP_UNITY_BUILD_INCLUSION ON",
			"Generated binding unity build enabled with batch size",
		):
			self.assertIn(token, cmake_text)

		self.assertNotIn("set(ENV{GODOT_CPP_DIR}", cmake_text)
		self.assertNotIn("\tgode_libnode\n", cmake_text)
		self.assertNotIn("GODE_LIBNODE_IMPORTED_TYPE", cmake_text)

	def test_node_runtime_helpers_are_split_from_runtime_lifecycle(self):
		expected_files = [
			ROOT / "include/runtime/node_bootstrap_scripts.h",
			ROOT / "src/runtime/node_bootstrap_scripts.cpp",
			ROOT / "include/runtime/node_godot_bridge.h",
			ROOT / "src/runtime/node_godot_bridge.cpp",
			ROOT / "src/runtime/node_probe_host.cpp",
			ROOT / "src/runtime/gode_node_main.cpp",
			ROOT / "include/runtime/node_inspector.h",
			ROOT / "src/runtime/node_inspector.cpp",
			ROOT / "include/runtime/node_module_resolver.h",
			ROOT / "src/runtime/node_module_resolver.cpp",
			ROOT / "src/compiler/node_typescript_compiler_bridge.cpp",
		]
		missing = [str(path.relative_to(ROOT)) for path in expected_files if not path.exists()]
		self.assertEqual([], missing)

		bridge_source = (ROOT / "src/runtime/node_godot_bridge.cpp").read_text(encoding="utf-8")
		header_source = (ROOT / "include/runtime/node_godot_bridge.h").read_text(encoding="utf-8")
		probe_source = (ROOT / "src/runtime/node_probe_host.cpp").read_text(encoding="utf-8")
		runtime_source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		self.assertIn("prepare_native_addon_host", header_source)
		self.assertIn("node_runtime_bridge::prepare_native_addon_host();", runtime_source)
		self.assertIn("node_runtime_bridge::prepare_native_addon_host();", probe_source)
		self.assertNotIn('GetModuleHandleW(L"libnode.dll")', bridge_source)
		self.assertNotIn("preload_node_dll_stub", header_source + bridge_source + runtime_source)
		self.assertIn('global.Set("GlobalClass", Napi::Function::New(env, noop_decorator));', bridge_source)

		source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		self.assertLessEqual(len(source.splitlines()), 470)
		for pattern in (
			r"std::string\s+boot_script\s*=\s*\n\s*\"",
			r"std::string\s+esm_script\s*=\s*\n\s*\"",
			r"static\s+Napi::Value\s+fs_readFile",
			r"static\s+Napi::Value\s+fs_stat",
			r"static\s+Napi::Value\s+preload_dlls",
			r"static\s+int\s+read_package_module_type",
			r"NodeRuntime::is_esm_file",
		):
			self.assertIsNone(re.search(pattern, source), pattern)

		bootstrap_source = (ROOT / "src/runtime/node_bootstrap_scripts.cpp").read_text(encoding="utf-8")
		self.assertIn("std::string commonjs_bootstrap_script()", bootstrap_source)
		self.assertIn("std::string esm_bootstrap_script()", bootstrap_source)
		self.assertIn("'.js', '.json', '.node', '.mjs', '.cjs'", bootstrap_source)
		self.assertIn("const _gode_source_fallback", bootstrap_source)
		self.assertIn("res://.gode/build/typescript/", bootstrap_source)
		self.assertIn("__gode_package_export_target", bootstrap_source)
		self.assertIn("__gode_package_manifest_entry", bootstrap_source)
		self.assertIn("__gode_package_import_target", bootstrap_source)
		self.assertIn("const packageExportTarget", bootstrap_source)
		self.assertIn("arguments.length >= 3 ? _originalDlopen.call", bootstrap_source)
		self.assertIn("gode.materialize_path", bootstrap_source)
		self.assertIn("gode.globalize_path", bootstrap_source)
		self.assertIn("global.__gode_real_path_for_virtual_path", bootstrap_source)
		self.assertIn("__gode_strip_virtual_generation", bootstrap_source)
		self.assertIn("crypto.createHash('sha256')", bootstrap_source)
		self.assertIn("__gode_exported_npm_manifest_fingerprint", bootstrap_source)
		self.assertIn("__gode_materialize_node_modules_tree", bootstrap_source)
		self.assertIn("__gode_node_modules_tree_marker_path", bootstrap_source)
		self.assertIn("_forkNeedsNodeModulesTree", bootstrap_source)
		self.assertIn("const _originalFork = cp.fork", bootstrap_source)
		self.assertIn("gode.native_probe_executable", bootstrap_source)
		self.assertIn("fs.chmodSync(value, 0o755)", bootstrap_source)
		self.assertIn("_gode_strip_js_comments", bootstrap_source)
		self.assertIn("_gode_is_esm_module_source", bootstrap_source)
		self.assertNotIn("simulate success", bootstrap_source)
		self.assertNotIn("GODE_NODE_EXECUTABLE", bootstrap_source)
		self.assertNotIn("child_process.fork is not supported", bootstrap_source)
		self.assertNotIn("_fallbackProbe", bootstrap_source)
		self.assertNotIn("msg.gpu === false", bootstrap_source)
		self.assertNotIn(".includes('testBindingBinary')", bootstrap_source)
		self.assertNotIn("testBindingBinary", bootstrap_source)
		self.assertNotIn("_origFork.apply", bootstrap_source)
		self.assertNotIn("user://.gode/typescript/", bootstrap_source)
		self.assertNotIn("typescript-6.0.3", bootstrap_source)

		npm_native_smoke_runner = (ROOT / "test/run_npm_native_smoke.py").read_text(encoding="utf-8")
		self.assertIn("FIXTURE_PROJECT", npm_native_smoke_runner)
		self.assertTrue((ROOT / "test/fixtures/npm_native_llama/scripts/npm_native_smoke.ts").exists())

		resolver_source = (ROOT / "src/runtime/node_module_resolver.cpp").read_text(encoding="utf-8")
		self.assertIn("sanitize_js_for_module_markers", resolver_source)
		self.assertIn("has_static_esm_syntax", resolver_source)
		self.assertIn("has_commonjs_markers", resolver_source)
		self.assertNotIn('code.find("import ")', resolver_source)

	def test_node_runtime_public_v8_entries_hold_locker_and_safe_scopes(self):
		source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		header = (ROOT / "include/runtime/node_runtime.h").read_text(encoding="utf-8")

		self.assertIn("static napi_env napi_environment;", header)
		self.assertNotIn("thread_local napi_env", header)
		self.assertIn("napi_env NodeRuntime::napi_environment = nullptr;", source)
		self.assertNotIn("thread_local_env", source)

		def method_body(name: str) -> str:
			marker = f"NodeRuntime::{name}"
			start = source.find(marker)
			self.assertNotEqual(-1, start, name)
			next_start = len(source)
			for match in re.finditer(r"\n(?:[A-Za-z0-9_:<>]+\s+)+NodeRuntime::[A-Za-z0-9_]+\(", source[start + len(marker):]):
				next_start = start + len(marker) + match.start()
				break
			return source[start:next_start]

		def assert_scope_order(body: str, *tokens: str) -> None:
			last = -1
			for token in tokens:
				index = body.index(token)
				self.assertGreater(index, last, token)
				last = index

		for method in ("run_script", "eval_expression"):
			body = method_body(method)
			assert_scope_order(
				body,
				"v8::Locker locker(isolate);",
				"v8::Isolate::Scope isolate_scope(isolate);",
				"v8::HandleScope handle_scope(isolate);",
			)

		for method in ("compile_script", "get_default_class"):
			body = method_body(method)
			assert_scope_order(
				body,
				"v8::Locker locker(isolate);",
				"v8::Isolate::Scope isolate_scope(isolate);",
				"Napi::EscapableHandleScope handle_scope",
			)
			self.assertIn("handle_scope.Escape", body)
			self.assertNotIn("v8::HandleScope handle_scope(isolate);", body)

	def test_node_runtime_reports_v8_compile_failures(self):
		node_source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		error_header = (ROOT / "include/runtime/napi_error_utils.h").read_text(encoding="utf-8")
		error_source = (ROOT / "src/runtime/napi_error_utils.cpp").read_text(encoding="utf-8")

		self.assertIn("void log_v8_exception(v8::Isolate *isolate, v8::TryCatch &try_catch, const std::string &context);", error_header)
		self.assertIn("void log_v8_exception(v8::Isolate *isolate, v8::TryCatch &try_catch, const std::string &context)", error_source)
		self.assertIn("try_catch.StackTrace(v8_context)", error_source)

		for token in (
			'log_v8_exception(isolate, try_catch, "[Gode ESM] Failed to compile ESM init script")',
			'log_v8_exception(isolate, try_catch, "NodeRuntime run_script compile")',
			'log_v8_exception(isolate, try_catch, "NodeRuntime run_script execution")',
			'log_v8_exception(isolate, try_catch, "NodeRuntime ESM compile call")',
			'log_js_error("NodeRuntime ESM compile rejected", js_error_to_string(js_error))',
			'log_v8_exception(isolate, try_catch, "NodeRuntime CJS compile call")',
		):
			self.assertIn(token, node_source)

	def test_module_lifecycle_owns_resource_format_refs_until_node_shutdown(self):
		source = (ROOT / "src/register_runtime_types.cpp").read_text(encoding="utf-8")

		for class_name, ref_name in (
			("TypeScriptSaver", "typescript_saver"),
			("TypeScriptLoader", "typescript_loader"),
		):
			self.assertIn(f"godot::Ref<gode::{class_name}> {ref_name};", source)

		self.assertNotRegex(
			source,
			r"add_resource_format_(?:loader|saver)\(gode::TypeScript(?:Loader|Saver)::get_singleton\(\)\)",
		)
		self.assertNotRegex(
			source,
			r"remove_resource_format_(?:loader|saver)\([^)]*::get_singleton\(\)\)",
		)

		shutdown_index = source.index("gode::NodeRuntime::shutdown();")
		for token in (
			"typescript_loader->clear_cache();",
			"typescript_loader.unref();",
			"typescript_saver.unref();",
		):
			self.assertIn(token, source)
			self.assertLess(source.index(token), shutdown_index, token)

		for token in (
			"javascript_loader",
			"javascript_saver",
			"add_resource_format_loader(javascript_loader)",
			"add_resource_format_saver(javascript_saver)",
		):
			self.assertNotIn(token, source)

	def test_napi_references_are_released_before_or_suppressed_after_runtime_shutdown(self):
		node_header = (ROOT / "include/runtime/node_runtime.h").read_text(encoding="utf-8")
		node_source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		typescript_header = (ROOT / "include/script/typescript_script.h").read_text(encoding="utf-8")
		typescript_runtime_source = (ROOT / "src/script/typescript_script_runtime.cpp").read_text(encoding="utf-8")
		typescript_script_source = (ROOT / "src/script/typescript_script.cpp").read_text(encoding="utf-8")
		instance_source = (ROOT / "src/script/script_instance.cpp").read_text(encoding="utf-8")
		callable_source = (ROOT / "src/script/script_callable.cpp").read_text(encoding="utf-8")

		self.assertIn("static bool is_running();", node_header)
		self.assertIn("bool NodeRuntime::is_running()", node_source)
		self.assertIn("node_initialized && isolate != nullptr && env != nullptr", node_source)

		self.assertIn("~TypeScriptScript();", typescript_header)
		self.assertIn("TypeScriptScript::~TypeScriptScript()", typescript_runtime_source)
		for source in (typescript_runtime_source, instance_source, callable_source):
			self.assertIn("NodeRuntime::is_running()", source)
			self.assertIn("SuppressDestruct()", source)

		self.assertIn("default_class.Reset();", typescript_runtime_source)
		self.assertIn("FileAccess::get_open_error() != OK", typescript_script_source)
		self.assertIn("Failed to read compiled script", typescript_script_source)
		self.assertIn("return Napi::Function();", typescript_script_source)
		self.assertIn("js_instance.Reset();", instance_source)
		self.assertIn("func_ref.Reset();", callable_source)
		self.assertIn("script->instance_objects.erase(owner);", instance_source)

	def test_typescript_instance_creation_refuses_invalid_runtime_instances(self):
		header = (ROOT / "include/script/script_instance.h").read_text(encoding="utf-8")
		instance_source = (ROOT / "src/script/script_instance.cpp").read_text(encoding="utf-8")
		runtime_source = (ROOT / "src/script/typescript_script_runtime.cpp").read_text(encoding="utf-8")

		self.assertIn("bool is_runtime_instance_valid() const;", header)
		self.assertIn("bool ScriptInstance::is_runtime_instance_valid() const", instance_source)
		self.assertIn("return placeholder || !js_instance.IsEmpty();", instance_source)
		self.assertIn("if (!p_for_object || !compile())", runtime_source)
		self.assertIn("if (!instance->is_runtime_instance_valid())", runtime_source)
		self.assertIn("void *gd_instance = gdextension_interface::script_instance_create3", runtime_source)
		self.assertIn("return gd_instance;", runtime_source)

		create_index = runtime_source.index("ScriptInstance *instance = memnew")
		valid_index = runtime_source.index("if (!instance->is_runtime_instance_valid())", create_index)
		godot_index = runtime_source.index("void *gd_instance = gdextension_interface::script_instance_create3", create_index)
		insert_index = runtime_source.index("instances.insert(instance);", create_index)
		self.assertLess(valid_index, godot_index)
		self.assertLess(godot_index, insert_index)

	def test_script_instance_v8_entries_require_running_node_runtime(self):
		source = (ROOT / "src/script/script_instance.cpp").read_text(encoding="utf-8")

		self.assertIn("NodeRuntime::init_once();", source)
		self.assertNotIn("script->get_default_class()", source)
		self.assertIn("script->ensure_default_class_loaded()", source)
		self.assertIn("script->get_cached_default_class()", source)
		self.assertIn("if (!NodeRuntime::is_running()) {\n\t\t\treturn;\n\t\t}\n\n\t\tv8::Locker locker(NodeRuntime::isolate);", source)
		self.assertIn("if (js_instance.IsEmpty() || !NodeRuntime::is_running()) {\n\t\treturn false;\n\t}", source)
		self.assertIn("if (!NodeRuntime::is_running()) {\n\t\tr_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;", source)
		self.assertIn("if (!NodeRuntime::is_running()) {\n\t\tr_is_valid = false;\n\t\treturn String();", source)

		def source_between(start_marker: str, end_marker: str) -> str:
			start = source.index(start_marker)
			end = source.index(end_marker, start)
			return source[start:end]

		for body in (
			source_between("Variant ScriptInstance::call", "void ScriptInstance::notification_bind"),
			source_between("void ScriptInstance::notification", "String ScriptInstance::to_string"),
		):
			locker = body.index("v8::Locker locker(NodeRuntime::isolate);")
			isolate_scope = body.index("v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);")
			handle_scope = body.index("v8::HandleScope handle_scope(NodeRuntime::isolate);")
			self.assertLess(locker, isolate_scope)
			self.assertLess(isolate_scope, handle_scope)

		has_method_body = source_between("bool ScriptInstance::has_method", "int32_t ScriptInstance::get_method_argument_count")
		self.assertIn("return script->_has_method(p_method);", has_method_body)
		self.assertNotIn("v8::Locker locker(NodeRuntime::isolate);", has_method_body)

		method_start = source.index("int32_t ScriptInstance::get_method_argument_count")
		method_end = source.index("Variant ScriptInstance::call", method_start)
		method_body = source[method_start:method_end]
		self.assertNotIn("v8::Locker locker(NodeRuntime::isolate);", method_body)

		to_string_body = source_between("String ScriptInstance::to_string", "bool ScriptInstance::property_can_revert")
		to_string_locker = to_string_body.index("v8::Locker locker(NodeRuntime::isolate);")
		to_string_class_name = to_string_body.index("String cls_name = String(script->_get_global_name());")
		to_string_scope_end = to_string_body.index("\n\t}\n\tr_is_valid = true;", to_string_locker)
		self.assertLess(to_string_locker, to_string_scope_end)
		self.assertLess(to_string_scope_end, to_string_class_name)

		constructor_body = source_between("ScriptInstance::ScriptInstance", "ScriptInstance::~ScriptInstance")
		constructor_load = constructor_body.index("if (!script->ensure_default_class_loaded())")
		constructor_locker = constructor_body.index("v8::Locker locker(NodeRuntime::isolate);")
		self.assertLess(constructor_load, constructor_locker)
		self.assertIn("ScriptInstanceOwnerScope owner_scope(owner);", constructor_body)
		self.assertNotIn("register_godot_instance(owner, instance);", constructor_body)
		self.assertIn('bind_script_signals_to_instance(instance, "JS script constructor")', constructor_body)

		reload_body = source_between("void ScriptInstance::reload", "bool ScriptInstance::set")
		reload_lockers = [match.start() for match in re.finditer(r"v8::Locker locker\(NodeRuntime::isolate\);", reload_body)]
		self.assertEqual(2, len(reload_lockers))
		reload_load = reload_body.index("if (!script->ensure_default_class_loaded())")
		self.assertLess(reload_lockers[0], reload_load)
		self.assertLess(reload_load, reload_lockers[1])
		self.assertIn("ScriptInstanceOwnerScope owner_scope(owner);", reload_body)
		self.assertNotIn("register_godot_instance(owner, instance);", reload_body)
		self.assertIn('bind_script_signals_to_instance(instance, "JS script reload constructor")', reload_body)

	def test_script_v8_scopes_do_not_call_compiling_metadata_apis(self):
		risky_calls = (
			"ensure_typescript_script_compiled",
			"script->compile()",
			"script->_has_method",
			"script->_is_tool",
			"script->_get_global_name",
			"script->_get_base_script",
			"script->get_base_class_name",
			"script->_has_property_default_value",
			"script->_get_property_default_value",
			"script->ensure_default_class_loaded()",
		)

		def enclosing_block_after(source: str, marker_index: int) -> str:
			stack = []
			for index, char in enumerate(source[:marker_index]):
				if char == "{":
					stack.append(index)
				elif char == "}" and stack:
					stack.pop()

			block_start = stack[-1] if stack else 0
			depth = 0
			for index in range(block_start, len(source)):
				if source[index] == "{":
					depth += 1
				elif source[index] == "}":
					depth -= 1
					if depth == 0:
						return source[marker_index : index + 1]
			return source[marker_index:]

		for path in (
			ROOT / "src/script/script_instance.cpp",
			ROOT / "src/script/typescript_script.cpp",
		):
			source = path.read_text(encoding="utf-8")
			search_from = 0
			while True:
				locker_index = source.find("v8::Locker locker(NodeRuntime::isolate);", search_from)
				if locker_index == -1:
					break
				scope = enclosing_block_after(source, locker_index)
				line = source.count("\n", 0, locker_index) + 1
				for call in risky_calls:
					self.assertNotIn(call, scope, f"{call} appears inside a V8 scope at {path.relative_to(ROOT)}:{line}")
				search_from = locker_index + 1

	def test_typescript_script_compile_state_does_not_reuse_stale_metadata(self):
		source = (ROOT / "src/script/typescript_script.cpp").read_text(encoding="utf-8")
		runtime_source = (ROOT / "src/script/typescript_script_runtime.cpp").read_text(encoding="utf-8")
		header = (ROOT / "include/script/typescript_script.h").read_text(encoding="utf-8")

		self.assertIn("if (!is_dirty) {\n\t\treturn is_valid;\n\t}", source)
		self.assertIn("if (!default_class.IsEmpty())", source)
		self.assertIn("default_class.Reset();", source)
		self.assertIn("is_valid = false;", source)
		self.assertIn("property_list.clear();", source)
		self.assertIn("methods.clear();", source)
		self.assertIn("static_methods.clear();", source)
		self.assertIn("member_lines.clear();", source)
		self.assertIn("bool source_code_loaded = false;", header)
		self.assertIn("HashMap<godot::StringName, godot::MethodInfo> static_methods;", header)
		self.assertIn("const bool source_changed = !source_code_loaded || source_code != p_code;", runtime_source)
		self.assertIn("if (source_changed || p_force_dirty) {\n\t\tis_dirty = true;\n\t}", runtime_source)
		self.assertIn("if (!source_changed && !p_force_dirty && !is_dirty)", runtime_source)
		self.assertIn("return is_valid ? Error::OK : Error::ERR_INVALID_PARAMETER;", runtime_source)
		self.assertIn("auto finish_failed_compile_attempt = [this]()", source)
		self.assertIn("is_dirty = false;", source[source.index("auto finish_failed_compile_attempt = [this]()") :])
		self.assertIn("bool retryable_compile_failure = true;", source)
		self.assertIn("ensure_typescript_script_compiled(path, &js_path, &retryable_compile_failure)", source)
		self.assertIn("if (retryable_compile_failure) {\n\t\t\tis_valid = false;\n\t\t\treturn false;\n\t\t}", source)

		compile_start = source.index("bool TypeScriptScript::compile() const")
		path_index = source.index("String path = get_path();", compile_start)
		for token in (
			"default_class.Reset();",
			"is_valid = false;",
			"class_name = StringName();",
			"property_list.clear();",
			"static_methods.clear();",
			"member_lines.clear();",
		):
			self.assertLess(source.index(token, compile_start), path_index, token)

		self.assertNotIn("Napi::Function get_default_class() const", header)
		self.assertNotIn("Napi::Function TypeScriptScript::get_default_class() const", source)
		self.assertIn("bool ensure_default_class_loaded() const;", header)
		self.assertIn("Napi::Function get_cached_default_class() const;", header)

		default_class_load_start = source.index("bool TypeScriptScript::ensure_default_class_loaded() const")
		first_compile = source.index("if (!compile())", default_class_load_start)
		cache_read = source.index("if (!default_class.IsEmpty())", default_class_load_start)
		self.assertLess(first_compile, cache_read)
		ensure_compiled = source.index("ensure_typescript_script_compiled", default_class_load_start)
		v8_locker = source.index("v8::Locker locker(NodeRuntime::isolate);", default_class_load_start)
		self.assertLess(ensure_compiled, v8_locker)
		self.assertIn("if (!compile()) {\n\t\treturn Error::ERR_INVALID_PARAMETER;\n\t}", runtime_source)
		reload_start = runtime_source.index("Error TypeScriptScript::_reload(bool p_keep_state)")
		reload_fail = runtime_source.index("return Error::ERR_INVALID_PARAMETER;", reload_start)
		reload_loop = runtime_source.index("for (ScriptInstance *instance : instances)", reload_start)
		self.assertLess(reload_fail, reload_loop)

	def test_typescript_static_methods_are_not_instance_methods(self):
		header = (ROOT / "include/script/typescript_script.h").read_text(encoding="utf-8")
		runtime_source = (ROOT / "src/script/typescript_script_runtime.cpp").read_text(encoding="utf-8")
		instance_source = (ROOT / "src/script/script_instance.cpp").read_text(encoding="utf-8")
		error_header = (ROOT / "include/runtime/napi_error_utils.h").read_text(encoding="utf-8")
		error_source = (ROOT / "src/runtime/napi_error_utils.cpp").read_text(encoding="utf-8")

		self.assertIn("HashMap<godot::StringName, godot::MethodInfo> static_methods;", header)
		self.assertIn("bool has_static_method(const godot::StringName &p_method) const;", header)
		self.assertIn("int32_t get_static_method_argument_count(const godot::StringName &p_method) const;", header)
		self.assertIn("godot::Variant call_static(const godot::Variant **p_args, GDExtensionInt p_argcount, GDExtensionCallError &r_error);", header)
		self.assertIn("godot::Variant call_static_method(const godot::StringName &p_method, const godot::Variant **p_args, int32_t p_argcount, GDExtensionCallError &r_error) const;", header)
		self.assertIn("Dictionary method_info_to_dictionary", runtime_source)
		self.assertIn("Dictionary method = p_method;", runtime_source)
		self.assertIn('dict["default_args"] = da;', (ROOT / "third/godot-cpp/src/core/object.cpp").read_text(encoding="utf-8"))
		self.assertNotIn('d["flags"] = (int)p_method.flags;', runtime_source)
		self.assertIn('ClassDB::bind_method(D_METHOD("has_static_method", "method"), &TypeScriptScript::has_static_method);', runtime_source)
		self.assertIn('ClassDB::bind_method(D_METHOD("get_static_method_argument_count", "method"), &TypeScriptScript::get_static_method_argument_count);', runtime_source)
		self.assertIn("ClassDB::bind_vararg_method(", runtime_source)
		self.assertIn('"call_static"', runtime_source)
		self.assertIn("void attach_promise_rejection_handler(Napi::Value value, const std::string &context);", error_header)
		self.assertIn("void attach_promise_rejection_handler(Napi::Value value, const std::string &context)", error_source)
		self.assertNotIn("_attach_promise_rejection_handler", instance_source)
		self.assertIn("attach_promise_rejection_handler(result, method_name);", instance_source)

		has_method_body = runtime_source[
			runtime_source.index("bool TypeScriptScript::_has_method") :
			runtime_source.index("bool TypeScriptScript::_has_static_method")
		]
		self.assertIn("return methods.has(p_method);", has_method_body)
		self.assertNotIn("static_methods", has_method_body)

		has_static_method_body = runtime_source[
			runtime_source.index("bool TypeScriptScript::_has_static_method") :
			runtime_source.index("Variant TypeScriptScript::_get_script_method_argument_count")
		]
		self.assertIn("return static_methods.has(p_method);", has_static_method_body)
		self.assertNotIn("METHOD_FLAG_STATIC", has_static_method_body)

		static_call_body = runtime_source[
			runtime_source.index("Variant TypeScriptScript::call_static_method") :
			runtime_source.index("bool TypeScriptScript::_editor_can_reload_from_file")
		]
		self.assertIn("!static_methods.has(p_method)", static_call_body)
		self.assertIn("ensure_default_class_loaded()", static_call_body)
		self.assertIn("class_object.HasOwnProperty(method_name)", static_call_body)
		self.assertIn("godot_to_napi(env, *p_args[i])", static_call_body)
		self.assertIn("method.Call(class_object, args)", static_call_body)
		self.assertIn("napi_to_godot(result)", static_call_body)
		self.assertIn("attach_promise_rejection_handler(result, context)", static_call_body)
		self.assertNotIn("default_class.New", static_call_body)

		argument_count_body = runtime_source[
			runtime_source.index("Variant TypeScriptScript::_get_script_method_argument_count") :
			runtime_source.index("Dictionary TypeScriptScript::_get_method_info")
		]
		self.assertIn("methods[p_method].arguments.size();", argument_count_body)
		self.assertIn("static_methods[p_method].arguments.size();", argument_count_body)

		method_info_body = runtime_source[
			runtime_source.index("Dictionary TypeScriptScript::_get_method_info") :
			runtime_source.index("bool TypeScriptScript::_is_tool")
		]
		self.assertIn("method_info_to_dictionary(methods[p_method]);", method_info_body)
		self.assertIn("method_info_to_dictionary(static_methods[p_method]);", method_info_body)

		method_list_body = runtime_source[
			runtime_source.index("TypedArray<Dictionary> TypeScriptScript::_get_script_method_list") :
			runtime_source.index("TypedArray<Dictionary> TypeScriptScript::_get_script_property_list")
		]
		self.assertIn("for (const KeyValue<StringName, MethodInfo> &E : methods)", method_list_body)
		self.assertIn("for (const KeyValue<StringName, MethodInfo> &E : static_methods)", method_list_body)

		instance_argument_count_body = instance_source[
			instance_source.index("int32_t ScriptInstance::get_method_argument_count") :
			instance_source.index("Variant ScriptInstance::call")
		]
		self.assertIn("script->methods.has(p_method)", instance_argument_count_body)
		self.assertNotIn("static_methods", instance_argument_count_body)

		instance_method_list_body = instance_source[
			instance_source.index("void ScriptInstance::get_method_list") :
			instance_source.index("void ScriptInstance::free_method_list")
		]
		self.assertIn("script->methods", instance_method_list_body)
		self.assertNotIn("static_methods", instance_method_list_body)

		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")
		test_runner = (ROOT / "example/scripts/tests/tests_runner.gd").read_text(encoding="utf-8")
		self.assertIn("static staticBridgeAdd(left: number, right: number): number", runtime_test)
		self.assertIn('current_test.has_method("staticBridgeAdd")', test_runner)
		self.assertIn('var script: Object = current_test.get_script()', test_runner)
		self.assertIn('script.call("has_static_method", "staticBridgeAdd")', test_runner)
		self.assertIn('script.call("get_static_method_argument_count", "staticBridgeAdd") != 2', test_runner)
		self.assertIn('script.call("call_static", "staticBridgeAdd", 6, 7) != 13', test_runner)

	def test_generated_static_napi_references_reset_before_node_environment_free(self):
		node_source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		builtin_header = (ROOT / "include/generated/register_builtin.gen.h").read_text(encoding="utf-8")
		builtin_source = (ROOT / "src/generated/register_builtin.gen.cpp").read_text(encoding="utf-8")
		class_header = (ROOT / "include/generated/register_classes.gen.h").read_text(encoding="utf-8")
		class_source = (ROOT / "src/generated/register_classes.gen.cpp").read_text(encoding="utf-8")
		register_builtin_template = (ROOT / "generator/templates/register_builtin.cpp.jinja2").read_text(encoding="utf-8")
		register_classes_template = (ROOT / "generator/templates/register_classes.cpp.jinja2").read_text(encoding="utf-8")

		for token in (
			"reset_builtin_references();",
			"reset_class_references();",
			"clear_registered_godot_classes();",
		):
			self.assertIn(token, node_source)
			self.assertLess(node_source.index(token), node_source.index("node::FreeEnvironment(env);"))

		self.assertIn("void reset_builtin_references();", builtin_header)
		self.assertIn("void reset_class_references();", class_header)
		self.assertIn("void reset_builtin_references()", builtin_source)
		self.assertIn("void reset_class_references()", class_source)
		self.assertIn("constructor.Reset();", builtin_source)
		self.assertIn("constructor.Reset();", class_source)
		self.assertIn("constructor.Reset();", register_builtin_template)
		self.assertIn("constructor.Reset();", register_classes_template)

	def test_generated_global_enums_have_runtime_exports(self):
		builtin_source = (ROOT / "src/generated/register_builtin.gen.cpp").read_text(encoding="utf-8")
		class_source = (ROOT / "src/generated/register_classes.gen.cpp").read_text(encoding="utf-8")
		register_generator = (ROOT / "generator/register_generator.py").read_text(encoding="utf-8")
		register_template = (ROOT / "generator/templates/register_builtin.cpp.jinja2").read_text(encoding="utf-8")
		register_classes_template = (ROOT / "generator/templates/register_classes.cpp.jinja2").read_text(encoding="utf-8")
		binding_policy = (ROOT / "generator/utils/binding_policy.py").read_text(encoding="utf-8")
		godot_dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		self.assertIn("'global_enums': global_enums", register_generator)
		self.assertNotIn("singleton_enum_aliases", register_generator)
		self.assertIn("global_enum_export_name", binding_policy)
		self.assertNotIn("singleton_enum_export_name", binding_policy)
		self.assertNotIn("from .dts_generator import", register_generator)
		self.assertIn('exports.Set("{{ enum.name }}", {{ enum.variable_name }});', register_template)
		self.assertNotIn('exports.Set("{{ enum.name }}", {{ enum.variable_name }});', register_classes_template)
		self.assertIn("gode::godot_result_to_napi", register_template)
		self.assertNotIn("const enum", godot_dts)
		for enum_name in ("PropertyHint", "VariantType", "VariantOperator"):
			self.assertIn(f'exports.Set("{enum_name}"', builtin_source)
			self.assertIn(f"    export enum {enum_name} {{", godot_dts)
		self.assertNotIn('exports.Set("ResourceLoader_CacheMode"', class_source)
		self.assertNotIn("ResourceLoader_CacheMode", godot_dts)
		self.assertIn("    export namespace ResourceLoader {", godot_dts)
		self.assertIn("        export type CacheMode = 0 | 1 | 2 | 3 | 4;", godot_dts)
		self.assertIn("            readonly CacheMode: {", godot_dts)
		self.assertIn("            load(path: GDString | StringName | string, type_hint?: GDString | StringName | string, cache_mode?: import(\"godot\").ResourceLoader.CacheMode): Resource;", godot_dts)

	def test_value_convert_registry_and_cache_are_restart_safe(self):
		header = (ROOT / "include/runtime/value_convert.h").read_text(encoding="utf-8")
		source = (ROOT / "src/runtime/value_convert.cpp").read_text(encoding="utf-8")

		self.assertIn("void clear_registered_godot_classes();", header)
		self.assertIn("static std::vector<std::string> class_order;", source)
		self.assertIn("const bool is_new_class", source)
		self.assertIn("if (is_new_class)", source)
		self.assertNotIn("std::vector<ClassInfo> class_list", source)
		self.assertNotIn("class_list.push_back", source)
		self.assertIn("static void release_object_reference", source)
		self.assertIn("NodeRuntime::is_running()", source)
		self.assertIn("object_cache[id] = Napi::Weak(js_obj);", source)
		self.assertNotIn("object_cache[id] = Napi::Persistent(js_obj);", source)
		self.assertIn("static thread_local godot::Object *script_instance_owner", source)
		self.assertIn("godot::Object *consume_script_instance_owner()", source)
		self.assertIn("ref.SuppressDestruct();", source)
		self.assertNotIn("entry.second.Reset();", source)

	def test_generated_refcounted_wrappers_delete_from_unref_result(self):
		template = (ROOT / "generator/templates/class_binding.cpp.jinja2").read_text(encoding="utf-8")
		resource_source = (ROOT / "src/generated/classes/resource_binding.gen.cpp").read_text(encoding="utf-8")

		for source in (template, resource_source):
			self.assertIn("if (ref->unreference())", source)
			self.assertNotIn("ref->get_reference_count() == 0", source)
			self.assertNotIn("ref->unreference();\n                if", source)

	def test_node_runtime_shutdown_releases_typescript_script_handles(self):
		node_runtime = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		script_header = (ROOT / "include/script/typescript_script.h").read_text(encoding="utf-8")
		script_runtime = (ROOT / "src/script/typescript_script_runtime.cpp").read_text(encoding="utf-8")

		self.assertIn("TypeScriptScript::release_all_runtime_state();", node_runtime)
		self.assertIn("static void release_all_runtime_state();", script_header)
		self.assertIn("void release_runtime_state();", script_header)
		self.assertIn("HashSet<TypeScriptScript *> live_scripts;", script_runtime)
		self.assertIn("live_scripts.insert(this);", script_runtime)
		self.assertIn("live_scripts.erase(this);", script_runtime)
		self.assertIn("instance->release_runtime_state();", script_runtime)

	def test_typescript_loader_exposes_explicit_cache_clear(self):
		header = (ROOT / "include/script/typescript_loader.h").read_text(encoding="utf-8")
		source = (ROOT / "src/script/typescript_loader.cpp").read_text(encoding="utf-8")

		self.assertIn("void clear_cache();", header)
		self.assertIn("void reload_cached_scripts();", header)
		self.assertIn("godot::HashMap<godot::StringName, godot::ObjectID> scripts;", header)
		self.assertNotIn("godot::HashMap<godot::StringName, godot::Ref<TypeScriptScript>> scripts;", header)
		self.assertIn("godot::Error reload_source_code(const godot::String &p_code, bool p_keep_state, bool p_force_dirty = false);", (ROOT / "include/script/typescript_script.h").read_text(encoding="utf-8"))
		self.assertIn("void TypeScriptLoader::clear_cache()", source)
		self.assertIn("void TypeScriptLoader::reload_cached_scripts()", source)
		self.assertIn("scripts.clear();", source)
		self.assertIn("clear_cache();", source)
		self.assertIn("cached_scripts.reserve(scripts.size());", source)
		self.assertIn("script_ref->reload_source_code(source_code, true, true);", source)
		self.assertIn("ObjectDB::get_instance", source)
		self.assertIn("should_cache_loaded_script", source)
		self.assertIn("bool is_typescript_script_path", source)
		self.assertIn('!lower.ends_with(".d.ts")', source)
		self.assertIn("CACHE_MODE_IGNORE", source)
		self.assertIn("CACHE_MODE_IGNORE_DEEP", source)
		self.assertIn("FileAccess::get_open_error() != OK", source)
		self.assertIn("return Error::ERR_CANT_OPEN;", source)
		self.assertIn("if (should_cache_loaded_script(p_cache_mode))", source)
		self.assertIn("normalize_load_path", source)
		self.assertIn('project_settings->localize_path(path).replace("\\\\", "/").simplify_path();', source)
		self.assertIn("String read_path = p_original_path.is_empty() ? load_path : p_original_path;", source)
		self.assertIn("StringName cache_key(load_path);", source)
		self.assertIn("scripts.has(cache_key)", source)
		self.assertIn("script->set_path(load_path);", source)
		self.assertIn("scripts[cache_key] = ObjectID(script->get_instance_id());", source)
		self.assertNotIn("scripts[cache_key] = Ref(script);", source)
		self.assertIn('p_type == StringName("Script")', source)
		self.assertIn("p_type == TypeScriptScript::get_class_static()", source)
		self.assertIn("return is_typescript_script_path(p_path);", source)
		self.assertIn("return String(TypeScriptScript::get_class_static());", source)
		resource_type_body = source[
			source.index("String TypeScriptLoader::_get_resource_type") :
			source.index("String TypeScriptLoader::_get_resource_script_class")
		]
		self.assertNotIn('return String("Script");', resource_type_body)
		self.assertIn("tree_sitter_typescript", source)
		self.assertIn("String TypeScriptLoader::_get_resource_script_class", source)
		self.assertIn("find_default_resource_class", source)
		self.assertIn("default_resource_class_name", source)
		self.assertIn("class_name_from_class_node", source)
		self.assertIn("default_exported_class_name_from_statement", source)
		self.assertIn("default_exported_name_from_clause", source)
		self.assertIn("find_class_declaration_by_name", source)
		self.assertIn("node_text_is_default", source)
		self.assertIn("find_default_resource_class(root_node, source)", source)
		self.assertNotIn("String TypeScriptLoader::_get_resource_script_class(const String &p_path) const {\n\treturn String();\n}", source)
		self.assertIn("PackedStringArray TypeScriptLoader::_get_dependencies", source)
		self.assertIn("collect_dependency_specifiers", source)
		self.assertIn("resolve_imported_typescript_path", source)
		self.assertIn('path_join(String(import_path.c_str())).replace("\\\\", "/").simplify_path()', source)
		self.assertIn('lower.ends_with(".jsx")', source)
		self.assertIn('"index.ts"', source)
		self.assertIn('"index.tsx"', source)
		self.assertIn('"index.d.ts"', source)
		self.assertIn("return FileAccess::file_exists(base) ? base : String();", source)
		self.assertIn("find_first_string_literal", source)
		self.assertIn("import_specifier_from_first_argument", source)
		self.assertIn("unwrap_import_specifier_expression", source)
		self.assertIn('ts_node_child_by_field_name(node, "arguments", 9)', source)
		self.assertIn("ts_node_named_child(arguments, 0)", source)
		self.assertIn("return import_specifier_from_literal_node(specifier_node, source, r_occurrence);", source)
		self.assertNotIn("return find_first_string_literal(ts_node_named_child(arguments, 0), source, r_occurrence);", source)
		self.assertIn("append_resolved_dependency(path, specifier, p_add_types, seen, dependencies);", source)
		self.assertIn("HashSet<String> seen;", source)
		self.assertNotIn("return PackedStringArray();\n}", source[source.index("PackedStringArray TypeScriptLoader::_get_dependencies"):source.index("Error TypeScriptLoader::_rename_dependencies")])
		self.assertIn("PackedStringArray TypeScriptLoader::_get_classes_used", source)
		self.assertIn("default_resource_base_class_name", source)
		self.assertIn("collect_godot_imported_classes", source)
		self.assertIn("TSNode import_clause_from_statement", source)
		self.assertIn('strcmp(ts_node_type(child), "import_clause") == 0', source)
		self.assertIn("ts_node_named_child_count(clause)", source)
		self.assertIn("append_unique_class_name", source)
		self.assertIn('source_occurrence.specifier != "godot"', source)
		self.assertNotIn('ts_node_child_by_field_name(child, "import_clause", 13)', source)
		classes_used_body = source[source.index("PackedStringArray TypeScriptLoader::_get_classes_used"):source.index("Variant TypeScriptLoader::_load")]
		self.assertIn("ts_parser_parse_string(parser, nullptr", classes_used_body)
		self.assertIn("append_unique_class_name(default_resource_base_class_name(root_node, source), seen, classes);", classes_used_body)
		self.assertIn("collect_godot_imported_classes(root_node, source, seen, classes);", classes_used_body)
		self.assertNotEqual("PackedStringArray TypeScriptLoader::_get_classes_used(const String &p_path) const {\n\treturn PackedStringArray();\n}", classes_used_body.strip())
		self.assertIn("Error TypeScriptLoader::_rename_dependencies", source)
		self.assertIn("normalized_rename_map", source)
		self.assertIn("collect_dependency_specifier_occurrences", source)
		self.assertIn("target_path_for_specifier_style", source)
		self.assertIn("source_to_runtime_output_path", source)
		self.assertIn("relative_module_path", source)
		self.assertIn('ascii_ends_with(lower_specifier, ".js")', source)
		self.assertIn("renames.has(resolved)", source)
		self.assertIn("source.replace(replacement.first.start", source)
		self.assertIn("std::sort(replacements.begin(), replacements.end()", source)
		self.assertIn("FileAccess::open(path, FileAccess::WRITE)", source)
		self.assertIn("return Error::ERR_PARSE_ERROR;", source)
		self.assertNotIn("Error TypeScriptLoader::_rename_dependencies(const String &p_path, const Dictionary &p_renames) const {\n\treturn Error::OK;\n}", source)

		load_body = source[source.index("Variant TypeScriptLoader::_load") :]
		self.assertIn("return Error::ERR_FILE_UNRECOGNIZED;", load_body)
		self.assertLess(load_body.index("ERR_FILE_UNRECOGNIZED"), load_body.index("FileAccess::get_file_as_string"))
		set_path_index = load_body.index("script->set_path(load_path);")
		set_source_index = load_body.index("script->_set_source_code(source_code);")
		self.assertLess(set_path_index, set_source_index)

	def test_typescript_language_editor_surface_is_not_stubbed(self):
		source = (ROOT / "src/script/typescript_language.cpp").read_text(encoding="utf-8")

		for token in (
			"TS_RESERVED_WORDS",
			"TS_CONTROL_FLOW_WORDS",
			"delimiters.push_back(\"/* */\");",
			"delimiters.push_back(\"/** */\");",
			"delimiters.push_back(\"``\");",
			"render_template_source",
			"make_template_entry",
			"TypeScript scripts must be saved under res://.",
			"TypeScript script paths cannot contain parent-directory segments.",
			"TypeScript script paths must end with .ts or .tsx.",
			"format_function_argument",
			"collect_tree_sitter_errors",
			"is_script_method_node",
			"is_script_method_name_node",
			"class_body_node",
			"method_node_is_accessor",
			"method_node_is_static",
			"script_method_name",
			"append_validate_function_names",
			"find_function_line",
			"describe_tree_sitter_error",
			"class_name_from_extends_node",
			"class_name_from_class_node",
			"default_exported_class_name_from_statement",
			"default_exported_name_from_clause",
			"find_class_declaration_by_name",
			"is_class_declaration_node",
			'"abstract_class_declaration"',
			"node_text_is_default",
			'strcmp(node_type, "member_expression")',
			'strcmp(node_type, "generic_type")',
			"tree_sitter_typescript",
			"find_default_class",
			"parse_global_class_metadata",
			"FileAccess::get_file_as_string(path)",
			"reload_typescript_script_from_file",
			"TypeScriptLoader::get_singleton()->reload_cached_scripts();",
			"source_code = FileAccess::get_file_as_string(path);",
			"source_code = script->_get_source_code();",
			"script->reload_source_code(source_code, p_keep_state, true);",
			"d[\"is_tool\"]",
			"d[\"base_type\"]",
			"p_type == String(TypeScriptScript::get_class_static())",
		):
			self.assertIn(token, source)

		handles_global_class_body = source[
			source.index("bool TypeScriptLanguage::_handles_global_class_type") :
			source.index("Dictionary TypeScriptLanguage::_get_global_class_name")
		]
		for legacy_type in ('String("TypeScript")', 'String("ts")', 'String("tsx")'):
			self.assertNotIn(legacy_type, handles_global_class_body)

		self.assertIn("bool TypeScriptLanguage::_is_using_templates() {\n\treturn true;\n}", source)
		self.assertIn("bool TypeScriptLanguage::_has_named_classes() const {\n\treturn true;\n}", source)
		self.assertIn("bool TypeScriptLanguage::_can_make_function() const {\n\treturn true;\n}", source)

		validate_body = source[
			source.index("Dictionary TypeScriptLanguage::_validate") :
			source.index("String TypeScriptLanguage::_validate_path")
		]
		for token in (
			'd["valid"] = true;',
			'd["errors"] = Array();',
			'd["warnings"] = Array();',
			'd["safe_lines"] = PackedInt32Array();',
			"TSParser *parser = ts_parser_new();",
			"!ts_parser_set_language(parser, tree_sitter_typescript())",
			"ts_parser_parse_string(parser, nullptr",
			"ts_node_has_error(root)",
			"collect_tree_sitter_errors(root, p_path, errors);",
			"TSNode script_class = find_default_class(root, ts_node_child_count(root), source);",
			"append_validate_function_names(script_class, source, functions);",
			"ts_tree_delete(tree);",
			"ts_parser_delete(parser);",
		):
			self.assertIn(token, validate_body)
		self.assertNotIn("NodeRuntime", validate_body)
		self.assertNotIn("GodeTypeScriptCompiler", validate_body)
		self.assertNotEqual("Dictionary TypeScriptLanguage::_validate(const String &p_script, const String &p_path, bool p_validate_functions, bool p_validate_errors, bool p_validate_warnings, bool p_validate_safe_lines) const {\n\tDictionary d;\n\treturn d;\n}", validate_body.strip())

		validate_path_body = source[
			source.index("String TypeScriptLanguage::_validate_path") :
			source.index("Object *TypeScriptLanguage::_create_script")
		]
		self.assertIn("normalize_resource_script_path(p_path)", validate_path_body)
		self.assertIn('ext != String("ts") && ext != String("tsx")', validate_path_body)
		self.assertNotEqual("String TypeScriptLanguage::_validate_path(const String &p_path) const {\n\treturn String();\n}", validate_path_body.strip())

		append_functions_body = source[
			source.index("void append_validate_function_names") :
			source.index("int32_t find_function_line")
		]
		self.assertIn("TSNode body = class_body_node(class_node);", append_functions_body)
		self.assertIn('return ts_node_child_by_field_name(class_node, "body", 4);', source)
		self.assertIn('return strcmp(ts_node_type(node), "method_definition") == 0;', source)
		self.assertIn('strcmp(ts_node_type(name_node), "property_identifier") == 0', source)
		self.assertIn('strcmp(ts_node_type(ts_node_child(method_node, i)), "static") == 0', source)
		self.assertIn("method_node_is_static(method_node)", source)
		self.assertIn('functions.push_back(name + ":" + String::num_int64(ts_node_start_point(name_node).row + 1));', append_functions_body)
		self.assertNotIn('strcmp(node_type, "function_declaration")', append_functions_body)
		self.assertNotIn('abstract_method_signature', append_functions_body)
		self.assertNotIn("append_validate_function_names(ts_node_named_child", source)

		find_function_body = source[
			source.index("int32_t TypeScriptLanguage::_find_function") :
			source.index("String TypeScriptLanguage::_make_function")
		]
		self.assertIn("TSNode script_class = find_default_class(root, ts_node_child_count(root), source);", find_function_body)
		self.assertIn("find_function_line(script_class, source, p_function);", find_function_body)
		self.assertNotIn("sanitize_typescript_identifier(p_function", find_function_body)
		self.assertIn("return static_cast<int32_t>(ts_node_start_point(name_node).row);", source)
		self.assertIn('r_name == String("constructor")', source)
		self.assertIn('strcmp(child_type, "get") == 0 || strcmp(child_type, "set") == 0', source)
		self.assertNotIn("find_function_line(ts_node_named_child", source)
		self.assertNotIn("strip_typescript_method_modifiers", source)

		global_class_body = source[
			source.index("Dictionary TypeScriptLanguage::_get_global_class_name") :
			len(source)
		]
		self.assertNotIn("ResourceLoader::get_singleton()->load", global_class_body)
		self.assertNotIn("script->_get_global_name()", global_class_body)
		self.assertNotIn("script->get_base_class_name()", global_class_body)

	def test_typescript_decorator_metadata_is_bound_to_default_class(self):
		language_source = (ROOT / "src/script/typescript_language.cpp").read_text(encoding="utf-8")
		script_source = (ROOT / "src/script/typescript_script.cpp").read_text(encoding="utf-8")

		for source in (language_source, script_source):
			for token in (
				"decorator_expression_name_matches",
				'ts_node_child_by_field_name(expression, "function", 8)',
				"ts_node_named_child_count(arguments) > 0",
				"decorator_matches_name",
				"class_node_has_decorator",
				"export_statement_declares_class",
				"ts_node_eq",
				"default_class_has_decorator",
				"!export_statement_declares_class(child, class_node)",
				'const char *GLOBAL_CLASS_DECORATORS[] = { "GlobalClass" }',
				'const char *TOOL_DECORATORS[] = { "Tool", "tool" }',
			):
				self.assertIn(token, source)
			self.assertNotIn("ClassName", source)
			self.assertNotIn("is_class_name_decorator_text", source)
			self.assertNotIn("class_node_has_class_name_decorator", source)

	def test_typescript_builtin_template_entries_include_godot_required_fields(self):
		source = (ROOT / "src/script/typescript_language.cpp").read_text(encoding="utf-8")
		match = re.search(r"Dictionary make_template_entry\(.*?\n\}", source, re.DOTALL)
		self.assertIsNotNone(match)
		body = match.group(0)

		for key in ("inherit", "name", "description", "content", "id", "origin"):
			self.assertIn(f'entry["{key}"]', body)
		self.assertIn("SCRIPT_TEMPLATE_ORIGIN_BUILT_IN", source)
		self.assertIn('entry["origin"] = SCRIPT_TEMPLATE_ORIGIN_BUILT_IN;', body)
		self.assertNotIn('entry["origin"] = 0;', body)

	def test_typescript_metadata_parser_resolves_project_imports_like_compiler(self):
		source = (ROOT / "src/script/typescript_script.cpp").read_text(encoding="utf-8")

		for token in (
			"resolve_imported_typescript_path",
			"first_existing_source_candidate",
			"existing_source_candidate",
			"class_name_from_extends_node",
			"class_name_from_class_node",
			"default_exported_class_name_from_statement",
			"default_exported_name_from_clause",
			"find_class_declaration_by_name",
			"is_class_declaration_node",
			'"abstract_class_declaration"',
			"node_text_is_default",
			"unwrap_metadata_expression",
			"qualifier_from_extends_node",
			"extends_class_node_from_class",
			"resolve_imported_class_path",
			"import_clause_from_statement",
			"ImportedSymbolResolution",
			"resolve_imported_symbol",
			"import_specifier_resolves_name",
			"namespace_import_binds_qualifier",
			'ts_node_child_by_field_name(import_specifier, "alias", 5)',
			'strcmp(child_type, "namespace_import")',
			'strcmp(child_type, "identifier")',
			"ts_node_named_child_count(clause)",
			"parent_ts->get_property_list_ordered()",
			"property_list.push_back(property);",
			"upsert_ordered_property",
			"parse_static_exports(class_node, source, get_path(), root_node, child_count, properties, property_list, property_defaults);",
			'strcmp(node_type, "member_expression")',
			'strcmp(node_type, "generic_type")',
			"class_name_tail",
			"ExportTypeKind",
			"ExportTypeClassification",
			"ExportObjectResolution",
			"TypeParameterBinding",
			"find_type_parameter_binding",
			"build_type_parameter_bindings",
			"if (effective_types.size() > 1)",
			"type_text_from_annotation",
			"find_type_alias_definition",
			"type_alias_parameter_names",
			"classify_type_alias_reference",
			"canonical_type_name",
			"godot_class_name_from_type",
			"classify_engine_object_class",
			"resolve_typescript_object_kind",
			"classify_export_type",
			"apply_export_type_classification",
			"configure_property_type",
			"finalize_explicit_object_hint",
			"PROPERTY_HINT_RESOURCE_TYPE",
			"PROPERTY_HINT_NODE_TYPE",
			'class_db->is_parent_class(class_name, StringName("Resource"))',
			'class_db->is_parent_class(class_name, StringName("Node"))',
			"TSNode default_node = {};",
			"if (!type_str.empty())",
			"parse_default_value(default_node, source, pi.type, default_value)",
			'lower.ends_with(".jsx")',
			'stem + String(".tsx")',
			'stem + String(".ts")',
			'stem + String(".d.ts")',
			'base.path_join("index.ts")',
			'base.path_join("index.tsx")',
			'base.path_join("index.d.ts")',
			"is_relative_module_specifier",
			'path_join(String(import_path.c_str())).replace("\\\\", "/").simplify_path()',
			"return FileAccess::file_exists(base) ? base : String();",
			"resolve_imported_typescript_path(file_path, import_path, include_dts)",
			"resolve_imported_symbol(file_path, source, root_node, child_count, class_name, class_qualifier, false).path",
			"resolve_imported_symbol(file_path, source, root_node, child_count, alias_name, qualifier_from_type_text(type_str), true)",
			"base_class_qualifier = qualifier_from_extends_node(base_class_node, source);",
			"base_script_path = resolve_imported_class_path(get_path(), source, root_node, child_count, base_class_name, base_class_qualifier);",
			"collect_parent_properties(class_node, source, root_node, child_count, get_path(), properties, property_list, interface_array_schemas, property_defaults, interfaces, visited_classes);",
			"FileAccess::get_open_error() != OK",
			"if (!ext_parser)",
			"if (!ext_tree)",
			"if (!parser)",
			"!ts_parser_set_language(parser, tree_sitter_typescript())",
			"if (!tree)",
			"Failed to create TypeScript metadata parser",
			"Failed to configure TypeScript metadata parser",
			"Failed to parse TypeScript metadata",
			"normalize_numeric_literal",
			"parse_integer_literal",
			"parse_integer_range",
			"parse_non_negative_int",
			"parse_bool_literal",
			"is_script_method_name_node",
			"node_has_static_modifier",
			"method_node_is_accessor",
			"static_methods",
			"parse_rpc_mode",
			"parse_transfer_mode",
			"parse_int_metadata_value",
			"parse_property_hint_value",
			"parse_metadata_string_value",
			"parse_float_literal",
			"parse_numeric_default",
			"std::isfinite(value)",
			"std::trunc(float_value)",
			"if (parse_rpc_mode(val_text, parsed_mode))",
			"if (parse_transfer_mode(val_text, parsed_mode))",
			"if (parse_bool_literal(val_text, parsed_bool))",
			"if (parse_non_negative_int(val_text, parsed_channel))",
		):
			self.assertIn(token, source)
		self.assertNotIn('path_join(String(import_path.c_str()) + ".ts")', source)
		self.assertIn('name == "rpc_config"', source)
		self.assertIn('key == "transfer_mode"', source)
		self.assertIn('key == "call_local"', source)
		self.assertIn('key_str == "hint_string"', source)
		self.assertIn('field_key == "hint_string"', source)
		self.assertNotIn('name == "rpcConfig"', source)
		self.assertNotIn('name == "rpcs"', source)
		self.assertNotIn('key == "transferMode"', source)
		self.assertNotIn('key == "callLocal"', source)
		self.assertNotIn('key == "mode"', source)
		self.assertNotIn('mode == "any"', source)
		self.assertNotIn('mode == "master"', source)
		self.assertNotIn('key_str == "hintString"', source)
		self.assertNotIn('field_key == "hintString"', source)
		self.assertNotIn("std::stoi(", source)
		self.assertNotIn("std::atoi", source)
		self.assertEqual(1, source.count("std::stod("))

		method_parse_start = source.index('if (strcmp(member_type, "method_definition") == 0)')
		method_parse_body = source[
			method_parse_start :
			source.index("static bool parse_default_value", method_parse_start)
		]
		self.assertIn("if (!is_script_method_name_node(mn))", method_parse_body)
		self.assertIn('method_name == StringName("constructor") || method_node_is_accessor(member, mn)', method_parse_body)
		self.assertIn("const bool is_static = node_has_static_modifier(member);", method_parse_body)
		self.assertIn("mi.flags |= METHOD_FLAG_STATIC;", method_parse_body)
		self.assertIn("static_methods[method_name] = mi;", method_parse_body)
		self.assertIn("methods[method_name] = mi;", method_parse_body)
		self.assertIn("member_lines[method_name] = ts_node_start_point(mn).row + 1;", method_parse_body)
		self.assertNotIn("member_lines[method_name] = ts_node_start_point(member).row + 1;", method_parse_body)

		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")
		runtime_base = (ROOT / "example/scripts/tests/runtime_base_test.ts").read_text(encoding="utf-8")
		runtime_export_types = (ROOT / "example/scripts/tests/runtime_export_types.ts").read_text(encoding="utf-8")
		runtime_external_resource = (ROOT / "example/scripts/tests/runtime_external_resource.ts").read_text(encoding="utf-8")
		self.assertIn("export abstract class RuntimeSameFileExportBase extends RuntimeBaseModule.RuntimeIntegrationBase", runtime_test)
		self.assertIn("class RuntimeIntegrationTest extends RuntimeSameFileExportBase", runtime_test)
		self.assertIn('same_file_inherited_label: string = "same-file-base";', runtime_test)
		self.assertIn("same_file_inherited_count: number = 23;", runtime_test)
		self.assertIn('this.property_can_revert("same_file_inherited_label")', runtime_test)
		self.assertIn("export default RuntimeIntegrationTest;", runtime_test)
		self.assertIn("static signals = {", runtime_test)
		self.assertIn("} as const;", runtime_test)
		self.assertIn("} satisfies ExportMap;", runtime_test)
		self.assertIn('"label": { "type": "String", "hint": 20, "hint_string": "runtime label" }', runtime_test)
		self.assertIn('"enabled": { "type": "bool", "default": true as const }', runtime_test)
		self.assertIn('"count": { "type": "int", "default": 7 as const }', runtime_test)
		self.assertIn('"static_resource_default_first": { "default": null, "type": "Resource" }', runtime_test)
		self.assertIn('"static_image": { "type": "Image", "default": null }', runtime_test)
		self.assertIn('"static_imported_resource_array": { "type": "RuntimeExportTypes.RuntimeImportedResourceArray", "default": [] as const }', runtime_test)
		self.assertIn('"static_imported_generic_dictionary": { "type": "RuntimeImportedGenericDictionary<RuntimeArrayResource>", "default": {} as const }', runtime_test)
		self.assertIn('"static_imported_generic_external_array": { "type": "RuntimeImportedGenericArray<RuntimeExternalResource>", "default": [] as const }', runtime_test)
		self.assertIn("static_imported_generic_external_array = new GDArray() as RuntimeImportedGenericArray<RuntimeExternalResource>;", runtime_test)
		self.assertIn("__gode_load_esm", runtime_test)
		self.assertIn("__gode_compile_esm", runtime_test)
		self.assertIn("runtime_pending_retry_fixture.js", runtime_test)
		self.assertIn("nodeAssert.equal(reloadedModule.recovered, 84);", runtime_test)
		self.assertIn("gode-esm-retry-", runtime_test)
		self.assertIn("throw new Error('dependency failed');", runtime_test)
		self.assertIn("const originalConsoleError = console.error;", runtime_test)
		self.assertIn("console.error = () => undefined;", runtime_test)
		self.assertIn("await nodeAssert.rejects(() => loadEsm", runtime_test)
		self.assertIn("nodeAssert.equal(retriedModule.recovered, 42);", runtime_test)
		self.assertIn("nodeAssert.equal(retriedLinkedModule.recovered, 42);", runtime_test)
		self.assertIn("static_dependency.mjs", runtime_test)
		self.assertIn("nodeAssert.equal(firstStaticModule.recovered, 101);", runtime_test)
		self.assertIn("nodeAssert.equal(recompiledStaticModule.recovered, 202);", runtime_test)
		self.assertIn('import { fileURLToPath } from "node:url";', runtime_test)
		self.assertIn("nodeAssert.equal(fileURLToPath(String(metaModule.url)), metaPath);", runtime_test)
		self.assertIn('ResourceLoader.get_dependencies("res://scripts/tests/dependency_scan_test.ts")', runtime_test)
		self.assertIn('scanDependencyPaths.includes("res://scripts/tests/runtime_helpers.ts")', runtime_test)
		self.assertIn('!scanDependencyPaths.includes("res://scripts/tests/signal_test.ts")', runtime_test)
		self.assertIn("nodeAssert.equal(Number(labelProperty.hint), 20);", runtime_test)
		self.assertIn('nodeAssert.equal(String(labelProperty.hint_string), "runtime label");', runtime_test)
		self.assertIn("resource_slot: Resource | null = null;", runtime_test)
		self.assertIn("image_slot: Image | null = null;", runtime_test)
		self.assertIn("node_slot: Node | null = null;", runtime_test)
		self.assertIn("const VARIANT_TYPE_OBJECT = 24;", runtime_test)
		self.assertIn("const PROPERTY_HINT_RESOURCE_TYPE = 17;", runtime_test)
		self.assertIn("const PROPERTY_HINT_NODE_TYPE = 34;", runtime_test)
		self.assertIn('type RuntimeEditorStringEnum = "idle" | \'running\' | null | "done";', runtime_test)
		self.assertIn('editor_string_enum: RuntimeEditorStringEnum = "idle";', runtime_test)
		self.assertIn('editor_imported_string_enum: RuntimeImportedLevel = "alpha";', runtime_test)
		self.assertIn('editor_imported_single_string_enum: RuntimeImportedSingleLevel = "solo";', runtime_test)
		self.assertIn('editor_string_enum_array: Array<RuntimeEditorStringEnum> = ["idle"];', runtime_test)
		self.assertIn('editor_imported_default_alias_resource_array: Array<RuntimeResourceAlias> = [];', runtime_test)
		self.assertIn('editor_imported_resource_map: ImportedResourceMapAlias = new Map();', runtime_test)
		self.assertIn('editor_imported_generic_resource_array: RuntimeImportedGenericArray<RuntimeArrayResource> = new GDArray() as RuntimeImportedGenericArray<RuntimeArrayResource>;', runtime_test)
		self.assertIn('editor_imported_generic_external_resource_array: RuntimeImportedGenericArray<RuntimeExternalResource> = new GDArray() as RuntimeImportedGenericArray<RuntimeExternalResource>;', runtime_test)
		self.assertIn('editor_imported_generic_resource_dictionary: RuntimeImportedGenericDictionary<RuntimeArrayResource> = new GDDictionary() as RuntimeImportedGenericDictionary<RuntimeArrayResource>;', runtime_test)
		self.assertIn('editor_namespace_alias_resource_array: RuntimeExportTypes.RuntimeImportedResourceArray = [];', runtime_test)
		self.assertIn('editor_mixed_union: "automatic" | number = "automatic";', runtime_test)
		self.assertIn("editor_mixed_object_union!: Resource | Node;", runtime_test)
		self.assertIn('nodeAssert.equal(String(stringEnumProperty.hint_string), "idle,running,done");', runtime_test)
		self.assertIn('nodeAssert.equal(String(importedStringEnumProperty.hint_string), "alpha,beta");', runtime_test)
		self.assertIn('nodeAssert.equal(String(importedSingleStringEnumProperty.hint_string), "solo");', runtime_test)
		self.assertIn('assertArrayExportMetadata("editor_string_enum_array", `${VARIANT_TYPE_STRING}/${PROPERTY_HINT_ENUM}:idle,running,done`);', runtime_test)
		self.assertIn('assertArrayExportMetadata("editor_imported_default_alias_resource_array", `${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeArrayResource`);', runtime_test)
		self.assertIn('assertDictionaryExportMetadata("editor_imported_resource_map", `${VARIANT_TYPE_STRING}:;${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeArrayResource`);', runtime_test)
		self.assertIn('assertArrayExportMetadata("editor_imported_generic_resource_array", `${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeArrayResource`);', runtime_test)
		self.assertIn('assertArrayExportMetadata("editor_imported_generic_external_resource_array", `${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeExternalResource`);', runtime_test)
		self.assertIn('assertDictionaryExportMetadata("editor_imported_generic_resource_dictionary", `${VARIANT_TYPE_STRING}:;${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeArrayResource`);', runtime_test)
		self.assertIn('assertArrayExportMetadata("editor_namespace_alias_resource_array", `${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeArrayResource`);', runtime_test)
		self.assertIn('assertArrayExportMetadata("static_imported_resource_array", `${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeArrayResource`);', runtime_test)
		self.assertIn('assertDictionaryExportMetadata("static_imported_generic_dictionary", `${VARIANT_TYPE_STRING}:;${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeArrayResource`);', runtime_test)
		self.assertIn('assertArrayExportMetadata("static_imported_generic_external_array", `${VARIANT_TYPE_OBJECT}/${PROPERTY_HINT_RESOURCE_TYPE}:RuntimeExternalResource`);', runtime_test)
		self.assertIn('nodeAssert.equal(String(explicitHintEnumProperty.hint_string), "manual enum hint");', runtime_test)
		self.assertIn('assertObjectExportMetadata("resource_slot", PROPERTY_HINT_RESOURCE_TYPE, "Resource");', runtime_test)
		self.assertIn('assertObjectExportMetadata("image_slot", PROPERTY_HINT_RESOURCE_TYPE, "Image");', runtime_test)
		self.assertIn('assertObjectExportMetadata("node_slot", PROPERTY_HINT_NODE_TYPE, "Node");', runtime_test)
		self.assertIn("packedScene.pack(this)", runtime_test)
		self.assertIn("ResourceSaver.save(packedScene, savePath)", runtime_test)
		self.assertIn('findPackedScenePropertyValue(loadedScene, "resource_slot")', runtime_test)
		self.assertIn('label = "runtime" as string;', runtime_test)
		self.assertIn("enabled = true as boolean;", runtime_test)
		self.assertIn("count = 7 as number;", runtime_test)
		self.assertIn("spawn_offset = new Vector3(4, 5, 6) as Vector3;", runtime_test)
		self.assertIn("class RuntimeIntegrationBase extends Node", runtime_base)
		self.assertIn('@Export({ "hint": 20, "hint_string": "base label" } as const)', runtime_base)
		self.assertIn('nodeAssert.equal(String(inheritedLabelProperty.hint_string), "base label");', runtime_test)
		self.assertIn("export { RuntimeIntegrationBase };", runtime_base)
		self.assertIn("export default RuntimeIntegrationBase;", runtime_base)
		self.assertIn('export type RuntimeImportedLevel = "alpha" | "beta";', runtime_export_types)
		self.assertIn('export type RuntimeImportedSingleLevel = "solo" | null;', runtime_export_types)
		self.assertIn("export type RuntimeImportedResourceArray = Array<RuntimeArrayResource>;", runtime_export_types)
		self.assertIn("export type RuntimeImportedResourceMap = ReadonlyMap<string, RuntimeArrayResource>;", runtime_export_types)
		self.assertIn("export type RuntimeImportedGenericArray<T extends VariantArgument> = GDArray<T>;", runtime_export_types)
		self.assertIn("export type RuntimeImportedGenericDictionary<T extends VariantArgument> = GDDictionary<string, T>;", runtime_export_types)
		self.assertIn("export default class RuntimeExternalResource extends Resource", runtime_external_resource)
		dependency_scan_test = (ROOT / "example/scripts/tests/dependency_scan_test.ts").read_text(encoding="utf-8")
		self.assertIn('import(dynamicSpecifier, { with: { type: "./signal_test" } } as any)', dependency_scan_test)
		self.assertIn('import("./runtime_helpers.js", { with: { type: "json" } } as any)', dependency_scan_test)
		self.assertIn('import(("./runtime_helpers.js" as const), { with: { type: "json" } } as any)', dependency_scan_test)
		self.assertIn('import(useSignal ? "./signal_test" : "./runtime_helpers")', dependency_scan_test)
		self.assertIn('import("./signal_test" + suffix)', dependency_scan_test)

		signal_test = (ROOT / "example/scripts/tests/signal_test.ts").read_text(encoding="utf-8")
		self.assertIn('import { GDDictionary, Node, Signal, type VariantArgument, Vector3 } from "godot";', signal_test)
		self.assertIn('Signal<(message: string, count: number) => void>', signal_test)
		self.assertIn('typed_completed!: Signal<(message: string, count: number) => void>;', signal_test)
		self.assertIn("constructor() {", signal_test)
		self.assertIn("super();", signal_test)
		self.assertIn("SignalTest.constructor_owner_id = this.get_instance_id();", signal_test)
		self.assertIn('SignalTest.constructor_owner_id === this.get_instance_id()', signal_test)
		self.assertIn('this.typed_completed.connect((message, count) => {', signal_test)
		self.assertIn('this.typed_completed.emit("ready", 2);', signal_test)
		self.assertIn("function dictionaryValue(container: VariantArgument, key: string): VariantArgument", signal_test)
		self.assertIn("static signals = {", signal_test)
		self.assertIn("} as const;", signal_test)
		self.assertIn("} satisfies ExportMap;", signal_test)
		self.assertIn('"threshold": { "type": "int", "hint": 1, "hint_string": "0,10,1", "default": 3 as const }', signal_test)
		self.assertIn("static rpc_config = {", signal_test)
		self.assertIn('run_test: { rpc_mode: "authority", transfer_mode: "reliable", call_local: true, channel: 0 }', signal_test)
		self.assertIn("} satisfies RpcConfig;", signal_test)
		self.assertIn("const script = this.get_script() as { get_rpc_config(): VariantArgument };", signal_test)
		self.assertIn('const rpcMetadata = dictionaryValue(script.get_rpc_config(), "run_test");', signal_test)
		self.assertIn('dictionaryValue(rpcMetadata, "rpc_mode")', signal_test)
		self.assertIn('dictionaryValue(rpcMetadata, "transfer_mode")', signal_test)
		self.assertIn('dictionaryValue(rpcMetadata, "call_local")', signal_test)
		self.assertIn('assert(String(thresholdProperty.hint_string) === "0,10,1"', signal_test)
		self.assertIn("threshold = 3 as const;", signal_test)

	def test_typescript_exported_property_state_is_not_a_noop(self):
		script_instance_header = (ROOT / "include/script/script_instance.h").read_text(encoding="utf-8")
		script_instance = (ROOT / "src/script/script_instance.cpp").read_text(encoding="utf-8")
		script_instance_info = (ROOT / "src/script/script_instance_info.cpp").read_text(encoding="utf-8")

		self.assertIn("void get_property_state(GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) const;", script_instance_header)
		self.assertIn("void ScriptInstance::get_property_state(GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) const", script_instance)
		self.assertIn("if (!script->compile())", script_instance)
		self.assertIn("PROPERTY_USAGE_STORAGE", script_instance)
		self.assertIn("script->get_property_list_ordered()", script_instance)
		self.assertIn("script->properties.has(property.name)", script_instance)
		self.assertIn("p_add_func(", script_instance)
		state_callback_start = script_instance_info.index("static void script_instance_get_property_state")
		state_callback_body = script_instance_info[state_callback_start:state_callback_start + 500]
		self.assertIn("ScriptInstance *instance = cast_instance(p_instance);", state_callback_body)
		self.assertIn("instance->get_property_state(p_add_func, p_userdata);", state_callback_body)
		self.assertNotIn("(void)p_instance;", state_callback_body)
		self.assertNotIn("(void)p_add_func;", state_callback_body)
		self.assertNotIn("(void)p_userdata;", state_callback_body)

	def test_typescript_resource_properties_have_lifetime_anchors(self):
		script_instance_header = (ROOT / "include/script/script_instance.h").read_text(encoding="utf-8")
		script_instance = (ROOT / "src/script/script_instance.cpp").read_text(encoding="utf-8")
		value_convert = (ROOT / "src/runtime/value_convert.cpp").read_text(encoding="utf-8")
		test_runner = (ROOT / "example/scripts/tests/tests_runner.gd").read_text(encoding="utf-8")

		self.assertIn("mutable godot::HashMap<godot::StringName, godot::Variant> property_storage;", script_instance_header)
		self.assertIn("void store_property_value_for_lifetime", script_instance_header)
		self.assertIn("variant_can_hold_godot_object_reference", script_instance)
		self.assertIn("property_storage[p_name] = p_value;", script_instance)
		self.assertIn("property_storage.erase(p_name);", script_instance)
		self.assertIn("object_cache[id] = Napi::Weak(js_obj);", value_convert)
		self.assertNotIn("object_cache[id] = Napi::Persistent(js_obj);", value_convert)
		self.assertIn('ResourceLoader.load(RUNTIME_NESTED_RESOURCE_PATH, "", ResourceLoader.CACHE_MODE_IGNORE_DEEP)', test_runner)
		self.assertIn('nested_container.get("nested")', test_runner)
		self.assertIn('nested_container.set("nested", null)', test_runner)

	def test_legacy_javascript_script_language_surface_is_removed(self):
		for path in (
			ROOT / "include/support",
			ROOT / "include/utils",
			ROOT / "src/support",
			ROOT / "src/utils",
			ROOT / "include/script/javascript_loader.h",
			ROOT / "include/script/javascript_saver.h",
			ROOT / "include/script/javascript_language.h",
			ROOT / "include/script/script_resource.h",
			ROOT / "src/script/javascript_loader.cpp",
			ROOT / "src/script/javascript_saver.cpp",
			ROOT / "src/script/javascript_language.cpp",
			ROOT / "src/script/script_resource.cpp",
			ROOT / "third/tree-sitter-javascript",
			EXAMPLE_ROOT / "addons/gode/icons/javascript.svg",
			EXAMPLE_ROOT / "addons/gode/icons/javascript.svg.import",
		):
			self.assertFalse(path.exists(), f"{path.relative_to(ROOT)} should not exist")

		register_source = (ROOT / "src/register_runtime_types.cpp").read_text(encoding="utf-8")
		self.assertNotIn("JavascriptLanguage", register_source)
		self.assertNotIn("GDREGISTER_ABSTRACT_CLASS", register_source)
		self.assertIn("GDREGISTER_CLASS(gode::TypeScriptScript);", register_source)
		self.assertIn("TypeScriptLanguage", register_source)

		typescript_language_header = (ROOT / "include/script/typescript_language.h").read_text(encoding="utf-8")
		typescript_language_source = (ROOT / "src/script/typescript_language.cpp").read_text(encoding="utf-8")
		typescript_script_header = (ROOT / "include/script/typescript_script.h").read_text(encoding="utf-8")
		self.assertIn("godot::ScriptLanguageExtension", typescript_language_header)
		self.assertIn("mutable godot::String base_script_path;", typescript_script_header)
		self.assertNotIn("JavascriptLanguage", typescript_language_header)
		self.assertNotIn('arr.push_back(String("js"))', typescript_language_source)
		self.assertIn('arr.push_back(String("ts"))', typescript_language_source)
		self.assertIn('arr.push_back(String("tsx"))', typescript_language_source)

		typescript_runtime_source = (ROOT / "src/script/typescript_script_runtime.cpp").read_text(encoding="utf-8")
		base_script_body = typescript_runtime_source[
			typescript_runtime_source.index("Ref<Script> TypeScriptScript::_get_base_script() const") :
			typescript_runtime_source.index("StringName TypeScriptScript::_get_global_name() const")
		]
		for token in ("require(", "res://node_modules", "package.json", "index.js"):
			self.assertNotIn(token, base_script_body)
		self.assertIn("ResourceLoader::get_singleton()->load(base_script_path)", base_script_body)
		self.assertIn("base_script_path == get_path()", base_script_body)

		inherits_body = typescript_runtime_source[
			typescript_runtime_source.index("bool TypeScriptScript::_inherits_script") :
			typescript_runtime_source.index("StringName TypeScriptScript::_get_instance_base_type")
		]
		self.assertIn("Ref<Script> direct_base = _get_base_script();", inherits_body)
		self.assertIn("direct_base_ts->_inherits_script(p_script)", inherits_body)

		for path in sorted((ROOT / "src").glob("**/*.cpp")) + sorted((ROOT / "include").glob("**/*.h")):
			if "generated" in path.parts:
				continue
			text = path.read_text(encoding="utf-8")
			self.assertNotIn("ScriptResource", text, str(path.relative_to(ROOT)))
			self.assertNotIn("JavascriptLoader", text, str(path.relative_to(ROOT)))
			self.assertNotIn("JavascriptSaver", text, str(path.relative_to(ROOT)))
			self.assertNotIn("Typescript", text, str(path.relative_to(ROOT)))

	def test_unused_javascript_parser_dependency_is_removed(self):
		for path in (ROOT / ".gitmodules", ROOT / "CMakeLists.txt"):
			text = path.read_text(encoding="utf-8")
			for token in (
				"third/tree-sitter-javascript",
				"TREE_SITTER_JAVASCRIPT",
				"gode_tree_sitter_javascript",
				"tree_sitter_javascript",
			):
				self.assertNotIn(token, text, str(path.relative_to(ROOT)))

	def test_typescript_default_config_is_packaged_and_project_root_config_exists(self):
		template_path = EXAMPLE_ROOT / "addons/gode/config/tsconfig.json"
		project_config_path = EXAMPLE_ROOT / "tsconfig.json"
		self.assertTrue(template_path.exists())
		self.assertTrue(project_config_path.exists())

		template = json.loads(template_path.read_text(encoding="utf-8"))
		project_config = json.loads(project_config_path.read_text(encoding="utf-8"))
		self.assertEqual(template, project_config)

		options = template["compilerOptions"]
		self.assertEqual("ESNext", options["module"])
		self.assertEqual("Bundler", options["moduleResolution"])
		self.assertEqual("react", options["jsx"])
		self.assertTrue(options["strict"])
		self.assertEqual([], options["types"])
		self.assertNotIn("baseUrl", options)
		self.assertNotIn("paths", options)
		self.assertIn("**/*.ts", template["include"])
		self.assertIn("**/*.tsx", template["include"])
		self.assertIn("**/*.d.ts", template["include"])
		self.assertIn("addons/gode/tsc", template["exclude"])

		compiler_header = (ROOT / "include/compiler/typescript_compiler.h").read_text(encoding="utf-8")
		compiler_wrapper_source = (ROOT / "src/compiler/typescript_compiler.cpp").read_text(encoding="utf-8")
		compiler_source = (ROOT / "src/compiler/typescript_project_compiler.cpp").read_text(encoding="utf-8")
		compile_service_header = (ROOT / "include/script/typescript_compile_service.h").read_text(encoding="utf-8")
		compile_service_source = (ROOT / "src/script/typescript_compile_service.cpp").read_text(encoding="utf-8")
		project_compiler_header = (ROOT / "include/compiler/typescript_project_compiler.h").read_text(encoding="utf-8")
		runtime_bridge_source = (ROOT / "src/runtime/gode_runtime_bridge.cpp").read_text(encoding="utf-8")
		register_source = (ROOT / "src/register_editor_types.cpp").read_text(encoding="utf-8")
		self.assertIn('PROJECT_TYPESCRIPT_CONFIG_PATH = "res://tsconfig.json"', compiler_source)
		self.assertIn('DEFAULT_TYPESCRIPT_CONFIG_PATH = "res://addons/gode/config/tsconfig.json"', compiler_source)
		self.assertIn('TYPESCRIPT_COMPILER_BRIDGE_PATH = "res://addons/gode/runtime/typescript_compiler.js"', compiler_source)
		self.assertIn("ensure_project_typescript_config", compiler_source)
		self.assertIn("make_error_diagnostic", compiler_source)
		self.assertIn("Failed to read TypeScript source", compiler_source)
		self.assertIn("collect_project_sources(source_diagnostics)", compiler_source)
		self.assertIn("Failed to read one or more TypeScript project sources.", compiler_source)
		self.assertIn('TYPESCRIPT_BUILD_ROOT = "res://.gode/build/typescript"', compiler_source)
		self.assertNotIn("ensure_script_compiled", compiler_header)
		self.assertIn("bool ensure_typescript_script_compiled(const godot::String &p_source_path, godot::String *r_compiled_path = nullptr, bool *r_retryable_failure = nullptr);", compile_service_header)
		self.assertIn("godot::Dictionary compile_typescript_project(bool p_force = false);", project_compiler_header)
		self.assertIn("godot::Dictionary compile_typescript_source(const godot::String &p_source_path, bool p_force = false);", project_compiler_header)
		self.assertIn("static void clear_compile_cache();", compiler_header)
		self.assertNotIn("compile_project_static", compiler_header)
		self.assertNotIn("compile_project_static", compiler_wrapper_source)
		self.assertIn("#include <mutex>", compiler_source)
		self.assertIn("#include <algorithm>", compiler_source)
		self.assertIn("#include <cstdint>", compiler_source)
		self.assertIn("std::lock_guard<std::mutex> compile_lock", compiler_source)
		self.assertIn("TypeScriptProjectCompileCache", compiler_source)
		self.assertIn("project_compile_cache", compiler_source)
		self.assertIn("sorted_sources_by_path(sources)", compiler_source)
		self.assertIn("project_input_hash(sources)", compiler_source)
		self.assertIn("compile_failure_is_cacheable", compiler_source)
		self.assertIn("compile_result_outputs_are_present(cache.result)", compiler_source)
		self.assertIn('cached_result["cached"] = true;', compiler_source)
		self.assertIn('cached_result["compiled"] = 0;', compiler_source)
		self.assertIn('result["retryable"] = !ok;', compiler_source)
		self.assertIn("const bool cacheable_failure = compile_failure_is_cacheable(compile_result);", compiler_source)
		self.assertIn('result["retryable"] = !cacheable_failure;', compiler_source)
		self.assertIn('if (!bool(result.get("cached", false)))', compile_service_source)
		self.assertIn('bool(result.get("retryable", true))', compile_service_source)
		self.assertIn("cache.input_hash = input_hash;", compiler_source)
		self.assertIn("cache.result = duplicate_compile_result(result);", compiler_source)
		self.assertIn("reset_project_compile_cache", compiler_source)
		self.assertIn("void GodeTypeScriptCompiler::clear_compile_cache()", compiler_wrapper_source)
		self.assertIn("GodeTypeScriptCompiler::clear_compile_cache();", register_source)
		self.assertIn('#include "compiler/typescript_project_compiler.h"', compiler_wrapper_source)
		self.assertIn('ClassDB::bind_static_method(get_class_static(), D_METHOD("compile_project", "force")', compiler_wrapper_source)
		self.assertIn("return compile_typescript_project(p_force);", compiler_wrapper_source)
		self.assertNotIn("ClassDB::bind_method", compiler_wrapper_source)
		self.assertIn('"res://package.json"', compiler_source)
		self.assertIn('"res://pnpm-lock.yaml"', compiler_source)
		self.assertIn("GODE_MODULE_TYPES_PATH", compiler_source)
		self.assertIn("String normalize_path_string(const String &path)", compiler_source)
		self.assertIn('return path.replace("\\\\", "/").simplify_path();', compiler_source)
		self.assertIn("bool path_has_parent_segment(const String &path)", compiler_source)
		self.assertIn("bool path_has_parent_segment(const String &path)", compile_service_source)
		self.assertIn("clear_generated_output_root", compiler_source)
		self.assertIn("clear_generated_directory_contents", compiler_source)
		self.assertIn("remove_generated_file_if_safe", compiler_source)
		self.assertIn("Failed to clear generated TypeScript output", compiler_source)
		self.assertIn("output_for_source", compiler_source)
		self.assertIn("append_error_diagnostic", compiler_source)
		self.assertIn("Source was not emitted by the active TypeScript project", compiler_source)
		self.assertIn('result["path"] = output.get("path", result["path"])', compiler_source)
		self.assertIn("output_entry_is_valid", compiler_source)
		self.assertIn("output_entry_is_valid", compile_service_source)
		self.assertIn("source_output_path_is_valid", compiler_source)
		self.assertIn("source_output_path_is_valid", compile_service_source)
		self.assertIn("normalize_typescript_source_path", compiler_source)
		self.assertIn("normalize_typescript_source_path", compile_service_source)
		self.assertIn("TypeScript source path cannot contain parent-directory segments", compiler_source)
		self.assertIn("TypeScript source path cannot contain parent-directory segments", compile_service_source)
		self.assertIn("Invalid TypeScript source path, expected a .ts or .tsx file under res://", compiler_source)
		self.assertIn("if (!normalize_typescript_source_path(p_source_path, source_path, &path_error))", compiler_source)
		self.assertIn("if (!normalize_typescript_source_path(p_source_path, source_path))", compiler_source)
		self.assertIn("path_is_under_root", compiler_source)
		self.assertIn("if (path_has_parent_segment(path) || path_has_parent_segment(root_path))", compiler_source)
		self.assertIn("if (path_has_parent_segment(path))", compiler_source)
		self.assertIn("path_is_under_root(output_path, cache_root())", compiler_source)
		self.assertIn('path_is_under_root(String(output["exported_path"]), exported_build_root())', compiler_source)
		self.assertIn("exported_manifest_path", compile_service_source)
		self.assertIn("ExportManifestCache", compile_service_source)
		self.assertIn("std::lock_guard<std::mutex> lock(export_manifest_mutex())", compile_service_source)
		self.assertIn("load_exported_manifest_outputs(exported_outputs)", compile_service_source)
		self.assertIn("bool is_export_runtime_process()", compile_service_source)
		self.assertIn('!os || !os->has_feature("editor")', compile_service_source)
		self.assertIn("if (is_export_runtime_process())", compile_service_source)
		self.assertIn("return ensure_from_export_manifest(source_path, r_compiled_path, r_retryable_failure);", compile_service_source)
		self.assertIn("Exported TypeScript manifest is missing or invalid", compile_service_source)
		self.assertIn("Source was not included in the exported TypeScript manifest", compile_service_source)
		self.assertIn("Exported TypeScript output is missing", compile_service_source)
		self.assertIn('path_has_extension(output_path, ".js")', compiler_source)
		self.assertIn('path_has_extension(String(output["exported_path"]), ".js")', compiler_source)
		self.assertIn('path_has_extension(String(output["exported_path"]), ".js")', compile_service_source)
		self.assertIn("DirAccess::remove_absolute(normalized_path)", compiler_source)
		self.assertNotIn(".compile-manifest.json", compiler_source)
		self.assertNotIn("String input_signature", compiler_source)
		self.assertNotIn("load_cached_outputs", compiler_source)
		self.assertNotIn("save_compile_manifest", compiler_source)
		self.assertNotIn("(void)force;", compiler_source)
		self.assertNotIn('result["skipped"]', compiler_source)
		self.assertNotIn("require_cache_path", compiler_source)
		self.assertNotIn("DirAccess::remove_absolute(source_map_path)", compiler_source)
		self.assertNotIn("user://.gode/typescript/", compiler_source)
		self.assertNotIn("!engine->is_editor_hint() && FileAccess::file_exists(exported_path)", compiler_source)
		self.assertNotIn("is_emittable_typescript_path", compiler_source)
		self.assertIn('GODE_RUNTIME_BRIDGE_CLASS = "GodeRuntimeBridge"', compiler_source)
		self.assertIn('compile_method("compile_typescript_project")', compiler_source)
		self.assertIn("ClassDB::class_call_static(bridge_class, compile_method, sources)", compiler_source)
		self.assertNotIn("NodeRuntime::compile_typescript_project", compiler_source)
		self.assertNotIn("NodeRuntime::compile_typescript_project", compiler_wrapper_source)
		self.assertIn("NodeRuntime::compile_typescript_project(p_files)", runtime_bridge_source)
		self.assertNotIn("compile_project(bool p_force)", runtime_bridge_source)
		self.assertNotIn("bridge_dictionary_result", compiler_wrapper_source)
		self.assertNotIn("bridge_string_result", compiler_wrapper_source)
		compile_start = compiler_source.index("Dictionary compile_project_internal(bool force)")
		cache_hit = compiler_source.index("compile_result_outputs_are_present(cache.result)", compile_start)
		clear_outputs = compiler_source.index("if (!clear_generated_output_root())", compile_start)
		self.assertLess(cache_hit, clear_outputs)

	def test_example_project_has_no_root_external_dependency_marker(self):
		for name in (
			"package.json",
			"node_modules",
			"gode.json",
		):
			self.assertFalse(
				(EXAMPLE_ROOT / name).exists(),
				f"example/{name} should not be present in the dependency-free sample project",
			)

	def test_package_script_requires_typescript_plugin_assets(self):
		package_script = (ROOT / ".github/shell/package-plugin.sh").read_text(encoding="utf-8")
		gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")

		for path in (
			"plugin.cfg",
			"gode.gd",
			"binary/gode.gdextension",
			"binary/gode.gdextension.uid",
			"binary/gode_editor.gdextension.template",
			"config/gode.json",
			"config/tsconfig.json",
			"icons/typescript.svg",
			"runtime/event_loop.gd",
			"runtime/export_plugin.gd",
			"runtime/typescript_compiler.js",
			"types/globals.d.ts",
			"types/godot.d.ts",
			"binary/windows/x64/libgode_runtime.dll",
			"binary/windows/x64/node.dll",
			"binary/windows/x64/gode_node.exe",
			"binary/linux/x64/libgode_runtime.so",
			"binary/linux/x64/gode_node",
			"binary/macos/arm64/libgode_runtime.dylib",
			"binary/macos/arm64/gode_node",
			"binary/android/arm64/libgode_runtime.so",
			"binary/ios/arm64/libgode_runtime.dylib",
			"binary/editor/windows/x64/libgode_editor.dll",
			"binary/editor/linux/x64/libgode_editor.so",
			"binary/editor/macos/arm64/libgode_editor.dylib",
		):
			self.assertIn(f'"{path}"', package_script)
		self.assertNotIn("icons/typescript.svg.import", package_script)
		for stale_name in ("libgode.dll", "libgode.so", "libgode.dylib"):
			self.assertIn(stale_name, package_script)

		self.assertIn("prepare-typescript.sh", package_script)
		self.assertIn("tsc/package.json", package_script)
		self.assertIn("tsc/lib/typescript.js", package_script)
		self.assertIn('if [ -f "$staged_addon_root/$file" ]; then', package_script)
		self.assertIn('chmod +x "$staged_addon_root/$file"', package_script)
		self.assertIn("!example/addons/gode/binary/gode_editor.gdextension.template", gitignore)
		self.assertNotIn('"binary/gode_editor.gdextension"', package_script)
		self.assertNotIn('"binary/gode_editor.gdextension.uid"', package_script)
		self.assertFalse((EXAMPLE_ROOT / "addons/gode/binary/gode_editor.gdextension").exists())
		self.assertFalse((EXAMPLE_ROOT / "addons/gode/binary/gode_editor.gdextension.uid").exists())

	def test_prepare_typescript_scripts_cover_shell_and_powershell(self):
		prepare_sh = (ROOT / ".github/shell/prepare-typescript.sh").read_text(encoding="utf-8")
		prepare_ps1 = (ROOT / ".github/shell/prepare-typescript.ps1").read_text(encoding="utf-8")
		build_doc = (ROOT / "BUILD-ZH.md").read_text(encoding="utf-8")

		for token in (
			"6.0.3",
			"https://github.com/microsoft/TypeScript/releases/download/v6.0.3/typescript-6.0.3.tgz",
			"33cd0ee1beaa8c9e9d15a9da836c62ddea4c34a42d7c2d349dbc80d94165d22a",
			"lib/typescript.js",
			"TypeScript download failed; retrying",
		):
			self.assertIn(token, prepare_sh)
			self.assertIn(token, prepare_ps1)
		self.assertIn("download_archive", prepare_sh)
		self.assertIn("Invoke-DownloadWithRetry", prepare_ps1)
		self.assertIn("./.github/shell/prepare-typescript.sh", build_doc)
		self.assertIn("./.github/shell/prepare-typescript.ps1", build_doc)

	def test_release_workflow_uses_packaged_smoke_test_and_changelog_notes(self):
		release_workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")

		for token in (
			"validate:",
			"Release tag must not use a v prefix",
			"needs: validate",
			"needs.validate.outputs.tag",
			"needs: build",
			"plugin_artifact_name: gode-plugin",
			"CHANGELOG.md",
			"version=\"$RELEASE_TAG\"",
			"sed '/./,$!d'",
			"sed -e :a",
			"body_path: ${{ steps.release_notes.outputs.path }}",
			"files: dist/gode.zip",
		):
			self.assertIn(token, release_workflow)
		self.assertNotIn("version=\"${RELEASE_TAG#v}\"", release_workflow)
		self.assertNotIn("tag_name: ${{ inputs.tag }}", release_workflow)
		self.assertNotIn("name: ${{ inputs.tag }}", release_workflow)
		self.assertNotIn("generate_release_notes: true", release_workflow)

	def test_static_workflow_prepares_typescript_compiler_before_tests(self):
		build_workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
		test_workflow = (ROOT / ".github/workflows/test.yml").read_text(encoding="utf-8")
		static_start = test_workflow.index("  static:")
		smoke_start = test_workflow.index("  smoke:", static_start)
		static_body = test_workflow[static_start:smoke_start]

		self.assertIn("Prepare TypeScript compiler", static_body)
		self.assertIn("./.github/shell/prepare-typescript.sh", static_body)
		self.assertLess(
			static_body.index("Prepare TypeScript compiler"),
			static_body.index("Run repository integrity tests"),
		)
		for token in (
			"example/addons/gode/binary/windows/x64/gode_node.exe",
			"example/addons/gode/binary/linux/x64/gode_node",
			"example/addons/gode/binary/macos/arm64/gode_node",
		):
			self.assertIn(token, build_workflow)
		self.assertIn("Packaged plugin is missing the Linux x64 Node helper", test_workflow)

	def test_npm_native_export_smoke_uses_packaged_plugin_on_desktop_ci(self):
		fixture_root = ROOT / "test/fixtures/npm_native_llama"
		expected_fixture_files = [
			fixture_root / "project.godot",
			fixture_root / "main.tscn",
			fixture_root / "tsconfig.json",
			fixture_root / "package.json",
			fixture_root / "scripts/npm_native_smoke.ts",
			fixture_root / "scripts/fork_probe_child.cjs",
		]
		missing = [str(path.relative_to(ROOT)) for path in expected_fixture_files if not path.exists()]
		self.assertEqual([], missing)
		self.assertFalse((fixture_root / "node_modules").exists())

		package_json = json.loads((fixture_root / "package.json").read_text(encoding="utf-8"))
		self.assertIn("node-llama-cpp", package_json["dependencies"])
		smoke_source = (fixture_root / "scripts/npm_native_smoke.ts").read_text(encoding="utf-8")
		self.assertIn("require.resolve(\"node-llama-cpp\")", smoke_source)
		self.assertIn("node_modules fork dependency OK", smoke_source)

		for path in (
			ROOT / "test/run_npm_native_smoke.py",
			ROOT / "test/run_npm_native_export_smoke.py",
			ROOT / "test/prepare_godot.py",
		):
			self.assertTrue(path.exists(), f"{path.relative_to(ROOT)} should exist")

		build_workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
		package_start = build_workflow.index("  package:")
		export_start = build_workflow.index("  npm-native-export:", package_start)
		self.assertLess(package_start, export_start)
		for token in (
			"GODOT_VERSION: \"4.7-stable\"",
			"name: npm native export ${{ matrix.platform }}",
			"needs: package",
			"runner: windows-2022",
			"runner: ubuntu-22.04",
			"runner: macos-26",
			"python test/run_npm_native_export_smoke.py",
		):
			self.assertIn(token, build_workflow)

	def test_identity_workflow_rejects_ai_attributed_pull_request_commits(self):
		workflow = (ROOT / ".github/workflows/identity-check.yml").read_text(encoding="utf-8")

		for token in (
			"pull_request_target:",
			"contents: read",
			"issues: write",
			"pull-requests: write",
			"actions/github-script@v9",
			"github.rest.pulls.listCommits",
			"commit.commit.author?.email",
			"commit.commit.committer?.email",
			"co-authored-by|signed-off-by",
			"commit.commit.message",
			"state: \"closed\"",
			"Anthropic Claude",
			"OpenAI Codex",
			"GitHub Copilot",
			"Cursor",
			"Gemini Code Assist",
			"Devin AI",
			"Aider",
			"OpenHands",
		):
			self.assertIn(token, workflow)
		self.assertIn("Never check out or execute code from the pull request", workflow)
		self.assertNotIn("actions/checkout", workflow)
		self.assertNotIn("ag" + "ent", workflow.lower())

	def test_gode_json_controls_commercial_npm_export_policy(self):
		template_path = EXAMPLE_ROOT / "addons/gode/config/gode.json"
		self.assertTrue(template_path.exists())
		config = json.loads(template_path.read_text(encoding="utf-8"))
		self.assertIn("debug", config)
		npm_config = config["export"]["npm"]
		self.assertEqual(
			{
				"exportDependencies",
				"requireTools",
				"includeManifests",
				"includeNodeModules",
				"excludePaths",
				"extraIncludePaths",
			},
			set(npm_config),
		)
		self.assertEqual(["node_modules/.cache", "node_modules/.bin"], npm_config["excludePaths"])

		plugin_source = (EXAMPLE_ROOT / "addons/gode/gode.gd").read_text(encoding="utf-8")
		export_source = (EXAMPLE_ROOT / "addons/gode/runtime/export_plugin.gd").read_text(encoding="utf-8")

		for token in (
			"gode/export/npm",
			"ProjectSettings.add_property_info",
			"_ensure_export_settings",
		):
			self.assertNotIn(token, plugin_source)
			self.assertNotIn(token, export_source)

		for token in (
			'GODE_CONFIG_PATH := "res://gode.json"',
			'DEFAULT_GODE_CONFIG_PATH := "res://addons/gode/config/gode.json"',
			"_load_npm_config",
			"_create_project_gode_config",
			"_default_npm_config",
			"_merge_npm_config_value",
			"Gode could not read project config: %s",
			"Gode could not read default config template: %s",
			"Gode export could not read res://package.json.",
			'"exportDependencies": true',
			'"requireTools": true',
			'"includeManifests": true',
			'"includeNodeModules": true',
			'"excludePaths": PackedStringArray(["node_modules/.cache", "node_modules/.bin"])',
			'"extraIncludePaths": PackedStringArray()',
		):
			self.assertIn(token, export_source)

		for token in (
			"NPM_MANIFEST_FILES",
			'NPM_EXPORT_MANIFEST_PATH := "res://.gode/build/npm/manifest.json"',
			"GODE_RUNTIME_EXTENSION_PATH",
			"GODE_EDITOR_EXTENSION_TEMPLATE_PATH",
			"GODE_EDITOR_EXTENSION_PATH",
			"GODE_LEGACY_EDITOR_EXTENSION_PATH",
			"GODE_LEGACY_EDITOR_EXTENSION_UID_PATH",
			"LOCAL_EXTENSION_LIST_PATH",
			"_prepare_extension_state_for_export",
			"_prepare_project_extension_paths_for_export",
			"_prepare_local_extension_list_for_export",
			"_restore_extension_state_after_export",
			"_restore_local_extension_list_after_export",
			"_remove_legacy_editor_extension_resources",
			"_prepare_npm_export",
			"_export_file",
			"_is_gode_managed_export_path",
			"_is_gode_generated_export_path",
			"_is_npm_export_owned_path",
			"_is_gode_editor_only_export_path",
			"_is_gode_editor_extension_path",
			"_is_gode_binary_resource_path",
			"_is_target_runtime_binary_path",
			"_is_target_native_probe_helper_path",
			"_add_native_probe_helper",
			"_add_shared_object_path",
			"_target_native_probe_helper_path",
			"_target_runtime_binary_paths",
			"_features_has",
			"res://.godot/gode/",
			"res://addons/gode/binary/editor/",
			"res://addons/gode/tsc/",
			"res://addons/gode/types/",
			"npm_export_owned_exact_paths",
			"npm_export_owned_prefixes",
			"export_injected_path_hashes",
			"legacy_editor_resources_checked_for_export",
			"local_extension_list_checked_for_export",
			"_export_npm_runtime_snapshot",
			"_collect_npm_export_owned_paths",
			"_remember_npm_export_owned_file",
			"_remember_npm_export_owned_directory",
			"_add_export_file_bytes",
			"_add_npm_export_manifest",
			"_add_export_directory(\"res://node_modules\")",
			"_add_file_from_bytes(exported_path, source_path, \"Failed to read Gode TypeScript output: %s\")",
			"FileAccess.get_open_error() != OK",
			"GODE_TYPESCRIPT_COMPILER_CLASS",
			"GODE_TYPESCRIPT_COMPILE_PROJECT_METHOD",
			"_run_typescript_export_compile",
			"ClassDB.class_exists(GODE_TYPESCRIPT_COMPILER_CLASS)",
			"ClassDB.class_has_method(GODE_TYPESCRIPT_COMPILER_CLASS, GODE_TYPESCRIPT_COMPILE_PROJECT_METHOD)",
			"ClassDB.class_call_static(GODE_TYPESCRIPT_COMPILER_CLASS, GODE_TYPESCRIPT_COMPILE_PROJECT_METHOD, true)",
			"compiler_unavailable",
			"Gode editor extension is not loaded; reopen the project or re-enable the Gode plugin before exporting.",
			"Gode editor extension does not expose the TypeScript project compiler.",
			"TYPESCRIPT_EXPORT_MANIFEST_PATH",
			"_add_typescript_export_manifest(export_manifest_outputs)",
			"_normalize_res_path",
			"_path_has_parent_segment",
			'ProjectSettings.set_setting("native_extensions/paths", filtered)',
			"Gode export would add conflicting contents for path: %s",
			'_command_exists("node")',
			'_command_exists("npm")',
			"_resolve_command_path",
			'OS.get_environment("PATH")',
			'_file_exists("res://package.json") or _dir_exists("res://node_modules")',
		):
			self.assertIn(token, export_source)

		for token in (
			"nativeAddons",
			"get_extension().to_lower() == \"node\"",
			"NPM_MARKER_FILES",
			"_detect_package_manager",
			"allowYarnPnP",
			"packageManager",
			".pnp.cjs",
			"_compile_typescript_project_for_export",
			"_collect_typescript_sources",
			"_clear_generated_output_root",
			"_remove_directory_recursive",
		):
			self.assertNotIn(token, export_source)
		self.assertNotIn("OS.execute(command", export_source)
		self.assertNotIn("GodeTypeScriptCompiler.compile_project(true)", export_source)
		self.assertIn("_ensure_native_extension_registered(RUNTIME_EXTENSION_PATH)", plugin_source)
		self.assertNotIn("_ensure_native_extension_registered(EDITOR_EXTENSION_PATH)", plugin_source)
		self.assertIn('EDITOR_EXTENSION_TEMPLATE_PATH := "res://addons/gode/binary/gode_editor.gdextension.template"', plugin_source)
		self.assertIn('EDITOR_EXTENSION_PATH := "res://.godot/gode/gode_editor.gdextension"', plugin_source)
		self.assertIn('LEGACY_EDITOR_EXTENSION_PATH := "res://addons/gode/binary/gode_editor.gdextension"', plugin_source)
		self.assertIn("_remove_legacy_editor_extension_resources()", plugin_source)
		self.assertIn("var editor_manifest_ready := _ensure_editor_extension_manifest()", plugin_source)
		self.assertIn("_ensure_editor_extension_manifest", plugin_source)
		self.assertIn("_is_editor_extension_path(normalized_path)", plugin_source)
		self.assertIn("normalized_paths.append(normalized_path)", plugin_source)
		self.assertIn("func _enter_tree() -> void:", plugin_source)
		self.assertIn("func _exit_tree() -> void:", plugin_source)
		self.assertIn("func _setup_plugin() -> void:", plugin_source)
		self.assertIn("func _teardown_editor_session() -> void:", plugin_source)
		self.assertIn('LOCAL_EXTENSION_LIST_PATH := "res://.godot/extension_list.cfg"', plugin_source)
		self.assertIn("var command_line_export := _is_command_line_export()", plugin_source)
		self.assertIn("if not command_line_export and editor_manifest_ready:", plugin_source)
		self.assertIn("func _is_command_line_export() -> bool:", plugin_source)
		self.assertIn('"--export-release", "--export-debug", "--export-pack", "--export-patch"', plugin_source)
		self.assertIn("_ensure_local_native_extension_registered(EDITOR_EXTENSION_PATH)", plugin_source)
		self.assertIn("ProjectSettings.globalize_path(LOCAL_EXTENSION_LIST_PATH.get_base_dir())", plugin_source)

	def test_export_plugin_skips_gode_owned_npm_files_to_prevent_duplicate_export_entries(self):
		export_source = (EXAMPLE_ROOT / "addons/gode/runtime/export_plugin.gd").read_text(encoding="utf-8")
		export_file_body = gdscript_function_body(export_source, "_export_file")
		generated_body = gdscript_function_body(export_source, "_is_gode_generated_export_path")
		npm_owned_body = gdscript_function_body(export_source, "_is_npm_export_owned_path")
		collect_body = gdscript_function_body(export_source, "_collect_npm_export_owned_paths")
		add_file_body = gdscript_function_body(export_source, "_add_export_file")
		add_bytes_body = gdscript_function_body(export_source, "_add_export_file_bytes")
		npm_snapshot_body = gdscript_function_body(export_source, "_export_npm_runtime_snapshot")

		self.assertIn("if _is_gode_managed_export_path(normalized):\n\t\tskip()\n\t\treturn", export_file_body)
		self.assertLess(
			export_file_body.index("_is_gode_managed_export_path"),
			export_file_body.index("_is_target_native_probe_helper_path"),
		)
		for path in (
			"res://.gode/build/typescript/",
			"res://.gode/build/npm/",
			"TYPESCRIPT_EXPORT_MANIFEST_PATH",
			"NPM_EXPORT_MANIFEST_PATH",
		):
			self.assertIn(path, generated_body)

		self.assertIn("npm_export_owned_exact_paths.has(path)", npm_owned_body)
		self.assertIn("path == prefix or path.begins_with(prefix + \"/\")", npm_owned_body)
		self.assertIn("_remember_npm_export_owned_file(manifest_path)", collect_body)
		self.assertIn('_remember_npm_export_owned_directory("res://node_modules")', collect_body)
		self.assertIn('_get_npm_string_array("extraIncludePaths")', collect_body)
		self.assertIn("_remember_npm_export_owned_file(normalized)", collect_body)
		self.assertIn("_remember_npm_export_owned_directory(normalized)", collect_body)

		self.assertIn("var already_added := export_injected_path_hashes.has(normalized)", add_file_body)
		self.assertIn("if already_added:\n\t\treturn true", add_file_body)
		self.assertIn("export_injected_path_hashes.has(normalized)", add_bytes_body)
		self.assertIn("Gode export would add conflicting contents for path: %s", add_bytes_body)
		self.assertIn("add_file(normalized, bytes, false)", add_bytes_body)
		self.assertIn("if npm_exported_files > 0:\n\t\tif not _add_npm_export_manifest():", npm_snapshot_body)

	def test_export_plugin_sanitizes_stale_editor_extension_project_settings_for_export(self):
		export_source = (EXAMPLE_ROOT / "addons/gode/runtime/export_plugin.gd").read_text(encoding="utf-8")
		prepare_body = gdscript_function_body(export_source, "_prepare_extension_state_for_export")
		settings_body = gdscript_function_body(export_source, "_prepare_project_extension_paths_for_export")
		restore_body = gdscript_function_body(export_source, "_restore_extension_state_after_export")

		self.assertIn("_prepare_project_extension_paths_for_export()", prepare_body)
		self.assertIn("_prepare_local_extension_list_for_export()", prepare_body)
		self.assertIn("_remove_legacy_editor_extension_resources()", prepare_body)
		self.assertIn('ProjectSettings.get_setting("native_extensions/paths"', settings_body)
		self.assertIn("_is_gode_editor_extension_path(normalized_path)", settings_body)
		self.assertIn('ProjectSettings.set_setting("native_extensions/paths", filtered)', settings_body)
		self.assertIn("ProjectSettings.save()", settings_body)
		self.assertIn("_restore_local_extension_list_after_export()", restore_body)
		self.assertIn("legacy_editor_resources_checked_for_export = false", restore_body)
		self.assertIn("local_extension_list_checked_for_export = false", restore_body)
		self.assertIn("native_extension_paths_checked_for_export = false", restore_body)

	def test_export_plugin_filters_target_runtime_binaries_by_feature(self):
		export_source = (EXAMPLE_ROOT / "addons/gode/runtime/export_plugin.gd").read_text(encoding="utf-8")
		target_body = gdscript_function_body(export_source, "_target_runtime_binary_paths")
		binary_filter_body = gdscript_function_body(export_source, "_is_gode_binary_resource_path")

		expected_by_platform = {
			"windows": [
				"res://addons/gode/binary/windows/x64/libgode_runtime.dll",
				"res://addons/gode/binary/windows/x64/node.dll",
			],
			"linux": [
				"res://addons/gode/binary/linux/x64/libgode_runtime.so",
			],
			"macos": [
				"res://addons/gode/binary/macos/arm64/libgode_runtime.dylib",
			],
			"android": ["res://addons/gode/binary/android/arm64/libgode_runtime.so"],
			"ios": ["res://addons/gode/binary/ios/arm64/libgode_runtime.dylib"],
		}

		for platform_name, paths in expected_by_platform.items():
			self.assertIn(f'_features_has(features, "{platform_name}")', target_body)
			for path in paths:
				self.assertEqual(1, target_body.count(f'"{path}"'))

		self.assertIn("return PackedStringArray()", target_body)
		self.assertNotIn("binary/editor/", target_body)
		self.assertNotIn("gode_node", target_body)
		self.assertNotIn("libgode.dll", target_body)
		self.assertNotIn("libgode.so", target_body)
		self.assertNotIn("libgode.dylib", target_body)
		self.assertIn("if path == GODE_RUNTIME_EXTENSION_PATH:\n\t\treturn false", binary_filter_body)

		helper_body = gdscript_function_body(export_source, "_target_native_probe_helper_path")
		for path in (
			"res://addons/gode/binary/windows/x64/gode_node.exe",
			"res://addons/gode/binary/linux/x64/gode_node",
			"res://addons/gode/binary/macos/arm64/gode_node",
		):
			self.assertEqual(1, helper_body.count(f'"{path}"'))
		add_helper_body = gdscript_function_body(export_source, "_add_native_probe_helper")
		shared_object_body = gdscript_function_body(export_source, "_add_shared_object_path")
		self.assertIn("for dependency_path: String in _target_runtime_binary_paths(features):", add_helper_body)
		self.assertIn("_add_shared_object_path(dependency_path, features", add_helper_body)
		self.assertIn("_add_shared_object_path(helper_path, features", add_helper_body)
		self.assertIn("add_shared_object(", shared_object_body)
		self.assertIn("_to_resource_relative(source_path.get_base_dir())", shared_object_body)
		self.assertIn("if _is_target_native_probe_helper_path(normalized, features):\n\t\tskip()\n\t\treturn", export_source)
		self.assertIn("return not _is_target_runtime_binary_path(path, features)", binary_filter_body)

	def test_gode_json_controls_node_inspector_debug_policy(self):
		template_path = EXAMPLE_ROOT / "addons/gode/config/gode.json"
		self.assertTrue(template_path.exists())
		config = json.loads(template_path.read_text(encoding="utf-8"))
		inspector_config = config["debug"]["inspector"]
		self.assertEqual(
			{
				"enabled",
				"host",
				"port",
				"waitForDebugger",
				"breakOnStart",
				"sourceMaps",
				"logUrl",
				"autoIncrementPort",
				"maxPortRetries",
				"allowInRelease",
			},
			set(inspector_config),
		)
		self.assertFalse(inspector_config["enabled"])
		self.assertEqual("127.0.0.1", inspector_config["host"])
		self.assertEqual(9229, inspector_config["port"])
		self.assertFalse(inspector_config["waitForDebugger"])
		self.assertFalse(inspector_config["breakOnStart"])
		self.assertTrue(inspector_config["sourceMaps"])
		self.assertTrue(inspector_config["logUrl"])
		self.assertTrue(inspector_config["autoIncrementPort"])
		self.assertEqual(20, inspector_config["maxPortRetries"])
		self.assertFalse(inspector_config["allowInRelease"])

		runtime_source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		inspector_source = (ROOT / "src/runtime/node_inspector.cpp").read_text(encoding="utf-8")
		bootstrap_source = (ROOT / "src/runtime/node_bootstrap_scripts.cpp").read_text(encoding="utf-8")
		compiler_source = (EXAMPLE_ROOT / "addons/gode/runtime/typescript_compiler.js").read_text(encoding="utf-8")
		for token in (
			'PROJECT_GODE_CONFIG_PATH = "res://gode.json"',
			'DEFAULT_GODE_CONFIG_PATH = "res://addons/gode/config/gode.json"',
			"Config load_config",
			"debug.inspector",
			"allowInRelease",
			"is_release_export_runtime",
			"open_node_inspector",
			"wait_for_node_debugger",
			"void close_if_open",
			"break_on_next_user_script",
			"is_user_compiled_typescript_module",
			"Node inspector is not bound to a loopback host",
			"void print_attach_info",
			"value_type == godot::Variant::Type::FLOAT",
			"double(number) != float_value",
		):
			self.assertIn(token, inspector_source)
		for token in (
			"node_inspector::load_config",
			"node_inspector::open_if_enabled",
			"node_inspector::maybe_break_on_user_script",
			"node_inspector::close_if_open",
			"--enable-source-maps",
		):
			self.assertIn(token, runtime_source)

		for token in (
			"__gode_open_inspector",
			"__gode_wait_for_debugger",
			"__gode_close_inspector",
			"require('inspector')",
			"inspector.open(candidate, safeHost, false)",
			"inspector.url()",
			"resolvedPort",
			"waitForDebugger()",
			"require('inspector').close()",
			"global.__gode_import_module = async function",
			"global.__gode_invalidate_esm_module = function",
			"global.__gode_esm_source_cache = new Map();",
			"global.__gode_esm_generation = 0;",
			"global.__gode_esm_load_tokens = new Map();",
			"_gode_strip_module_generation",
			"_gode_module_cache_key",
			"_gode_module_identifier",
			"global.__gode_esm_generation++;",
			"path.isAbsolute(specifier)",
			"specifier.startsWith('file://')",
			"require('url').pathToFileURL(p).href",
			"require('url').pathToFileURL(abs).href",
			"global.__gode_esm_source_cache.get(filepath) !== source",
			"global.__gode_esm_source_cache.set(filepath, source);",
			"global.__gode_esm_pending_source = new Map();",
			"global.__gode_esm_load_tokens.set(filepath, loadToken);",
			"global.__gode_esm_load_tokens.get(filepath) === loadToken",
			"global.__gode_esm_load_tokens.clear();",
			"global.__gode_esm_load_tokens.delete(filepath);",
			"global.__gode_forget_esm_module(filename);",
			"_gode_should_invalidate_require_cache",
			"return await global.__gode_import_module(spec, ref.identifier);",
			"return await global.__gode_import_module(specifier, referrer.identifier);",
			"if (mod.status === 'unlinked')",
			"if (mod.status === 'linked')",
			"if (mod.status === 'errored')",
			"CommonJS module load failed",
			"global.__gode_esm_mod_cache.clear();",
			"})().finally(() => {",
			"global.__gode_esm_pending.delete(filepath);",
		):
			self.assertIn(token, bootstrap_source)
		for token in (
			"inlineSourceMap: true",
			"sourceMap: false",
			"sourceRootForSource",
			"parseJsonConfigFileContent",
			"projectRootNames(config, sources)",
			"program.getSourceFiles()",
			"toTypescriptVirtualPath",
			"fromTypescriptVirtualPath",
			"configuredModuleCandidates",
			"resolveProjectModule",
			"createProjectModuleSpecifierTransformer",
			"relativeOutputSpecifier",
			'normalized.includes("://") && !hasResourcePrefix',
			"if (segments.length === 0)",
			"if (!base) {",
			"if (!baseUrl) {",
			"transformers: {",
			'ignoreDeprecations: "6.0"',
			"jsx: tsApi.JsxEmit.React",
		):
			self.assertIn(token, compiler_source)

		self.assertNotIn("--inspect", runtime_source)
		self.assertNotIn("--inspect-brk", runtime_source)

	def test_native_sources_use_domain_directories(self):
		for directory in (
			ROOT / "include/script",
			ROOT / "include/compiler",
			ROOT / "include/runtime",
			ROOT / "src/script",
			ROOT / "src/compiler",
			ROOT / "src/runtime",
		):
			self.assertTrue(directory.exists(), f"{directory.relative_to(ROOT)} should exist")

		for directory in (
			ROOT / "include/script/javascript",
			ROOT / "include/script/typescript",
			ROOT / "src/script/javascript",
			ROOT / "src/script/typescript",
			ROOT / "include/support",
			ROOT / "include/utils",
			ROOT / "include/export",
			ROOT / "src/support",
			ROOT / "src/utils",
			ROOT / "src/export",
		):
			self.assertFalse(directory.exists(), f"{directory.relative_to(ROOT)} should not exist")

	def test_extension_entrypoint_is_self_contained(self):
		header = ROOT / "include/register_types.h"
		cmake_source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
		project_config = (EXAMPLE_ROOT / "project.godot").read_text(encoding="utf-8")
		self.assertFalse(header.exists(), "register_types is an internal extension entrypoint, not a public header")

		runtime_source = (ROOT / "src/register_runtime_types.cpp").read_text(encoding="utf-8")
		editor_source = (ROOT / "src/register_editor_types.cpp").read_text(encoding="utf-8")
		for source in (runtime_source, editor_source):
			self.assertNotIn('#include "register_types.h"', source)
			self.assertIn("namespace {", source)
			self.assertIn('extern "C"', source)
		self.assertIn("void initialize_gode_runtime_module(godot::ModuleInitializationLevel p_level)", runtime_source)
		self.assertIn("void uninitialize_gode_runtime_module(godot::ModuleInitializationLevel p_level)", runtime_source)
		self.assertIn("GDE_EXPORT gode_runtime_library_init", runtime_source)
		self.assertIn("void initialize_gode_editor_module(godot::ModuleInitializationLevel p_level)", editor_source)
		self.assertIn("void uninitialize_gode_editor_module(godot::ModuleInitializationLevel p_level)", editor_source)
		self.assertIn("GDE_EXPORT gode_editor_library_init", editor_source)

		extension_config = (EXAMPLE_ROOT / "addons/gode/binary/gode.gdextension").read_text(encoding="utf-8")
		editor_extension_config = (EXAMPLE_ROOT / "addons/gode/binary/gode_editor.gdextension.template").read_text(encoding="utf-8")
		self.assertIn('entry_symbol = "gode_runtime_library_init"', extension_config)
		self.assertIn('entry_symbol = "gode_editor_library_init"', editor_extension_config)
		self.assertIn("libgode_runtime", extension_config)
		self.assertIn("libgode_editor", editor_extension_config)
		self.assertIn("[dependencies]", extension_config)
		self.assertIn("node.dll", extension_config)
		self.assertNotIn("TypeScriptLanguage", editor_source)
		self.assertNotIn("GodeTypeScriptCompiler", runtime_source)
		self.assertIn("add_library(gode_runtime SHARED ${GODE_RUNTIME_SOURCES})", cmake_source)
		self.assertIn("add_library(gode_editor SHARED ${GODE_EDITOR_SOURCES})", cmake_source)
		self.assertIn("GODE_BUILD_EDITOR_EXTENSION_EFFECTIVE", cmake_source)
		self.assertIn('paths=["res://addons/gode/binary/gode.gdextension"]', project_config)
		self.assertNotIn("gode_editor.gdextension\"]", project_config)

	def test_godot_module_no_longer_exports_legacy_globals(self):
		for path in (
			ROOT / "generator/templates/register_classes.cpp.jinja2",
			ROOT / "generator/templates/builtin_binding.cpp.jinja2",
			ROOT / "generator/templates/utility_functions.cpp.jinja2",
			ROOT / "src/generated/register_classes.gen.cpp",
			ROOT / "src/generated/utility_functions/utility_functions.cpp",
		):
			text = path.read_text(encoding="utf-8")
			self.assertNotIn("env.Global()", text, str(path.relative_to(ROOT)))
			self.assertNotIn("global.Set(", text, str(path.relative_to(ROOT)))

		for path in (ROOT / "src/generated/builtin").glob("*_binding.gen.cpp"):
			text = path.read_text(encoding="utf-8")
			self.assertNotIn("env.Global()", text, str(path.relative_to(ROOT)))
			self.assertNotIn("global.Set(", text, str(path.relative_to(ROOT)))

		for path in (
			ROOT / "generator/dts_generator.py",
			ROOT / "example/addons/gode/types/godot.d.ts",
			ROOT / "example/addons/gode/types/globals.d.ts",
		):
			text = path.read_text(encoding="utf-8")
			self.assertNotIn("GodotNamespace", text, str(path.relative_to(ROOT)))
			self.assertNotIn("export default", text, str(path.relative_to(ROOT)))
			self.assertNotIn("GodotModule.", text, str(path.relative_to(ROOT)))

	def test_gallery_scripts_use_explicit_godot_imports(self):
		for script_root in (ROOT / "example/scripts", ROOT / "gallery/tps-demo-js", ROOT / "gallery/tps-demo-ts"):
			if not script_root.exists():
				continue
			for path in sorted(list(script_root.rglob("*.js")) + list(script_root.rglob("*.ts"))):
				if "node_modules" in path.parts or path.name.endswith(".d.ts"):
					continue
				text = path.read_text(encoding="utf-8")
				self.assertNotIn("import godot from \"godot\"", text, str(path.relative_to(ROOT)))
				self.assertIsNone(
					re.search(
						r"globalThis\.(?:Performance|Engine|ProjectSettings|OS|Time|ResourceLoader|ResourceSaver|Input|DisplayServer|RenderingServer|PhysicsServer3D|GD|Vector[234]i?|Color|Node)",
						text,
					),
					str(path.relative_to(ROOT)),
				)

	def test_default_value_evaluator_uses_local_godot_scope_only(self):
		source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		self.assertIn("with (godot)", source)
		self.assertIn("process._linkedBinding('godot')", source)
		self.assertNotIn("globalThis.Vector3", source)
		self.assertNotIn("globalThis.Engine", source)

	def test_func_utils_short_circuits_pending_conversion_exceptions(self):
		source = (ROOT / "include/runtime/func_utils.h").read_text(encoding="utf-8")
		self.assertIn("env.IsExceptionPending()", source)
		self.assertIn("convert_args<0, P...>", source)
		self.assertNotIn("Func(napi_to_godot<P>(args[Is])...)", source)
		self.assertNotIn("(instance->*Func)(napi_to_godot<P>(args[Is])...)", source)
		self.assertNotIn("(instance->*Func)(napi_to_godot<P>(val)...)", source)

	def test_fixed_arity_bindings_validate_argument_count(self):
		source = (ROOT / "include/runtime/func_utils.h").read_text(encoding="utf-8")
		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")

		self.assertIn("prepare_fixed_args", source)
		self.assertIn("required_arg_count", source)
		self.assertIn("Godot API call expected", source)
		self.assertNotIn("apply_default_args", source)
		self.assertNotIn("args.resize(sizeof...(P), info.Env().Undefined())", source)

		for token in (
			"this.set_process()",
			"this.set_process(true, false)",
			"this.tr()",
			"this.tr(\"message\", \"context\", \"extra\")",
			"packedInts.slice()",
			"packedInts.slice(1, 2, 3)",
			"packedInts.size(1)",
			):
				self.assertIn(token, runtime_test)

	def test_scalar_and_string_conversions_reject_wrong_javascript_types(self):
		value_convert = (ROOT / "include/runtime/value_convert.h").read_text(encoding="utf-8")
		value_convert_source = (ROOT / "src/runtime/value_convert.cpp").read_text(encoding="utf-8")
		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")

		for token in (
			"napi_to_godot_bool",
			"napi_to_godot_float",
			"godot_int_to_napi",
			"godot_uint_to_napi",
			"godot_result_to_napi",
			"throw_string_type_error",
			"throw_node_path_type_error",
			"std::isnan(number)",
			"!value.IsBoolean()",
			"!value.IsNumber()",
			"variant.get_type() == godot::Variant::Type::STRING",
			"variant.get_type() == godot::Variant::Type::STRING_NAME",
			"variant.get_type() == godot::Variant::Type::NODE_PATH",
		):
			self.assertIn(token, value_convert)

		for token in (
			"return value.ToBoolean().Value();",
			"return static_cast<ClearType>(value.ToNumber().DoubleValue());",
			"return godot::String();",
		):
			self.assertNotIn(token, value_convert)

		self.assertIn("Napi::BigInt::New(env, value)", value_convert_source)
		self.assertNotIn("return Napi::Number::New(env, variant.operator int64_t());", value_convert_source)

		for token in (
			'this.set_process("true")',
			"this.tr(1)",
			"this.has_node(1)",
			'Color.from_ok_hsl("0.58", 0.5, 0.79)',
			"Color.from_ok_hsl(NaN, 0.5, 0.79)",
			"new Vector3(Infinity, 0, 0)",
			'vector3.x = "4"',
			'GD.str_to_var("9223372036854775807")',
			'typeof restoredLargeInt, "bigint"',
		):
			self.assertIn(token, runtime_test)

	def test_object_and_ref_conversions_reject_plain_javascript_objects(self):
		value_convert = (ROOT / "include/runtime/value_convert.h").read_text(encoding="utf-8")
		class_template = (ROOT / "generator/templates/class_binding.cpp.jinja2").read_text(encoding="utf-8")
		node_source = (ROOT / "src/generated/classes/node_binding.gen.cpp").read_text(encoding="utf-8")
		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")

		for token in (
			"napi_to_godot_object_value",
			"napi_to_godot_object_pointer",
			"is_godot_ref",
			"Expected a Godot object wrapper or null",
			"Expected a Godot object wrapper compatible with the API parameter type",
			"unwrap_godot_object(value.As<Napi::Object>())",
			"ClearType(godot::Variant(typed_object))",
		):
			self.assertIn(token, value_convert)

		for token in (
			"godot::Object *script_owner = gode::consume_script_instance_owner();",
			"script owner is not compatible with",
			"constructor expected a Godot object wrapper",
			"constructor expected an object compatible with",
			"constructor expected no arguments",
			"cannot be constructed directly",
		):
			self.assertIn(token, class_template)

		for token in (
			"Node constructor expected a Godot object wrapper",
			"Node constructor expected an object compatible with Node",
			"Node constructor expected no arguments",
		):
			self.assertIn(token, node_source)

		for token in (
			"this.add_child({})",
			"ImageTexture.create_from_image({})",
			"const directImage = new Image();",
			"Direct Image constructor did not hold a RefCounted reference",
			"new Node({})",
			"new Node(1)",
		):
			self.assertIn(token, runtime_test)

	def test_builtin_constructors_and_operators_reject_invalid_signatures(self):
		template = (ROOT / "generator/templates/builtin_binding.cpp.jinja2").read_text(encoding="utf-8")
		builtin_generator = (ROOT / "generator/builtin_classes_generator.py").read_text(encoding="utf-8")
		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")
		vector2i_source = (ROOT / "src/generated/builtin/vector2i_binding.gen.cpp").read_text(encoding="utf-8")
		array_source = (ROOT / "src/generated/builtin/array_binding.gen.cpp").read_text(encoding="utf-8")
		packed_source = (ROOT / "src/generated/builtin/packed_int32_array_binding.gen.cpp").read_text(encoding="utf-8")

		self.assertIn("No matching constructor overload for {{ js_class_name }}", template)
		self.assertIn("has_unary", builtin_generator)
		self.assertIn("has_binary", builtin_generator)
		self.assertIn("gode::throw_arg_count_error(info.Env(), info.Length(), 1, 1);", template)
		self.assertIn("gode::throw_arg_count_error(info.Env(), info.Length(), 0, 0);", template)

		self.assertIn("No matching constructor overload for Vector2i", vector2i_source)
		self.assertIn("No matching constructor overload for GDArray", array_source)
		self.assertIn("No matching constructor overload for PackedInt32Array", packed_source)
		self.assertIn("info[0].IsNumber() || info[0].IsBigInt()", vector2i_source)
		self.assertIn("gode::throw_arg_count_error(info.Env(), info.Length(), 1, 1);", vector2i_source)
		self.assertIn("gode::throw_arg_count_error(info.Env(), info.Length(), 0, 0);", vector2i_source)

		for token in (
			"new Vector2i(1)",
			"new Vector2i(\"x\", 2)",
			"new Vector2i(1, 2, 3)",
			"vector2i.add(new Vector2i(3, 4), new Vector2i(5, 6))",
			"vector2i.negate(1)",
			"new Vector2i(1n, 2n)",
			"vector2i.multiply(2n)",
			"new PackedInt32Array([1n, 2n])",
			"new GDArray(1)",
			"new PackedInt32Array(1)",
		):
			self.assertIn(token, runtime_test)

	def test_js_arrays_are_first_class_array_arguments(self):
		func_utils = (ROOT / "include/runtime/func_utils.h").read_text(encoding="utf-8")
		value_convert = (ROOT / "include/runtime/value_convert.h").read_text(encoding="utf-8")
		value_convert_source = (ROOT / "src/runtime/value_convert.cpp").read_text(encoding="utf-8")
		binding_policy = (ROOT / "generator/utils/binding_policy.py").read_text(encoding="utf-8")
		builtin_generator = (ROOT / "generator/builtin_classes_generator.py").read_text(encoding="utf-8")
		class_template = (ROOT / "generator/templates/class_binding.cpp.jinja2").read_text(encoding="utf-8")
		builtin_template = (ROOT / "generator/templates/builtin_binding.cpp.jinja2").read_text(encoding="utf-8")
		dts_generator = (ROOT / "generator/dts_generator.py").read_text(encoding="utf-8")
		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")
		packed_source = (ROOT / "src/generated/builtin/packed_int32_array_binding.gen.cpp").read_text(encoding="utf-8")
		array_source = (ROOT / "src/generated/builtin/array_binding.gen.cpp").read_text(encoding="utf-8")
		resource_loader_source = (ROOT / "src/generated/classes/resource_loader_binding.gen.cpp").read_text(encoding="utf-8")

		self.assertIn("is_godot_typed_array", value_convert)
		self.assertIn("is_godot_packed_array", value_convert)
		self.assertIn("js_array_to_packed_array", value_convert)
		self.assertIn("js_array_to_typed_array", value_convert)
		self.assertIn("std::is_same_v<ClearType, godot::Array>", value_convert)
		self.assertIn("sync_godot_out_argument", value_convert)
		self.assertIn("godot_array_length_to_uint32", value_convert)
		self.assertIn("sync_godot_array_to_js_array", value_convert_source)
		self.assertIn("sync_godot_variant_out_argument", value_convert_source)
		self.assertIn("godot_array_length_to_uint32", value_convert_source)
		self.assertIn("sync_out_args", func_utils)
		self.assertIn("args.reserve(argc);", func_utils)
		self.assertIn("godot_result_to_napi(env, result)", func_utils)
		self.assertIn("godot_result_to_napi(info.Env(), result)", func_utils)
		self.assertNotIn("return godot_to_napi(env, result);", func_utils)
		self.assertNotIn("return godot_to_napi(info.Env(), Func", func_utils)
		self.assertIn("godot_result_to_napi(env, {{ constant.value }})", class_template)
		self.assertNotIn("Napi::Number::New(env, static_cast<double>({{ constant.value }}))", class_template)
		self.assertIn("std::is_lvalue_reference_v<Param>", func_utils)
		self.assertIn("call_class_method_bind", func_utils)
		self.assertIn('("ResourceLoader", "load_threaded_get_status"): (1,)', binding_policy)
		self.assertIn("BIND_NAPI_TO_BUILTIN(StringBinding)", value_convert_source)
		self.assertIn("BIND_NAPI_TO_BUILTIN(StringNameBinding)", value_convert_source)
		self.assertIn("BIND_NAPI_TO_BUILTIN(NodePathBinding)", value_convert_source)
		self.assertIn("PACKED_ARRAY_TYPES", builtin_generator)
		self.assertIn("PACKED_ARRAY_ELEMENT_TYPES", dts_generator)
		self.assertIn("new PackedInt32Array([1, 2, 3])", runtime_test)
		self.assertIn("load_threaded_get_status", runtime_test)
		self.assertIn("threadedProgress[0]", runtime_test)
		self.assertIn("call_class_method_bind<", resource_loader_source)
		self.assertIn('"load_threaded_get_status"', resource_loader_source)
		self.assertNotIn("call_builtin_method(&godot::ResourceLoader::load_threaded_get_status", resource_loader_source)
		self.assertIn("info[0].IsArray() || (info[0].IsObject() && info[0].As<Napi::Object>().InstanceOf(PackedInt32ArrayBinding::constructor.Value()))", packed_source)
		self.assertIn("info[0].IsArray() || (info[0].IsObject() && info[0].As<Napi::Object>().InstanceOf(ArrayBinding::constructor.Value()))", array_source)
		self.assertIn('"{{ js_class_name }} iterator"', builtin_template)

	def test_method_bind_out_argument_policy_matches_generated_bindings(self):
		from generator.dts_generator import member_name
		from generator.utils.binding_policy import METHOD_BIND_OUT_ARGUMENTS, skipped_method_reason

		api = load_extension_api()
		object_class_names = {cls["name"] for cls in api.get("classes", [])}
		api_classes = {cls["name"]: cls for cls in api.get("classes", [])}
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for (class_name, method_name), out_indices in sorted(METHOD_BIND_OUT_ARGUMENTS.items()):
			cls = api_classes.get(class_name)
			if not cls:
				mismatches.append(f"{class_name}.{method_name} class missing from extension API")
				continue
			method = next((item for item in cls.get("methods", []) if item["name"] == method_name), None)
			if not method:
				mismatches.append(f"{class_name}.{method_name} method missing from extension API")
				continue
			reason = skipped_method_reason(method, object_class_names)
			if reason:
				mismatches.append(f"{class_name}.{method_name} is no longer bindable: {reason}")
				continue

			arguments = method.get("arguments", [])
			for index in out_indices:
				if index >= len(arguments):
					mismatches.append(f"{class_name}.{method_name} out argument index {index} is out of range")
				elif arguments[index]["type"] not in {"Array", "PackedFloat32Array", "PackedInt32Array", "PackedInt64Array", "PackedByteArray"}:
					mismatches.append(f"{class_name}.{method_name} out argument {index} has unexpected type {arguments[index]['type']}")

			source = (ROOT / "src/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")
			function_match = re.search(
				rf"Napi::Value {re.escape(class_name)}Binding::{re.escape(method_name)}\(const Napi::CallbackInfo& info\) \{{(?P<body>.*?)\n\}}",
				source,
				re.DOTALL,
			)
			if not function_match:
				mismatches.append(f"{class_name}.{method_name} generated function missing")
				continue

			function_body = function_match.group("body")
			expected_indices = ", ".join(str(index) for index in out_indices)
			for token in (
				"call_class_method_bind<",
				f'"{class_name}"',
				f'"{method_name}"',
				str(method["hash"]),
				f"{{ {expected_indices} }}",
			):
				if token not in function_body:
					mismatches.append(f"{class_name}.{method_name} generated MethodBind body missing {token}")

			body = class_body(class_name)
			if re.search(rf"^\s+{re.escape(member_name(method_name))}\(", body, re.MULTILINE) is None:
				mismatches.append(f"{class_name}.{method_name} missing dts declaration")

		self.assertEqual([], mismatches)

	def test_builtin_template_short_circuits_pending_conversion_exceptions(self):
		source = (ROOT / "generator/templates/builtin_binding.cpp.jinja2").read_text(encoding="utf-8")
		self.assertIn("gode::ConvertedArgTuple", source)
		self.assertIn("if (!gode::convert_args<0,", source)
		self.assertIn("auto converted_value = gode::napi_to_godot", source)
		self.assertIn("if (info.Env().IsExceptionPending())", source)
		self.assertIn("return info.Env().Undefined();", source)
		self.assertNotIn("{{ member.custom_setter }}(instance, gode::napi_to_godot", source)
		self.assertNotIn("replace('{value}', 'gode::napi_to_godot", source)

	def test_javascript_runtime_clears_pending_conversion_exceptions(self):
		instance_source = (ROOT / "src/script/script_instance.cpp").read_text(encoding="utf-8")
		callable_source = (ROOT / "src/script/script_callable.cpp").read_text(encoding="utf-8")
		node_runtime_source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")

		self.assertIn('log_and_clear_pending_js_exception(env, context + " return conversion")', instance_source)
		self.assertIn('log_and_clear_pending_js_exception(env, "JS Callable return conversion")', callable_source)
		self.assertIn('log_and_clear_pending_js_exception(napi_environment, "NodeRuntime eval expression result conversion")', node_runtime_source)
		self.assertNotIn("r_error.error = GDEXTENSION_CALL_OK;\n\t\tr_error.argument = 0;\n\t\tr_error.expected = 0;\n\t\tif (result.IsPromise())", instance_source)
		self.assertNotIn("r_return_value = napi_to_godot(result);\n\t\tr_call_error.error = GDEXTENSION_CALL_OK", callable_source)

	def test_class_vararg_methodbind_errors_surface_to_javascript(self):
		func_utils = (ROOT / "include/runtime/func_utils.h").read_text(encoding="utf-8")
		vararg_macros = (ROOT / "include/runtime/vararg_macros.h").read_text(encoding="utf-8")
		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")

		self.assertIn("throw_if_godot_call_failed", func_utils)
		self.assertIn("GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS", func_utils)
		self.assertIn("GDExtensionCallError *r_error", vararg_macros)
		self.assertIn("object_method_bind_call", vararg_macros)
		self.assertIn("error->error != GDEXTENSION_CALL_OK", vararg_macros)
		self.assertNotIn("GDExtensionCallError error; \\\n\t::godot::gdextension_interface::object_method_bind_call", vararg_macros)
		self.assertIn("this.emit_signal()", runtime_test)
		self.assertIn("Godot vararg MethodBind call failed: TOO_FEW_ARGUMENTS", runtime_test)

	def test_addon_manifest_paths_exist(self):
		missing = []
		for manifest in (
			EXAMPLE_ROOT / "addons/gode/binary/gode.gdextension",
			EXAMPLE_ROOT / "addons/gode/binary/gode_editor.gdextension.template",
		):
			text = manifest.read_text(encoding="utf-8")
			for match in re.finditer(r'=\s*"(res://[^"]+)"', text):
				resource_path = match.group(1)
				if "/binary/" in resource_path and resource_path not in (
					"res://addons/gode/binary/gode.gdextension",
					"res://addons/gode/binary/gode_editor.gdextension.template",
				):
					continue
				if not res_path_to_file(resource_path).exists():
					missing.append(resource_path)

		self.assertEqual([], sorted(missing))

	def test_generated_utility_functions_match_extension_api(self):
		api = load_extension_api()
		expected = sorted(func["name"] for func in api.get("utility_functions", []))

		source = (ROOT / "src/generated/utility_functions/utility_functions.cpp").read_text(encoding="utf-8")
		actual = sorted(re.findall(r'InstanceMethod\("([^"]+)"', source))

		self.assertEqual(expected, actual)

	def test_generated_utility_functions_match_typescript_contract(self):
		from generator.dts_generator import member_name

		api = load_extension_api()
		source = (ROOT / "src/generated/utility_functions/utility_functions.cpp").read_text(encoding="utf-8")
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")
		match = re.search(
			r"^\s*export interface GD \{\n(?P<body>.*?)^\s*\}",
			dts,
			re.DOTALL | re.MULTILINE,
		)
		self.assertIsNotNone(match, "GD declaration was not found")
		body = match.group("body")

		mismatches = []
		for func in api.get("utility_functions", []):
			name = func["name"]
			if f'InstanceMethod("{name}", &GD::{name})' not in source:
				mismatches.append(f"{name} missing runtime binding")

			dts_name = member_name(name)
			line_match = re.search(rf"^\s+{re.escape(dts_name)}\((?P<params>[^)]*)\): (?P<ret>[^;]+);", body, re.MULTILINE)
			if not line_match:
				mismatches.append(f"{name} missing dts declaration")
				continue
			if func.get("is_vararg") and "...args: VariantArgument[]" not in line_match.group("params"):
				mismatches.append(f"{name} missing dts vararg rest parameter")

		for expected in (
			'"typeof"(variable: VariantArgument): VariantType;',
			'type_convert(variant: VariantArgument, type: VariantType): VariantArgument;',
			'type_string(type: VariantType): string;',
			'error_string(error: Error): string;',
			'instance_from_id(instance_id: number | bigint): GodotObject | null;',
		):
			if expected not in body:
				mismatches.append(f"GD dts missing exact declaration: {expected}")

		self.assertEqual([], mismatches)

	def test_godot_cpp_omitted_utility_functions_use_low_level_bindings(self):
		from generator.utility_functions_generator import GODOT_CPP_OMITTED_UTILITY_FUNCTIONS

		api = load_extension_api()
		api_functions = {func["name"]: func for func in api.get("utility_functions", [])}
		upstream_generator = (ROOT / "third/godot-cpp/binding_generator.py").read_text(encoding="utf-8")
		source = (ROOT / "src/generated/utility_functions/utility_functions.cpp").read_text(encoding="utf-8")
		low_level_header = (ROOT / "include/generated/utility_functions/utility_functions_vararg_method.h").read_text(encoding="utf-8")

		self.assertEqual({"is_instance_valid"}, set(GODOT_CPP_OMITTED_UTILITY_FUNCTIONS))
		for name in GODOT_CPP_OMITTED_UTILITY_FUNCTIONS:
			self.assertIn(name, api_functions)
			self.assertIn(f'function["name"] == "{name}"', upstream_generator)
			self.assertIn(f"gode::utility::{name}_internal", source)
			self.assertNotIn(f"godot::UtilityFunctions::{name}", source)
			self.assertRegex(
				low_level_header,
				rf"DEFINE_UTILITY_FUNC_RET\({re.escape(name)}, {api_functions[name]['hash']}, bool\)",
			)

	def test_generated_builtin_registration_matches_extension_api(self):
		api = load_extension_api()
		expected = sorted(
			cls["name"]
			for cls in api.get("builtin_classes", [])
			if cls["name"] not in SKIPPED_BUILTIN_CLASSES
		)

		source = (ROOT / "src/generated/register_builtin.gen.cpp").read_text(encoding="utf-8")
		actual = sorted(re.findall(r"\b([A-Za-z0-9_]+)Binding::init\(env, exports\);", source))

		self.assertEqual(expected, actual)

	def test_generated_builtin_operators_match_typescript_contract(self):
		sys.path.insert(0, str(ROOT / "generator"))
		try:
			from utils.binding_policy import builtin_operator_method_name
			from utils.type_mappings import js_class_name
		finally:
			sys.path.pop(0)

		api = load_extension_api()
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name, exported=True)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for cls in api.get("builtin_classes", []):
			class_name = cls["name"]
			if class_name in SKIPPED_BUILTIN_CLASSES:
				continue

			expected = sorted({
				method_name
				for operator in cls.get("operators", [])
				for method_name in [builtin_operator_method_name(operator["name"])]
				if method_name
			})

			source = (ROOT / "src/generated/builtin" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")
			runtime = sorted(set(re.findall(
				rf'InstanceMethod\("([^"]+)",\s*&{re.escape(class_name)}Binding::operator_',
				source,
			)))
			if runtime != expected:
				mismatches.append(f"{class_name} runtime operators expected {expected} got {runtime}")

			body = class_body(js_class_name(class_name))
			dts_missing = [
				operator
				for operator in expected
				if re.search(rf"^\s+{re.escape(operator)}\(", body, re.MULTILINE) is None
			]
			if dts_missing:
				mismatches.append(f"{class_name} dts missing operators {dts_missing}")

		self.assertEqual([], mismatches)

	def test_generated_builtin_constants_and_enums_match_typescript_contract(self):
		from generator.dts_generator import sanitize_name
		from generator.utils.type_mappings import js_class_name

		api = load_extension_api()
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name, exported=True)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for cls in api.get("builtin_classes", []):
			class_name = cls["name"]
			if class_name in SKIPPED_BUILTIN_CLASSES:
				continue
			constants = cls.get("constants", [])
			enums = cls.get("enums", [])
			if not constants and not enums:
				continue

			dts_name = js_class_name(class_name)
			body = class_body(dts_name)
			source = (ROOT / "src/generated/builtin" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")

			for const in constants:
				const_name = const["name"]
				if f'func.As<Napi::Object>().Set("{const_name}", gode::godot_to_napi(env,' not in source:
					mismatches.append(f"{class_name}.{const_name} missing constructor constant")
				if f'func.Get("prototype").As<Napi::Object>().Set("{const_name}", gode::godot_to_napi(env,' not in source:
					mismatches.append(f"{class_name}.{const_name} missing prototype constant")
				if re.search(rf"^\s+static readonly {re.escape(const_name)}: [^;]+;", body, re.MULTILINE) is None:
					mismatches.append(f"{class_name}.{const_name} missing dts constant")

			for enum in enums:
				enum_name = sanitize_name(enum["name"])
				enum_type = f"{dts_name}.{enum_name}"
				if f'func.As<Napi::Object>().Set("{enum["name"]}", enum_values);' not in source:
					mismatches.append(f"{class_name}.{enum['name']} missing constructor enum object")
				if f'func.Get("prototype").As<Napi::Object>().Set("{enum["name"]}", enum_values);' not in source:
					mismatches.append(f"{class_name}.{enum['name']} missing prototype enum object")
				for value in enum.get("values", []):
					value_name = sanitize_name(value["name"])
					if f'func.As<Napi::Object>().Set("{value["name"]}", gode::godot_result_to_napi(env,' not in source:
						mismatches.append(f"{class_name}.{value['name']} missing constructor enum value")
					if f'func.Get("prototype").As<Napi::Object>().Set("{value["name"]}", gode::godot_result_to_napi(env,' not in source:
						mismatches.append(f"{class_name}.{value['name']} missing prototype enum value")
					if re.search(rf"^\s+static readonly {re.escape(value_name)}: {re.escape(enum_type)};", body, re.MULTILINE) is None:
						mismatches.append(f"{class_name}.{value_name} missing dts enum value")

		self.assertEqual([], mismatches)

	def test_generated_builtin_constructors_match_typescript_contract(self):
		from generator.builtin_classes_generator import napi_match_expr
		from generator.dts_generator import DtsGenerator
		from generator.utils.type_mappings import js_class_name

		api = load_extension_api()
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")
		dts_generator = DtsGenerator.__new__(DtsGenerator)

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name, exported=True)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for cls in api.get("builtin_classes", []):
			class_name = cls["name"]
			if class_name in SKIPPED_BUILTIN_CLASSES:
				continue

			body = class_body(js_class_name(class_name))
			source = (ROOT / "src/generated/builtin" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")

			for ctor in cls.get("constructors", []):
				args = ctor.get("arguments", [])
				param_overrides = dts_generator._builtin_constructor_param_overrides(js_class_name(class_name), args)
				params = dts_generator._format_params(args, param_overrides) if args else ""
				if f"constructor({params});" not in body:
					mismatches.append(f"{class_name} constructor({params}) missing dts declaration")

				if f"if (info.Length() == {len(args)}" not in source:
					mismatches.append(f"{class_name} constructor arity {len(args)} missing runtime branch")
				for index, arg in enumerate(args):
					match_expr = napi_match_expr(arg["type"], index)
					if match_expr not in source:
						mismatches.append(f"{class_name} constructor argument {index} missing runtime matcher {match_expr}")

		self.assertEqual([], mismatches)

	def test_generated_builtin_methods_match_typescript_contract(self):
		from generator.dts_generator import member_name
		from generator.utils.binding_policy import method_conflicts_with_builtin_member, skipped_method_reason
		from generator.utils.type_mappings import js_class_name

		api = load_extension_api()
		object_class_names = {cls["name"] for cls in api.get("classes", [])}
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name, exported=True)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for cls in api.get("builtin_classes", []):
			class_name = cls["name"]
			if class_name in SKIPPED_BUILTIN_CLASSES:
				continue

			member_names = {member["name"] for member in cls.get("members", [])}
			body = class_body(js_class_name(class_name))
			source = (ROOT / "src/generated/builtin" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")

			for method in cls.get("methods", []):
				method_name = method["name"]
				reason = skipped_method_reason(method, object_class_names)
				if reason or method_conflicts_with_builtin_member(method_name, member_names):
					continue

				if method.get("is_static"):
					source_has_method = f'StaticMethod("{method_name}", &{class_name}Binding::' in source
					static_prefix = "static "
				else:
					source_has_method = f'InstanceMethod("{method_name}", &{class_name}Binding::' in source
					static_prefix = ""

				dts_method_name = member_name(method_name)
				dts_has_method = re.search(
					rf"^\s+{static_prefix}{re.escape(dts_method_name)}\(",
					body,
					re.MULTILINE,
				) is not None

				if not source_has_method:
					mismatches.append(f"{class_name}.{method_name} missing runtime binding")
				if not dts_has_method:
					mismatches.append(f"{class_name}.{method_name} missing dts declaration")

		self.assertEqual([], mismatches)

	def test_generated_class_methods_match_typescript_contract(self):
		from generator.dts_generator import member_name
		from generator.utils.binding_policy import skipped_method_reason

		api = load_extension_api()
		object_class_names = {cls["name"] for cls in api.get("classes", [])}
		singleton_names = {singleton["name"] for singleton in api.get("singletons", [])}
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for cls in api.get("classes", []):
			class_name = cls["name"]
			dts_name = "GodotObject" if class_name == "Object" else class_name
			body = class_body(dts_name)
			source = (ROOT / "src/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")

			for method in cls.get("methods", []):
				method_name = method["name"]
				reason = skipped_method_reason(method, object_class_names)
				if reason:
					continue

				if method.get("is_static"):
					source_has_method = f'StaticMethod("{method_name}", &{class_name}Binding::' in source
				else:
					source_has_method = f'prototype.Set("{method_name}", Napi::Function::New(env, &{class_name}Binding::' in source

				static_prefix = "static " if method.get("is_static") and class_name not in singleton_names else ""
				dts_method_name = member_name(method_name)
				dts_has_method = re.search(
					rf"^\s+{static_prefix}{re.escape(dts_method_name)}\(",
					body,
					re.MULTILINE,
				) is not None

				if not source_has_method:
					mismatches.append(f"{class_name}.{method_name} missing runtime binding")
				if not dts_has_method:
					mismatches.append(f"{class_name}.{method_name} missing dts declaration")

		self.assertEqual([], mismatches)

	def test_generated_class_signals_match_typescript_contract(self):
		api = load_extension_api()
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for cls in api.get("classes", []):
			class_name = cls["name"]
			signals = cls.get("signals", [])
			if not signals:
				continue

			dts_name = "GodotObject" if class_name == "Object" else class_name
			body = class_body(dts_name)
			source = (ROOT / "src/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")
			header = (ROOT / "include/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.h").read_text(encoding="utf-8")

			for signal in signals:
				signal_name = signal["name"]
				if f'Napi::PropertyDescriptor::Accessor(\n            "{signal_name}",' not in source:
					mismatches.append(f"{class_name}.{signal_name} missing runtime accessor")
				if f"Signal signal(instance, \"{signal_name}\");" not in source:
					mismatches.append(f"{class_name}.{signal_name} missing Godot Signal wrapper")
				if f"signal_{signal_name}(const Napi::CallbackInfo& info)" not in header:
					mismatches.append(f"{class_name}.{signal_name} missing header declaration")
				if re.search(rf"^\s+{re.escape(signal_name)}: Signal<\(.*\) => void>;", body, re.MULTILINE) is None:
					mismatches.append(f"{class_name}.{signal_name} missing dts declaration")

		self.assertEqual([], mismatches)

	def test_generated_class_constants_and_enums_match_typescript_contract(self):
		from generator.dts_generator import sanitize_name

		api = load_extension_api()
		singleton_names = {singleton["name"] for singleton in api.get("singletons", [])}
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for cls in api.get("classes", []):
			class_name = cls["name"]
			constants = cls.get("constants", [])
			enums = cls.get("enums", [])
			if not constants and not enums:
				continue

			dts_name = "GodotObject" if class_name == "Object" else class_name
			is_singleton = class_name in singleton_names
			modifier = "readonly" if is_singleton else "static readonly"
			body = class_body(dts_name)
			source = (ROOT / "src/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")

			for const in constants:
				const_name = const["name"]
				if f'func.As<Napi::Object>().Set("{const_name}", gode::godot_result_to_napi(env,' not in source:
					mismatches.append(f"{class_name}.{const_name} missing constructor constant")
				if f'prototype.Set("{const_name}", gode::godot_result_to_napi(env,' not in source:
					mismatches.append(f"{class_name}.{const_name} missing prototype constant")
				if re.search(rf"^\s+{modifier} {re.escape(const_name)}: number;", body, re.MULTILINE) is None:
					mismatches.append(f"{class_name}.{const_name} missing dts constant")

			for enum in enums:
				enum_name = sanitize_name(enum["name"])
				enum_type = f'import("godot").{dts_name}.{enum_name}' if is_singleton else f"{dts_name}.{enum_name}"
				if f'func.As<Napi::Object>().Set("{enum["name"]}", enum_values);' not in source:
					mismatches.append(f"{class_name}.{enum['name']} missing constructor enum object")
				if f'prototype.Set("{enum["name"]}", enum_values);' not in source:
					mismatches.append(f"{class_name}.{enum['name']} missing prototype enum object")
				if is_singleton and re.search(rf"^\s+readonly {re.escape(enum_name)}: \{{", body, re.MULTILINE) is None:
					mismatches.append(f"{class_name}.{enum_name} missing dts singleton enum object")
				reverse_values = {}
				for value in enum.get("values", []):
					value_name = sanitize_name(value["name"])
					if f'func.As<Napi::Object>().Set("{value["name"]}", gode::godot_result_to_napi(env,' not in source:
						mismatches.append(f"{class_name}.{value['name']} missing constructor enum value")
					if f'prototype.Set("{value["name"]}", gode::godot_result_to_napi(env,' not in source:
						mismatches.append(f"{class_name}.{value['name']} missing prototype enum value")
					if f'enum_values.Set(Napi::Number::New(env, {value["value"]}), Napi::String::New(env, "{value["name"]}"));' not in source:
						mismatches.append(f"{class_name}.{value['name']} missing runtime enum reverse mapping")
					if re.search(rf"^\s+{modifier} {re.escape(value_name)}: {re.escape(enum_type)};", body, re.MULTILINE) is None:
						mismatches.append(f"{class_name}.{value_name} missing dts enum value")
					reverse_values[value["value"]] = value["name"]
				if is_singleton:
					for value, value_name in reverse_values.items():
						if re.search(
							rf"^\s+readonly \[{re.escape(str(value))}\]: {re.escape(json.dumps(value_name))};",
							body,
							re.MULTILINE,
						) is None:
							mismatches.append(f"{class_name}.{value_name} missing dts singleton enum reverse mapping")

		self.assertEqual([], mismatches)

	def test_generated_class_instantiability_matches_typescript_contract(self):
		from generator.utils.type_mappings import js_class_name

		api = load_extension_api()
		singleton_names = {singleton["name"] for singleton in api.get("singletons", [])}
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		mismatches = []
		for cls in api.get("classes", []):
			class_name = cls["name"]
			dts_name = js_class_name(class_name)
			match = find_dts_class_match(dts, dts_name)
			if not match:
				mismatches.append(f"{class_name} dts declaration missing")
				continue

			expected_abstract = class_name in singleton_names or not cls.get("is_instantiable", False)
			actual_abstract = match.group("abstract") is not None
			if actual_abstract != expected_abstract:
				expected = "abstract" if expected_abstract else "concrete"
				actual = "abstract" if actual_abstract else "concrete"
				mismatches.append(f"{class_name} dts is {actual}, expected {expected}")

			source = (ROOT / "src/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")
			if cls.get("is_instantiable", False):
				cpp_class_name = "ClassDBSingleton" if class_name == "ClassDB" else class_name
				if f"instance = memnew(godot::{cpp_class_name});" not in source:
					mismatches.append(f"{class_name} runtime constructor missing memnew branch")
			elif "cannot be constructed directly" not in source:
				mismatches.append(f"{class_name} runtime constructor missing direct-construction rejection")

		self.assertIn("    export class Node extends GodotObject {", dts)
		self.assertIn("    export abstract class AnimationMixer extends Node {", dts)
		self.assertEqual([], mismatches)

	def test_generated_class_registration_matches_extension_api(self):
		api = load_extension_api()
		expected = sorted(cls["name"] for cls in api.get("classes", []))

		source = (ROOT / "src/generated/register_classes.gen.cpp").read_text(encoding="utf-8")
		actual = sorted(re.findall(r"\b([A-Za-z0-9_]+)Binding::init\(env, exports\);", source))

		self.assertEqual(expected, actual)

		missing_files = []
		for class_name in expected:
			snake_name = to_snake_case(class_name)
			for generated_path in (
				ROOT / "include/generated/classes" / f"{snake_name}_binding.gen.h",
				ROOT / "src/generated/classes" / f"{snake_name}_binding.gen.cpp",
			):
				if not generated_path.exists():
					missing_files.append(str(generated_path.relative_to(ROOT)))

		self.assertEqual([], missing_files)

	def test_generated_singleton_accessors_use_shared_object_wrapping(self):
		source = (ROOT / "src/generated/register_classes.gen.cpp").read_text(encoding="utf-8")
		self.assertNotIn("static Napi::ObjectReference ref", source)
		self.assertIn("gode::wrap_godot_object", source)

	def test_generated_class_object_wrappers_use_external_factory(self):
		source = (ROOT / "src/generated/classes/project_settings_binding.gen.cpp").read_text(encoding="utf-8")
		self.assertIn("Napi::Object ProjectSettingsBinding_create", source)
		self.assertIn("return ProjectSettingsBinding::create_singleton(env, typed_instance);", source)
		self.assertIn("&ProjectSettingsBinding_create", source)

	def test_generated_refcounted_wrappers_hold_external_references(self):
		api = load_extension_api()
		missing = []
		refcounted_classes = [cls for cls in api.get("classes", []) if cls.get("is_refcounted")]
		self.assertTrue(refcounted_classes, "Extension API must contain RefCounted classes")

		for cls in refcounted_classes:
			class_name = cls["name"]
			source_path = ROOT / "src/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.cpp"
			source = source_path.read_text(encoding="utf-8")
			# Check each lifetime operation in its own function. Ignore comments and
			# whitespace so formatting changes do not alter the ownership contract.
			functions = {
				"constructor": f"{class_name}Binding::{class_name}Binding(",
				"destructor": f"{class_name}Binding::~{class_name}Binding(",
				"wrap": f"void {class_name}Binding_wrap(",
			}
			bodies = {}
			for operation, signature in functions.items():
				match = re.search(
					rf"{re.escape(signature)}[^\n]*\{{(?P<body>.*?)^\}}",
					source, re.DOTALL | re.MULTILINE,
				)
				if not match:
					missing.append(f"{class_name}: missing {operation} function")
					continue
				body = re.sub(r"//[^\n]*|/\*.*?\*/", "", match.group("body"), flags=re.DOTALL)
				bodies[operation] = re.sub(r"\s+", "", body)

			constructor = bodies.get("constructor", "")
			if "if(ref){referenced_instance=owns_instance?ref->init_ref():ref->reference();}" not in constructor:
				missing.append(f"{class_name}: constructor must record initial or external reference acquisition")
			destructor = bodies.get("destructor", "")
			if "if(ref&&referenced_instance){if(ref->unreference()){godot::memdelete(ref);}}" not in destructor:
				missing.append(f"{class_name}: destructor must release only an acquired reference and delete at zero")
			if destructor.count("ref->unreference()") != 1:
				missing.append(f"{class_name}: destructor must unreference exactly once")
			wrap = bodies.get("wrap", "")
			if "binding->referenced_instance=false;" not in wrap or "if(ref){binding->referenced_instance=ref->reference();}" not in wrap:
				missing.append(f"{class_name}: wrap must record whether reference acquisition succeeded")
			if any("referenced_instance=true;" in body for body in bodies.values()):
				missing.append(f"{class_name}: reference flag must not be set unconditionally")
			header_path = ROOT / "include/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.h"
			header = re.sub(r"\s+", "", header_path.read_text(encoding="utf-8"))
			if "boolreferenced_instance=false;" not in header:
				missing.append(f"{class_name}: reference flag must default to false")

		self.assertFalse(missing, f"{len(missing)} RefCounted lifetime violations:\n" + "\n".join(missing[:20]))

	def test_generated_default_args_do_not_emit_raw_godot_string_markers(self):
		offenders = []
		for source_path in sorted((ROOT / "src/generated").glob("**/*.gen.cpp")):
			source = source_path.read_text(encoding="utf-8")
			for line_number, line in enumerate(source.splitlines(), start=1):
				if 'godot_to_napi(info.Env(), &"' in line or 'godot_to_napi(info.Env(), ^"' in line:
					offenders.append(f"{source_path.relative_to(ROOT)}:{line_number}: {line.strip()}")

		self.assertEqual([], offenders)

	def test_generated_js_api_renames_match_typescript_contract(self):
		sys.path.insert(0, str(ROOT / "generator"))
		try:
			from utils.type_mappings import JS_CLASS_RENAME_MAP
		finally:
			sys.path.pop(0)

		self.assertEqual(
			{
				"Object": "GodotObject",
				"String": "GDString",
				"Dictionary": "GDDictionary",
				"Array": "GDArray",
			},
			JS_CLASS_RENAME_MAP,
		)

		godot_dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")
		globals_dts = (ROOT / "example/addons/gode/types/globals.d.ts").read_text(encoding="utf-8")
		dts_generator = (ROOT / "generator/dts_generator.py").read_text(encoding="utf-8")
		source_paths = {
			"Object": ROOT / "src/generated/classes/object_binding.gen.cpp",
			"String": ROOT / "src/generated/builtin/string_binding.gen.cpp",
			"Dictionary": ROOT / "src/generated/builtin/dictionary_binding.gen.cpp",
			"Array": ROOT / "src/generated/builtin/array_binding.gen.cpp",
		}

		for godot_name, js_name in JS_CLASS_RENAME_MAP.items():
			source = source_paths[godot_name].read_text(encoding="utf-8")
			self.assertIn(f'DefineClass(env, "{js_name}"', source)
			self.assertIn(f'exports.Set("{js_name}", func);', source)
			self.assertIn(f"    export class {js_name}", godot_dts)
			self.assertNotIn(f"        {js_name}: typeof {js_name};", godot_dts)
			self.assertNotIn(f"  type {js_name} = GodotModule.{js_name};", globals_dts)
			self.assertNotIn(f"  const {js_name}: typeof GodotModule.{js_name};", globals_dts)

		object_source = source_paths["Object"].read_text(encoding="utf-8")
		self.assertIn('register_class("GodotObject", "Object"', object_source)
		bootstrap_source = (ROOT / "src/runtime/node_bootstrap_scripts.cpp").read_text(encoding="utf-8")
		self.assertIn("gode.GodotObject.prototype.to_signal", bootstrap_source)
		self.assertNotIn("gode.GDObject", bootstrap_source)
		self.assertNotIn("GDObject", object_source)
		self.assertNotIn("GDObject", godot_dts)
		self.assertNotIn("GDObject", globals_dts)
		self.assertIn('"typeof"(variable: VariantArgument): VariantType;', godot_dts)
		self.assertIn("type_convert(variant: VariantArgument, type: VariantType): VariantArgument;", godot_dts)
		self.assertNotIn("typeof_gd(", godot_dts)
		self.assertIn("add(right: Vector2i): Vector2i;", godot_dts)
		self.assertIn("multiply(right: bigint): Vector2i;", godot_dts)
		self.assertIn("multiply(right: number): Vector2;", godot_dts)
		self.assertNotIn("'NodePath':   'string'", dts_generator)
		self.assertIn("if type_str == 'NodePath':", dts_generator)
		self.assertIn("return 'NodePath | string' if is_input else 'NodePath'", dts_generator)
		self.assertIn("constructor(from_gd: NodePath | string);", godot_dts)
		self.assertIn("get_as_property_path(): NodePath;", godot_dts)
		self.assertIn("get_node(path: NodePath | string): Node;", godot_dts)
		self.assertIn("get_path(): NodePath;", godot_dts)
		self.assertIn("set_indexed(property_path: NodePath | string, value: VariantArgument): void;", godot_dts)

	def test_generated_color_okhsl_compatibility_matches_dts(self):
		source = (ROOT / "src/generated/builtin/color_binding.gen.cpp").read_text(encoding="utf-8")
		header = (ROOT / "include/generated/builtin/color_binding.gen.h").read_text(encoding="utf-8")
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		self.assertIn('#include "runtime/color_okhsl_compat.h"', source)
		self.assertIn('StaticMethod("from_ok_hsl", &ColorBinding::from_ok_hsl)', source)
		self.assertIn("return call_builtin_method(&gode::color_okhsl_compat::from_ok_hsl", source)
		self.assertIn("static Napi::Value from_ok_hsl", header)
		self.assertIn("static from_ok_hsl(h: number, s: number, l: number, alpha?: number): Color;", dts)

		for member in ("ok_hsl_h", "ok_hsl_s", "ok_hsl_l"):
			self.assertIn(f'InstanceAccessor("{member}"', source)
			self.assertIn(f"{member}: number;", dts)
			self.assertIn(f"gode::color_okhsl_compat::get_{member}", source)
			self.assertIn(f"gode::color_okhsl_compat::set_{member}", source)

	def test_generated_dts_singletons_are_instances_not_constructors(self):
		api = load_extension_api()
		godot_dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")
		globals_dts = (ROOT / "example/addons/gode/types/globals.d.ts").read_text(encoding="utf-8")
		rename_map = {
			"Object": "GodotObject",
			"String": "GDString",
			"Dictionary": "GDDictionary",
			"Array": "GDArray",
		}

		for singleton in api.get("singletons", []):
			name = singleton["name"]
			type_name = rename_map.get(singleton["type"], singleton["type"])

			self.assertEqual(
				1,
				godot_dts.count(f"    export const {name}: {type_name};"),
				f"{name} should be exported once as a singleton instance",
			)
			self.assertIn(f"    export type {type_name} = __GodotSingletonBases.{type_name};", godot_dts)
			self.assertNotIn(f"    export const {name}: typeof {type_name};", godot_dts)
			self.assertNotIn(f"        {name}: {type_name};", godot_dts)
			self.assertNotIn(f"        {name}: typeof {type_name};", godot_dts)
			self.assertNotIn(f"  type {name} = GodotModule.{type_name};", globals_dts)
			self.assertNotIn(f"  const {name}: GodotModule.{type_name};", globals_dts)
			self.assertNotIn(f"  const {name}: typeof GodotModule.{type_name};", globals_dts)

		self.assertIn("    export class Node extends GodotObject {", godot_dts)
		self.assertIn("        get_instance_id(): number | bigint;", godot_dts)
		self.assertNotIn("    export const Node: typeof Node;", godot_dts)
		self.assertNotIn("    export type Node = Node;", godot_dts)
		self.assertNotIn("        Node: typeof Node;", godot_dts)
		self.assertIn("    export class Color {", godot_dts)
		self.assertIn("export type VariantArgument = null | undefined | boolean | number | bigint | string", godot_dts)
		self.assertIn("Map<VariantArgument, VariantArgument>", godot_dts)
		self.assertIn("GDDictionary | { [key: string]: VariantArgument } | Map<VariantArgument, VariantArgument>", godot_dts)
		self.assertIn("export class GDDictionary<K extends VariantArgument = VariantArgument, V extends VariantArgument = VariantArgument>", godot_dts)
		self.assertIn("constructor(from_gd: GDDictionary<K, V> | { [key: string]: V } | Map<K, V>);", godot_dts)
		self.assertIn("export class GDArray<T extends VariantArgument = VariantArgument>", godot_dts)
		self.assertIn("export class Signal<T extends (...args: any[]) => void = (...args: VariantArgument[]) => void>", godot_dts)
		self.assertIn("connect(callable: Callable | T, flags?: number | bigint): number | bigint;", godot_dts)
		self.assertIn("emit(...args: Parameters<T>): void;", godot_dts)
		self.assertIn("ready: Signal<() => void>;", godot_dts)
		self.assertIn("child_entered_tree: Signal<(node: Node) => void>;", godot_dts)
		self.assertIn("get(index: number | bigint): T;", godot_dts)
		self.assertIn("count: number | bigint", godot_dts)
		self.assertNotIn("  const Color: typeof GodotModule.Color;", globals_dts)
		self.assertNotIn("  const Engine: GodotModule.Engine;", globals_dts)
		self.assertIn("  function Export(hint: number, hint_string?: string): any;", globals_dts)
		self.assertIn("    hint_string?: string;", globals_dts)
		self.assertNotIn("hintString?:", globals_dts)
		self.assertIn('  type RpcMode = "authority" | "any_peer" | "disabled" | number;', globals_dts)
		self.assertIn('  type RpcTransferMode = "reliable" | "unreliable" | "unreliable_ordered" | number;', globals_dts)
		self.assertIn("    rpc_mode?: RpcMode;", globals_dts)
		self.assertIn("    transfer_mode?: RpcTransferMode;", globals_dts)
		self.assertIn("    call_local?: boolean;", globals_dts)
		self.assertNotIn("transferMode?:", globals_dts)
		self.assertNotIn("callLocal?:", globals_dts)
		export_options_body = globals_dts[
			globals_dts.index("  interface ExportOptions {") :
			globals_dts.index("  function Export(hint: number, hint_string?: string): any;")
		]
		export_entry_body = globals_dts[
			globals_dts.index("  interface ExportEntry {") :
			globals_dts.index("  type ExportMap = Record<string, ExportEntry>;")
		]
		self.assertNotIn("default?:", export_options_body)
		self.assertIn('    default?: import("godot").VariantArgument;', export_entry_body)
		self.assertIn("  type int = number | bigint;", globals_dts)
		self.assertIn("  type float = number;", globals_dts)
		self.assertNotIn("type int = number;\n", globals_dts)
		self.assertIn("  function Signal(...args: any[]): any;", globals_dts)
		self.assertIn("  function Tool(target: object): void;", globals_dts)
		self.assertIn("  function Tool(): any;", globals_dts)
		self.assertIn("  function GlobalClass(target: object): void;", globals_dts)
		self.assertIn("  function GlobalClass(): any;", globals_dts)
		self.assertNotIn("ClassName", globals_dts)
		self.assertNotIn("GodotModule.", globals_dts)
		self.assertNotIn("const enum", godot_dts)
		self.assertIn("    export enum VariantType {", godot_dts)
		self.assertIn("    export namespace ResourceLoader {", godot_dts)
		self.assertIn("        export type CacheMode = 0 | 1 | 2 | 3 | 4;", godot_dts)
		self.assertNotIn("ResourceLoader_CacheMode", godot_dts)
		self.assertIn("    export class PhysicsServer3DExtension extends __GodotSingletonBases.PhysicsServer3D {", godot_dts)

	def test_generated_dts_has_no_duplicate_class_member_declarations(self):
		godot_dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		class_matches = re.finditer(
			r"^(?P<indent>[ \t]*)(?:export )?(?:abstract )?class (?P<name>[A-Za-z_][A-Za-z0-9_]*)(?=[\s<])[^\n{]*\{\n(?P<body>.*?)^(?P=indent)\}",
			godot_dts,
			re.DOTALL | re.MULTILINE,
		)
		duplicates = []
		class_count = 0
		for match in class_matches:
			class_count += 1
			seen = set()
			nested_depth = 0
			for line in match.group("body").splitlines():
				declaration = line.strip()
				if not declaration:
					continue
				if nested_depth == 0:
					if declaration in seen:
						duplicates.append(f"{match.group('name')}: {declaration}")
					else:
						seen.add(declaration)
				nested_depth += declaration.count("{") - declaration.count("}")

		self.assertGreater(class_count, 0)
		self.assertEqual([], duplicates)

	def test_generated_dts_does_not_claim_unsupported_builtin_index_access(self):
		godot_dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")
		self.assertNotIn("[index: number]:", godot_dts)

	def test_generated_variant_alias_enums_match_extension_api(self):
		api = load_extension_api()
		godot_dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")
		enum_aliases = {
			"Variant.Type": "VariantType",
			"Variant.Operator": "VariantOperator",
		}
		api_enums = {enum["name"]: enum for enum in api.get("global_enums", [])}

		def dts_enum_values(enum_name: str) -> list[tuple[str, int]]:
			match = re.search(
				rf"^\s*export enum {re.escape(enum_name)} \{{\n(?P<body>.*?)^\s*\}}",
				godot_dts,
				re.DOTALL | re.MULTILINE,
			)
			self.assertIsNotNone(match, f"{enum_name} declaration was not found")
			return [
				(name, int(value))
				for name, value in re.findall(r"^\s*([A-Z0-9_]+)\s*=\s*(-?\d+),", match.group("body"), re.MULTILINE)
			]

		mismatches = []
		for api_name, dts_name in enum_aliases.items():
			api_enum = api_enums.get(api_name)
			if not api_enum:
				mismatches.append(f"{api_name} missing from extension_api.json")
				continue
			expected = [(value["name"], value["value"]) for value in api_enum.get("values", [])]
			actual = dts_enum_values(dts_name)
			if actual != expected:
				mismatches.append(f"{dts_name} expected {expected} got {actual}")

		self.assertEqual([], mismatches)

	def test_generated_numeric_constant_values_fit_typescript_number_contract(self):
		api = load_extension_api()
		unsafe_values = []

		def check_value(scope: str, name: str, value):
			if type(value) is int and abs(value) > JS_MAX_SAFE_INTEGER:
				unsafe_values.append(f"{scope}.{name} = {value}")

		for enum in api.get("global_enums", []):
			for value in enum.get("values", []):
				check_value(enum["name"], value["name"], value["value"])

		for cls in api.get("classes", []):
			for const in cls.get("constants", []):
				check_value(cls["name"], const["name"], const["value"])
			for enum in cls.get("enums", []):
				for value in enum.get("values", []):
					check_value(f"{cls['name']}.{enum['name']}", value["name"], value["value"])

		for cls in api.get("builtin_classes", []):
			for const in cls.get("constants", []):
				check_value(cls["name"], const["name"], const["value"])
			for enum in cls.get("enums", []):
				for value in enum.get("values", []):
					check_value(f"{cls['name']}.{enum['name']}", value["name"], value["value"])

		self.assertEqual([], unsafe_values)

	def test_unsafe_pointer_methods_are_not_exposed(self):
		sys.path.insert(0, str(ROOT / "generator"))
		try:
			from utils.binding_policy import skipped_method_reason
		finally:
			sys.path.pop(0)

		api = load_extension_api()
		object_class_names = {cls["name"] for cls in api.get("classes", [])}
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		unsafe_methods = []
		for cls in api.get("classes", []):
			for method in cls.get("methods", []):
				reason = skipped_method_reason(method, object_class_names)
				if reason:
					unsafe_methods.append((cls["name"], method["name"], reason))

		self.assertGreater(len(unsafe_methods), 0)

		still_exposed = []
		for class_name, method_name, reason in unsafe_methods:
			snake_name = to_snake_case(class_name)
			source = (ROOT / "src/generated/classes" / f"{snake_name}_binding.gen.cpp").read_text(encoding="utf-8")
			header = (ROOT / "include/generated/classes" / f"{snake_name}_binding.gen.h").read_text(encoding="utf-8")
			dts_name = "GodotObject" if class_name == "Object" else class_name
			class_match = re.search(
				rf"    class {re.escape(dts_name)}(?:\s|<)[^\n]* \{{\n(?P<body>.*?)\n    \}}",
				dts,
				re.DOTALL,
			)
			if f'prototype.Set("{method_name}"' in source:
				still_exposed.append(f"{class_name}.{method_name} source ({reason})")
			if f" {method_name}(const Napi::CallbackInfo& info)" in header:
				still_exposed.append(f"{class_name}.{method_name} header ({reason})")
			if class_match and f"        {method_name}(" in class_match.group("body"):
				still_exposed.append(f"{class_name}.{method_name} dts ({reason})")

		self.assertEqual([], still_exposed)

	def test_generated_dts_properties_match_runtime_accessors(self):
		sys.path.insert(0, str(ROOT / "generator"))
		try:
			from utils.binding_policy import resolve_property_accessor, skipped_method_reason
		finally:
			sys.path.pop(0)

		api = load_extension_api()
		object_class_names = {cls["name"] for cls in api.get("classes", [])}
		singleton_names = {singleton["name"] for singleton in api.get("singletons", [])}
		dts = (ROOT / "example/addons/gode/types/godot.d.ts").read_text(encoding="utf-8")

		def class_body(dts_name: str) -> str:
			body = find_dts_class_body(dts, dts_name)
			self.assertIsNotNone(body, f"{dts_name} declaration was not found")
			return body

		mismatches = []
		for cls in api.get("classes", []):
			class_name = cls["name"]
			method_names = {
				method["name"]
				for method in cls.get("methods", [])
				if skipped_method_reason(method, object_class_names) is None
			}
			snake_name = to_snake_case(class_name)
			source = (ROOT / "src/generated/classes" / f"{snake_name}_binding.gen.cpp").read_text(encoding="utf-8")
			dts_name = "GodotObject" if class_name == "Object" else class_name
			body = class_body(dts_name)

			for prop in cls.get("properties", []):
				prop_name = prop["name"]
				if "/" in prop_name:
					continue

				getter = resolve_property_accessor(prop.get("getter", ""), method_names)
				setter = resolve_property_accessor(prop.get("setter", ""), method_names)
				source_property_match = re.search(
					rf'Napi::PropertyDescriptor::Accessor\(\s*"{re.escape(prop_name)}",(?P<descriptor>.*?)napi_default\s*\)\s*;',
					source,
					re.DOTALL,
				)
				source_has_property = source_property_match is not None
				source_has_setter = (
					source_property_match is not None and
					setter is not None and
					f"&{class_name}Binding::{setter}" in source_property_match.group("descriptor")
				)
				dts_has_getter = re.search(rf"^\s+get {re.escape(prop_name)}\(\):", body, re.MULTILINE) is not None
				dts_has_setter = re.search(rf"^\s+set {re.escape(prop_name)}\(value:", body, re.MULTILINE) is not None
				expected_property = getter is not None
				expected_setter = getter is not None and setter is not None

				if source_has_property != expected_property:
					mismatches.append(f"{class_name}.{prop_name} runtime accessor expected {expected_property} got {source_has_property}")
				if dts_has_getter != expected_property:
					mismatches.append(f"{class_name}.{prop_name} dts getter expected {expected_property} got {dts_has_getter}")
				if source_has_setter != expected_setter:
					mismatches.append(f"{class_name}.{prop_name} runtime setter expected {expected_setter} got {source_has_setter}")
				if dts_has_setter != expected_setter:
					mismatches.append(f"{class_name}.{prop_name} dts setter expected {expected_setter} got {dts_has_setter}")

		self.assertGreater(len(singleton_names), 0)
		self.assertEqual([], mismatches)

	def test_all_typed_collection_api_types_have_cpp_mappings(self):
		sys.path.insert(0, str(ROOT / "generator"))
		try:
			from utils.type_mappings import get_cpp_type
		finally:
			sys.path.pop(0)

		api = load_extension_api()
		refcounted_classes = {cls["name"] for cls in api.get("classes", []) if cls.get("is_refcounted")}
		typed_types = set()

		def collect_type(type_name):
			if isinstance(type_name, str) and type_name.startswith(("typedarray::", "typeddictionary::")):
				typed_types.add(type_name)

		for section in ("classes", "builtin_classes"):
			for cls in api.get(section, []):
				for prop in cls.get("properties", []):
					collect_type(prop.get("type"))
				for method in cls.get("methods", []):
					collect_type((method.get("return_value") or {}).get("type"))
					collect_type(method.get("return_type"))
					for arg in method.get("arguments", []):
						collect_type(arg.get("type"))

		for func in api.get("utility_functions", []):
			collect_type((func.get("return_value") or {}).get("type"))
			collect_type(func.get("return_type"))
			for arg in func.get("arguments", []):
				collect_type(arg.get("type"))

		self.assertGreater(len(typed_types), 0)
		invalid = []
		for type_name in sorted(typed_types):
			cpp_type = get_cpp_type(type_name, "", refcounted_classes, is_arg=True)
			if "typedarray::" in cpp_type or "typeddictionary::" in cpp_type:
				invalid.append(f"{type_name} -> {cpp_type}")

		self.assertEqual([], invalid)


if __name__ == "__main__":
	unittest.main()
