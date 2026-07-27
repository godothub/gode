#include "compiler/typescript_compiler.h"

#include "runtime/node_runtime.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <vector>

using namespace godot;

namespace gode {
namespace {

constexpr const char *TYPESCRIPT_VERSION = "6.0.3";
constexpr const char *TYPESCRIPT_COMPILER_BRIDGE_PATH = "res://addons/gode/runtime/typescript_compiler.js";
constexpr const char *TYPESCRIPT_RUNTIME_PATH = "res://addons/gode/tsc/lib/typescript.js";
constexpr const char *PROJECT_TYPESCRIPT_CONFIG_PATH = "res://tsconfig.json";
constexpr const char *DEFAULT_TYPESCRIPT_CONFIG_PATH = "res://addons/gode/config/tsconfig.json";
constexpr const char *GODE_GLOBAL_TYPES_PATH = "res://addons/gode/types/globals.d.ts";
constexpr const char *GODE_MODULE_TYPES_PATH = "res://addons/gode/types/godot.d.ts";

bool is_dts_path(const String &path) {
	return path.to_lower().ends_with(".d.ts");
}

bool is_typescript_path(const String &path) {
	String lower = path.to_lower();
	return lower.ends_with(".ts") || lower.ends_with(".tsx");
}

String normalize_path_string(const String &path) {
	return path.replace("\\", "/").simplify_path();
}

bool path_has_parent_segment(const String &path) {
	String normalized = path.replace("\\", "/");
	PackedStringArray segments = normalized.split("/", false);
	for (int64_t i = 0; i < segments.size(); i++) {
		if (segments[i] == "..") {
			return true;
		}
	}
	return false;
}

String normalize_project_path(const String &path) {
	String normalized = normalize_path_string(path);
	if (normalized.begins_with("res://")) {
		return normalized;
	}
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings) {
		String localized = normalize_path_string(project_settings->localize_path(normalized));
		if (localized.begins_with("res://")) {
			return localized;
		}
	}
	return normalized;
}

String resource_relative_path(const String &source_path) {
	String normalized = normalize_project_path(source_path);
	if (normalized.begins_with("res://")) {
		return normalized.substr(6);
	}
	return normalized;
}

String project_hash() {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	String project_root = project_settings ? project_settings->globalize_path("res://") : String("res://");
	String hash = project_root.sha256_text();
	return hash.substr(0, 16);
}

String cache_root() {
	return String("user://.gode/typescript/") + project_hash() + "/typescript-" + TYPESCRIPT_VERSION;
}

String exported_build_root() {
	return "res://.gode/build/typescript";
}

String exported_manifest_path() {
	return exported_build_root().path_join("manifest.json");
}

String manifest_path() {
	return cache_root().path_join("manifest.json");
}

bool should_skip_directory(const String &directory_path) {
	String rel = resource_relative_path(directory_path).trim_suffix("/");
	if (rel.is_empty()) {
		return false;
	}

	static const char *skipped_prefixes[] = {
		".godot",
		".gode",
		"node_modules",
		"addons/gode/tsc",
		"addons/gode/types"
	};

	for (const char *prefix : skipped_prefixes) {
		String skipped(prefix);
		if (rel == skipped || rel.begins_with(skipped + "/")) {
			return true;
		}
	}
	return false;
}

Dictionary make_error_diagnostic(const String &message, const String &file_path = String()) {
	Dictionary diagnostic;
	diagnostic["category"] = "error";
	diagnostic["code"] = 0;
	diagnostic["message"] = message;
	diagnostic["file"] = file_path;
	diagnostic["line"] = 0;
	diagnostic["column"] = 0;
	return diagnostic;
}

bool read_source_file(const String &path, Dictionary &file, Array &diagnostics) {
	String source = FileAccess::get_file_as_string(path);
	if (FileAccess::get_open_error() != OK) {
		diagnostics.append(make_error_diagnostic("Failed to read TypeScript source: " + path, path));
		return false;
	}
	file["path"] = path;
	file["source"] = source;
	return true;
}

void collect_sources_recursive(const String &directory_path, Array &sources, Array &diagnostics) {
	if (should_skip_directory(directory_path)) {
		return;
	}

	PackedStringArray files = DirAccess::get_files_at(directory_path);
	for (int64_t i = 0; i < files.size(); i++) {
		String file_name = files[i];
		String file_path = directory_path.path_join(file_name);
		if (!is_typescript_path(file_path) && !is_dts_path(file_path)) {
			continue;
		}
		Dictionary file;
		if (read_source_file(file_path, file, diagnostics)) {
			sources.append(file);
		}
	}

	PackedStringArray directories = DirAccess::get_directories_at(directory_path);
	for (int64_t i = 0; i < directories.size(); i++) {
		String child = directory_path.path_join(directories[i]);
		collect_sources_recursive(child, sources, diagnostics);
	}
}

Array collect_project_sources(Array &diagnostics) {
	Array sources;
	collect_sources_recursive("res://", sources, diagnostics);
	if (FileAccess::file_exists(GODE_GLOBAL_TYPES_PATH)) {
		Dictionary file;
		if (read_source_file(GODE_GLOBAL_TYPES_PATH, file, diagnostics)) {
			sources.append(file);
		}
	}
	return sources;
}

String compiled_path_for_source_internal(const String &source_path) {
	String rel = resource_relative_path(source_path);
	return cache_root().path_join(rel.get_basename() + ".js");
}

String exported_path_for_source_internal(const String &source_path) {
	String rel = resource_relative_path(source_path);
	return exported_build_root().path_join(rel.get_basename() + ".js");
}

Dictionary output_mapping_for_source(const String &source_path) {
	Dictionary output;
	output["source"] = source_path;
	output["path"] = compiled_path_for_source_internal(source_path);
	output["exported_path"] = exported_path_for_source_internal(source_path);
	return output;
}

uint64_t newest_input_mtime(const Array &sources) {
	uint64_t newest = 0;
	for (int64_t i = 0; i < sources.size(); i++) {
		Dictionary source = sources[i];
		String source_path = source["path"];
		if (FileAccess::file_exists(source_path)) {
			newest = std::max(newest, FileAccess::get_modified_time(source_path));
		}
	}
	if (FileAccess::file_exists(PROJECT_TYPESCRIPT_CONFIG_PATH)) {
		newest = std::max(newest, FileAccess::get_modified_time(PROJECT_TYPESCRIPT_CONFIG_PATH));
	}
	if (FileAccess::file_exists(TYPESCRIPT_COMPILER_BRIDGE_PATH)) {
		newest = std::max(newest, FileAccess::get_modified_time(TYPESCRIPT_COMPILER_BRIDGE_PATH));
	}
	if (FileAccess::file_exists(TYPESCRIPT_RUNTIME_PATH)) {
		newest = std::max(newest, FileAccess::get_modified_time(TYPESCRIPT_RUNTIME_PATH));
	}
	if (FileAccess::file_exists(GODE_MODULE_TYPES_PATH)) {
		newest = std::max(newest, FileAccess::get_modified_time(GODE_MODULE_TYPES_PATH));
	}
	return newest;
}

void append_file_state_signature(std::vector<String> &entries, const String &path, bool hash_content) {
	if (!FileAccess::file_exists(path)) {
		entries.push_back(path + String("\tmissing"));
		return;
	}

	String entry = path + String("\tmtime:") + String::num_int64(static_cast<int64_t>(FileAccess::get_modified_time(path)));
	if (hash_content) {
		String content = FileAccess::get_file_as_string(path);
		if (FileAccess::get_open_error() == OK) {
			entry += String("\tsha256:") + content.sha256_text();
		} else {
			entry += String("\tunreadable");
		}
	}
	entries.push_back(entry);
}

String input_signature(const Array &sources) {
	std::vector<String> entries;
	entries.reserve(static_cast<size_t>(sources.size()) + 4);

	for (int64_t i = 0; i < sources.size(); i++) {
		Dictionary source = sources[i];
		String source_path = source["path"];
		String source_content = source.get("source", String());
		entries.push_back(source_path + String("\tsha256:") + source_content.sha256_text());
	}

	append_file_state_signature(entries, PROJECT_TYPESCRIPT_CONFIG_PATH, true);
	append_file_state_signature(entries, TYPESCRIPT_COMPILER_BRIDGE_PATH, true);
	append_file_state_signature(entries, TYPESCRIPT_RUNTIME_PATH, true);
	append_file_state_signature(entries, GODE_MODULE_TYPES_PATH, true);

	std::sort(entries.begin(), entries.end());

	String joined;
	for (const String &entry : entries) {
		joined += entry + String("\n");
	}
	return joined.sha256_text();
}

bool output_string_field_is_valid(const Dictionary &output, const String &field) {
	Variant value = output.get(field, Variant());
	return value.get_type() == Variant::STRING && !String(value).is_empty();
}

bool path_is_under_root(const String &path, const String &root_path) {
	if (path_has_parent_segment(path) || path_has_parent_segment(root_path)) {
		return false;
	}

	String normalized_path = normalize_path_string(path);
	String root = normalize_path_string(root_path).trim_suffix("/");
	return normalized_path == root || normalized_path.begins_with(root + String("/"));
}

bool path_has_extension(const String &path, const String &extension) {
	return path.to_lower().ends_with(extension);
}

bool source_output_path_is_valid(const String &path) {
	if (path_has_parent_segment(path)) {
		return false;
	}

	String normalized = normalize_project_path(path);
	return normalized.begins_with("res://") &&
			(path_has_extension(normalized, ".ts") || path_has_extension(normalized, ".tsx")) &&
			!path_has_extension(normalized, ".d.ts");
}

bool normalize_typescript_source_path(const String &path, String &r_source_path, String *r_error = nullptr) {
	if (path_has_parent_segment(path)) {
		if (r_error) {
			*r_error = "TypeScript source path cannot contain parent-directory segments: " + path;
		}
		return false;
	}

	String normalized = normalize_project_path(path);
	if (!source_output_path_is_valid(normalized)) {
		if (r_error) {
			*r_error = "Invalid TypeScript source path, expected a .ts or .tsx file under res://: " + path;
		}
		return false;
	}

	r_source_path = normalized;
	return true;
}

bool output_entry_is_valid(const Variant &output_value, bool require_cache_path) {
	if (output_value.get_type() != Variant::DICTIONARY) {
		return false;
	}

	Dictionary output = output_value;
	if (!output_string_field_is_valid(output, "source") || !output_string_field_is_valid(output, "exported_path")) {
		return false;
	}
	if (!source_output_path_is_valid(String(output["source"]))) {
		return false;
	}
	if (!path_is_under_root(String(output["exported_path"]), exported_build_root()) || !path_has_extension(String(output["exported_path"]), ".js")) {
		return false;
	}
	if (require_cache_path && !output_string_field_is_valid(output, "path")) {
		return false;
	}
	if (require_cache_path && (!path_is_under_root(String(output["path"]), cache_root()) || !path_has_extension(String(output["path"]), ".js"))) {
		return false;
	}
	return true;
}

bool manifest_outputs_are_valid(const Array &outputs, bool require_cache_path) {
	for (int64_t i = 0; i < outputs.size(); i++) {
		if (!output_entry_is_valid(outputs[i], require_cache_path)) {
			return false;
		}
	}
	return true;
}

bool outputs_exist(const Array &outputs) {
	for (int64_t i = 0; i < outputs.size(); i++) {
		Variant output_value = outputs[i];
		if (!output_entry_is_valid(output_value, true)) {
			return false;
		}

		Dictionary output = output_value;
		String output_path = output["path"];
		if (!FileAccess::file_exists(output_path)) {
			return false;
		}
	}
	return true;
}

bool output_for_source(const Array &outputs, const String &source_path, Dictionary &r_output) {
	const String normalized_source_path = normalize_project_path(source_path);
	for (int64_t i = 0; i < outputs.size(); i++) {
		Variant output_value = outputs[i];
		if (!output_entry_is_valid(output_value, false)) {
			continue;
		}

		Dictionary output = output_value;
		String output_source = output.get("source", String());
		if (normalize_project_path(output_source) == normalized_source_path) {
			r_output = output;
			return true;
		}
	}
	return false;
}

Dictionary make_result(bool ok, const String &message = String()) {
	Dictionary result;
	result["ok"] = ok;
	result["compiled"] = 0;
	result["skipped"] = 0;
	result["outputs"] = Array();
	result["diagnostics"] = Array();
	result["cache_root"] = cache_root();
	if (!message.is_empty()) {
		Array diagnostics;
		diagnostics.append(make_error_diagnostic(message));
		result["diagnostics"] = diagnostics;
		result["message"] = message;
	}
	return result;
}

void append_error_diagnostic(Dictionary &result, const String &message) {
	Array diagnostics = result.has("diagnostics") ? Array(result["diagnostics"]) : Array();
	diagnostics.append(make_error_diagnostic(message));
	result["diagnostics"] = diagnostics;
	result["message"] = message;
	result["ok"] = false;
}

void print_diagnostics(const Array &diagnostics) {
	for (int64_t i = 0; i < diagnostics.size(); i++) {
		Dictionary diagnostic = diagnostics[i];
		String message = diagnostic.has("message") ? String(diagnostic["message"]) : String();
		String file = diagnostic.has("file") ? String(diagnostic["file"]) : String();
		int64_t line = diagnostic.has("line") ? int64_t(diagnostic["line"]) : 0;
		int64_t column = diagnostic.has("column") ? int64_t(diagnostic["column"]) : 0;
		if (!file.is_empty()) {
			UtilityFunctions::printerr("[Gode TypeScript] ", file, ":", line, ":", column, " ", message);
		} else {
			UtilityFunctions::printerr("[Gode TypeScript] ", message);
		}
	}
}

bool write_text_file(const String &path, const String &content) {
	Error dir_error = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	if (dir_error != OK) {
		return false;
	}
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		return false;
	}
	file->store_string(content);
	file->close();
	return true;
}

