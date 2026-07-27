#include "script/typescript_loader.h"
#include "godot_cpp/classes/resource_uid.hpp"
#include "script/typescript_script.h"
#include <tree_sitter/api.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/array.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace godot;
using namespace gode;

TypeScriptLoader *TypeScriptLoader::singleton = nullptr;

extern "C" const TSLanguage *tree_sitter_typescript();

namespace {

bool should_cache_loaded_script(int32_t p_cache_mode) {
	return p_cache_mode != ResourceLoader::CacheMode::CACHE_MODE_IGNORE &&
			p_cache_mode != ResourceLoader::CacheMode::CACHE_MODE_IGNORE_DEEP;
}

String normalize_load_path(const String &p_path, const String &p_original_path) {
	String path = p_path.is_empty() ? p_original_path : p_path;
	path = path.replace("\\", "/").simplify_path();
	if (path.begins_with("res://")) {
		return path;
	}

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return path;
	}

	String localized = project_settings->localize_path(path).replace("\\", "/").simplify_path();
	return localized.begins_with("res://") ? localized : path;
}

std::string node_text(const std::string &source, TSNode node) {
	if (ts_node_is_null(node)) {
		return std::string();
	}
	return source.substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node));
}

bool ascii_ends_with(const std::string &text, const char *suffix) {
	const size_t suffix_length = strlen(suffix);
	if (text.length() < suffix_length) {
		return false;
	}
	return text.compare(text.length() - suffix_length, suffix_length, suffix) == 0;
}

std::string ascii_lowercase(std::string text) {
	for (char &character : text) {
		if (character >= 'A' && character <= 'Z') {
			character = static_cast<char>(character - 'A' + 'a');
		}
	}
	return text;
}

bool is_relative_module_specifier(const std::string &specifier) {
	return specifier.find("./") == 0 || specifier.find("../") == 0;
}

String existing_source_candidate(const String &path, bool include_dts) {
	const String lower = path.to_lower();
	if (lower.ends_with(".d.ts")) {
		return include_dts && FileAccess::file_exists(path) ? path : String();
	}
	if ((lower.ends_with(".ts") || lower.ends_with(".tsx")) && FileAccess::file_exists(path)) {
		return path;
	}
	return String();
}

String first_existing_source_candidate(const String &base, bool include_dts) {
	const String lower = base.to_lower();
	if (lower.ends_with(".d.ts") || lower.ends_with(".ts") || lower.ends_with(".tsx")) {
		return existing_source_candidate(base, include_dts);
	}

	if (lower.ends_with(".jsx")) {
		const String stem = base.substr(0, base.length() - 4);
		String candidate = existing_source_candidate(stem + String(".tsx"), include_dts);
		if (!candidate.is_empty()) {
			return candidate;
		}
		candidate = existing_source_candidate(stem + String(".ts"), include_dts);
		if (!candidate.is_empty()) {
			return candidate;
		}
		if (include_dts) {
			candidate = existing_source_candidate(stem + String(".d.ts"), include_dts);
			if (!candidate.is_empty()) {
				return candidate;
			}
		}
		return FileAccess::file_exists(base) ? base : String();
	}

	for (const char *extension : { ".js", ".mjs", ".cjs" }) {
		if (!lower.ends_with(extension)) {
			continue;
		}
		const String stem = base.substr(0, base.length() - String(extension).length());
		String candidate = existing_source_candidate(stem + String(".ts"), include_dts);
		if (!candidate.is_empty()) {
			return candidate;
		}
		candidate = existing_source_candidate(stem + String(".tsx"), include_dts);
		if (!candidate.is_empty()) {
			return candidate;
		}
		if (include_dts) {
			candidate = existing_source_candidate(stem + String(".d.ts"), include_dts);
			if (!candidate.is_empty()) {
				return candidate;
			}
		}
		return FileAccess::file_exists(base) ? base : String();
	}

	for (const String &candidate : { base + String(".ts"), base + String(".tsx") }) {
		String resolved = existing_source_candidate(candidate, include_dts);
		if (!resolved.is_empty()) {
			return resolved;
		}
	}
	if (include_dts) {
		String resolved = existing_source_candidate(base + String(".d.ts"), include_dts);
		if (!resolved.is_empty()) {
			return resolved;
		}
	}
	for (const String &candidate : { base.path_join("index.ts"), base.path_join("index.tsx") }) {
		String resolved = existing_source_candidate(candidate, include_dts);
		if (!resolved.is_empty()) {
			return resolved;
		}
	}
	if (include_dts) {
		return existing_source_candidate(base.path_join("index.d.ts"), include_dts);
	}
	if (FileAccess::file_exists(base)) {
		return base;
	}
	return String();
}

