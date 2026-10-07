@tool
extends EditorExportPlugin

const LITE_RUNTIME := false

const GODE_CONFIG_PATH := "res://gode.json"
const DEFAULT_GODE_CONFIG_PATH := "res://addons/gode/config/gode.json"
const INLINE_SOURCE_MAP_MARKER := "//# sourceMappingURL=data:application/json;base64,"
const EMPTY_SHA256 := "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
const TYPESCRIPT_EXPORT_MANIFEST_PATH := "res://.gode/build/typescript/manifest.json"
const NPM_EXPORT_MANIFEST_PATH := "res://.gode/build/npm/manifest.json"
const GODE_RUNTIME_EXTENSION_PATH := "res://addons/gode/binary/gode.gdextension"
const GODE_EDITOR_EXTENSION_TEMPLATE_PATH := "res://addons/gode/binary/gode_editor.gdextension.template"
const GODE_EDITOR_EXTENSION_PATH := "res://.godot/gode/gode_editor.gdextension"
const GODE_LEGACY_EDITOR_EXTENSION_PATH := "res://addons/gode/binary/gode_editor.gdextension"
const GODE_LEGACY_EDITOR_EXTENSION_UID_PATH := "res://addons/gode/binary/gode_editor.gdextension.uid"
const LOCAL_EXTENSION_LIST_PATH := "res://.godot/extension_list.cfg"
const MACOS_NATIVE_PROBE_FRAMEWORK_DIR := "user://.gode/export/gode_node.framework"
const GODE_TYPESCRIPT_COMPILER_CLASS := &"GodeTypeScriptCompiler"
const GODE_TYPESCRIPT_COMPILE_PROJECT_METHOD := &"compile_project"

const NPM_MANIFEST_FILES := [
	"package.json",
	"package-lock.json",
	"npm-shrinkwrap.json",
	"pnpm-lock.yaml",
	"yarn.lock",
	"bun.lock",
	"bun.lockb",
	".npmrc",
	".yarnrc",
	".yarnrc.yml",
]

var npm_exported_files := 0
var npm_export_manifest_entries := PackedStringArray()
var npm_export_owned_exact_paths := {}
var npm_export_owned_prefixes := PackedStringArray()
var export_injected_path_hashes := {}
var npm_config: Dictionary = {}
var config_error := ""
var editor_extension_list_entry_removed_for_export := false
var legacy_editor_resources_checked_for_export := false
var local_extension_list_checked_for_export := false
var native_extension_paths_checked_for_export := false

func _get_name() -> String:
	return "GodeTypeScriptExport"

func _export_file(path: String, _type: String, features: PackedStringArray) -> void:
	_prepare_extension_state_for_export()
	var normalized := _normalize_res_path(path)
	if _is_gode_managed_export_path(normalized):
		skip()
		return
	if _is_target_native_probe_helper_path(normalized, features):
		skip()
		return
	if _is_gode_editor_only_export_path(normalized) or _is_gode_binary_resource_path(normalized, features):
		skip()
		return

func _export_begin(features: PackedStringArray, is_debug: bool, path: String, flags: int) -> void:
	_reset_export_state()
	_prepare_extension_state_for_export()
	if not _add_native_probe_helper(features):
		_restore_extension_state_after_export()
		return
	var has_npm_project := _has_npm_project()
	npm_config = _load_npm_config(has_npm_project)
	if not config_error.is_empty():
		push_error(config_error)
		_restore_extension_state_after_export()
		return
	if not _prepare_npm_export():
		_restore_extension_state_after_export()
		return
	_collect_npm_export_owned_paths(has_npm_project)

	var result := _run_typescript_export_compile()
	if not result.get("ok", false):
		_print_diagnostics(result.get("diagnostics", []))
		if not result.get("compiler_unavailable", false):
			push_error("Gode TypeScript export failed. Fix TypeScript diagnostics before exporting.")
		_restore_extension_state_after_export()
		return

	var export_manifest_outputs: Array = []
	for output: Dictionary in result.get("outputs", []):
		var source_resource_path: String = output.get("source", "")
		var source_path: String = output.get("path", "")
		var exported_path: String = output.get("exported_path", "")
		if not _add_compiled_file(exported_path, source_path, is_debug):
			_restore_extension_state_after_export()
			return
		if is_debug and FileAccess.file_exists(source_path + ".map"):
			if not _add_compiled_file(exported_path + ".map", source_path + ".map"):
				_restore_extension_state_after_export()
				return
		if not source_resource_path.is_empty() and not exported_path.is_empty():
			export_manifest_outputs.append({
				"source": source_resource_path,
				"exported_path": exported_path,
			})

	if not _add_typescript_export_manifest(export_manifest_outputs):
		_restore_extension_state_after_export()
		return

	if _should_export_npm_dependencies() and has_npm_project:
		if not _export_npm_runtime_snapshot():
			_restore_extension_state_after_export()
			return
		if npm_exported_files > 0:
			print("[Gode Export] Added npm runtime snapshot files: %d" % npm_exported_files)