bool load_manifest(const String &path, Dictionary &r_manifest) {
	if (!FileAccess::file_exists(path)) {
		return false;
	}

	String content = FileAccess::get_file_as_string(path);
	if (FileAccess::get_open_error() != OK) {
		return false;
	}

	Variant parsed = JSON::parse_string(content);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return false;
	}

	r_manifest = parsed;
	return true;
}

bool load_compile_manifest(Dictionary &r_manifest) {
	return load_manifest(manifest_path(), r_manifest);
}

bool load_manifest_outputs_from_path(const String &path, Array &r_outputs, bool require_cache_path = false) {
	Dictionary manifest;
	if (!load_manifest(path, manifest)) {
		return false;
	}

	Variant outputs_value = manifest.get("outputs", Array());
	if (outputs_value.get_type() != Variant::ARRAY) {
		return false;
	}

	Array outputs = outputs_value;
	if (!manifest_outputs_are_valid(outputs, require_cache_path)) {
		return false;
	}

	r_outputs = outputs;
	return true;
}

bool load_manifest_outputs(Array &r_outputs) {
	return load_manifest_outputs_from_path(manifest_path(), r_outputs, true);
}

bool load_cached_outputs(uint64_t input_mtime, const String &signature, Array &r_outputs) {
	Dictionary manifest;
	if (!load_compile_manifest(manifest)) {
		return false;
	}

	Variant outputs_value = manifest.get("outputs", Array());
	if (outputs_value.get_type() != Variant::ARRAY) {
		return false;
	}

	Array outputs = outputs_value;
	if (!manifest_outputs_are_valid(outputs, true)) {
		return false;
	}

	const int64_t manifest_input_mtime = int64_t(manifest.get("input_mtime", 0));
	if (manifest_input_mtime < int64_t(input_mtime)) {
		return false;
	}
	if (String(manifest.get("input_signature", String())) != signature) {
		return false;
	}

	if (!outputs_exist(outputs)) {
		return false;
	}

	r_outputs = outputs;
	return true;
}

