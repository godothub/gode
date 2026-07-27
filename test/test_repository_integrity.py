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
		comment_pattern = re.compile(r"//.*|/\*.*?\*/|#.*", re.DOTALL)
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

	def test_node_runtime_helpers_are_split_from_runtime_lifecycle(self):
		expected_files = [
			ROOT / "include/runtime/node_bootstrap_scripts.h",
			ROOT / "src/runtime/node_bootstrap_scripts.cpp",
			ROOT / "include/runtime/node_godot_bridge.h",
			ROOT / "src/runtime/node_godot_bridge.cpp",
			ROOT / "include/runtime/node_inspector.h",
			ROOT / "src/runtime/node_inspector.cpp",
			ROOT / "include/runtime/node_module_resolver.h",
			ROOT / "src/runtime/node_module_resolver.cpp",
			ROOT / "src/compiler/node_typescript_compiler_bridge.cpp",
		]
		missing = [str(path.relative_to(ROOT)) for path in expected_files if not path.exists()]
		self.assertEqual([], missing)

		source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")
		self.assertLessEqual(len(source.splitlines()), 450)
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

	def test_node_runtime_public_v8_entries_hold_locker_and_safe_scopes(self):
		source = (ROOT / "src/runtime/node_runtime.cpp").read_text(encoding="utf-8")

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
		source = (ROOT / "src/register_types.cpp").read_text(encoding="utf-8")

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
		self.assertIn("if (!NodeRuntime::is_running()) {\n\t\t\treturn;\n\t\t}\n\n\t\tv8::Locker locker(NodeRuntime::isolate);", source)
		self.assertIn("if (js_instance.IsEmpty() || !NodeRuntime::is_running()) {\n\t\treturn false;\n\t}", source)
		self.assertIn("if (!NodeRuntime::is_running()) {\n\t\tr_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;", source)
		self.assertIn("if (!NodeRuntime::is_running()) {\n\t\tr_is_valid = false;\n\t\treturn String();", source)

		def source_between(start_marker: str, end_marker: str) -> str:
			start = source.index(start_marker)
			end = source.index(end_marker, start)
			return source[start:end]

		for body in (
			source_between("bool ScriptInstance::has_method", "int32_t ScriptInstance::get_method_argument_count"),
			source_between("Variant ScriptInstance::call", "void ScriptInstance::notification_bind"),
			source_between("void ScriptInstance::notification", "String ScriptInstance::to_string"),
		):
			locker = body.index("v8::Locker locker(NodeRuntime::isolate);")
			isolate_scope = body.index("v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);")
			handle_scope = body.index("v8::HandleScope handle_scope(NodeRuntime::isolate);")
			self.assertLess(locker, isolate_scope)
			self.assertLess(isolate_scope, handle_scope)

		method_start = source.index("int32_t ScriptInstance::get_method_argument_count")
		method_end = source.index("Variant ScriptInstance::call", method_start)
		method_body = source[method_start:method_end]
		self.assertNotIn("v8::Locker locker(NodeRuntime::isolate);", method_body)

	def test_typescript_script_compile_state_does_not_reuse_stale_metadata(self):
		source = (ROOT / "src/script/typescript_script.cpp").read_text(encoding="utf-8")
		runtime_source = (ROOT / "src/script/typescript_script_runtime.cpp").read_text(encoding="utf-8")

		self.assertIn("if (!is_dirty) {\n\t\treturn is_valid;\n\t}", source)
		self.assertIn("if (!default_class.IsEmpty())", source)
		self.assertIn("default_class.Reset();", source)
		self.assertIn("is_valid = false;", source)
		self.assertIn("property_list.clear();", source)
		self.assertIn("methods.clear();", source)
		self.assertIn("member_lines.clear();", source)

		compile_start = source.index("bool TypeScriptScript::compile() const")
		path_index = source.index("String path = get_path();", compile_start)
		for token in (
			"default_class.Reset();",
			"is_valid = false;",
			"class_name = StringName();",
			"property_list.clear();",
			"member_lines.clear();",
		):
			self.assertLess(source.index(token, compile_start), path_index, token)

		get_default_class_start = source.index("Napi::Function TypeScriptScript::get_default_class() const")
		first_compile = source.index("if (!compile())", get_default_class_start)
		cache_read = source.index("if (!default_class.IsEmpty())", get_default_class_start)
		self.assertLess(first_compile, cache_read)
		self.assertIn("if (!compile()) {\n\t\treturn Error::ERR_INVALID_PARAMETER;\n\t}", runtime_source)
		reload_start = runtime_source.index("Error TypeScriptScript::_reload(bool p_keep_state)")
		reload_fail = runtime_source.index("return Error::ERR_INVALID_PARAMETER;", reload_start)
		reload_loop = runtime_source.index("for (ScriptInstance *instance : instances)", reload_start)
		self.assertLess(reload_fail, reload_loop)

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
		self.assertIn("object_cache[id] = Napi::Persistent(js_obj);", source)
		self.assertNotIn("object_cache[id] = Napi::Weak(js_obj);", source)
		self.assertIn("ref.SuppressDestruct();", source)
		self.assertNotIn("entry.second.Reset();", source)

	def test_generated_refcounted_wrappers_delete_from_unref_result(self):
		template = (ROOT / "generator/templates/class_binding.cpp.jinja2").read_text(encoding="utf-8")
		resource_source = (ROOT / "src/generated/classes/resource_binding.gen.cpp").read_text(encoding="utf-8")

		for source in (template, resource_source):
			self.assertIn("if (ref->unreference())", source)
			self.assertNotIn("ref->get_reference_count() == 0", source)
			self.assertNotIn("ref->unreference();\n                if", source)

	def test_typescript_loader_exposes_explicit_cache_clear(self):
		header = (ROOT / "include/script/typescript_loader.h").read_text(encoding="utf-8")
		source = (ROOT / "src/script/typescript_loader.cpp").read_text(encoding="utf-8")

		self.assertIn("void clear_cache();", header)
		self.assertIn("void reload_cached_scripts();", header)
		self.assertIn("godot::Error reload_source_code(const godot::String &p_code, bool p_keep_state);", (ROOT / "include/script/typescript_script.h").read_text(encoding="utf-8"))
		self.assertIn("void TypeScriptLoader::clear_cache()", source)
		self.assertIn("void TypeScriptLoader::reload_cached_scripts()", source)
		self.assertIn("scripts.clear();", source)
		self.assertIn("clear_cache();", source)
		self.assertIn("cached_scripts.reserve(scripts.size());", source)
		self.assertIn("script->reload_source_code(source_code, true);", source)
		self.assertIn("should_cache_loaded_script", source)
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
		self.assertIn("scripts[cache_key] = Ref(script);", source)
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
			"strip_typescript_method_modifiers",
			"format_function_argument",
			"collect_tree_sitter_errors",
			"append_validate_function_names",
			"describe_tree_sitter_error",
			"class_name_from_extends_node",
			"class_name_from_class_node",
			"default_exported_class_name_from_statement",
			"default_exported_name_from_clause",
			"find_class_declaration_by_name",
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
			"script->reload_source_code(source_code, p_keep_state);",
			"d[\"is_tool\"]",
			"d[\"base_type\"]",
			"p_type == String(\"TypeScript\")",
		):
			self.assertIn(token, source)

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
			"append_validate_function_names(root, source, functions);",
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

		global_class_body = source[
			source.index("Dictionary TypeScriptLanguage::_get_global_class_name") :
			len(source)
		]
		self.assertNotIn("ResourceLoader::get_singleton()->load", global_class_body)
		self.assertNotIn("script->_get_global_name()", global_class_body)
		self.assertNotIn("script->get_base_class_name()", global_class_body)

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
			"node_text_is_default",
			"unwrap_metadata_expression",
			"qualifier_from_extends_node",
			"extends_class_node_from_class",
			"resolve_imported_class_path",
			"import_clause_from_statement",
			"import_clause_binds_name",
			"import_specifier_binds_name",
			"namespace_import_binds_qualifier",
			'ts_node_child_by_field_name(import_specifier, "alias", 5)',
			'strcmp(child_type, "namespace_import")',
			'strcmp(child_type, "identifier")',
			"ts_node_named_child_count(clause)",
			"parent_ts->get_property_list_ordered()",
			"property_list.push_back(property);",
			"upsert_ordered_property",
			"parse_static_exports(class_node, source, properties, property_list, property_defaults);",
			'strcmp(node_type, "member_expression")',
			'strcmp(node_type, "generic_type")',
			"class_name_tail",
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
			"resolve_imported_typescript_path(file_path, import_path, false)",
			"resolve_imported_typescript_path(file_path, import_path, true)",
			"base_class_qualifier = qualifier_from_extends_node(base_class_node, source);",
			"base_script_path = resolve_imported_class_path(get_path(), source, root_node, child_count, base_class_name, base_class_qualifier);",
			"collect_parent_properties(base_class_name, base_class_qualifier, source, root_node, child_count, get_path(), properties, property_list, property_defaults);",
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
		self.assertNotIn("std::stoi(", source)
		self.assertNotIn("std::atoi", source)
		self.assertEqual(1, source.count("std::stod("))

		runtime_test = (ROOT / "example/scripts/tests/runtime_integration_test.ts").read_text(encoding="utf-8")
		runtime_base = (ROOT / "example/scripts/tests/runtime_base_test.ts").read_text(encoding="utf-8")
		self.assertIn("class RuntimeIntegrationTest extends RuntimeBaseModule.RuntimeIntegrationBase", runtime_test)
		self.assertIn("export default RuntimeIntegrationTest;", runtime_test)
		self.assertIn("static signals = {", runtime_test)
		self.assertIn("} as const;", runtime_test)
		self.assertIn("} satisfies ExportMap;", runtime_test)
		self.assertIn('"label": { "type": "String", "hint": 20, "hintString": "runtime label" }', runtime_test)
		self.assertIn('"enabled": { "type": "bool", "default": true as const }', runtime_test)
		self.assertIn('"count": { "type": "int", "default": 7 as const }', runtime_test)
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
		self.assertIn('label = "runtime" as string;', runtime_test)
		self.assertIn("enabled = true as boolean;", runtime_test)
		self.assertIn("count = 7 as number;", runtime_test)
		self.assertIn("spawn_offset = new Vector3(4, 5, 6) as Vector3;", runtime_test)
		self.assertIn("class RuntimeIntegrationBase extends Node", runtime_base)
		self.assertIn('@Export({ "hint": 20, "hint_string": "base label" } as const)', runtime_base)
		self.assertIn('nodeAssert.equal(String(inheritedLabelProperty.hint_string), "base label");', runtime_test)
		self.assertIn("export { RuntimeIntegrationBase };", runtime_base)
		self.assertIn("export default RuntimeIntegrationBase;", runtime_base)
		dependency_scan_test = (ROOT / "example/scripts/tests/dependency_scan_test.ts").read_text(encoding="utf-8")
		self.assertIn('import(dynamicSpecifier, { with: { type: "./signal_test" } } as any)', dependency_scan_test)
		self.assertIn('import("./runtime_helpers.js", { with: { type: "json" } } as any)', dependency_scan_test)
		self.assertIn('import(("./runtime_helpers.js" as const), { with: { type: "json" } } as any)', dependency_scan_test)
		self.assertIn('import(useSignal ? "./signal_test" : "./runtime_helpers")', dependency_scan_test)
		self.assertIn('import("./signal_test" + suffix)', dependency_scan_test)

		signal_test = (ROOT / "example/scripts/tests/signal_test.ts").read_text(encoding="utf-8")
		self.assertIn("static signals = {", signal_test)
		self.assertIn("} as const;", signal_test)
		self.assertIn("} satisfies ExportMap;", signal_test)
		self.assertIn('"threshold": { "type": "int", "hint": 1, "hint_string": "0,10,1", "default": 3 as const }', signal_test)
		self.assertIn('assert(String(thresholdProperty.hint_string) === "0,10,1"', signal_test)
		self.assertIn("threshold = 3 as const;", signal_test)

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

		register_source = (ROOT / "src/register_types.cpp").read_text(encoding="utf-8")
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

		compiler_source = (ROOT / "src/compiler/typescript_compiler.cpp").read_text(encoding="utf-8")
		self.assertIn('PROJECT_TYPESCRIPT_CONFIG_PATH = "res://tsconfig.json"', compiler_source)
		self.assertIn('DEFAULT_TYPESCRIPT_CONFIG_PATH = "res://addons/gode/config/tsconfig.json"', compiler_source)
		self.assertIn('TYPESCRIPT_COMPILER_BRIDGE_PATH = "res://addons/gode/runtime/typescript_compiler.js"', compiler_source)
		self.assertIn("ensure_project_typescript_config", compiler_source)
		self.assertIn("DirAccess::remove_absolute(source_map_path)", compiler_source)
		self.assertIn("make_error_diagnostic", compiler_source)
		self.assertIn("Failed to read TypeScript source", compiler_source)
		self.assertIn("collect_project_sources(source_diagnostics)", compiler_source)
		self.assertIn("Failed to read one or more TypeScript project sources.", compiler_source)
		self.assertIn('cache_root().path_join("manifest.json")', compiler_source)
		self.assertIn("String normalize_path_string(const String &path)", compiler_source)
		self.assertIn('return path.replace("\\\\", "/").simplify_path();', compiler_source)
		self.assertIn("bool path_has_parent_segment(const String &path)", compiler_source)
		self.assertIn("String input_signature(const Array &sources)", compiler_source)
		self.assertIn('manifest["input_signature"]', compiler_source)
		self.assertIn('source_content.sha256_text()', compiler_source)
		self.assertIn("append_file_state_signature(entries, TYPESCRIPT_COMPILER_BRIDGE_PATH, true)", compiler_source)
		self.assertIn("append_file_state_signature(entries, TYPESCRIPT_RUNTIME_PATH, true)", compiler_source)
		self.assertIn("output_for_source", compiler_source)
		self.assertIn("append_error_diagnostic", compiler_source)
		self.assertIn("Source was not emitted by the active TypeScript project", compiler_source)
		self.assertIn('result["path"] = output.get("path", result["path"])', compiler_source)
		self.assertIn("load_cached_outputs", compiler_source)
		self.assertIn("save_compile_manifest", compiler_source)
		self.assertIn("manifest_outputs_are_valid", compiler_source)
		self.assertIn("output_entry_is_valid", compiler_source)
		self.assertIn("source_output_path_is_valid", compiler_source)
		self.assertIn("normalize_typescript_source_path", compiler_source)
		self.assertIn("TypeScript source path cannot contain parent-directory segments", compiler_source)
		self.assertIn("Invalid TypeScript source path, expected a .ts or .tsx file under res://", compiler_source)
		self.assertIn("if (!normalize_typescript_source_path(p_source_path, source_path, &path_error))", compiler_source)
		self.assertIn("if (!normalize_typescript_source_path(p_source_path, source_path))", compiler_source)
		self.assertIn("require_cache_path", compiler_source)
		self.assertIn("path_is_under_root", compiler_source)
		self.assertIn("if (path_has_parent_segment(path) || path_has_parent_segment(root_path))", compiler_source)
		self.assertIn("if (path_has_parent_segment(path))", compiler_source)
		self.assertIn('path_is_under_root(String(output["path"]), cache_root())', compiler_source)
		self.assertIn('path_is_under_root(String(output["exported_path"]), exported_build_root())', compiler_source)
		self.assertIn("exported_manifest_path", compiler_source)
		self.assertIn("load_manifest_outputs_from_path(exported_manifest_path(), exported_outputs)", compiler_source)
		self.assertIn("Source was not included in the exported TypeScript manifest", compiler_source)
		self.assertIn("Exported TypeScript output is missing", compiler_source)
		self.assertIn("prune_stale_outputs", compiler_source)
		self.assertIn("remove_cache_file_if_safe", compiler_source)
		self.assertIn('path_has_extension(String(output["path"]), ".js")', compiler_source)
		self.assertIn('path_has_extension(String(output["exported_path"]), ".js")', compiler_source)
		self.assertIn("DirAccess::remove_absolute(normalized_path)", compiler_source)
		self.assertIn('normalized_path.ends_with(".js")', compiler_source)
		self.assertIn('normalized_path.ends_with(".js.map")', compiler_source)
		self.assertNotIn("!engine->is_editor_hint() && FileAccess::file_exists(exported_path)", compiler_source)
		self.assertNotIn("is_emittable_typescript_path", compiler_source)

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

		for path in (
			"plugin.cfg",
			"gode.gd",
			"binary/gode.gdextension",
			"config/gode.json",
			"config/tsconfig.json",
			"icons/typescript.svg",
			"runtime/event_loop.gd",
			"runtime/export_plugin.gd",
			"runtime/typescript_compiler.js",
			"types/globals.d.ts",
			"types/godot.d.ts",
		):
			self.assertIn(f'"{path}"', package_script)
		self.assertNotIn("icons/typescript.svg.import", package_script)

		self.assertIn("prepare-typescript.sh", package_script)
		self.assertIn("tsc/package.json", package_script)
		self.assertIn("tsc/lib/typescript.js", package_script)

	def test_release_workflow_uses_packaged_smoke_test_and_changelog_notes(self):
		release_workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")

		for token in (
			"needs: build",
			"plugin_artifact_name: gode-plugin",
			"CHANGELOG.md",
			"version=\"${RELEASE_TAG#v}\"",
			"sed '/./,$!d'",
			"sed -e :a",
			"body_path: ${{ steps.release_notes.outputs.path }}",
			"files: dist/gode.zip",
		):
			self.assertIn(token, release_workflow)
		self.assertNotIn("generate_release_notes: true", release_workflow)

	def test_static_workflow_prepares_typescript_compiler_before_tests(self):
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
			"_prepare_npm_export",
			"_export_npm_runtime_snapshot",
			"_add_export_directory(\"res://node_modules\")",
			"_add_file_from_bytes(exported_path, source_path, \"Failed to read Gode TypeScript output: %s\")",
			"_add_file_from_bytes(source_path, source_path, \"Failed to read Gode export file: %s\")",
			"FileAccess.get_open_error() != OK",
			"INLINE_SOURCE_MAP_MARKER",
			"TYPESCRIPT_EXPORT_MANIFEST_PATH",
			"_strip_inline_source_map",
			"func _add_compiled_file(exported_path: String, source_path: String, include_inline_source_map := true) -> bool:",
			"_add_compiled_file(exported_path, source_path, is_debug)",
			"Gode TypeScript output mapping is incomplete.",
			"_add_typescript_export_manifest(export_manifest_outputs)",
			"export_manifest_outputs.append",
			'JSON.stringify(manifest, "\\t")',
			"_normalize_res_path",
			"_path_has_parent_segment",
			'normalized.contains("://") and not normalized.begins_with("res://")',
			"normalized.simplify_path()",
			"OS.execute(candidate, PackedStringArray([\"--version\"])",
			'_command_exists("node")',
			'_command_exists("npm")',
			'_file_exists("res://package.json") or _dir_exists("res://node_modules")',
			"[Gode Export] Created project config from template",
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
		):
			self.assertNotIn(token, export_source)

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
			"global.__gode_invalidate_esm_module(filename);",
			"_gode_should_invalidate_require_cache",
			"return await global.__gode_import_module(spec, ref.identifier);",
			"return await global.__gode_import_module(specifier, referrer.identifier);",
			"if (mod.status === 'unlinked')",
			"if (mod.status === 'linked')",
			"if (mod.status === 'errored')",
			"CommonJS module load failed",
			"global.__gode_cjs_pending.delete(resolvedPath);",
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
		self.assertFalse(header.exists(), "register_types is an internal extension entrypoint, not a public header")

		source = (ROOT / "src/register_types.cpp").read_text(encoding="utf-8")
		self.assertNotIn('#include "register_types.h"', source)
		self.assertIn("namespace {", source)
		self.assertIn("void initialize_node_module(godot::ModuleInitializationLevel p_level)", source)
		self.assertIn("void uninitialize_node_module(godot::ModuleInitializationLevel p_level)", source)
		self.assertIn('extern "C"', source)
		self.assertIn("GDE_EXPORT node_library_init", source)

		extension_config = (EXAMPLE_ROOT / "addons/gode/binary/gode.gdextension").read_text(encoding="utf-8")
		self.assertIn('entry_symbol = "node_library_init"', extension_config)

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
		self.assertIn("call_class_method_bind(", resource_loader_source)
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
				"call_class_method_bind(",
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
		self.assertIn('log_and_clear_pending_js_exception(thread_local_env, "NodeRuntime eval expression result conversion")', node_runtime_source)
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
		manifest = EXAMPLE_ROOT / "addons/gode/binary/gode.gdextension"
		text = manifest.read_text(encoding="utf-8")
		missing = []
		for match in re.finditer(r'=\s*"(res://[^"]+)"', text):
			resource_path = match.group(1)
			if "/binary/" in resource_path and resource_path != "res://addons/gode/binary/gode.gdextension":
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
		from generator.dts_generator import godot_type_to_ts, sanitize_name
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

			body = class_body(js_class_name(class_name))
			source = (ROOT / "src/generated/builtin" / f"{to_snake_case(class_name)}_binding.gen.cpp").read_text(encoding="utf-8")

			for ctor in cls.get("constructors", []):
				args = ctor.get("arguments", [])
				params = ", ".join(
					f"{sanitize_name(arg['name'])}: {godot_type_to_ts(arg['type'], is_input=True)}"
					for arg in args
				)
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
				if re.search(rf"^\s+{re.escape(signal_name)}: Signal;", body, re.MULTILINE) is None:
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
				enum_type = f"{dts_name}_{enum_name}" if is_singleton else f"{dts_name}.{enum_name}"
				if f'func.As<Napi::Object>().Set("{enum["name"]}", enum_values);' not in source:
					mismatches.append(f"{class_name}.{enum['name']} missing constructor enum object")
				if f'prototype.Set("{enum["name"]}", enum_values);' not in source:
					mismatches.append(f"{class_name}.{enum['name']} missing prototype enum object")
				for value in enum.get("values", []):
					value_name = sanitize_name(value["name"])
					if f'func.As<Napi::Object>().Set("{value["name"]}", gode::godot_result_to_napi(env,' not in source:
						mismatches.append(f"{class_name}.{value['name']} missing constructor enum value")
					if f'prototype.Set("{value["name"]}", gode::godot_result_to_napi(env,' not in source:
						mismatches.append(f"{class_name}.{value['name']} missing prototype enum value")
					if re.search(rf"^\s+{modifier} {re.escape(value_name)}: {re.escape(enum_type)};", body, re.MULTILINE) is None:
						mismatches.append(f"{class_name}.{value_name} missing dts enum value")

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

		for cls in api.get("classes", []):
			if not cls.get("is_refcounted"):
				continue
			class_name = cls["name"]
			source_path = ROOT / "src/generated/classes" / f"{to_snake_case(class_name)}_binding.gen.cpp"
			source = source_path.read_text(encoding="utf-8")
			if "if (!owns_instance) {" not in source:
				missing.append(f"{class_name}: missing non-owned reference guard")
			if "ref->reference();" not in source:
				missing.append(f"{class_name}: missing RefCounted reference")
			if "referenced_instance = true;" not in source:
				missing.append(f"{class_name}: missing referenced_instance flag")

		self.assertEqual([], missing)

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
		self.assertIn("multiply(right: number | bigint): Vector2i;", godot_dts)
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
		self.assertIn("count: number | bigint", godot_dts)
		self.assertNotIn("  const Color: typeof GodotModule.Color;", globals_dts)
		self.assertNotIn("  const Engine: GodotModule.Engine;", globals_dts)
		self.assertIn("  function Export(hint: number, hintString?: string): any;", globals_dts)
		self.assertIn("    hintString?: string;", globals_dts)
		self.assertIn("    hint_string?: string;", globals_dts)
		export_options_body = globals_dts[
			globals_dts.index("  interface ExportOptions {") :
			globals_dts.index("  function Export(hint: number, hintString?: string): any;")
		]
		export_entry_body = globals_dts[
			globals_dts.index("  interface ExportEntry {") :
			globals_dts.index("  type ExportMap = Record<string, ExportEntry>;")
		]
		self.assertNotIn("default?:", export_options_body)
		self.assertIn('    default?: import("godot").VariantArgument;', export_entry_body)
		self.assertIn("  function Signal(...args: any[]): any;", globals_dts)
		self.assertIn("  function Tool(...args: any[]): any;", globals_dts)
		self.assertNotIn("GodotModule.", globals_dts)
		self.assertIn("    export const enum VariantType {", godot_dts)
		self.assertNotIn("    export const VariantType: typeof VariantType;", godot_dts)
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
			for line in match.group("body").splitlines():
				declaration = line.strip()
				if not declaration:
					continue
				if declaration in seen:
					duplicates.append(f"{match.group('name')}: {declaration}")
				else:
					seen.add(declaration)

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
				rf"^\s*export const enum {re.escape(enum_name)} \{{\n(?P<body>.*?)^\s*\}}",
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