String resolve_imported_typescript_path(const String &file_path, const std::string &import_path, bool include_dts) {
	if (!is_relative_module_specifier(import_path)) {
		return String();
	}
	const String base = file_path.get_base_dir().path_join(String(import_path.c_str())).replace("\\", "/").simplify_path();
	return first_existing_source_candidate(base, include_dts);
}

void append_resolved_dependency(
		const String &file_path,
		const std::string &specifier,
		bool include_dts,
		HashSet<String> &seen,
		PackedStringArray &dependencies) {
	String resolved = resolve_imported_typescript_path(file_path, specifier, include_dts);
	if (resolved.is_empty() || seen.has(resolved)) {
		return;
	}
	seen.insert(resolved);
	dependencies.push_back(resolved);
}

struct ImportSpecifierOccurrence {
	std::string specifier;
	uint32_t start = 0;
	uint32_t end = 0;
};

bool import_specifier_from_literal_node(TSNode node, const std::string &source, ImportSpecifierOccurrence &r_occurrence) {
	const char *node_type = ts_node_type(node);
	if (strcmp(node_type, "string") != 0 && strcmp(node_type, "template_string") != 0) {
		return false;
	}

	const std::string text = node_text(source, node);
	if (text.size() < 2) {
		return false;
	}

	const char quote = text.front();
	if (!((quote == '"' && text.back() == '"') ||
				(quote == '\'' && text.back() == '\'') ||
				(quote == '`' && text.back() == '`'))) {
		return false;
	}
	if (quote == '`' && text.find("${") != std::string::npos) {
		return false;
	}

	r_occurrence.start = ts_node_start_byte(node) + 1;
	r_occurrence.end = ts_node_end_byte(node) - 1;
	r_occurrence.specifier = text.substr(1, text.size() - 2);
	return true;
}

bool find_first_string_literal(TSNode node, const std::string &source, ImportSpecifierOccurrence &r_occurrence) {
	if (import_specifier_from_literal_node(node, source, r_occurrence)) {
		return true;
	}
	for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
		if (find_first_string_literal(ts_node_child(node, i), source, r_occurrence)) {
			return true;
		}
	}
	return false;
}

TSNode unwrap_import_specifier_expression(TSNode node) {
	while (!ts_node_is_null(node)) {
		const char *node_type = ts_node_type(node);
		if (strcmp(node_type, "parenthesized_expression") == 0 ||
				strcmp(node_type, "as_expression") == 0 ||
				strcmp(node_type, "satisfies_expression") == 0 ||
				strcmp(node_type, "non_null_expression") == 0) {
			TSNode expression = ts_node_named_child(node, 0);
			if (ts_node_is_null(expression)) {
				return node;
			}
			node = expression;
			continue;
		}
		if (strcmp(node_type, "type_assertion") == 0) {
			TSNode expression = {};
			for (int64_t i = static_cast<int64_t>(ts_node_named_child_count(node)) - 1; i >= 0; i--) {
				TSNode child = ts_node_named_child(node, static_cast<uint32_t>(i));
				const char *child_type = ts_node_type(child);
				if (strcmp(child_type, "type_arguments") != 0 &&
						strcmp(child_type, "type_identifier") != 0 &&
						strcmp(child_type, "predefined_type") != 0) {
					expression = child;
					break;
				}
			}
			if (ts_node_is_null(expression)) {
				return node;
			}
			node = expression;
			continue;
		}
		return node;
	}
	return node;
}

bool import_specifier_from_first_argument(TSNode node, const std::string &source, ImportSpecifierOccurrence &r_occurrence) {
	TSNode arguments = ts_node_child_by_field_name(node, "arguments", 9);
	if (ts_node_is_null(arguments) || ts_node_named_child_count(arguments) == 0) {
		return false;
	}
	TSNode specifier_node = unwrap_import_specifier_expression(ts_node_named_child(arguments, 0));
	return import_specifier_from_literal_node(specifier_node, source, r_occurrence);
}