void save_compile_manifest(const Array &outputs, uint64_t input_mtime, const String &signature) {
	Dictionary manifest;
	manifest["input_mtime"] = int64_t(input_mtime);
	manifest["input_signature"] = signature;
	manifest["outputs"] = outputs;

	if (!write_text_file(manifest_path(), JSON::stringify(manifest, "\t"))) {
		UtilityFunctions::printerr("[Gode TypeScript] Failed to write compile manifest: ", manifest_path());
	}
}

bool output_path_in_outputs(const Array &outputs, const String &path) {
	String normalized_path = path.replace("\\", "/");
	for (int64_t i = 0; i < outputs.size(); i++) {
		Variant output_value = outputs[i];
		if (output_value.get_type() != Variant::DICTIONARY) {
			continue;
		}

		Dictionary output = output_value;
		String output_path = String(output.get("path", String())).replace("\\", "/");
		if (output_path == normalized_path) {
			return true;
		}
	}
	return false;
}

void remove_cache_file_if_safe(const String &path) {
	if (path.is_empty()) {
		return;
	}

	String normalized_path = path.replace("\\", "/");
	if (!path_is_under_root(normalized_path, cache_root())) {
		return;
	}
	if (!normalized_path.ends_with(".js") && !normalized_path.ends_with(".js.map")) {
		return;
	}
	if (!FileAccess::file_exists(normalized_path)) {
		return;
	}

	Error remove_error = DirAccess::remove_absolute(normalized_path);
	if (remove_error != OK) {
		UtilityFunctions::printerr("[Gode TypeScript] Failed to remove stale cache output: ", normalized_path);
	}
}

