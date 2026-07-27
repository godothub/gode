#include "script/typescript_script.h"
#include "compiler/typescript_compiler.h"
#include "runtime/node_runtime.h"
#include "runtime/value_convert.h"
#include "script/typescript_language.h"

#include <tree_sitter/api.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <limits>

using namespace godot;
using namespace gode;

extern "C" const TSLanguage *tree_sitter_typescript();

static std::string node_text(const std::string &source, TSNode node) {
	if (ts_node_is_null(node)) {
		return std::string();
	}
	uint32_t start = ts_node_start_byte(node);
	uint32_t end = ts_node_end_byte(node);
	return source.substr(start, end - start);
}

static std::string strip_quotes(std::string text) {
	if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
		return text.substr(1, text.size() - 2);
	}
	return text;
}

static String existing_source_candidate(const String &path, bool include_dts) {
	const String lower = path.to_lower();
	if (lower.ends_with(".d.ts")) {
		return include_dts && FileAccess::file_exists(path) ? path : String();
	}
	if ((lower.ends_with(".ts") || lower.ends_with(".tsx")) && FileAccess::file_exists(path)) {
		return path;
	}
	return String();
}

static String first_existing_source_candidate(const String &base, bool include_dts) {
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
		String resolved = existing_source_candidate(base.path_join("index.d.ts"), include_dts);
		if (!resolved.is_empty()) {
			return resolved;
		}
	}
	if (FileAccess::file_exists(base)) {
		return base;
	}
	return String();
}

static bool is_relative_module_specifier(const std::string &import_path) {
	return import_path.find("./") == 0 || import_path.find("../") == 0;
}

static String resolve_imported_typescript_path(const String &file_path, const std::string &import_path, bool include_dts) {
	if (!is_relative_module_specifier(import_path)) {
		return String();
	}
	const String base = file_path.get_base_dir().path_join(String(import_path.c_str())).replace("\\", "/").simplify_path();
	return first_existing_source_candidate(base, include_dts);
}

static String class_name_tail(String class_name) {
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

static StringName class_name_from_extends_node(TSNode node, const std::string &source) {
	const char *node_type = ts_node_type(node);
	if (strcmp(node_type, "identifier") != 0 &&
			strcmp(node_type, "member_expression") != 0 &&
			strcmp(node_type, "generic_type") != 0) {
		return StringName();
	}
	return StringName(class_name_tail(String(node_text(source, node).c_str())));
}

static StringName qualifier_from_extends_node(TSNode node, const std::string &source) {
	const char *node_type = ts_node_type(node);
	if (strcmp(node_type, "member_expression") != 0 && strcmp(node_type, "generic_type") != 0) {
		return StringName();
	}

	String expression = String(node_text(source, node).c_str()).strip_edges();
	const int64_t generic_start = expression.find("<");
	if (generic_start >= 0) {
		expression = expression.substr(0, generic_start).strip_edges();
	}
	const int64_t separator = expression.rfind(".");
	if (separator <= 0) {
		return StringName();
	}
	return StringName(expression.substr(0, separator).strip_edges());
}

static bool node_text_matches_name(TSNode node, const std::string &source, const StringName &name) {
	return !ts_node_is_null(node) && node_text(source, node) == String(name).utf8().get_data();
}

static StringName class_name_from_class_node(TSNode class_node, const std::string &source) {
	TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
	if (ts_node_is_null(name_node)) {
		return StringName();
	}
	return StringName(node_text(source, name_node).c_str());
}

static bool node_text_is_default(TSNode node, const std::string &source) {
	return !ts_node_is_null(node) && node_text(source, node) == "default";
}

static TSNode extends_class_node_from_class(TSNode class_node) {
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
				return candidate;
			}
		}
	}
	return {};
}

static StringName default_exported_name_from_clause(TSNode export_clause, const std::string &source) {
	for (uint32_t i = 0; i < ts_node_named_child_count(export_clause); i++) {
		TSNode specifier = ts_node_named_child(export_clause, i);
		if (strcmp(ts_node_type(specifier), "export_specifier") != 0) {
			continue;
		}

		TSNode alias_node = ts_node_child_by_field_name(specifier, "alias", 5);
		TSNode name_node = ts_node_child_by_field_name(specifier, "name", 4);
		if (!ts_node_is_null(alias_node)) {
			if (node_text_is_default(alias_node, source) && !ts_node_is_null(name_node)) {
				return StringName(node_text(source, name_node).c_str());
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
			return StringName(node_text(source, first_identifier).c_str());
		}
	}
	return StringName();
}

static StringName default_exported_class_name_from_statement(TSNode export_statement, const std::string &source) {
	bool is_default = false;
	for (uint32_t i = 0; i < ts_node_child_count(export_statement); i++) {
		TSNode child = ts_node_child(export_statement, i);
		const char *child_type = ts_node_type(child);
		if (strcmp(child_type, "default") == 0) {
			is_default = true;
			continue;
		}
		if (is_default && strcmp(child_type, "identifier") == 0) {
			return StringName(node_text(source, child).c_str());
		}
		if (strcmp(child_type, "export_clause") == 0) {
			StringName exported_name = default_exported_name_from_clause(child, source);
			if (!exported_name.is_empty()) {
				return exported_name;
			}
		}
	}
	return StringName();
}

static TSNode find_class_declaration_by_name(TSNode root_node, uint32_t child_count, const std::string &source, const StringName &name) {
	if (name.is_empty()) {
		return {};
	}

	for (uint32_t i = 0; i < child_count; i++) {
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

static bool import_specifier_binds_name(TSNode import_specifier, const std::string &source, const StringName &name) {
	TSNode alias_node = ts_node_child_by_field_name(import_specifier, "alias", 5);
	if (!ts_node_is_null(alias_node)) {
		return node_text_matches_name(alias_node, source, name);
	}

	TSNode name_node = ts_node_child_by_field_name(import_specifier, "name", 4);
	return node_text_matches_name(name_node, source, name);
}

static bool namespace_import_binds_qualifier(TSNode namespace_import, const std::string &source, const StringName &qualifier) {
	if (qualifier.is_empty()) {
		return false;
	}
	for (uint32_t i = 0; i < ts_node_named_child_count(namespace_import); i++) {
		if (node_text_matches_name(ts_node_named_child(namespace_import, i), source, qualifier)) {
			return true;
		}
	}
	return false;
}

static bool import_clause_binds_name(TSNode clause, const std::string &source, const StringName &name, const StringName &qualifier = StringName()) {
	if (name.is_empty() || ts_node_is_null(clause)) {
		return false;
	}

	for (uint32_t i = 0; i < ts_node_named_child_count(clause); i++) {
		TSNode child = ts_node_named_child(clause, i);
		const char *child_type = ts_node_type(child);
		if (strcmp(child_type, "namespace_import") == 0) {
			if (namespace_import_binds_qualifier(child, source, qualifier)) {
				return true;
			}
			continue;
		}
		if (!qualifier.is_empty()) {
			continue;
		}
		if (strcmp(child_type, "identifier") == 0) {
			if (node_text_matches_name(child, source, name)) {
				return true;
			}
		} else if (strcmp(child_type, "named_imports") == 0) {
			for (uint32_t j = 0; j < ts_node_named_child_count(child); j++) {
				TSNode imported = ts_node_named_child(child, j);
				if (strcmp(ts_node_type(imported), "import_specifier") == 0 && import_specifier_binds_name(imported, source, name)) {
					return true;
				}
			}
		}
	}
	return false;
}

static TSNode import_clause_from_statement(TSNode import_statement) {
	for (uint32_t i = 0; i < ts_node_child_count(import_statement); i++) {
		TSNode child = ts_node_child(import_statement, i);
		if (strcmp(ts_node_type(child), "import_clause") == 0) {
			return child;
		}
	}
	return {};
}

static String resolve_imported_class_path(const String &file_path, const std::string &source, TSNode root_node, uint32_t child_count, const StringName &class_name, const StringName &class_qualifier = StringName()) {
	if (class_name.is_empty()) {
		return String();
	}

	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "import_statement") != 0) {
			continue;
		}
		TSNode clause = import_clause_from_statement(child);
		if (!import_clause_binds_name(clause, source, class_name, class_qualifier)) {
			continue;
		}
		TSNode src = ts_node_child_by_field_name(child, "source", 6);
		if (ts_node_is_null(src)) {
			continue;
		}
		uint32_t ss = ts_node_start_byte(src);
		uint32_t se = ts_node_end_byte(src);
		std::string import_path = source.substr(ss + 1, se - ss - 2);
		return resolve_imported_typescript_path(file_path, import_path, false);
	}
	return String();
}

