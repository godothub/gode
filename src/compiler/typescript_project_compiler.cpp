#include "runtime/addon_paths.h"
#include "compiler/typescript_project_compiler.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

using namespace godot;

namespace gode {
namespace {

constexpr const char *TYPESCRIPT_COMPILER_BRIDGE_PATH = GODE_ADDON_RES_PATH "runtime/typescript_compiler.js";
constexpr const char *TYPESCRIPT_RUNTIME_PATH = GODE_ADDON_RES_PATH "tsc/lib/typescript.js";
constexpr const char *PROJECT_TYPESCRIPT_CONFIG_PATH = "res://tsconfig.json";
constexpr const char *DEFAULT_TYPESCRIPT_CONFIG_PATH = GODE_ADDON_RES_PATH "config/tsconfig.json";
constexpr const char *GODE_GLOBAL_TYPES_PATH = GODE_ADDON_RES_PATH "types/globals.d.ts";
constexpr const char *GODE_MODULE_TYPES_PATH = GODE_ADDON_RES_PATH "types/godot.d.ts";
constexpr const char *TYPESCRIPT_BUILD_ROOT = "res://.gode/build/typescript";
constexpr const char *GODE_RUNTIME_BRIDGE_CLASS = "GodeRuntimeBridge";
constexpr uint64_t FNV1A_64_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV1A_64_PRIME = 1099511628211ULL;

struct TypeScriptProjectCompileCache {
	bool has_result = false;
	uint64_t input_hash = 0;
	int64_t source_count = 0;
	Dictionary result;
};

std::mutex &typescript_compile_mutex() {
	static std::mutex mutex;
	return mutex;
}

TypeScriptProjectCompileCache &project_compile_cache() {
	static TypeScriptProjectCompileCache cache;
	return cache;
}

void reset_project_compile_cache() {
	TypeScriptProjectCompileCache &cache = project_compile_cache();
	cache.has_result = false;
	cache.input_hash = 0;
	cache.source_count = 0;
	cache.result = Dictionary();
}

std::string to_utf8_string(const String &value) {
	CharString utf8 = value.utf8();
	return std::string(utf8.get_data(), utf8.length());
}

void update_hash_byte(uint64_t &hash, uint8_t value) {
	hash ^= value;
	hash *= FNV1A_64_PRIME;
}

void update_hash_integer(uint64_t &hash, uint64_t value) {
	for (int i = 0; i < 8; i++) {
		update_hash_byte(hash, static_cast<uint8_t>((value >> (i * 8)) & 0xff));
	}
}

void update_hash_string(uint64_t &hash, const String &value) {
	CharString utf8 = value.utf8();
	const int64_t length = utf8.length();
	update_hash_integer(hash, static_cast<uint64_t>(length));
	const char *data = utf8.get_data();
	for (int64_t i = 0; i < length; i++) {
		update_hash_byte(hash, static_cast<uint8_t>(data[i]));
	}
}

void update_hash_file(uint64_t &hash, const String &path) {
	update_hash_string(hash, path);
	if (!FileAccess::file_exists(path)) {
		update_hash_string(hash, "<missing>");
		return;
	}

	String content = FileAccess::get_file_as_string(path);
	if (FileAccess::get_open_error() != OK) {
		update_hash_string(hash, "<unreadable>");
		return;
	}
	update_hash_string(hash, content);
}

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

String cache_root() {
	return TYPESCRIPT_BUILD_ROOT;
}

String exported_build_root() {
	return TYPESCRIPT_BUILD_ROOT;
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
		GODE_ADDON_REL_PATH "tsc",
		GODE_ADDON_REL_PATH "types"
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

Array sorted_sources_by_path(const Array &sources) {
	std::vector<Dictionary> sorted;
	sorted.reserve(sources.size());
	for (int64_t i = 0; i < sources.size(); i++) {
		Variant value = sources[i];
		if (value.get_type() == Variant::DICTIONARY) {
			sorted.push_back(Dictionary(value));
		}
	}

	std::sort(sorted.begin(), sorted.end(), [](const Dictionary &left, const Dictionary &right) {
		return to_utf8_string(String(left.get("path", String()))) < to_utf8_string(String(right.get("path", String())));
	});

	Array result;
	for (const Dictionary &source : sorted) {
		result.append(source);
	}
	return result;
}

uint64_t project_input_hash(const Array &sources) {
	uint64_t hash = FNV1A_64_OFFSET_BASIS;
	update_hash_string(hash, "gode-typescript-project-v1");
	update_hash_integer(hash, static_cast<uint64_t>(sources.size()));
	for (int64_t i = 0; i < sources.size(); i++) {
		Dictionary source = sources[i];
		update_hash_string(hash, String(source.get("path", String())));
		update_hash_string(hash, String(source.get("source", String())));
	}

	static const char *signature_file_paths[] = {
		PROJECT_TYPESCRIPT_CONFIG_PATH,
		GODE_MODULE_TYPES_PATH,
		TYPESCRIPT_COMPILER_BRIDGE_PATH,
		"res://package.json",
		"res://package-lock.json",
		"res://npm-shrinkwrap.json",
		"res://pnpm-lock.yaml",
		"res://yarn.lock",
		"res://bun.lock",
		"res://bun.lockb",
		"res://.npmrc",
	};
	for (const char *path : signature_file_paths) {
		update_hash_file(hash, path);
	}
	return hash;
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

bool output_entry_is_valid(const Variant &output_value) {
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
	if (output.has("path")) {
		String output_path = output["path"];
		if (!output_string_field_is_valid(output, "path") ||
				!path_is_under_root(output_path, cache_root()) ||
				!path_has_extension(output_path, ".js")) {
			return false;
		}
	}
	return true;
}

bool output_for_source(const Array &outputs, const String &source_path, Dictionary &r_output) {
	const String normalized_source_path = normalize_project_path(source_path);
	for (int64_t i = 0; i < outputs.size(); i++) {
		Variant output_value = outputs[i];
		if (!output_entry_is_valid(output_value)) {
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

bool compile_result_outputs_are_present(const Dictionary &result) {
	if (!bool(result.get("ok", false))) {
		return true;
	}

	Array outputs = result.has("outputs") ? Array(result["outputs"]) : Array();
	for (int64_t i = 0; i < outputs.size(); i++) {
		Variant output_value = outputs[i];
		if (!output_entry_is_valid(output_value)) {
			return false;
		}
		Dictionary output = output_value;
		String output_path = output.get("path", String());
		if (output_path.is_empty() || !FileAccess::file_exists(output_path)) {
			return false;
		}
	}
	return true;
}

bool compile_failure_is_cacheable(const Dictionary &compile_result) {
	Array outputs = compile_result.has("outputs") ? Array(compile_result["outputs"]) : Array();
	if (!outputs.is_empty()) {
		return true;
	}

	Array diagnostics = compile_result.has("diagnostics") ? Array(compile_result["diagnostics"]) : Array();
	for (int64_t i = 0; i < diagnostics.size(); i++) {
		Variant diagnostic_value = diagnostics[i];
		if (diagnostic_value.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary diagnostic = diagnostic_value;
		if (!String(diagnostic.get("file", String())).is_empty()) {
			return true;
		}
		if (int64_t(diagnostic.get("code", 0)) != 0) {
			return true;
		}
	}
	return false;
}

Dictionary duplicate_compile_result(const Dictionary &result) {
	return result.duplicate(true);
}

Dictionary make_result(bool ok, const String &message = String()) {
	Dictionary result;
	result["ok"] = ok;
	result["compiled"] = 0;
	result["outputs"] = Array();
	result["diagnostics"] = Array();
	result["cache_root"] = cache_root();
	result["output_root"] = cache_root();
	result["cached"] = false;
	result["retryable"] = !ok;
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

bool remove_generated_file_if_safe(const String &path) {
	if (path.is_empty()) {
		return true;
	}

	String normalized_path = path.replace("\\", "/");
	if (!path_is_under_root(normalized_path, cache_root())) {
		return false;
	}
	if (!FileAccess::file_exists(normalized_path)) {
		return true;
	}

	Error remove_error = DirAccess::remove_absolute(normalized_path);
	if (remove_error != OK) {
		UtilityFunctions::printerr("[Gode TypeScript] Failed to remove generated output: ", normalized_path);
		return false;
	}
	return true;
}

bool clear_generated_directory_contents(const String &directory_path) {
	if (!path_is_under_root(directory_path, cache_root())) {
		return false;
	}
	if (!DirAccess::dir_exists_absolute(directory_path)) {
		return true;
	}

	PackedStringArray files = DirAccess::get_files_at(directory_path);
	for (int64_t i = 0; i < files.size(); i++) {
		String file_path = directory_path.path_join(files[i]);
		if (!remove_generated_file_if_safe(file_path)) {
			return false;
		}
	}

	PackedStringArray directories = DirAccess::get_directories_at(directory_path);
	for (int64_t i = 0; i < directories.size(); i++) {
		String child_path = directory_path.path_join(directories[i]);
		if (!clear_generated_directory_contents(child_path)) {
			return false;
		}
		Error remove_error = DirAccess::remove_absolute(child_path);
		if (remove_error != OK) {
			UtilityFunctions::printerr("[Gode TypeScript] Failed to remove generated output directory: ", child_path);
			return false;
		}
	}
	return true;
}

bool clear_generated_output_root() {
	return clear_generated_directory_contents(cache_root());
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

Dictionary compile_sources_with_runtime(const Array &sources) {
	const StringName bridge_class(GODE_RUNTIME_BRIDGE_CLASS);
	const StringName compile_method("compile_typescript_project");
	if (!ClassDB::class_exists(bridge_class) || !ClassDB::class_has_method(bridge_class, compile_method)) {
		return make_result(false, "Gode runtime TypeScript compiler bridge is not loaded.");
	}

	Variant result = ClassDB::class_call_static(bridge_class, compile_method, sources);
	if (result.get_type() != Variant::DICTIONARY) {
		return make_result(false, "Gode runtime TypeScript compiler bridge returned an invalid result.");
	}
	return result;
}

Dictionary compile_project_internal(bool force) {
	std::lock_guard<std::mutex> compile_lock(typescript_compile_mutex());

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
	sources = sorted_sources_by_path(sources);

	const uint64_t input_hash = project_input_hash(sources);
	TypeScriptProjectCompileCache &cache = project_compile_cache();
	if (!force &&
			cache.has_result &&
			cache.input_hash == input_hash &&
			cache.source_count == sources.size() &&
			compile_result_outputs_are_present(cache.result)) {
		Dictionary cached_result = duplicate_compile_result(cache.result);
		cached_result["cached"] = true;
		cached_result["compiled"] = 0;
		return cached_result;
	}

	if (!clear_generated_output_root()) {
		return make_result(false, "Failed to clear generated TypeScript output: " + cache_root());
	}

	Dictionary compile_result = compile_sources_with_runtime(sources);
	Array diagnostics = compile_result.has("diagnostics") ? Array(compile_result["diagnostics"]) : Array();
	result["diagnostics"] = diagnostics;
	if (!bool(compile_result.get("ok", false))) {
		const bool cacheable_failure = compile_failure_is_cacheable(compile_result);
		result["ok"] = false;
		result["message"] = "TypeScript compilation failed.";
		result["cached"] = false;
		result["retryable"] = !cacheable_failure;
		if (cacheable_failure) {
			cache.has_result = true;
			cache.input_hash = input_hash;
			cache.source_count = sources.size();
			cache.result = duplicate_compile_result(result);
		}
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
		actual_outputs.append(output);
		written_count++;
	}

	result["outputs"] = actual_outputs;
	result["compiled"] = written_count;
	result["cached"] = false;
	result["retryable"] = false;

	cache.has_result = true;
	cache.input_hash = input_hash;
	cache.source_count = sources.size();
	cache.result = duplicate_compile_result(result);
	return result;
}

} // namespace

Dictionary compile_typescript_project(bool p_force) {
	return compile_project_internal(p_force);
}

Dictionary compile_typescript_source(const String &p_source_path, bool p_force) {
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

String get_typescript_compiled_path(const String &p_source_path) {
	String source_path;
	if (!normalize_typescript_source_path(p_source_path, source_path)) {
		return String();
	}
	return compiled_path_for_source_internal(source_path);
}

String get_typescript_exported_path(const String &p_source_path) {
	String source_path;
	if (!normalize_typescript_source_path(p_source_path, source_path)) {
		return String();
	}
	return exported_path_for_source_internal(source_path);
}

String get_typescript_cache_root() {
	return cache_root();
}

void clear_typescript_project_compile_cache() {
	std::lock_guard<std::mutex> compile_lock(typescript_compile_mutex());
	reset_project_compile_cache();
}

} // namespace gode