void collect_dependency_specifier_occurrences(TSNode node, const std::string &source, std::vector<ImportSpecifierOccurrence> &occurrences) {
	const char *node_type = ts_node_type(node);
	if (strcmp(node_type, "import_statement") == 0 || strcmp(node_type, "export_statement") == 0) {
		TSNode source_node = ts_node_child_by_field_name(node, "source", 6);
		if (!ts_node_is_null(source_node)) {
			ImportSpecifierOccurrence occurrence;
			if (import_specifier_from_literal_node(source_node, source, occurrence)) {
				occurrences.push_back(occurrence);
			}
		}
	} else if (strcmp(node_type, "call_expression") == 0 && ts_node_child_count(node) > 0) {
		TSNode callee = ts_node_child(node, 0);
		if (node_text(source, callee) == "import") {
			ImportSpecifierOccurrence occurrence;
			if (import_specifier_from_first_argument(node, source, occurrence)) {
				occurrences.push_back(occurrence);
			}
		}
	}

	for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
		collect_dependency_specifier_occurrences(ts_node_child(node, i), source, occurrences);
	}
}

void collect_dependency_specifiers(TSNode node, const std::string &source, std::vector<std::string> &specifiers) {
	std::vector<ImportSpecifierOccurrence> occurrences;
	collect_dependency_specifier_occurrences(node, source, occurrences);
	specifiers.reserve(specifiers.size() + occurrences.size());
	for (const ImportSpecifierOccurrence &occurrence : occurrences) {
		specifiers.push_back(occurrence.specifier);
	}
}

String class_name_from_class_node(TSNode class_node, const std::string &source);
String default_exported_class_name_from_statement(TSNode export_statement, const std::string &source);
TSNode find_class_declaration_by_name(TSNode root_node, const std::string &source, const String &name);

TSNode find_default_resource_class(TSNode root_node, const std::string &source) {
	String exported_class_name;
	for (uint32_t i = 0; i < ts_node_child_count(root_node); i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "export_statement") != 0) {
			continue;
		}

		bool is_default = false;
		for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
			TSNode node = ts_node_child(child, j);
			if (strcmp(ts_node_type(node), "default") == 0) {
				is_default = true;
			} else if (strcmp(ts_node_type(node), "class_declaration") == 0 && is_default) {
				return node;
			}
		}
		if (exported_class_name.is_empty()) {
			exported_class_name = default_exported_class_name_from_statement(child, source);
		}
	}
	return find_class_declaration_by_name(root_node, source, exported_class_name);
}

String default_resource_class_name(TSNode root_node, const std::string &source) {
	TSNode class_node = find_default_resource_class(root_node, source);
	if (ts_node_is_null(class_node)) {
		return String();
	}
	return class_name_from_class_node(class_node, source);
}

String class_name_tail(String class_name) {
	class_name = class_name.strip_edges();
	const int64_t generic_start = class_name.find("<");
	if (generic_start >= 0) {
		class_name = class_name.substr(0, generic_start).strip_edges();
	}
	const int64_t namespace_separator = class_name.rfind(".");
	if (namespace_separator >= 0) {
		class_name = class_name.substr(namespace_separator + 1).strip_edges();
	}
	return class_name;
}

String class_name_from_class_node(TSNode class_node, const std::string &source) {
	TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
	if (ts_node_is_null(name_node)) {
		return String();
	}
	return String(node_text(source, name_node).c_str());
}

bool node_text_is_default(TSNode node, const std::string &source) {
	return !ts_node_is_null(node) && node_text(source, node) == "default";
}

String default_exported_name_from_clause(TSNode export_clause, const std::string &source) {
	for (uint32_t i = 0; i < ts_node_named_child_count(export_clause); i++) {
		TSNode specifier = ts_node_named_child(export_clause, i);
		if (strcmp(ts_node_type(specifier), "export_specifier") != 0) {
			continue;
		}

		TSNode alias_node = ts_node_child_by_field_name(specifier, "alias", 5);
		TSNode name_node = ts_node_child_by_field_name(specifier, "name", 4);
		if (!ts_node_is_null(alias_node)) {
			if (node_text_is_default(alias_node, source) && !ts_node_is_null(name_node)) {
				return String(node_text(source, name_node).c_str());
			}
			continue;
		}

		TSNode first_identifier = {};
		TSNode last_identifier = {};
		for (uint32_t j = 0; j < ts_node_named_child_count(specifier); j++) {
			TSNode child = ts_node_named_child(specifier, j);
			if (strcmp(ts_node_type(child), "identifier") != 0) {
				continue;
			}
			if (ts_node_is_null(first_identifier)) {
				first_identifier = child;
			}
			last_identifier = child;
		}
		if (!ts_node_is_null(first_identifier) && !ts_node_is_null(last_identifier) && node_text_is_default(last_identifier, source)) {
			return String(node_text(source, first_identifier).c_str());
		}
	}
	return String();
}