static std::string normalize_numeric_literal(std::string text) {
	std::string normalized;
	normalized.reserve(text.size());
	for (char c : text) {
		if (c != '_') {
			normalized.push_back(c);
		}
	}
	if (!normalized.empty() && normalized.back() == 'n') {
		normalized.pop_back();
	}
	return normalized;
}

static int numeric_digit_value(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return 10 + c - 'a';
	}
	if (c >= 'A' && c <= 'F') {
		return 10 + c - 'A';
	}
	return -1;
}

static bool parse_integer_literal(const std::string &text, int64_t &r_value) {
	std::string normalized = normalize_numeric_literal(text);
	if (normalized.empty()) {
		return false;
	}

	bool negative = false;
	size_t offset = 0;
	if (normalized[offset] == '+' || normalized[offset] == '-') {
		negative = normalized[offset] == '-';
		offset++;
	}
	if (offset >= normalized.size()) {
		return false;
	}

	int base = 10;
	if (offset + 1 < normalized.size() && normalized[offset] == '0') {
		const char prefix = normalized[offset + 1];
		if (prefix == 'b' || prefix == 'B') {
			base = 2;
			offset += 2;
		} else if (prefix == 'o' || prefix == 'O') {
			base = 8;
			offset += 2;
		} else if (prefix == 'x' || prefix == 'X') {
			base = 16;
			offset += 2;
		}
	}
	if (offset >= normalized.size()) {
		return false;
	}

	const uint64_t limit = negative ?
			static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1ULL :
			static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
	uint64_t value = 0;
	for (size_t i = offset; i < normalized.size(); i++) {
		const int digit = numeric_digit_value(normalized[i]);
		if (digit < 0 || digit >= base) {
			return false;
		}
		if (value > (limit - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base)) {
			return false;
		}
		value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
	}

	if (negative) {
		if (value == limit) {
			r_value = std::numeric_limits<int64_t>::min();
		} else {
			r_value = -static_cast<int64_t>(value);
		}
	} else {
		r_value = static_cast<int64_t>(value);
	}
	return true;
}

static bool parse_float_literal(const std::string &text, double &r_value) {
	std::string normalized = normalize_numeric_literal(text);
	if (normalized.empty()) {
		return false;
	}

	try {
		size_t parsed = 0;
		const double value = std::stod(normalized, &parsed);
		if (parsed != normalized.size() || !std::isfinite(value)) {
			return false;
		}
		r_value = value;
		return true;
	} catch (...) {
		return false;
	}
}

static bool parse_numeric_default(const std::string &text, Variant::Type property_type, Variant &r_value) {
	int64_t integer_value = 0;
	const std::string normalized = normalize_numeric_literal(text);
	const bool integer_candidate = normalized.find_first_of(".eE") == std::string::npos;
	if (integer_candidate && parse_integer_literal(text, integer_value)) {
		if (property_type == Variant::INT) {
			r_value = integer_value;
		} else {
			r_value = static_cast<double>(integer_value);
		}
		return true;
	}

	double float_value = 0.0;
	if (!parse_float_literal(text, float_value)) {
		return false;
	}
	if (property_type == Variant::INT) {
		if (std::trunc(float_value) != float_value ||
				float_value < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
				float_value > static_cast<double>(std::numeric_limits<int64_t>::max())) {
			return false;
		}
		r_value = static_cast<int64_t>(float_value);
	} else {
		r_value = float_value;
	}
	return true;
}

static Variant::Type parse_type_string(const std::string &type_str) {
	if (type_str == "bool" || type_str == "boolean" || type_str == "Boolean") {
		return Variant::BOOL;
	}
	if (type_str == "int") {
		return Variant::INT;
	}
	if (type_str == "float" || type_str == "number" || type_str == "Number") {
		return Variant::FLOAT;
	}
	if (type_str == "String" || type_str == "string") {
		return Variant::STRING;
	}
	if (type_str == "Vector2") {
		return Variant::VECTOR2;
	}
	if (type_str == "Vector2i") {
		return Variant::VECTOR2I;
	}
	if (type_str == "Vector3") {
		return Variant::VECTOR3;
	}
	if (type_str == "Vector3i") {
		return Variant::VECTOR3I;
	}
	if (type_str == "Vector4") {
		return Variant::VECTOR4;
	}
	if (type_str == "Vector4i") {
		return Variant::VECTOR4I;
	}
	if (type_str == "Color") {
		return Variant::COLOR;
	}
	if (type_str == "NodePath") {
		return Variant::NODE_PATH;
	}
	if (type_str == "Object") {
		return Variant::OBJECT;
	}
	return Variant::NIL;
}