void prune_stale_outputs(const Array &previous_outputs, const Array &current_outputs) {
	for (int64_t i = 0; i < previous_outputs.size(); i++) {
		Variant previous_output_value = previous_outputs[i];
		if (previous_output_value.get_type() != Variant::DICTIONARY) {
			continue;
		}

		Dictionary previous_output = previous_output_value;
		String output_path = previous_output.get("path", String());
		if (output_path.is_empty() || output_path_in_outputs(current_outputs, output_path)) {
			continue;
		}

		remove_cache_file_if_safe(output_path);
		remove_cache_file_if_safe(output_path + String(".map"));
	}
}

bool ensure_project_typescript_config(String *r_error) {
	if (FileAccess::file_exists(PROJECT_TYPESCRIPT_CONFIG_PATH)) {
		return true;
	}

	if (!FileAccess::file_exists(DEFAULT_TYPESCRIPT_CONFIG_PATH)) {
		if (r_error) {
			*r_error = "The packaged default TypeScript config is missing: " + String(DEFAULT_TYPESCRIPT_CONFIG_PATH);
		}
		return false;
	}

	String content = FileAccess::get_file_as_string(DEFAULT_TYPESCRIPT_CONFIG_PATH);
	if (FileAccess::get_open_error() != OK) {
		if (r_error) {
			*r_error = "Failed to read the packaged default TypeScript config: " + String(DEFAULT_TYPESCRIPT_CONFIG_PATH);
		}
		return false;
	}

	Ref<FileAccess> file = FileAccess::open(PROJECT_TYPESCRIPT_CONFIG_PATH, FileAccess::WRITE);
	if (file.is_null()) {
		if (r_error) {
			*r_error = "Project tsconfig.json is missing and Gode could not create " + String(PROJECT_TYPESCRIPT_CONFIG_PATH) + " from " + String(DEFAULT_TYPESCRIPT_CONFIG_PATH);
		}
		return false;
	}

	file->store_string(content);
	file->close();
	UtilityFunctions::print("[Gode TypeScript] Created default project config: ", PROJECT_TYPESCRIPT_CONFIG_PATH);
	return true;
}