String default_exported_class_name_from_statement(TSNode export_statement, const std::string &source) {
	bool is_default = false;
	for (uint32_t i = 0; i < ts_node_child_count(export_statement); i++) {
		TSNode child = ts_node_child(export_statement, i);
		const char *child_type = ts_node_type(child);
		if (strcmp(child_type, "default") == 0) {
			is_default = true;
			continue;
		}
		if (is_default && strcmp(child_type, "identifier") == 0) {
			return String(node_text(source, child).c_str());
		}
		if (strcmp(child_type, "export_clause") == 0) {
			String exported_name = default_exported_name_from_clause(child, source);
			if (!exported_name.is_empty()) {
				return exported_name;
			}
		}
	}
	return String();
}

TSNode find_class_declaration_by_name(TSNode root_node, const std::string &source, const String &name) {
	if (name.is_empty()) {
		return {};
	}

	for (uint32_t i = 0; i < ts_node_child_count(root_node); i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "class_declaration") == 0) {
			if (class_name_from_class_node(child, source) == name) {
				return child;
			}
			continue;
		}
		if (strcmp(ts_node_type(child), "export_statement") != 0) {
			continue;
		}
		for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
			TSNode exported_child = ts_node_child(child, j);
			if (strcmp(ts_node_type(exported_child), "class_declaration") == 0 && class_name_from_class_node(exported_child, source) == name) {
				return exported_child;
			}
		}
	}
	return {};
}

String default_resource_base_class_name(TSNode root_node, const std::string &source) {
	TSNode class_node = find_default_resource_class(root_node, source);
	if (ts_node_is_null(class_node)) {
		return String();
	}

	for (uint32_t i = 0; i < ts_node_child_count(class_node); i++) {
		TSNode child = ts_node_child(class_node, i);
		child = ts_node_named_child(child, 0);
		if (ts_node_is_null(child) || strcmp(ts_node_type(child), "extends_clause") != 0) {
			continue;
		}
		for (uint32_t j = 0; j < ts_node_named_child_count(child); j++) {
			TSNode candidate = ts_node_named_child(child, j);
			const char *candidate_type = ts_node_type(candidate);
			if (strcmp(candidate_type, "identifier") == 0 ||
					strcmp(candidate_type, "member_expression") == 0 ||
					strcmp(candidate_type, "generic_type") == 0) {
				return class_name_tail(String(node_text(source, candidate).c_str()));
			}
		}
	}
	return String();
}

void append_unique_class_name(const String &class_name, HashSet<String> &seen, PackedStringArray &classes) {
	const String normalized = class_name_tail(class_name);
	if (normalized.is_empty() || seen.has(normalized)) {
		return;
	}
	seen.insert(normalized);
	classes.push_back(normalized);
}

TSNode import_clause_from_statement(TSNode import_statement) {
	for (uint32_t i = 0; i < ts_node_child_count(import_statement); i++) {
		TSNode child = ts_node_child(import_statement, i);
		if (strcmp(ts_node_type(child), "import_clause") == 0) {
			return child;
		}
	}
	return {};
}

void collect_godot_imported_classes(TSNode root_node, const std::string &source, HashSet<String> &seen, PackedStringArray &classes) {
	for (uint32_t i = 0; i < ts_node_child_count(root_node); i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "import_statement") != 0) {
			continue;
		}

		TSNode source_node = ts_node_child_by_field_name(child, "source", 6);
		ImportSpecifierOccurrence source_occurrence;
		if (ts_node_is_null(source_node) || !import_specifier_from_literal_node(source_node, source, source_occurrence) || source_occurrence.specifier != "godot") {
			continue;
		}

		TSNode clause = import_clause_from_statement(child);
		if (ts_node_is_null(clause)) {
			continue;
		}
		for (uint32_t j = 0; j < ts_node_named_child_count(clause); j++) {
			TSNode spec = ts_node_named_child(clause, j);
			if (strcmp(ts_node_type(spec), "named_imports") != 0) {
				continue;
			}
			for (uint32_t k = 0; k < ts_node_named_child_count(spec); k++) {
				TSNode imported = ts_node_named_child(spec, k);
				if (strcmp(ts_node_type(imported), "import_specifier") != 0) {
					continue;
				}
				TSNode name_node = ts_node_child_by_field_name(imported, "name", 4);
				if (!ts_node_is_null(name_node)) {
					append_unique_class_name(String(node_text(source, name_node).c_str()), seen, classes);
				}
			}
		}
	}
}