func _export_end() -> void:
	_restore_extension_state_after_export()

func _restore_extension_state_after_export() -> void:
	_restore_local_extension_list_after_export()
	legacy_editor_resources_checked_for_export = false
	local_extension_list_checked_for_export = false
	native_extension_paths_checked_for_export = false

func _reset_export_state() -> void:
	npm_exported_files = 0
	npm_export_manifest_entries = PackedStringArray()
	npm_export_owned_exact_paths = {}
	npm_export_owned_prefixes = PackedStringArray()
	export_injected_path_hashes = {}
	npm_config = {}
	config_error = ""
	legacy_editor_resources_checked_for_export = false
	local_extension_list_checked_for_export = false
	native_extension_paths_checked_for_export = false

func _add_compiled_file(exported_path: String, source_path: String, include_inline_source_map := true) -> bool:
	if exported_path.is_empty() or source_path.is_empty():
		push_error("Gode TypeScript output mapping is incomplete.")
		return false
	if not FileAccess.file_exists(source_path):
		push_error("Missing Gode TypeScript output: %s" % source_path)
		return false
	if not include_inline_source_map and source_path.ends_with(".js"):
		var code := FileAccess.get_file_as_string(source_path)
		if FileAccess.get_open_error() != OK:
			push_error("Failed to read Gode TypeScript output: %s" % source_path)
			return false
		return _add_export_file_bytes(exported_path, _strip_inline_source_map(code).to_utf8_buffer())
	return _add_file_from_bytes(exported_path, source_path, "Failed to read Gode TypeScript output: %s")

func _add_file_from_bytes(exported_path: String, source_path: String, error_message: String) -> bool:
	var bytes := FileAccess.get_file_as_bytes(source_path)
	if FileAccess.get_open_error() != OK:
		push_error(error_message % source_path)
		return false
	return _add_export_file_bytes(exported_path, bytes)

func _add_typescript_export_manifest(outputs: Array) -> bool:
	var manifest := {
		"outputs": outputs,
	}
	return _add_export_file_bytes(TYPESCRIPT_EXPORT_MANIFEST_PATH, JSON.stringify(manifest, "\t").to_utf8_buffer())

func _strip_inline_source_map(code: String) -> String:
	var marker_index := code.rfind(INLINE_SOURCE_MAP_MARKER)
	if marker_index == -1:
		return code
	var line_start := code.rfind("\n", marker_index)
	if line_start == -1:
		return ""
	var line_end := code.find("\n", marker_index)
	if line_end == -1:
		return code.substr(0, line_start + 1)
	return code.substr(0, line_start + 1) + code.substr(line_end + 1)

func _print_diagnostics(diagnostics: Array) -> void:
	for diagnostic: Dictionary in diagnostics:
		var message: String = diagnostic.get("message", "")
		var file: String = diagnostic.get("file", "")
		var line: int = diagnostic.get("line", 0)
		var column: int = diagnostic.get("column", 0)
		if file.is_empty():
			push_error("[Gode TypeScript] %s" % message)
		else:
			push_error("[Gode TypeScript] %s:%d:%d %s" % [file, line, column, message])

func _run_typescript_export_compile() -> Dictionary:
	if not ClassDB.class_exists(GODE_TYPESCRIPT_COMPILER_CLASS):
		push_error("Gode editor extension is not loaded; reopen the project or re-enable the Gode plugin before exporting.")
		return {"ok": false, "compiler_unavailable": true, "diagnostics": []}
	if not ClassDB.class_has_method(GODE_TYPESCRIPT_COMPILER_CLASS, GODE_TYPESCRIPT_COMPILE_PROJECT_METHOD):
		push_error("Gode editor extension does not expose the TypeScript project compiler.")
		return {"ok": false, "compiler_unavailable": true, "diagnostics": []}
	var result: Variant = ClassDB.class_call_static(GODE_TYPESCRIPT_COMPILER_CLASS, GODE_TYPESCRIPT_COMPILE_PROJECT_METHOD, true)
	if typeof(result) != TYPE_DICTIONARY:
		push_error("Gode TypeScript compiler returned an invalid export result.")
		return {"ok": false, "compiler_unavailable": true, "diagnostics": []}
	return result