Dictionary compile_project_internal(bool force) {
	if (!FileAccess::file_exists(TYPESCRIPT_RUNTIME_PATH)) {
		return make_result(false, "The packaged TypeScript compiler is missing: " + String(TYPESCRIPT_RUNTIME_PATH));
	}

	String config_error;
	if (!ensure_project_typescript_config(&config_error)) {
		return make_result(false, config_error);
	}

	Array source_diagnostics;
	Array sources = collect_project_sources(source_diagnostics);
	Dictionary result = make_result(true);
	if (!source_diagnostics.is_empty()) {
		result["ok"] = false;
		result["message"] = "Failed to read one or more TypeScript project sources.";
		result["diagnostics"] = source_diagnostics;
		return result;
	}

	uint64_t newest_input = newest_input_mtime(sources);
	String signature = input_signature(sources);
	Array cached_outputs;
	if (!force && load_cached_outputs(newest_input, signature, cached_outputs)) {
		result["outputs"] = cached_outputs;
		result["skipped"] = cached_outputs.size();
		return result;
	}

	Array previous_outputs;
	load_manifest_outputs(previous_outputs);

	Dictionary compile_result = NodeRuntime::compile_typescript_project(sources);
	Array diagnostics = compile_result.has("diagnostics") ? Array(compile_result["diagnostics"]) : Array();
	result["diagnostics"] = diagnostics;
	if (!bool(compile_result.get("ok", false))) {
		result["ok"] = false;
		result["message"] = "TypeScript compilation failed.";
		return result;
	}

	Array emitted_outputs = compile_result.has("outputs") ? Array(compile_result["outputs"]) : Array();
	Array actual_outputs;
	int64_t written_count = 0;
	for (int64_t i = 0; i < emitted_outputs.size(); i++) {
		Dictionary emitted = emitted_outputs[i];
		String source_path = emitted["source"];
		Dictionary output = output_mapping_for_source(source_path);
		String output_path = output["path"];
		String code = emitted["code"];
		String source_map = emitted.has("sourceMap") ? String(emitted["sourceMap"]) : String();
		String source_map_path = output_path + ".map";
		if (!write_text_file(output_path, code)) {
			return make_result(false, "Failed to write compiled TypeScript output: " + output_path);
		}
		if (!source_map.is_empty() && !write_text_file(source_map_path, source_map)) {
			return make_result(false, "Failed to write TypeScript source map: " + source_map_path);
		}
		if (source_map.is_empty() && FileAccess::file_exists(source_map_path)) {
			DirAccess::remove_absolute(source_map_path);
		}
		actual_outputs.append(output);
		written_count++;
	}

	result["outputs"] = actual_outputs;
	result["compiled"] = written_count;
	prune_stale_outputs(previous_outputs, actual_outputs);
	save_compile_manifest(actual_outputs, newest_input, signature);
	return result;
}

} // namespace