bool is_emittable_typescript_source(const String &path) {
	const String lower = path.to_lower();
	return !lower.ends_with(".d.ts") && (lower.ends_with(".ts") || lower.ends_with(".tsx"));
}

String source_to_runtime_output_path(const String &path) {
	const String lower = path.to_lower();
	if (lower.ends_with(".tsx")) {
		return path.substr(0, path.length() - 4) + String(".js");
	}
	if (!lower.ends_with(".d.ts") && lower.ends_with(".ts")) {
		return path.substr(0, path.length() - 3) + String(".js");
	}
	return path;
}

std::string resource_path_body(const String &path) {
	String normalized = path.replace("\\", "/").simplify_path();
	if (normalized.begins_with("res://")) {
		normalized = normalized.substr(String("res://").length());
	} else if (normalized.begins_with("user://")) {
		normalized = normalized.substr(String("user://").length());
	}
	return normalized.utf8().get_data();
}

std::vector<std::string> split_relative_path(const std::string &path) {
	std::vector<std::string> parts;
	size_t start = 0;
	while (start <= path.length()) {
		const size_t slash = path.find('/', start);
		const size_t end = slash == std::string::npos ? path.length() : slash;
		if (end > start) {
			const std::string part = path.substr(start, end - start);
			if (part != ".") {
				parts.push_back(part);
			}
		}
		if (slash == std::string::npos) {
			break;
		}
		start = slash + 1;
	}
	return parts;
}

std::string relative_module_path(const String &from_source_path, const String &target_path) {
	std::string from_body = resource_path_body(from_source_path);
	const size_t from_last_slash = from_body.find_last_of('/');
	if (from_last_slash == std::string::npos) {
		from_body.clear();
	} else {
		from_body = from_body.substr(0, from_last_slash);
	}

	const std::vector<std::string> from_parts = split_relative_path(from_body);
	const std::vector<std::string> target_parts = split_relative_path(resource_path_body(target_path));

	size_t common = 0;
	while (common < from_parts.size() && common < target_parts.size() && from_parts[common] == target_parts[common]) {
		common++;
	}

	std::string relative;
	for (size_t i = common; i < from_parts.size(); i++) {
		if (!relative.empty()) {
			relative += "/";
		}
		relative += "..";
	}
	for (size_t i = common; i < target_parts.size(); i++) {
		if (!relative.empty()) {
			relative += "/";
		}
		relative += target_parts[i];
	}

	if (relative.empty()) {
		relative = ".";
	}
	if (relative[0] != '.') {
		relative = "./" + relative;
	}
	return relative;
}

bool has_explicit_module_extension(const std::string &specifier) {
	const size_t slash = specifier.find_last_of('/');
	const size_t dot = specifier.find_last_of('.');
	return dot != std::string::npos && (slash == std::string::npos || dot > slash);
}

String remove_typescript_extension(const String &path) {
	const String lower = path.to_lower();
	if (lower.ends_with(".d.ts")) {
		return path.substr(0, path.length() - 5);
	}
	if (lower.ends_with(".tsx")) {
		return path.substr(0, path.length() - 4);
	}
	if (lower.ends_with(".ts")) {
		return path.substr(0, path.length() - 3);
	}
	return path;
}

String target_path_for_specifier_style(const String &target_path, const std::string &original_specifier) {
	const std::string lower_specifier = ascii_lowercase(original_specifier);
	if (is_emittable_typescript_source(target_path) &&
			(ascii_ends_with(lower_specifier, ".js") ||
					ascii_ends_with(lower_specifier, ".jsx") ||
					ascii_ends_with(lower_specifier, ".mjs") ||
					ascii_ends_with(lower_specifier, ".cjs"))) {
		return source_to_runtime_output_path(target_path);
	}
	if (!has_explicit_module_extension(original_specifier)) {
		return remove_typescript_extension(target_path);
	}
	return target_path;
}