func _prepare_npm_export() -> bool:
	if not _has_npm_project():
		return true
	if not _get_npm_bool("requireTools"):
		return _validate_npm_layout()

	if not _command_exists("node"):
		push_error("Gode export found npm project files in the project root, but Node.js was not found in PATH.")
		return false
	if not _command_exists("npm"):
		push_error("Gode export found npm project files in the project root, but npm was not found in PATH.")
		return false
	return _validate_npm_layout()

func _validate_npm_layout() -> bool:
	var package_json := _read_package_json()
	if _package_has_dependencies(package_json) and not _dir_exists("res://node_modules"):
		push_error("Gode export found dependencies in package.json, but res://node_modules is missing. Run your package manager install command before exporting.")
		return false
	return true

func _has_npm_project() -> bool:
	return _file_exists("res://package.json") or _dir_exists("res://node_modules")

func _should_export_npm_dependencies() -> bool:
	return _get_npm_bool("exportDependencies")

func _is_gode_managed_export_path(path: String) -> bool:
	return _is_gode_generated_export_path(path) or _is_npm_export_owned_path(path)

func _is_gode_generated_export_path(path: String) -> bool:
	if path == TYPESCRIPT_EXPORT_MANIFEST_PATH or path == NPM_EXPORT_MANIFEST_PATH:
		return true
	return path.begins_with("res://.gode/build/typescript/") or path.begins_with("res://.gode/build/npm/")

func _is_npm_export_owned_path(path: String) -> bool:
	if path.is_empty():
		return false
	if npm_export_owned_exact_paths.has(path):
		return true
	for prefix: String in npm_export_owned_prefixes:
		if path == prefix or path.begins_with(prefix + "/"):
			return true
	return false

func _is_gode_editor_only_export_path(path: String) -> bool:
	if path.is_empty():
		return false
	if _is_gode_editor_extension_path(path):
		return true
	for exact_path: String in [
		LOCAL_EXTENSION_LIST_PATH,
		GODE_EDITOR_EXTENSION_TEMPLATE_PATH,
		GODE_EDITOR_EXTENSION_TEMPLATE_PATH + ".uid",
		GODE_LEGACY_EDITOR_EXTENSION_UID_PATH,
		"res://addons/gode/plugin.cfg",
		"res://addons/gode/gode.gd",
		"res://addons/gode/gode.gdc",
		"res://addons/gode/gode.gd.uid",
		"res://addons/gode/gode.gd.remap",
		"res://addons/gode/runtime/export_plugin.gd",
		"res://addons/gode/runtime/export_plugin.gdc",
		"res://addons/gode/runtime/export_plugin.gd.uid",
		"res://addons/gode/runtime/export_plugin.gd.remap",
		"res://addons/gode/runtime/typescript_compiler.js",
	]:
		if path == exact_path:
			return true
	for prefix: String in [
		"res://.godot/gode/",
		"res://addons/gode/binary/editor/",
		"res://addons/gode/config/",
		"res://addons/gode/icons/",
		"res://addons/gode/tsc/",
		"res://addons/gode/types/",
	]:
		if path.begins_with(prefix):
			return true
	return false

func _is_gode_editor_extension_path(path: String) -> bool:
	return path == GODE_EDITOR_EXTENSION_PATH or path == GODE_LEGACY_EDITOR_EXTENSION_PATH

func _is_gode_binary_resource_path(path: String, features: PackedStringArray) -> bool:
	if path == GODE_RUNTIME_EXTENSION_PATH:
		return false
	if not path.begins_with("res://addons/gode/binary/"):
		return false
	return not _is_target_runtime_binary_path(path, features)

func _is_target_runtime_binary_path(path: String, features: PackedStringArray) -> bool:
	return _target_runtime_binary_paths(features).has(path)

func _is_target_native_probe_helper_path(path: String, features: PackedStringArray) -> bool:
	var helper_path := _target_native_probe_helper_path(features)
	return not helper_path.is_empty() and path == helper_path

func _add_native_probe_helper(features: PackedStringArray) -> bool:
	var helper_path := _target_native_probe_helper_path(features)
	if helper_path.is_empty():
		return true
	if not _file_exists(helper_path):
		push_error("Missing Gode native probe helper: %s" % helper_path)
		return false
	if _features_has(features, "macos"):
		return _add_macos_native_probe_helper(helper_path)
	for dependency_path: String in _target_runtime_binary_paths(features):
		if not _add_shared_object_path(dependency_path, features, "Missing Gode runtime binary required by native probe helper: %s"):
			return false
	if not _add_shared_object_path(helper_path, features, "Missing Gode native probe helper: %s"):
		return false
	return true