static TSNode unwrap_metadata_expression(TSNode node) {
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
				const char *expression_type = ts_node_type(child);
				if (strcmp(expression_type, "type_arguments") != 0 &&
						strcmp(expression_type, "type_identifier") != 0 &&
						strcmp(expression_type, "predefined_type") != 0) {
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

static bool parse_default_value(TSNode value_node, const std::string &source, Variant::Type property_type, Variant &r_value);

static void collect_parent_properties(
		const StringName &parent_name,
		const StringName &parent_qualifier,
		const std::string &source,
		TSNode root_node,
		uint32_t child_count,
		const String &file_path,
		HashMap<StringName, PropertyInfo> &properties,
		Vector<PropertyInfo> &property_list,
		HashMap<StringName, Variant> &property_defaults) {
	if (parent_name.is_empty()) {
		return;
	}

	if (parent_qualifier.is_empty()) {
		for (uint32_t i = 0; i < child_count; i++) {
			TSNode child = ts_node_child(root_node, i);
			TSNode parent_node = { 0 };
			if (strcmp(ts_node_type(child), "export_statement") == 0) {
				for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
					TSNode en = ts_node_child(child, j);
					if (strcmp(ts_node_type(en), "class_declaration") == 0) {
						parent_node = en;
						break;
					}
				}
			} else if (strcmp(ts_node_type(child), "class_declaration") == 0) {
				parent_node = child;
			}
			if (!ts_node_is_null(parent_node)) {
				TSNode pname = ts_node_child_by_field_name(parent_node, "name", 4);
				if (!ts_node_is_null(pname)) {
					uint32_t ps = ts_node_start_byte(pname);
					uint32_t pe = ts_node_end_byte(pname);
					if (source.substr(ps, pe - ps) == String(parent_name).utf8().get_data()) {
						StringName grandparent;
						StringName grandparent_qualifier;
						TSNode grandparent_node = extends_class_node_from_class(parent_node);
						if (!ts_node_is_null(grandparent_node)) {
							grandparent = class_name_from_extends_node(grandparent_node, source);
							grandparent_qualifier = qualifier_from_extends_node(grandparent_node, source);
						}
						collect_parent_properties(grandparent, grandparent_qualifier, source, root_node, child_count, file_path, properties, property_list, property_defaults);
						TSNode pbody = ts_node_child_by_field_name(parent_node, "body", 4);
						for (uint32_t j = 0; j < ts_node_child_count(pbody); j++) {
							TSNode field = ts_node_child(pbody, j);
							if (strcmp(ts_node_type(field), "public_field_definition") != 0) {
								continue;
							}
							TSNode deco = ts_node_child_by_field_name(field, "decorator", 9);
							if (ts_node_is_null(deco)) {
								continue;
							}
							uint32_t ds = ts_node_start_byte(deco);
							uint32_t de = ts_node_end_byte(deco);
							if (source.substr(ds, de - ds).find("@Export") == std::string::npos) {
								continue;
							}
							TSNode fname = ts_node_child_by_field_name(field, "name", 4);
							if (ts_node_is_null(fname)) {
								continue;
							}
							uint32_t ns = ts_node_start_byte(fname);
							uint32_t ne = ts_node_end_byte(fname);
							StringName prop_name(source.substr(ns, ne - ns).c_str());
							if (properties.has(prop_name)) {
								continue;
							}
							PropertyInfo pi;
							pi.name = prop_name;
							pi.usage = PROPERTY_USAGE_DEFAULT;
							TSNode ftype = ts_node_child_by_field_name(field, "type", 4);
							if (!ts_node_is_null(ftype)) {
								ftype = ts_node_named_child(ftype, 0);
								uint32_t ts = ts_node_start_byte(ftype);
								uint32_t te = ts_node_end_byte(ftype);
								std::string type_str = source.substr(ts, te - ts);
								if (type_str == "string") {
									pi.type = Variant::STRING;
								} else if (type_str == "number") {
									pi.type = Variant::FLOAT;
								} else if (type_str == "boolean") {
									pi.type = Variant::BOOL;
								} else {
									pi.type = Variant::OBJECT;
								}
							}
							properties[prop_name] = pi;
							property_list.push_back(pi);
							TSNode fvalue = ts_node_child_by_field_name(field, "value", 5);
							if (!ts_node_is_null(fvalue)) {
								Variant default_value;
								if (parse_default_value(fvalue, source, pi.type, default_value)) {
									property_defaults[prop_name] = default_value;
								}
							}
						}
						return;
					}
				}
			}
		}
	}

	String ts_path = resolve_imported_class_path(file_path, source, root_node, child_count, parent_name, parent_qualifier);
	if (ts_path.is_empty()) {
		return;
	}
	Ref<Script> parent_script = ResourceLoader::get_singleton()->load(ts_path);
	if (parent_script.is_valid()) {
		Ref<TypeScriptScript> parent_ts = parent_script;
		if (parent_ts.is_valid() && parent_ts->_is_valid()) {
			HashSet<StringName> inherited_properties;
			for (const KeyValue<StringName, PropertyInfo> &E : parent_ts->get_exported_properties()) {
				if (!properties.has(E.key)) {
					properties[E.key] = E.value;
					inherited_properties.insert(E.key);
				}
			}
			for (const PropertyInfo &property : parent_ts->get_property_list_ordered()) {
				if (inherited_properties.has(property.name)) {
					property_list.push_back(property);
				}
			}
			for (const KeyValue<StringName, Variant> &E : parent_ts->get_property_defaults()) {
				if (!property_defaults.has(E.key)) {
					property_defaults[E.key] = E.value;
				}
			}
		}
	}
}

static void collect_interfaces_from_node(TSNode root_node, uint32_t child_count, const std::string &source, HashMap<StringName, Vector<PropertyInfo>> &interfaces);

// Recursively parse object literals and store values as prefix::key entries in property_defaults.
static void parse_object_defaults(TSNode obj_node, const std::string &source, const std::string &prefix, HashMap<StringName, Variant> &property_defaults) {
	for (uint32_t i = 0; i < ts_node_child_count(obj_node); i++) {
		TSNode pair = ts_node_child(obj_node, i);
		if (strcmp(ts_node_type(pair), "pair") != 0) {
			continue;
		}

		TSNode key = ts_node_child_by_field_name(pair, "key", 3);
		TSNode val = ts_node_child_by_field_name(pair, "value", 5);
		if (ts_node_is_null(key) || ts_node_is_null(val)) {
			continue;
		}
		val = unwrap_metadata_expression(val);

		uint32_t ks = ts_node_start_byte(key);
		uint32_t ke = ts_node_end_byte(key);
		std::string full_key = prefix + strip_quotes(source.substr(ks, ke - ks));
		StringName prop_name(full_key.c_str());

		const char *vt = ts_node_type(val);
		uint32_t vs = ts_node_start_byte(val);
		uint32_t ve = ts_node_end_byte(val);

		if (strcmp(vt, "object") == 0) {
			parse_object_defaults(val, source, full_key + "::", property_defaults);
		} else if (strcmp(vt, "string") == 0) {
			property_defaults[prop_name] = String(source.substr(vs + 1, ve - vs - 2).c_str());
		} else if (strcmp(vt, "number") == 0) {
			Variant default_value;
			if (parse_numeric_default(source.substr(vs, ve - vs), Variant::FLOAT, default_value)) {
				property_defaults[prop_name] = default_value;
			}
		} else if (strcmp(vt, "true") == 0) {
			property_defaults[prop_name] = true;
		} else if (strcmp(vt, "false") == 0) {
			property_defaults[prop_name] = false;
		}
	}
}

static HashMap<StringName, Vector<PropertyInfo>> parse_interfaces(TSNode root_node, uint32_t child_count, const std::string &source, const String &file_path) {
	HashMap<StringName, Vector<PropertyInfo>> interfaces;

	// Parse interfaces declared in the current file.
	collect_interfaces_from_node(root_node, child_count, source, interfaces);

	// Scan imports and load interfaces from external files.
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "import_statement") != 0) {
			continue;
		}

		TSNode src_node = ts_node_child_by_field_name(child, "source", 6);
		if (ts_node_is_null(src_node)) {
			continue;
		}

		uint32_t ss = ts_node_start_byte(src_node);
		uint32_t se = ts_node_end_byte(src_node);
		std::string import_path = source.substr(ss + 1, se - ss - 2);
		String ts_path = resolve_imported_typescript_path(file_path, import_path, true);
		if (ts_path.is_empty()) {
			continue;
		}

		String ext_src_str = FileAccess::get_file_as_string(ts_path);
		if (FileAccess::get_open_error() != OK) {
			continue;
		}
		std::string ext_src = ext_src_str.utf8().get_data();

		TSParser *ext_parser = ts_parser_new();
		if (!ext_parser) {
			continue;
		}
		ts_parser_set_language(ext_parser, tree_sitter_typescript());
		TSTree *ext_tree = ts_parser_parse_string(ext_parser, nullptr, ext_src.c_str(), ext_src.length());
		if (!ext_tree) {
			ts_parser_delete(ext_parser);
			continue;
		}
		TSNode ext_root = ts_tree_root_node(ext_tree);
		uint32_t ext_count = ts_node_child_count(ext_root);

		collect_interfaces_from_node(ext_root, ext_count, ext_src, interfaces);

		ts_tree_delete(ext_tree);
		ts_parser_delete(ext_parser);
	}

	return interfaces;
}

static void collect_interfaces_from_node(TSNode root_node, uint32_t child_count, const std::string &source, HashMap<StringName, Vector<PropertyInfo>> &interfaces) {
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		TSNode iface_node = { 0 };

		if (strcmp(ts_node_type(child), "interface_declaration") == 0) {
			iface_node = child;
		} else if (strcmp(ts_node_type(child), "export_statement") == 0) {
			for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
				TSNode en = ts_node_child(child, j);
				if (strcmp(ts_node_type(en), "interface_declaration") == 0) {
					iface_node = en;
					break;
				}
			}
		}

		if (ts_node_is_null(iface_node)) {
			continue;
		}

		TSNode name_node = ts_node_child_by_field_name(iface_node, "name", 4);
		if (ts_node_is_null(name_node)) {
			continue;
		}
		uint32_t ns = ts_node_start_byte(name_node);
		uint32_t ne = ts_node_end_byte(name_node);
		StringName iface_name(source.substr(ns, ne - ns).c_str());

		TSNode body = ts_node_child_by_field_name(iface_node, "body", 4);
		if (ts_node_is_null(body)) {
			continue;
		}

		Vector<PropertyInfo> fields;
		for (uint32_t j = 0; j < ts_node_child_count(body); j++) {
			TSNode member = ts_node_child(body, j);
			if (strcmp(ts_node_type(member), "property_signature") != 0) {
				continue;
			}

			TSNode field_name_node = ts_node_child_by_field_name(member, "name", 4);
			if (ts_node_is_null(field_name_node)) {
				continue;
			}
			uint32_t fns = ts_node_start_byte(field_name_node);
			uint32_t fne = ts_node_end_byte(field_name_node);

			PropertyInfo pi;
			pi.name = StringName(source.substr(fns, fne - fns).c_str());
			pi.usage = PROPERTY_USAGE_DEFAULT;
			pi.hint = PROPERTY_HINT_NONE;
			pi.type = Variant::NIL;

			TSNode type_node = ts_node_child_by_field_name(member, "type", 4);
			if (!ts_node_is_null(type_node)) {
				TSNode inner = ts_node_named_child(type_node, 0);
				if (!ts_node_is_null(inner)) {
					uint32_t ts = ts_node_start_byte(inner);
					uint32_t te = ts_node_end_byte(inner);
					std::string type_str = source.substr(ts, te - ts);
					pi.type = parse_type_string(type_str);
					// Preserve the raw type name so nested object detection can use it when the type is not a known Variant.
					if (pi.type == Variant::NIL) {
						pi.class_name = StringName(type_str.c_str());
					}
				}
			}

			fields.push_back(pi);
		}

		interfaces[iface_name] = fields;
	}
}