HashMap<String, String> normalized_rename_map(const Dictionary &renames) {
	HashMap<String, String> normalized;
	Array keys = renames.keys();
	for (int64_t i = 0; i < keys.size(); i++) {
		const Variant &key = keys[i];
		const String old_path = normalize_load_path(String(key), String());
		const String new_path = normalize_load_path(String(renames[key]), String());
		if (old_path.is_empty() || new_path.is_empty()) {
			continue;
		}
		normalized[old_path] = new_path;
	}
	return normalized;
}

} // namespace

TypeScriptLoader *TypeScriptLoader::get_singleton() {
	if (singleton) {
		return singleton;
	}
	singleton = memnew(TypeScriptLoader);
	return singleton;
}

TypeScriptLoader::~TypeScriptLoader() {
	clear_cache();
	if (singleton == this) {
		singleton = nullptr;
	}
}

void TypeScriptLoader::clear_cache() {
	scripts.clear();
}

void TypeScriptLoader::reload_cached_scripts() {
	std::vector<std::pair<String, Ref<TypeScriptScript>>> cached_scripts;
	cached_scripts.reserve(scripts.size());
	for (const KeyValue<StringName, Ref<TypeScriptScript>> &E : scripts) {
		cached_scripts.push_back({ String(E.key), E.value });
	}

	for (const auto &cached_script : cached_scripts) {
		Ref<TypeScriptScript> script = cached_script.second;
		if (script.is_null()) {
			continue;
		}

		String source_code = FileAccess::get_file_as_string(cached_script.first);
		if (FileAccess::get_open_error() != OK) {
			source_code = script->_get_source_code();
		}
		script->reload_source_code(source_code, true);
	}
}

PackedStringArray TypeScriptLoader::_get_recognized_extensions() const {
	PackedStringArray arr;
	arr.push_back(String("ts"));
	arr.push_back(String("tsx"));
	return arr;
}

bool TypeScriptLoader::_recognize_path(const String &p_path, const StringName &p_type) const {
	String ext = p_path.get_extension().to_lower();
	return ext == String("ts") || ext == String("tsx");
}

bool TypeScriptLoader::_handles_type(const StringName &p_type) const {
	return p_type == StringName("Script");
}

String TypeScriptLoader::_get_resource_type(const String &p_path) const {
	String ext = p_path.get_extension().to_lower();
	if (ext == String("ts") || ext == String("tsx")) {
		return String("Script");
	}
	return String();
}

String TypeScriptLoader::_get_resource_script_class(const String &p_path) const {
	const String path = normalize_load_path(p_path, String());
	String source_string = FileAccess::get_file_as_string(path);
	if (FileAccess::get_open_error() != OK) {
		return String();
	}

	std::string source = source_string.utf8().get_data();
	TSParser *parser = ts_parser_new();
	if (!parser) {
		return String();
	}
	if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
		ts_parser_delete(parser);
		return String();
	}
	TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.length());
	if (!tree) {
		ts_parser_delete(parser);
		return String();
	}

	const String class_name = default_resource_class_name(ts_tree_root_node(tree), source);
	ts_tree_delete(tree);
	ts_parser_delete(parser);
	return class_name;
}

int64_t TypeScriptLoader::_get_resource_uid(const String &p_path) const {
	return ResourceUID::get_singleton()->text_to_id(p_path);
}

PackedStringArray TypeScriptLoader::_get_dependencies(const String &p_path, bool p_add_types) const {
	PackedStringArray dependencies;
	const String path = normalize_load_path(p_path, String());
	String source_string = FileAccess::get_file_as_string(path);
	if (FileAccess::get_open_error() != OK) {
		return dependencies;
	}

	std::string source = source_string.utf8().get_data();
	TSParser *parser = ts_parser_new();
	if (!parser) {
		return dependencies;
	}
	if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
		ts_parser_delete(parser);
		return dependencies;
	}
	TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.length());
	if (!tree) {
		ts_parser_delete(parser);
		return dependencies;
	}

	std::vector<std::string> specifiers;
	TSNode root_node = ts_tree_root_node(tree);
	collect_dependency_specifiers(root_node, source, specifiers);

	HashSet<String> seen;
	for (const std::string &specifier : specifiers) {
		append_resolved_dependency(path, specifier, p_add_types, seen, dependencies);
	}

	ts_tree_delete(tree);
	ts_parser_delete(parser);
	return dependencies;
}