func _add_shared_object_path(source_path: String, features: PackedStringArray, missing_message: String) -> bool:
	if not _file_exists(source_path):
		push_error(missing_message % source_path)
		return false
	add_shared_object(ProjectSettings.globalize_path(source_path), features, _to_resource_relative(source_path.get_base_dir()))
	return true

func _add_macos_native_probe_helper(helper_path: String) -> bool:
	var framework_dir := ProjectSettings.globalize_path(MACOS_NATIVE_PROBE_FRAMEWORK_DIR)
	var resources_dir := framework_dir.path_join("Resources")
	if DirAccess.make_dir_recursive_absolute(resources_dir) != OK:
		push_error("Gode export could not create macOS native probe framework directory: %s" % MACOS_NATIVE_PROBE_FRAMEWORK_DIR)
		return false
	var helper_bytes := FileAccess.get_file_as_bytes(helper_path)
	if FileAccess.get_open_error() != OK:
		push_error("Failed to read Gode native probe helper: %s" % helper_path)
		return false
	var staged_helper := framework_dir.path_join(helper_path.get_file())
	var file := FileAccess.open(staged_helper, FileAccess.WRITE)
	if file == null:
		push_error("Gode export could not stage macOS native probe helper: %s" % staged_helper)
		return false
	file.store_buffer(helper_bytes)
	file.close()
	OS.execute("chmod", PackedStringArray(["755", staged_helper]), [], true, false)
	var info_plist := FileAccess.open(resources_dir.path_join("Info.plist"), FileAccess.WRITE)
	if info_plist == null:
		push_error("Gode export could not stage macOS native probe Info.plist: %s" % MACOS_NATIVE_PROBE_FRAMEWORK_DIR)
		return false
	info_plist.store_string("""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>gode_node</string>
	<key>CFBundleIdentifier</key>
	<string>com.godothub.gode.node</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>gode_node</string>
	<key>CFBundlePackageType</key>
	<string>FMWK</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>CFBundleSupportedPlatforms</key>
	<array>
		<string>MacOSX</string>
	</array>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>LSMinimumSystemVersion</key>
	<string>10.15</string>
</dict>
</plist>
""")
	info_plist.close()
	add_macos_plugin_file(framework_dir)
	return true

func _target_native_probe_helper_path(features: PackedStringArray) -> String:
	if LITE_RUNTIME:
		return ""
	if _features_has(features, "windows") and (_features_has(features, "x86_64") or _features_has(features, "x64")):
		return "res://addons/gode/binary/windows/x64/gode_node.exe"
	if _features_has(features, "linux") and (_features_has(features, "x86_64") or _features_has(features, "x64")):
		return "res://addons/gode/binary/linux/x64/gode_node"
	if _features_has(features, "macos") and _features_has(features, "arm64"):
		return "res://addons/gode/binary/macos/arm64/gode_node"
	return ""

func _target_runtime_binary_paths(features: PackedStringArray) -> PackedStringArray:
	if _features_has(features, "windows") and (_features_has(features, "x86_64") or _features_has(features, "x64")):
		var paths := PackedStringArray(["res://addons/gode/binary/windows/x64/libgode_runtime.dll"])
		if not LITE_RUNTIME:
			paths.append("res://addons/gode/binary/windows/x64/node.dll")
		return paths
	if _features_has(features, "linux") and (_features_has(features, "x86_64") or _features_has(features, "x64")):
		return PackedStringArray([
			"res://addons/gode/binary/linux/x64/libgode_runtime.so",
		])
	if _features_has(features, "macos") and _features_has(features, "arm64"):
		return PackedStringArray([
			"res://addons/gode/binary/macos/arm64/libgode_runtime.dylib",
		])
	if _features_has(features, "android") and _features_has(features, "arm64"):
		return PackedStringArray(["res://addons/gode/binary/android/arm64/libgode_runtime.so"])
	if _features_has(features, "ios") and _features_has(features, "arm64"):
		return PackedStringArray(["res://addons/gode/binary/ios/arm64/libgode_runtime.dylib"])
	return PackedStringArray()

func _features_has(features: PackedStringArray, feature: String) -> bool:
	for value: String in features:
		if value == feature:
			return true
	return false

func _prepare_extension_state_for_export() -> void:
	_remove_legacy_editor_extension_resources()
	_prepare_project_extension_paths_for_export()
	_prepare_local_extension_list_for_export()