void GodeTypeScriptCompiler::_bind_methods() {
	ClassDB::bind_method(D_METHOD("compile_project", "force"), &GodeTypeScriptCompiler::compile_project, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("compile_script", "source_path", "force"), &GodeTypeScriptCompiler::compile_script, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_compiled_path", "source_path"), &GodeTypeScriptCompiler::get_compiled_path);
	ClassDB::bind_method(D_METHOD("get_exported_path", "source_path"), &GodeTypeScriptCompiler::get_exported_path);
	ClassDB::bind_method(D_METHOD("get_cache_root"), &GodeTypeScriptCompiler::get_cache_root);
}

Dictionary GodeTypeScriptCompiler::compile_project(bool p_force) {
	return compile_project_internal(p_force);
}

Dictionary GodeTypeScriptCompiler::compile_script(const String &p_source_path, bool p_force) {
	String source_path;
	String path_error;
	if (!normalize_typescript_source_path(p_source_path, source_path, &path_error)) {
		Dictionary result = make_result(false, path_error);
		result["source"] = String();
		result["path"] = String();
		result["exported_path"] = String();
		return result;
	}

	Dictionary result = compile_project_internal(p_force);
	result["source"] = source_path;
	result["path"] = compiled_path_for_source_internal(source_path);
	result["exported_path"] = exported_path_for_source_internal(source_path);
	if (bool(result.get("ok", false))) {
		Array outputs = result.has("outputs") ? Array(result["outputs"]) : Array();
		Dictionary output;
		if (output_for_source(outputs, source_path, output)) {
			result["path"] = output.get("path", result["path"]);
			result["exported_path"] = output.get("exported_path", result["exported_path"]);
		} else {
			append_error_diagnostic(result, String("Source was not emitted by the active TypeScript project: ") + source_path);
		}
	}
	return result;
}

String GodeTypeScriptCompiler::get_compiled_path(const String &p_source_path) const {
	String source_path;
	if (!normalize_typescript_source_path(p_source_path, source_path)) {
		return String();
	}
	return compiled_path_for_source_internal(source_path);
}

String GodeTypeScriptCompiler::get_exported_path(const String &p_source_path) const {
	String source_path;
	if (!normalize_typescript_source_path(p_source_path, source_path)) {
		return String();
	}
	return exported_path_for_source_internal(source_path);
}

String GodeTypeScriptCompiler::get_cache_root() const {
	return cache_root();
}

String GodeTypeScriptCompiler::compiled_path_for_source(const String &p_source_path) {
	String source_path;
	if (!normalize_typescript_source_path(p_source_path, source_path)) {
		return String();
	}
	return compiled_path_for_source_internal(source_path);
}

String GodeTypeScriptCompiler::exported_path_for_source(const String &p_source_path) {
	String source_path;
	if (!normalize_typescript_source_path(p_source_path, source_path)) {
		return String();
	}
	return exported_path_for_source_internal(source_path);
}

bool GodeTypeScriptCompiler::ensure_script_compiled(const String &p_source_path, String *r_compiled_path) {
	String source_path;
	String path_error;
	if (!normalize_typescript_source_path(p_source_path, source_path, &path_error)) {
		UtilityFunctions::printerr("[Gode TypeScript] ", path_error);
		return false;
	}
	String exported_path = exported_path_for_source_internal(source_path);
	Engine *engine = Engine::get_singleton();
	if (engine && !engine->is_editor_hint()) {
		Array exported_outputs;
		if (load_manifest_outputs_from_path(exported_manifest_path(), exported_outputs)) {
			Dictionary output;
			if (!output_for_source(exported_outputs, source_path, output)) {
				UtilityFunctions::printerr("[Gode TypeScript] Source was not included in the exported TypeScript manifest: ", source_path);
				return false;
			}

			String manifest_exported_path = output.get("exported_path", exported_path);
			if (!FileAccess::file_exists(manifest_exported_path)) {
				UtilityFunctions::printerr("[Gode TypeScript] Exported TypeScript output is missing: ", manifest_exported_path);
				return false;
			}

			if (r_compiled_path) {
				*r_compiled_path = manifest_exported_path;
			}
			return true;
		}
	}

	Dictionary result = compile_project_internal(false);
	if (!bool(result.get("ok", false))) {
		Array diagnostics = result.has("diagnostics") ? Array(result["diagnostics"]) : Array();
		print_diagnostics(diagnostics);
		return false;
	}

	Array outputs = result.has("outputs") ? Array(result["outputs"]) : Array();
	Dictionary output;
	if (!output_for_source(outputs, source_path, output)) {
		UtilityFunctions::printerr("[Gode TypeScript] Source was not emitted by the active TypeScript project: ", source_path);
		return false;
	}

	String compiled_path = output.get("path", compiled_path_for_source_internal(source_path));
	if (!FileAccess::file_exists(compiled_path)) {
		UtilityFunctions::printerr("[Gode TypeScript] Compiled output was not generated: ", compiled_path);
		return false;
	}

	if (r_compiled_path) {
		*r_compiled_path = compiled_path;
	}
	return true;
}

} // namespace gode