Error TypeScriptLoader::_rename_dependencies(const String &p_path, const Dictionary &p_renames) const {
	if (p_renames.is_empty()) {
		return Error::OK;
	}

	const String path = normalize_load_path(p_path, String());
	String source_string = FileAccess::get_file_as_string(path);
	if (FileAccess::get_open_error() != OK) {
		return Error::ERR_CANT_OPEN;
	}

	const HashMap<String, String> renames = normalized_rename_map(p_renames);
	if (renames.is_empty()) {
		return Error::OK;
	}

	std::string source = source_string.utf8().get_data();
	TSParser *parser = ts_parser_new();
	if (!parser) {
		return Error::ERR_CANT_CREATE;
	}
	if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
		ts_parser_delete(parser);
		return Error::ERR_CANT_CREATE;
	}
	TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.length());
	if (!tree) {
		ts_parser_delete(parser);
		return Error::ERR_PARSE_ERROR;
	}

	std::vector<ImportSpecifierOccurrence> occurrences;
	TSNode root_node = ts_tree_root_node(tree);
	collect_dependency_specifier_occurrences(root_node, source, occurrences);

	std::vector<std::pair<ImportSpecifierOccurrence, std::string>> replacements;
	for (const ImportSpecifierOccurrence &occurrence : occurrences) {
		if (!is_relative_module_specifier(occurrence.specifier)) {
			continue;
		}

		const String resolved = resolve_imported_typescript_path(path, occurrence.specifier, true);
		if (resolved.is_empty() || !renames.has(resolved)) {
			continue;
		}

		const String target_path = target_path_for_specifier_style(renames[resolved], occurrence.specifier);
		std::string replacement = relative_module_path(path, target_path);
		if (replacement != occurrence.specifier) {
			replacements.push_back({ occurrence, replacement });
		}
	}

	ts_tree_delete(tree);
	ts_parser_delete(parser);

	if (replacements.empty()) {
		return Error::OK;
	}

	std::sort(replacements.begin(), replacements.end(), [](const auto &left, const auto &right) {
		return left.first.start > right.first.start;
	});
	for (const auto &replacement : replacements) {
		source.replace(replacement.first.start, replacement.first.end - replacement.first.start, replacement.second);
	}

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		return Error::ERR_CANT_OPEN;
	}
	file->store_string(String(source.c_str()));
	return Error::OK;
}

bool TypeScriptLoader::_exists(const String &p_path) const {
	return FileAccess::file_exists(p_path);
}

PackedStringArray TypeScriptLoader::_get_classes_used(const String &p_path) const {
	PackedStringArray classes;
	const String path = normalize_load_path(p_path, String());
	String source_string = FileAccess::get_file_as_string(path);
	if (FileAccess::get_open_error() != OK) {
		return classes;
	}

	std::string source = source_string.utf8().get_data();
	TSParser *parser = ts_parser_new();
	if (!parser) {
		return classes;
	}
	if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
		ts_parser_delete(parser);
		return classes;
	}
	TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.length());
	if (!tree) {
		ts_parser_delete(parser);
		return classes;
	}

	HashSet<String> seen;
	TSNode root_node = ts_tree_root_node(tree);
	append_unique_class_name(default_resource_base_class_name(root_node, source), seen, classes);
	collect_godot_imported_classes(root_node, source, seen, classes);

	ts_tree_delete(tree);
	ts_parser_delete(parser);
	return classes;
}

Variant TypeScriptLoader::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
	String load_path = normalize_load_path(p_path, p_original_path);
	String read_path = p_original_path.is_empty() ? load_path : p_original_path;
	StringName cache_key(load_path);

	if (p_cache_mode == ResourceLoader::CacheMode::CACHE_MODE_REUSE && scripts.has(cache_key)) {
		return scripts.get(cache_key);
	}

	String source_code = FileAccess::get_file_as_string(read_path);
	if (FileAccess::get_open_error() != OK) {
		return Error::ERR_CANT_OPEN;
	}

	TypeScriptScript *script = memnew(TypeScriptScript);
	script->set_path(load_path);
	script->_set_source_code(source_code);
	if (should_cache_loaded_script(p_cache_mode)) {
		scripts[cache_key] = Ref(script);
	}
	return script;
}