func _prepare_project_extension_paths_for_export() -> void:
	if native_extension_paths_checked_for_export:
		return
	native_extension_paths_checked_for_export = true

	var value: Variant = ProjectSettings.get_setting("native_extensions/paths", PackedStringArray())
	var paths := PackedStringArray()
	if typeof(value) == TYPE_PACKED_STRING_ARRAY:
		paths = value
	elif typeof(value) == TYPE_ARRAY:
		var array: Array = value
		for item in array:
			paths.append(String(item))

	var filtered := PackedStringArray()
	var changed := false
	for existing_path: String in paths:
		var normalized_path := existing_path.strip_edges()
		if _is_gode_editor_extension_path(normalized_path):
			changed = true
			continue
		if normalized_path.is_empty() or filtered.has(normalized_path):
			changed = true
			continue
		filtered.append(normalized_path)
	if not changed:
		return

	ProjectSettings.set_setting("native_extensions/paths", filtered)
	var save_error := ProjectSettings.save()
	if save_error != OK:
		push_warning("Gode export could not save sanitized native extension paths.")

func _prepare_local_extension_list_for_export() -> void:
	if local_extension_list_checked_for_export:
		return
	local_extension_list_checked_for_export = true
	if editor_extension_list_entry_removed_for_export:
		return
	if not FileAccess.file_exists(LOCAL_EXTENSION_LIST_PATH):
		return

	var lines := _read_local_extension_list()
	var filtered := PackedStringArray()
	for line: String in lines:
		if _is_gode_editor_extension_path(line.strip_edges()):
			editor_extension_list_entry_removed_for_export = true
			continue
		filtered.append(line)
	if not editor_extension_list_entry_removed_for_export:
		return
	_write_or_remove_local_extension_list(filtered)

func _restore_local_extension_list_after_export() -> void:
	if not editor_extension_list_entry_removed_for_export:
		return
	editor_extension_list_entry_removed_for_export = false

	var lines := _read_local_extension_list()
	for line: String in lines:
		if line.strip_edges() == GODE_EDITOR_EXTENSION_PATH:
			return
	lines.append(GODE_EDITOR_EXTENSION_PATH)
	_write_or_remove_local_extension_list(lines)

func _remove_legacy_editor_extension_resources() -> void:
	if legacy_editor_resources_checked_for_export:
		return
	legacy_editor_resources_checked_for_export = true
	for path in [GODE_LEGACY_EDITOR_EXTENSION_PATH, GODE_LEGACY_EDITOR_EXTENSION_UID_PATH]:
		if not FileAccess.file_exists(path):
			continue
		var remove_error := DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
		if remove_error != OK:
			push_warning("Gode export could not remove stale editor extension resource: %s" % path)

func _read_local_extension_list() -> PackedStringArray:
	if not FileAccess.file_exists(LOCAL_EXTENSION_LIST_PATH):
		return PackedStringArray()
	var content := FileAccess.get_file_as_string(LOCAL_EXTENSION_LIST_PATH)
	if FileAccess.get_open_error() != OK:
		push_warning("Gode export could not read local GDExtension list: %s" % LOCAL_EXTENSION_LIST_PATH)
		return PackedStringArray()
	return content.replace("\r\n", "\n").replace("\r", "\n").split("\n", false)

func _write_or_remove_local_extension_list(lines: PackedStringArray) -> void:
	var global_path := ProjectSettings.globalize_path(LOCAL_EXTENSION_LIST_PATH)
	if lines.is_empty():
		if FileAccess.file_exists(LOCAL_EXTENSION_LIST_PATH):
			var remove_error := DirAccess.remove_absolute(global_path)
			if remove_error != OK:
				push_warning("Gode export could not remove local GDExtension list: %s" % LOCAL_EXTENSION_LIST_PATH)
		return

	var directory := ProjectSettings.globalize_path(LOCAL_EXTENSION_LIST_PATH.get_base_dir())
	if DirAccess.make_dir_recursive_absolute(directory) != OK:
		push_warning("Gode export could not create local GDExtension directory: %s" % LOCAL_EXTENSION_LIST_PATH.get_base_dir())
		return
	var file := FileAccess.open(LOCAL_EXTENSION_LIST_PATH, FileAccess.WRITE)
	if file == null:
		push_warning("Gode export could not write local GDExtension list: %s" % LOCAL_EXTENSION_LIST_PATH)
		return
	file.store_string("\n".join(lines) + "\n")

func _command_exists(command: String) -> bool:
	var resolved := _resolve_command_path(command)
	if resolved.is_empty():
		return false
	var output: Array = []
	return OS.execute(resolved, PackedStringArray(["--version"]), output, true, false) == 0