static TSNode find_default_class(TSNode root_node, uint32_t child_count, const std::string &source) {
	StringName exported_class_name;
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "export_statement") == 0) {
			bool is_default = false;
			for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
				TSNode en = ts_node_child(child, j);
				if (strcmp(ts_node_type(en), "default") == 0) {
					is_default = true;
				} else if (strcmp(ts_node_type(en), "class_declaration") == 0 && is_default) {
					return en;
				}
			}
			if (exported_class_name.is_empty()) {
				exported_class_name = default_exported_class_name_from_statement(child, source);
			}
		}
	}
	return find_class_declaration_by_name(root_node, child_count, source, exported_class_name);
}

static bool class_has_tool_decorator(TSNode class_node, const std::string &source) {
	if (ts_node_is_null(class_node)) {
		return false;
	}
	for (uint32_t i = 0; i < ts_node_child_count(class_node); i++) {
		TSNode child = ts_node_child(class_node, i);
		if (strcmp(ts_node_type(child), "decorator") != 0) {
			continue;
		}
		std::string decorator = node_text(source, child);
		if (decorator == "@Tool" || decorator == "@tool") {
			return true;
		}
	}
	return false;
}

// Check whether the default export class has an @Tool decorator. Runtime decorators are no-ops, so this is parsed statically with tree-sitter.
static bool check_tool_decorator(TSNode root_node, uint32_t child_count, const std::string &source) {
	if (class_has_tool_decorator(find_default_class(root_node, child_count, source), source)) {
		return true;
	}

	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "export_statement") != 0) {
			continue;
		}

		for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
			TSNode en = ts_node_child(child, j);
			const char *en_type = ts_node_type(en);
			// The decorator is attached to the export_statement.
			if (strcmp(en_type, "decorator") == 0) {
				uint32_t ds = ts_node_start_byte(en);
				uint32_t de = ts_node_end_byte(en);
				std::string deco = source.substr(ds, de - ds);
				if (deco == "@Tool" || deco == "@tool") {
					return true;
				}
			}
			// The decorator is attached inside the class_declaration.
			if (strcmp(en_type, "class_declaration") == 0) {
				for (uint32_t k = 0; k < ts_node_child_count(en); k++) {
					TSNode cn = ts_node_child(en, k);
					if (strcmp(ts_node_type(cn), "decorator") == 0) {
						uint32_t ds = ts_node_start_byte(cn);
						uint32_t de = ts_node_end_byte(cn);
						std::string deco = source.substr(ds, de - ds);
						if (deco == "@Tool" || deco == "@tool") {
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

static void parse_class_metadata(TSNode class_node, const std::string &source, StringName &class_name, StringName &base_class_name) {
	class_name = class_name_from_class_node(class_node, source);

	for (uint32_t j = 0; j < ts_node_child_count(class_node); j++) {
		TSNode cn = ts_node_child(class_node, j);
		cn = ts_node_named_child(cn, 0);
		if (!ts_node_is_null(cn) && strcmp(ts_node_type(cn), "extends_clause") == 0) {
			for (uint32_t k = 0; k < ts_node_named_child_count(cn); k++) {
				base_class_name = class_name_from_extends_node(ts_node_named_child(cn, k), source);
				if (!base_class_name.is_empty()) {
					return;
				}
			}
		}
	}
}

static void expand_interface_fields(
		const StringName &iface_name,
		const std::string &prefix,
		int depth,
		HashSet<StringName> &visited,
		const HashMap<StringName, Vector<PropertyInfo>> &interfaces,
		HashMap<StringName, PropertyInfo> &properties,
		Vector<PropertyInfo> &property_list) {
	if (visited.has(iface_name)) {
		return;
	}
	visited.insert(iface_name);

	PropertyInfo group_pi;
	group_pi.name = iface_name;
	group_pi.usage = depth == 0 ? PROPERTY_USAGE_GROUP : PROPERTY_USAGE_SUBGROUP;
	group_pi.hint_string = String(prefix.c_str());
	property_list.push_back(group_pi);

	for (const PropertyInfo &field : interfaces[iface_name]) {
		std::string sub_prefix = prefix + String(field.name).utf8().get_data();
		StringName sub_name(sub_prefix.c_str());
		std::string field_type = field.class_name.is_empty() ? "" : String(field.class_name).utf8().get_data();
		StringName nested_iface(field_type.c_str());
		if (!field_type.empty() && interfaces.has(nested_iface)) {
			expand_interface_fields(nested_iface, sub_prefix + "::", depth + 1, visited, interfaces, properties, property_list);
		} else {
			PropertyInfo sub_pi = field;
			sub_pi.name = sub_name;
			properties[sub_name] = sub_pi;
			property_list.push_back(sub_pi);
		}
	}

	visited.erase(iface_name);
}

static void parse_signal_params(TSNode func_type_node, const std::string &source, MethodInfo &mi) {
	// func_type_node is the function_type node: (params) => void.
	if (strcmp(ts_node_type(func_type_node), "function_type") != 0) {
		return;
	}

	// Find the formal_parameters child.
	TSNode params = { 0 };
	for (uint32_t i = 0; i < ts_node_child_count(func_type_node); i++) {
		TSNode c = ts_node_child(func_type_node, i);
		if (strcmp(ts_node_type(c), "formal_parameters") == 0) {
			params = c;
			break;
		}
	}
	if (ts_node_is_null(params)) {
		return;
	}

	for (uint32_t i = 0; i < ts_node_child_count(params); i++) {
		TSNode param = ts_node_child(params, i);
		const char *ptype = ts_node_type(param);
		if (strcmp(ptype, "required_parameter") != 0 && strcmp(ptype, "optional_parameter") != 0) {
			continue;
		}

		TSNode pattern = ts_node_child_by_field_name(param, "pattern", 7);
		if (ts_node_is_null(pattern)) {
			continue;
		}
		uint32_t ps = ts_node_start_byte(pattern);
		uint32_t pe = ts_node_end_byte(pattern);

		PropertyInfo arg_pi;
		arg_pi.name = StringName(source.substr(ps, pe - ps).c_str());
		arg_pi.usage = PROPERTY_USAGE_DEFAULT;
		arg_pi.hint = PROPERTY_HINT_NONE;
		arg_pi.type = Variant::NIL;

		// Find type_annotation by walking children instead of relying on field names.
		for (uint32_t j = 0; j < ts_node_child_count(param); j++) {
			TSNode pc = ts_node_child(param, j);
			if (strcmp(ts_node_type(pc), "type_annotation") == 0) {
				TSNode inner_type = ts_node_named_child(pc, 0);
				if (!ts_node_is_null(inner_type)) {
					uint32_t ts = ts_node_start_byte(inner_type);
					uint32_t te = ts_node_end_byte(inner_type);
					arg_pi.type = parse_type_string(source.substr(ts, te - ts));
				}
				break;
			}
		}

		mi.arguments.push_back(arg_pi);
	}
}

static void parse_signal_entry(const std::string &signal_name, TSNode value, const std::string &source, HashMap<StringName, MethodInfo> &signals) {
	MethodInfo mi;
	mi.name = StringName(signal_name.c_str());

	value = unwrap_metadata_expression(value);
	if (ts_node_is_null(value)) {
		signals[mi.name] = mi;
		return;
	}
	TSNode args_node = { 0 };
	if (strcmp(ts_node_type(value), "array") == 0) {
		args_node = value;
	} else if (strcmp(ts_node_type(value), "object") == 0) {
		for (uint32_t i = 0; i < ts_node_named_child_count(value); i++) {
			TSNode pair = ts_node_named_child(value, i);
			if (strcmp(ts_node_type(pair), "pair") != 0) {
				continue;
			}
			TSNode key = ts_node_child_by_field_name(pair, "key", 3);
			if (strip_quotes(node_text(source, key)) == "args") {
				args_node = ts_node_child_by_field_name(pair, "value", 5);
				args_node = unwrap_metadata_expression(args_node);
				break;
			}
		}
	}

	if (!ts_node_is_null(args_node) && strcmp(ts_node_type(args_node), "array") == 0) {
		for (uint32_t i = 0; i < ts_node_named_child_count(args_node); i++) {
			TSNode arg_node = unwrap_metadata_expression(ts_node_named_child(args_node, i));
			PropertyInfo arg;
			arg.type = Variant::NIL;
			arg.usage = PROPERTY_USAGE_DEFAULT;
			arg.hint = PROPERTY_HINT_NONE;

			if (strcmp(ts_node_type(arg_node), "string") == 0) {
				arg.name = StringName(strip_quotes(node_text(source, arg_node)).c_str());
			} else if (strcmp(ts_node_type(arg_node), "object") == 0) {
				for (uint32_t j = 0; j < ts_node_named_child_count(arg_node); j++) {
					TSNode pair = ts_node_named_child(arg_node, j);
					if (strcmp(ts_node_type(pair), "pair") != 0) {
						continue;
					}
					TSNode key = ts_node_child_by_field_name(pair, "key", 3);
					TSNode val = unwrap_metadata_expression(ts_node_child_by_field_name(pair, "value", 5));
					std::string key_text = strip_quotes(node_text(source, key));
					if (key_text == "name") {
						arg.name = StringName(strip_quotes(node_text(source, val)).c_str());
					} else if (key_text == "type") {
						arg.type = parse_type_string(strip_quotes(node_text(source, val)));
					}
				}
			}

			mi.arguments.push_back(arg);
		}
	}

	signals[mi.name] = mi;
}

static bool parse_integer_range(const std::string &text, int min_value, int max_value, int &r_value) {
	int64_t parsed = 0;
	if (!parse_integer_literal(text, parsed) || parsed < min_value || parsed > max_value) {
		return false;
	}
	r_value = static_cast<int>(parsed);
	return true;
}

static bool parse_non_negative_int(const std::string &text, int &r_value) {
	return parse_integer_range(text, 0, std::numeric_limits<int>::max(), r_value);
}

static bool parse_bool_literal(const std::string &text, bool &r_value) {
	if (text == "true") {
		r_value = true;
		return true;
	}
	if (text == "false") {
		r_value = false;
		return true;
	}
	return false;
}

static bool parse_rpc_mode(const std::string &mode, int &r_mode) {
	if (mode == "any_peer" || mode == "any") {
		r_mode = 1;
		return true;
	}
	if (mode == "authority" || mode == "master") {
		r_mode = 2;
		return true;
	}
	if (mode == "disabled") {
		r_mode = 0;
		return true;
	}
	return parse_integer_range(mode, 0, 2, r_mode);
}

static bool parse_transfer_mode(const std::string &mode, int &r_mode) {
	if (mode == "unreliable") {
		r_mode = 0;
		return true;
	}
	if (mode == "unreliable_ordered") {
		r_mode = 1;
		return true;
	}
	if (mode == "reliable") {
		r_mode = 2;
		return true;
	}
	return parse_integer_range(mode, 0, 2, r_mode);
}

static bool parse_int_metadata_value(TSNode value, const std::string &source, int &r_value) {
	value = unwrap_metadata_expression(value);
	if (ts_node_is_null(value)) {
		return false;
	}

	std::string value_text = node_text(source, value);
	if (strcmp(ts_node_type(value), "number") == 0) {
		return parse_non_negative_int(value_text, r_value);
	}

	Variant evaluated = NodeRuntime::eval_expression(value_text);
	if (evaluated.get_type() == Variant::INT) {
		const int64_t int_value = evaluated;
		if (int_value < 0 || int_value > std::numeric_limits<int>::max()) {
			return false;
		}
		r_value = static_cast<int>(int_value);
		return true;
	}
	if (evaluated.get_type() == Variant::FLOAT) {
		const double float_value = evaluated;
		if (!std::isfinite(float_value) || std::trunc(float_value) != float_value ||
				float_value < 0.0 || float_value > static_cast<double>(std::numeric_limits<int>::max())) {
			return false;
		}
		r_value = static_cast<int>(float_value);
		return true;
	}
	return false;
}

static bool parse_property_hint_value(TSNode value, const std::string &source, PropertyHint &r_hint) {
	int parsed = 0;
	if (!parse_int_metadata_value(value, source, parsed)) {
		return false;
	}
	r_hint = static_cast<PropertyHint>(parsed);
	return true;
}

static bool parse_metadata_string_value(TSNode value, const std::string &source, String &r_value) {
	value = unwrap_metadata_expression(value);
	if (ts_node_is_null(value) || strcmp(ts_node_type(value), "string") != 0) {
		return false;
	}
	r_value = String(strip_quotes(node_text(source, value)).c_str());
	return true;
}

static void parse_static_metadata(const std::string &name, TSNode value, const std::string &source, HashMap<StringName, MethodInfo> &signals, HashMap<StringName, Dictionary> &rpc_configs) {
	value = unwrap_metadata_expression(value);
	if (ts_node_is_null(value)) {
		return;
	}
	if (name == "signals" && strcmp(ts_node_type(value), "object") == 0) {
		for (uint32_t i = 0; i < ts_node_named_child_count(value); i++) {
			TSNode pair = ts_node_named_child(value, i);
			if (strcmp(ts_node_type(pair), "pair") != 0) {
				continue;
			}
			TSNode key = ts_node_child_by_field_name(pair, "key", 3);
			TSNode val = unwrap_metadata_expression(ts_node_child_by_field_name(pair, "value", 5));
			parse_signal_entry(strip_quotes(node_text(source, key)), val, source, signals);
		}
		return;
	}

	if ((name == "rpc_config" || name == "rpcs") && strcmp(ts_node_type(value), "object") == 0) {
		for (uint32_t i = 0; i < ts_node_named_child_count(value); i++) {
			TSNode pair = ts_node_named_child(value, i);
			if (strcmp(ts_node_type(pair), "pair") != 0) {
				continue;
			}
			StringName method(strip_quotes(node_text(source, ts_node_child_by_field_name(pair, "key", 3))).c_str());
			TSNode cfg_node = unwrap_metadata_expression(ts_node_child_by_field_name(pair, "value", 5));
			Dictionary cfg;
			cfg["rpc_mode"] = 2;
			cfg["transfer_mode"] = 2;
			cfg["call_local"] = false;
			cfg["channel"] = 0;
			if (strcmp(ts_node_type(cfg_node), "object") == 0) {
				for (uint32_t j = 0; j < ts_node_named_child_count(cfg_node); j++) {
					TSNode cfg_pair = ts_node_named_child(cfg_node, j);
					if (strcmp(ts_node_type(cfg_pair), "pair") != 0) {
						continue;
					}
					std::string key = strip_quotes(node_text(source, ts_node_child_by_field_name(cfg_pair, "key", 3)));
					TSNode val = unwrap_metadata_expression(ts_node_child_by_field_name(cfg_pair, "value", 5));
					std::string val_text = strip_quotes(node_text(source, val));
					if (key == "rpc_mode" || key == "mode") {
						int parsed_mode = 0;
						if (parse_rpc_mode(val_text, parsed_mode)) {
							cfg["rpc_mode"] = parsed_mode;
						}
					} else if (key == "transfer_mode" || key == "transferMode") {
						int parsed_mode = 0;
						if (parse_transfer_mode(val_text, parsed_mode)) {
							cfg["transfer_mode"] = parsed_mode;
						}
					} else if (key == "call_local" || key == "callLocal") {
						bool parsed_bool = false;
						if (parse_bool_literal(val_text, parsed_bool)) {
							cfg["call_local"] = parsed_bool;
						}
					} else if (key == "channel") {
						int parsed_channel = 0;
						if (parse_non_negative_int(val_text, parsed_channel)) {
							cfg["channel"] = parsed_channel;
						}
					}
				}
			}
			rpc_configs[method] = cfg;
		}
	}
}

static void parse_method_params(TSNode method_node, const std::string &source, MethodInfo &mi) {
	TSNode params = ts_node_child_by_field_name(method_node, "parameters", strlen("parameters"));
	if (ts_node_is_null(params)) {
		return;
	}

	for (uint32_t i = 0; i < ts_node_child_count(params); i++) {
		TSNode param = ts_node_child(params, i);
		const char *ptype = ts_node_type(param);
		if (strcmp(ptype, "required_parameter") != 0 && strcmp(ptype, "optional_parameter") != 0) {
			continue;
		}

		TSNode pattern = ts_node_child_by_field_name(param, "pattern", 7);
		if (ts_node_is_null(pattern)) {
			continue;
		}

		PropertyInfo arg_pi;
		uint32_t pattern_start = ts_node_start_byte(pattern);
		uint32_t pattern_end = ts_node_end_byte(pattern);
		arg_pi.name = StringName(source.substr(pattern_start, pattern_end - pattern_start).c_str());
		arg_pi.usage = PROPERTY_USAGE_DEFAULT;
		arg_pi.hint = PROPERTY_HINT_NONE;
		arg_pi.type = Variant::NIL;

		for (uint32_t j = 0; j < ts_node_child_count(param); j++) {
			TSNode child = ts_node_child(param, j);
			if (strcmp(ts_node_type(child), "type_annotation") == 0) {
				TSNode inner_type = ts_node_named_child(child, 0);
				if (!ts_node_is_null(inner_type)) {
					uint32_t type_start = ts_node_start_byte(inner_type);
					uint32_t type_end = ts_node_end_byte(inner_type);
					arg_pi.type = parse_type_string(source.substr(type_start, type_end - type_start));
				}
				break;
			}
		}

		mi.arguments.push_back(arg_pi);
	}
}

static void parse_class_members(TSNode class_node, const std::string &source, HashMap<StringName, PropertyInfo> &properties, Vector<PropertyInfo> &property_list, HashMap<StringName, Variant> &property_defaults, HashMap<StringName, MethodInfo> &methods, HashMap<StringName, MethodInfo> &signals, HashMap<StringName, Dictionary> &rpc_configs, HashMap<StringName, int> &member_lines, const HashMap<StringName, Vector<PropertyInfo>> &interfaces) {
	TSNode body_node = ts_node_child_by_field_name(class_node, "body", 4);
	if (ts_node_is_null(body_node)) {
		return;
	}

	for (uint32_t j = 0; j < ts_node_child_count(body_node); j++) {
		TSNode member = ts_node_child(body_node, j);
		const char *member_type = ts_node_type(member);

		if (strcmp(member_type, "public_field_definition") == 0) {
			bool is_static = false;
			for (uint32_t k = 0; k < ts_node_child_count(member); k++) {
				if (strcmp(ts_node_type(ts_node_child(member, k)), "static") == 0) {
					is_static = true;
					break;
				}
			}
			if (is_static) {
				TSNode field_name_node = ts_node_child_by_field_name(member, "name", 4);
				TSNode field_value_node = ts_node_child_by_field_name(member, "value", 5);
				if (!ts_node_is_null(field_name_node) && !ts_node_is_null(field_value_node)) {
					parse_static_metadata(strip_quotes(node_text(source, field_name_node)), field_value_node, source, signals, rpc_configs);
				}
			}

			// Scan decorators, detect @Export, and parse its arguments.
			bool has_export_decorator = false;
			PropertyHint export_hint = PROPERTY_HINT_NONE;
			String export_hint_string;
			for (uint32_t k = 0; k < ts_node_child_count(member); k++) {
				TSNode child = ts_node_child(member, k);
				if (strcmp(ts_node_type(child), "decorator") != 0) {
					continue;
				}
				uint32_t ds = ts_node_start_byte(child);
				uint32_t de = ts_node_end_byte(child);
				std::string deco_text = source.substr(ds, de - ds);
				if (deco_text.find("@Export") != 0) {
					continue;
				}
				has_export_decorator = true;
				// Parse @Export(...) arguments by locating call_expression or decorator_call_expression.
				for (uint32_t di = 0; di < ts_node_named_child_count(child); di++) {
					TSNode expr = ts_node_named_child(child, di);
					TSNode args = ts_node_child_by_field_name(expr, "arguments", 9);
					if (ts_node_is_null(args)) {
						break;
					}
					uint32_t nargs = ts_node_named_child_count(args);
					if (nargs == 0) {
						break;
					}
					TSNode first_arg = unwrap_metadata_expression(ts_node_named_child(args, 0));
					const char *first_type = ts_node_type(first_arg);
					if (strcmp(first_type, "object") == 0) {
						// @Export({ hint: N, hintString: "..." })
						for (uint32_t pi = 0; pi < ts_node_named_child_count(first_arg); pi++) {
							TSNode pair = ts_node_named_child(first_arg, pi);
							if (strcmp(ts_node_type(pair), "pair") != 0) {
								continue;
							}
							TSNode key = ts_node_child_by_field_name(pair, "key", 3);
							TSNode val = ts_node_child_by_field_name(pair, "value", 5);
							if (ts_node_is_null(key) || ts_node_is_null(val)) {
								continue;
							}
							val = unwrap_metadata_expression(val);
							if (ts_node_is_null(val)) {
								continue;
							}
							std::string key_str = strip_quotes(source.substr(ts_node_start_byte(key), ts_node_end_byte(key) - ts_node_start_byte(key)));
							if (key_str == "hint") {
								parse_property_hint_value(val, source, export_hint);
							} else if (key_str == "hintString" || key_str == "hint_string") {
								parse_metadata_string_value(val, source, export_hint_string);
							}
						}
					} else {
						// @Export(hint) or @Export(hint, "hintString").
						parse_property_hint_value(first_arg, source, export_hint);
						if (nargs >= 2) {
							TSNode second_arg = unwrap_metadata_expression(ts_node_named_child(args, 1));
							parse_metadata_string_value(second_arg, source, export_hint_string);
						}
					}
					break;
				}
			}

			// Signal<T> annotation: fieldName!: Signal<(...) => void>.
			TSNode type_anno = ts_node_child_by_field_name(member, "type", 4);
			if (!ts_node_is_null(type_anno)) {
				TSNode type_inner = ts_node_named_child(type_anno, 0);
				if (!ts_node_is_null(type_inner) && strcmp(ts_node_type(type_inner), "generic_type") == 0) {
					TSNode type_name_node = ts_node_child_by_field_name(type_inner, "name", 4);
					if (!ts_node_is_null(type_name_node)) {
						uint32_t tns = ts_node_start_byte(type_name_node);
						uint32_t tne = ts_node_end_byte(type_name_node);
						std::string type_name_str = source.substr(tns, tne - tns);
						if (type_name_str == "Signal") {
							TSNode name_node = ts_node_child_by_field_name(member, "name", 4);
							if (!ts_node_is_null(name_node)) {
								uint32_t ns = ts_node_start_byte(name_node);
								uint32_t ne = ts_node_end_byte(name_node);
								StringName signal_name(source.substr(ns, ne - ns).c_str());
								MethodInfo mi;
								mi.name = signal_name;
								// Extract function_type from Signal<T> type arguments and parse its parameters.
								TSNode type_args = ts_node_child_by_field_name(type_inner, "type_arguments", 14);
								if (!ts_node_is_null(type_args)) {
									for (uint32_t ti = 0; ti < ts_node_named_child_count(type_args); ti++) {
										TSNode targ = ts_node_named_child(type_args, ti);
										if (strcmp(ts_node_type(targ), "function_type") == 0) {
											parse_signal_params(targ, source, mi);
											break;
										}
									}
								}
								signals[signal_name] = mi;
							}
							continue;
						}
					}
				}
			}

			if (!has_export_decorator) {
				continue;
			}

			TSNode field_name_node = ts_node_child_by_field_name(member, "name", 4);
			TSNode field_value_node = ts_node_child_by_field_name(member, "value", 5);
			TSNode field_type_node = ts_node_child_by_field_name(member, "type", 4);

			if (ts_node_is_null(field_name_node)) {
				continue;
			}

			uint32_t fns = ts_node_start_byte(field_name_node);
			uint32_t fne = ts_node_end_byte(field_name_node);
			StringName field_name(source.substr(fns, fne - fns).c_str());

			PropertyInfo pi;
			pi.name = field_name;
			pi.usage = PROPERTY_USAGE_DEFAULT;
			pi.hint = export_hint;
			pi.hint_string = export_hint_string;
			pi.type = Variant::NIL;

			std::string type_str;
			if (!ts_node_is_null(field_type_node)) {
				TSNode inner_type = ts_node_named_child(field_type_node, 0);
				if (!ts_node_is_null(inner_type)) {
					uint32_t ts = ts_node_start_byte(inner_type);
					uint32_t te = ts_node_end_byte(inner_type);
					type_str = source.substr(ts, te - ts);
					pi.type = parse_type_string(type_str);
				}
			}

			StringName iface_key(type_str.c_str());
			if (!type_str.empty() && interfaces.has(iface_key)) {
				std::string prefix = String(field_name).utf8().get_data() + std::string("::");
				HashSet<StringName> visited;
				expand_interface_fields(iface_key, prefix, 0, visited, interfaces, properties, property_list);
				// Parse default values from field initializers.
				TSNode default_object_node = unwrap_metadata_expression(field_value_node);
				if (!ts_node_is_null(default_object_node) && strcmp(ts_node_type(default_object_node), "object") == 0) {
					parse_object_defaults(default_object_node, source, prefix, property_defaults);
				}
			} else {
				properties[field_name] = pi;
				property_list.push_back(pi);

				if (!ts_node_is_null(field_value_node)) {
					Variant default_value;
					if (parse_default_value(field_value_node, source, pi.type, default_value)) {
						property_defaults[field_name] = default_value;
					}
				}
			}
			continue;
		}

		if (strcmp(member_type, "method_definition") == 0) {
			TSNode mn = ts_node_child_by_field_name(member, "name", 4);
			if (ts_node_is_null(mn)) {
				continue;
			}

			uint32_t s = ts_node_start_byte(mn);
			uint32_t e = ts_node_end_byte(mn);
			StringName method_name(source.substr(s, e - s).c_str());
			if (method_name == StringName("constructor")) {
				continue;
			}

			bool is_static = false;
			for (uint32_t k = 0; k < ts_node_child_count(member); k++) {
				if (strcmp(ts_node_type(ts_node_child(member, k)), "static") == 0) {
					is_static = true;
					break;
				}
			}

			MethodInfo mi;
			mi.name = method_name;
			if (is_static) {
				mi.flags |= METHOD_FLAG_STATIC;
			}
			parse_method_params(member, source, mi);
			methods[method_name] = mi;
			member_lines[method_name] = ts_node_start_point(member).row + 1;
		}
	}
}

static bool parse_default_value(TSNode value_node, const std::string &source, Variant::Type property_type, Variant &r_value) {
	if (ts_node_is_null(value_node)) {
		return false;
	}

	value_node = unwrap_metadata_expression(value_node);
	if (ts_node_is_null(value_node)) {
		return false;
	}

	const char *value_type = ts_node_type(value_node);
	uint32_t value_start = ts_node_start_byte(value_node);
	uint32_t value_end = ts_node_end_byte(value_node);
	if (strcmp(value_type, "string") == 0) {
		r_value = String(source.substr(value_start + 1, value_end - value_start - 2).c_str());
		return true;
	}
	if (strcmp(value_type, "number") == 0) {
		return parse_numeric_default(source.substr(value_start, value_end - value_start), property_type, r_value);
	}
	if (strcmp(value_type, "true") == 0) {
		r_value = true;
		return true;
	}
	if (strcmp(value_type, "false") == 0) {
		r_value = false;
		return true;
	}
	if (strcmp(value_type, "new_expression") == 0 ||
			strcmp(value_type, "object") == 0 ||
			strcmp(value_type, "array") == 0) {
		r_value = NodeRuntime::eval_expression(source.substr(value_start, value_end - value_start));
		return true;
	}
	if (strcmp(value_type, "null") == 0) {
		r_value = Variant();
		return true;
	}
	return false;
}

static void upsert_ordered_property(const PropertyInfo &property, HashMap<StringName, PropertyInfo> &properties, Vector<PropertyInfo> &property_list) {
	const bool had_property = properties.has(property.name);
	properties[property.name] = property;
	if (!had_property) {
		property_list.push_back(property);
		return;
	}
	for (int64_t i = 0; i < property_list.size(); i++) {
		if (property_list[i].name == property.name) {
			property_list.set(i, property);
			return;
		}
	}
	property_list.push_back(property);
}

static void parse_exports_object(TSNode obj_node, const std::string &source, HashMap<StringName, PropertyInfo> &properties, Vector<PropertyInfo> &property_list, HashMap<StringName, Variant> &property_defaults) {
	// obj_node is the outer object. Each pair key is a property name and each value is a descriptor object: { type, default, ... }.
	for (uint32_t j = 0; j < ts_node_child_count(obj_node); j++) {
		TSNode pair = ts_node_child(obj_node, j);
		if (strcmp(ts_node_type(pair), "pair") != 0) {
			continue;
		}

		TSNode key = ts_node_child_by_field_name(pair, "key", 3);
		TSNode value = unwrap_metadata_expression(ts_node_child_by_field_name(pair, "value", 5));
		if (ts_node_is_null(key) || ts_node_is_null(value)) {
			continue;
		}
		if (strcmp(ts_node_type(value), "object") != 0) {
			continue;
		}

		uint32_t ks = ts_node_start_byte(key);
		uint32_t ke = ts_node_end_byte(key);
		StringName prop_name(strip_quotes(source.substr(ks, ke - ks)).c_str());

		PropertyInfo pi;
		pi.name = prop_name;
		pi.usage = PROPERTY_USAGE_DEFAULT;
		pi.hint = PROPERTY_HINT_NONE;
		pi.type = Variant::NIL;

		for (uint32_t k = 0; k < ts_node_child_count(value); k++) {
			TSNode field = ts_node_child(value, k);
			if (strcmp(ts_node_type(field), "pair") != 0) {
				continue;
			}

			TSNode fkey = ts_node_child_by_field_name(field, "key", 3);
			TSNode fval = unwrap_metadata_expression(ts_node_child_by_field_name(field, "value", 5));
			if (ts_node_is_null(fkey) || ts_node_is_null(fval)) {
				continue;
			}

			uint32_t fks = ts_node_start_byte(fkey);
			uint32_t fke = ts_node_end_byte(fkey);
			std::string field_key = strip_quotes(source.substr(fks, fke - fks));

			if (field_key == "type") {
				pi.type = parse_type_string(strip_quotes(node_text(source, fval)));
			} else if (field_key == "hint") {
				PropertyHint parsed_hint = PROPERTY_HINT_NONE;
				if (parse_property_hint_value(fval, source, parsed_hint)) {
					pi.hint = parsed_hint;
				}
			} else if (field_key == "hintString" || field_key == "hint_string") {
				parse_metadata_string_value(fval, source, pi.hint_string);
			} else if (field_key == "default") {
				Variant default_value;
				if (parse_default_value(fval, source, pi.type, default_value)) {
					property_defaults[prop_name] = default_value;
				}
			}
		}

		upsert_ordered_property(pi, properties, property_list);
	}
}

static void parse_exported_field_defaults(TSNode class_node, const std::string &source, const HashMap<StringName, PropertyInfo> &properties, HashMap<StringName, Variant> &property_defaults) {
	TSNode body = ts_node_child_by_field_name(class_node, "body", 4);
	if (ts_node_is_null(body)) {
		return;
	}

	for (uint32_t i = 0; i < ts_node_child_count(body); i++) {
		TSNode member = ts_node_child(body, i);
		if (strcmp(ts_node_type(member), "public_field_definition") != 0) {
			continue;
		}

		bool is_static = false;
		for (uint32_t j = 0; j < ts_node_child_count(member); j++) {
			if (strcmp(ts_node_type(ts_node_child(member, j)), "static") == 0) {
				is_static = true;
				break;
			}
		}
		if (is_static) {
			continue;
		}

		TSNode name = ts_node_child_by_field_name(member, "name", 4);
		TSNode value = ts_node_child_by_field_name(member, "value", 5);
		if (ts_node_is_null(name) || ts_node_is_null(value)) {
			continue;
		}

		StringName property_name(node_text(source, name).c_str());
		if (!properties.has(property_name) || property_defaults.has(property_name)) {
			continue;
		}

		const PropertyInfo *property = properties.getptr(property_name);
		Variant default_value;
		if (property && parse_default_value(value, source, property->type, default_value)) {
			property_defaults[property_name] = default_value;
		}
	}
}

// static exports = {...} appears in the TypeScript class body as a public_field_definition with a static modifier.
static void parse_static_exports(TSNode class_node, const std::string &source, HashMap<StringName, PropertyInfo> &properties, Vector<PropertyInfo> &property_list, HashMap<StringName, Variant> &property_defaults) {
	TSNode body = ts_node_child_by_field_name(class_node, "body", 4);
	if (ts_node_is_null(body)) {
		return;
	}

	for (uint32_t i = 0; i < ts_node_child_count(body); i++) {
		TSNode member = ts_node_child(body, i);
		if (strcmp(ts_node_type(member), "public_field_definition") != 0) {
			continue;
		}

		// Check for a static modifier.
		bool is_static = false;
		for (uint32_t j = 0; j < ts_node_child_count(member); j++) {
			if (strcmp(ts_node_type(ts_node_child(member, j)), "static") == 0) {
				is_static = true;
				break;
			}
		}
		if (!is_static) {
			continue;
		}

		// Check whether the field name is "exports".
		TSNode name = ts_node_child_by_field_name(member, "name", 4);
		if (ts_node_is_null(name)) {
			continue;
		}
		uint32_t ns = ts_node_start_byte(name);
		uint32_t ne = ts_node_end_byte(name);
		if (source.substr(ns, ne - ns) != "exports") {
			continue;
		}

		// Parse the value object literal.
		TSNode value = unwrap_metadata_expression(ts_node_child_by_field_name(member, "value", 5));
		if (ts_node_is_null(value) || strcmp(ts_node_type(value), "object") != 0) {
			continue;
		}

		parse_exports_object(value, source, properties, property_list, property_defaults);
		return;
	}
}

bool TypeScriptScript::compile() const {
	if (!is_dirty) {
		return is_valid;
	}

	if (!default_class.IsEmpty()) {
		if (NodeRuntime::is_running()) {
			v8::Locker locker(NodeRuntime::isolate);
			default_class.Reset();
		}
	}

	is_valid = false;
	is_tool_script = false;
	class_name = StringName();
	base_class_name = StringName();
	base_script_path = String();
	property_list.clear();
	methods.clear();
	signals.clear();
	rpc_configs.clear();
	properties.clear();
	property_defaults.clear();
	constants.clear();
	member_lines.clear();

	String path = get_path();
	if (path.is_empty()) {
		return false;
	}

	String js_path;
	if (!GodeTypeScriptCompiler::ensure_script_compiled(path, &js_path)) {
		return false;
	}

	TSParser *parser = ts_parser_new();
	if (!parser) {
		UtilityFunctions::printerr("[Gode TypeScript] Failed to create TypeScript metadata parser: ", path);
		is_valid = false;
		return false;
	}
	if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
		UtilityFunctions::printerr("[Gode TypeScript] Failed to configure TypeScript metadata parser: ", path);
		ts_parser_delete(parser);
		is_valid = false;
		return false;
	}

	std::string source = source_code.utf8().get_data();
	TSTree *tree = ts_parser_parse_string(parser, NULL, source.c_str(), source.length());
	if (!tree) {
		UtilityFunctions::printerr("[Gode TypeScript] Failed to parse TypeScript metadata: ", path);
		ts_parser_delete(parser);
		is_valid = false;
		return false;
	}
	TSNode root_node = ts_tree_root_node(tree);

	uint32_t child_count = ts_node_child_count(root_node);
	is_tool_script = check_tool_decorator(root_node, child_count, source);
	TSNode class_node = find_default_class(root_node, child_count, source);

	if (ts_node_is_null(class_node)) {
		ts_tree_delete(tree);
		ts_parser_delete(parser);
		is_valid = false;
		return false;
	}

	HashMap<StringName, Vector<PropertyInfo>> interfaces = parse_interfaces(root_node, child_count, source, get_path());
	parse_class_metadata(class_node, source, class_name, base_class_name);
	StringName base_class_qualifier;
	TSNode base_class_node = extends_class_node_from_class(class_node);
	if (!ts_node_is_null(base_class_node)) {
		base_class_qualifier = qualifier_from_extends_node(base_class_node, source);
	}
	base_script_path = resolve_imported_class_path(get_path(), source, root_node, child_count, base_class_name, base_class_qualifier);
	parse_class_members(class_node, source, properties, property_list, property_defaults, methods, signals, rpc_configs, member_lines, interfaces);
	parse_static_exports(class_node, source, properties, property_list, property_defaults);
	parse_exported_field_defaults(class_node, source, properties, property_defaults);
	collect_parent_properties(base_class_name, base_class_qualifier, source, root_node, child_count, get_path(), properties, property_list, property_defaults);

	ts_tree_delete(tree);
	ts_parser_delete(parser);

	if (!default_class.IsEmpty() && NodeRuntime::is_running()) {
		v8::Locker locker(NodeRuntime::isolate);
		default_class.Reset();
	}

	is_valid = true;
	is_dirty = false;
	return true;
}

Napi::Function TypeScriptScript::get_default_class() const {
	if (!compile()) {
		return Napi::Function();
	}

	if (!default_class.IsEmpty()) {
		return default_class.Value();
	}

	String path = get_path();
	if (path.is_empty()) {
		return Napi::Function();
	}

	String js_path;
	if (!GodeTypeScriptCompiler::ensure_script_compiled(path, &js_path)) {
		return Napi::Function();
	}

	String js_code = FileAccess::get_file_as_string(js_path);
	if (FileAccess::get_open_error() != OK) {
		UtilityFunctions::printerr("[Gode TypeScript] Failed to read compiled script: ", js_path);
		return Napi::Function();
	}
	Napi::Value exports = NodeRuntime::compile_script(js_code.utf8().get_data(), js_path.utf8().get_data());
	Napi::Function cls = NodeRuntime::get_default_class(exports);

	if (!cls.IsEmpty() && !cls.IsUndefined() && !cls.IsNull()) {
		const_cast<TypeScriptScript *>(this)->default_class = Napi::Persistent(cls);
		return cls;
	}

	return Napi::Function();
}

ScriptLanguage *TypeScriptScript::_get_language() const {
	return TypeScriptLanguage::get_singleton();
}