func _resolve_command_path(command: String) -> String:
	if command.is_empty():
		return ""
	if command.contains("/") or command.contains("\\"):
		return command if FileAccess.file_exists(command) else ""

	var candidates := PackedStringArray()
	if OS.get_name() == "Windows":
		var lower_command := command.to_lower()
		if lower_command.ends_with(".exe") or lower_command.ends_with(".cmd") or lower_command.ends_with(".bat") or lower_command.ends_with(".com"):
			candidates.append(command)
		else:
			candidates.append(command + ".cmd")
			candidates.append(command + ".exe")
			candidates.append(command + ".bat")
			candidates.append(command + ".com")
			candidates.append(command)
	else:
		candidates.append(command)

	var path_separator := ";" if OS.get_name() == "Windows" else ":"
	for directory: String in OS.get_environment("PATH").split(path_separator, false):
		var normalized_directory := directory.strip_edges().trim_prefix("\"").trim_suffix("\"")
		if normalized_directory.is_empty():
			continue
		for candidate: String in candidates:
			var absolute_candidate := normalized_directory.path_join(candidate)
			if FileAccess.file_exists(absolute_candidate):
				return absolute_candidate
	return ""

func _export_npm_runtime_snapshot() -> bool:
	if _get_npm_bool("includeManifests"):
		for manifest: String in NPM_MANIFEST_FILES:
			var manifest_path := "res://" + manifest
			if _file_exists(manifest_path):
				if not _add_export_file(manifest_path):
					return false

	if _get_npm_bool("includeNodeModules") and _dir_exists("res://node_modules"):
		if not _add_export_directory("res://node_modules"):
			return false

	for extra_path: String in _get_npm_string_array("extraIncludePaths"):
		var normalized := _normalize_res_path(extra_path)
		if normalized.is_empty():
			continue
		if _file_exists(normalized):
			if not _add_export_file(normalized):
				return false
		elif _dir_exists(normalized):
			if not _add_export_directory(normalized):
				return false
		else:
			push_warning("Gode export extra npm include path does not exist: %s" % normalized)
	if npm_exported_files > 0:
		if not _add_npm_export_manifest():
			return false
	return true

func _collect_npm_export_owned_paths(has_npm_project: bool) -> void:
	npm_export_owned_exact_paths = {}
	npm_export_owned_prefixes = PackedStringArray()
	if not has_npm_project or not _should_export_npm_dependencies():
		return

	if _get_npm_bool("includeManifests"):
		for manifest: String in NPM_MANIFEST_FILES:
			var manifest_path := "res://" + manifest
			if _file_exists(manifest_path):
				_remember_npm_export_owned_file(manifest_path)

	if _get_npm_bool("includeNodeModules") and _dir_exists("res://node_modules"):
		_remember_npm_export_owned_directory("res://node_modules")

	for extra_path: String in _get_npm_string_array("extraIncludePaths"):
		var normalized := _normalize_res_path(extra_path)
		if normalized.is_empty():
			continue
		if _file_exists(normalized):
			_remember_npm_export_owned_file(normalized)
		elif _dir_exists(normalized):
			_remember_npm_export_owned_directory(normalized)

func _remember_npm_export_owned_file(path: String) -> void:
	var normalized := _normalize_res_path(path)
	if normalized.is_empty():
		return
	npm_export_owned_exact_paths[normalized] = true

func _remember_npm_export_owned_directory(path: String) -> void:
	var normalized := _normalize_res_path(path).trim_suffix("/")
	if normalized.is_empty():
		return
	npm_export_owned_prefixes.append(normalized)

func _add_export_directory(directory_path: String) -> bool:
	if _is_excluded_export_path(directory_path):
		return true

	for file_name in DirAccess.get_files_at(directory_path):
		var file_path := directory_path.path_join(file_name)
		if _is_excluded_export_path(file_path):
			continue
		if not _add_export_file(file_path):
			return false

	for directory_name in DirAccess.get_directories_at(directory_path):
		var child_path := directory_path.path_join(directory_name)
		if not _add_export_directory(child_path):
			return false
	return true

func _add_export_file(source_path: String) -> bool:
	if _is_excluded_export_path(source_path):
		return true

	if not _file_exists(source_path):
		push_error("Gode export expected file does not exist: %s" % source_path)
		return false
	var bytes := FileAccess.get_file_as_bytes(source_path)
	if FileAccess.get_open_error() != OK:
		push_error("Failed to read Gode export file: %s" % source_path)
		return false
	var normalized := _normalize_res_path(source_path)
	if normalized.is_empty():
		push_error("Gode export path is invalid: %s" % source_path)
		return false
	var sha256 := _sha256_hex(bytes)
	if sha256.is_empty():
		push_error("Failed to hash Gode export file: %s" % normalized)
		return false
	var already_added := export_injected_path_hashes.has(normalized)
	if not _add_export_file_bytes(normalized, bytes, sha256):
		return false
	if already_added:
		return true
	npm_exported_files += 1
	if not _record_npm_export_manifest_entry(normalized, bytes.size(), sha256):
		return false
	return true

func _add_export_file_bytes(exported_path: String, bytes: PackedByteArray, sha256 := "") -> bool:
	var normalized := _normalize_res_path(exported_path)
	if normalized.is_empty():
		push_error("Gode export path is invalid: %s" % exported_path)
		return false
	var content_sha256 := sha256
	if content_sha256.is_empty():
		content_sha256 = _sha256_hex(bytes)
	if content_sha256.is_empty():
		push_error("Failed to hash Gode export file: %s" % normalized)
		return false
	if export_injected_path_hashes.has(normalized):
		if String(export_injected_path_hashes[normalized]) != content_sha256:
			push_error("Gode export would add conflicting contents for path: %s" % normalized)
			return false
		return true
	add_file(normalized, bytes, false)
	export_injected_path_hashes[normalized] = content_sha256
	return true

func _record_npm_export_manifest_entry(source_path: String, size: int, sha256: String) -> bool:
	var normalized := _normalize_res_path(source_path)
	if normalized.is_empty() or normalized == NPM_EXPORT_MANIFEST_PATH:
		return true
	if sha256.is_empty():
		push_error("Failed to hash Gode export file: %s" % normalized)
		return false
	npm_export_manifest_entries.append("%s\t%d\t%s" % [_to_resource_relative(normalized), size, sha256])
	return true

func _sha256_hex(bytes: PackedByteArray) -> String:
	if bytes.is_empty():
		return EMPTY_SHA256
	var hashing_context := HashingContext.new()
	if hashing_context == null:
		return ""
	if hashing_context.start(HashingContext.HASH_SHA256) != OK:
		return ""
	if hashing_context.update(bytes) != OK:
		return ""
	return hashing_context.finish().hex_encode()

func _add_npm_export_manifest() -> bool:
	var entries: Array = []
	for entry: String in npm_export_manifest_entries:
		entries.append(entry)
	entries.sort()
	var manifest := {
		"version": 1,
		"files": entries,
	}
	return _add_export_file_bytes(NPM_EXPORT_MANIFEST_PATH, JSON.stringify(manifest, "\t").to_utf8_buffer())

func _read_package_json() -> Dictionary:
	if not _file_exists("res://package.json"):
		return {}
	var package_json_text := FileAccess.get_file_as_string("res://package.json")
	if FileAccess.get_open_error() != OK:
		push_warning("Gode export could not read res://package.json.")
		return {}
	var parsed := JSON.parse_string(package_json_text)
	if typeof(parsed) == TYPE_DICTIONARY:
		return parsed
	push_warning("Gode export could not parse res://package.json.")
	return {}

func _package_has_dependencies(package_json: Dictionary) -> bool:
	for key in ["dependencies", "devDependencies", "optionalDependencies", "peerDependencies"]:
		if package_json.has(key) and typeof(package_json[key]) == TYPE_DICTIONARY and not package_json[key].is_empty():
			return true
	return false

func _is_excluded_export_path(path: String) -> bool:
	var rel_path := _to_resource_relative(path)
	for raw_pattern in _get_npm_string_array("excludePaths"):
		var pattern := _to_resource_relative(_normalize_res_path(raw_pattern))
		if pattern.is_empty():
			continue
		if rel_path == pattern or rel_path.begins_with(pattern.trim_suffix("/") + "/"):
			return true
	return false

func _load_npm_config(should_create_project_config: bool) -> Dictionary:
	config_error = ""
	var config := _default_npm_config()
	if not _file_exists(GODE_CONFIG_PATH):
		if not should_create_project_config:
			return config
		if not _create_project_gode_config():
			return config

	var config_text := FileAccess.get_file_as_string(GODE_CONFIG_PATH)
	if FileAccess.get_open_error() != OK:
		config_error = "Gode could not read project config: %s" % GODE_CONFIG_PATH
		return config
	var parsed: Variant = JSON.parse_string(config_text)
	if typeof(parsed) != TYPE_DICTIONARY:
		config_error = "Gode config must be a JSON object: %s" % GODE_CONFIG_PATH
		return config

	var root: Dictionary = parsed
	var export_value: Variant = root.get("export", {})
	if typeof(export_value) != TYPE_DICTIONARY:
		config_error = "Gode config field export must be an object: %s" % GODE_CONFIG_PATH
		return config

	var export_config: Dictionary = export_value
	var npm_value: Variant = export_config.get("npm", {})
	if typeof(npm_value) != TYPE_DICTIONARY:
		config_error = "Gode config field export.npm must be an object: %s" % GODE_CONFIG_PATH
		return config

	var user_config: Dictionary = npm_value
	for key_value in user_config.keys():
		var key := String(key_value)
		if not config.has(key):
			push_warning("Unknown Gode npm export config key ignored: export.npm.%s" % key)
			continue
		_merge_npm_config_value(config, key, user_config[key_value])
	return config

func _create_project_gode_config() -> bool:
	if not _file_exists(DEFAULT_GODE_CONFIG_PATH):
		config_error = "Gode default config template is missing: %s" % DEFAULT_GODE_CONFIG_PATH
		return false

	var default_config := FileAccess.get_file_as_string(DEFAULT_GODE_CONFIG_PATH)
	if FileAccess.get_open_error() != OK:
		config_error = "Gode could not read default config template: %s" % DEFAULT_GODE_CONFIG_PATH
		return false
	if typeof(JSON.parse_string(default_config)) != TYPE_DICTIONARY:
		config_error = "Gode default config template must be a JSON object: %s" % DEFAULT_GODE_CONFIG_PATH
		return false

	var project_config := FileAccess.open(GODE_CONFIG_PATH, FileAccess.WRITE)
	if project_config == null:
		config_error = "Gode could not create project config: %s" % GODE_CONFIG_PATH
		return false

	project_config.store_string(default_config)
	project_config.close()
	print("[Gode Export] Created project config from template: %s" % GODE_CONFIG_PATH)
	return true

func _default_npm_config() -> Dictionary:
	return {
		"exportDependencies": true,
		"requireTools": true,
		"includeManifests": true,
		"includeNodeModules": true,
		"excludePaths": PackedStringArray(["node_modules/.cache", "node_modules/.bin"]),
		"extraIncludePaths": PackedStringArray(),
	}

func _merge_npm_config_value(config: Dictionary, key: String, value: Variant) -> void:
	if key in ["exportDependencies", "requireTools", "includeManifests", "includeNodeModules"]:
		if typeof(value) != TYPE_BOOL:
			config_error = "Gode config field export.npm.%s must be a boolean." % key
			return
		config[key] = value
		return

	if key in ["excludePaths", "extraIncludePaths"]:
		config[key] = _read_string_array_config(value, "export.npm.%s" % key)

func _read_string_array_config(value: Variant, field_name: String) -> PackedStringArray:
	var out := PackedStringArray()
	var values: Array = []
	if value is PackedStringArray:
		for item in value:
			out.append(String(item))
		return out
	if typeof(value) != TYPE_ARRAY:
		config_error = "Gode config field %s must be an array of strings." % field_name
		return out

	values = value
	for item in values:
		if typeof(item) != TYPE_STRING:
			config_error = "Gode config field %s must contain only strings." % field_name
			return PackedStringArray()
		out.append(String(item))
	return out

func _get_npm_bool(name: String) -> bool:
	return bool(npm_config.get(name, _default_npm_config().get(name, false)))

func _get_npm_string(name: String) -> String:
	return String(npm_config.get(name, _default_npm_config().get(name, "")))

func _get_npm_string_array(name: String) -> PackedStringArray:
	var value: Variant = npm_config.get(name, _default_npm_config().get(name, PackedStringArray()))
	if value is PackedStringArray:
		return value
	if value is Array:
		var out := PackedStringArray()
		for item in value:
			out.append(String(item))
		return out
	return PackedStringArray()

func _normalize_res_path(path: String) -> String:
	var normalized := path.strip_edges().replace("\\", "/")
	if normalized.is_empty():
		return ""
	if normalized.contains("://") and not normalized.begins_with("res://"):
		return ""
	if normalized.begins_with("res://"):
		if _path_has_parent_segment(normalized):
			return ""
		return "res://" + normalized.trim_prefix("res://").simplify_path()
	normalized = normalized.trim_prefix("/")
	if _path_has_parent_segment(normalized):
		return ""
	return "res://" + normalized.simplify_path()

func _to_resource_relative(path: String) -> String:
	return _normalize_res_path(path).trim_prefix("res://").trim_suffix("/")

func _path_has_parent_segment(path: String) -> bool:
	for segment in path.replace("\\", "/").split("/", false):
		if segment == "..":
			return true
	return false

func _file_exists(path: String) -> bool:
	return FileAccess.file_exists(path)

func _dir_exists(path: String) -> bool:
	return DirAccess.open(path) != null
