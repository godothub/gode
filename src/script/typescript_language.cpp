#include "script/typescript_language.h"
#include "script/typescript_loader.h"
#include "script/typescript_script.h"
#include <tree_sitter/api.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/array.hpp>

#include <cstring>
#include <string>

using namespace godot;
using namespace gode;

TypeScriptLanguage *TypeScriptLanguage::singleton = nullptr;

extern "C" const TSLanguage *tree_sitter_typescript();

namespace {

constexpr const char *TS_RESERVED_WORDS[] = {
	"abstract", "any", "as", "asserts", "async", "await", "bigint", "boolean",
	"break", "case", "catch", "class", "const", "constructor", "continue",
	"debugger", "declare", "default", "delete", "do", "else", "enum", "export",
	"extends", "false", "finally", "for", "from", "function", "get", "global",
	"if", "implements", "import", "in", "infer", "instanceof", "interface",
	"is", "keyof", "let", "module", "namespace", "never", "new", "null",
	"number", "object", "of", "out", "override", "package", "private",
	"protected", "public", "readonly", "require", "return", "satisfies", "set",
	"static", "string", "super", "switch", "symbol", "this", "throw", "true",
	"try", "type", "typeof", "undefined", "unique", "unknown", "var", "void",
	"while", "with", "yield"
};

constexpr const char *TS_CONTROL_FLOW_WORDS[] = {
	"await", "break", "case", "catch", "continue", "default", "do", "else",
	"finally", "for", "if", "return", "switch", "throw", "try", "while", "yield"
};

bool contains_word(const String &p_word, const char *const *p_words, size_t p_count) {
	const String word = p_word.strip_edges();
	for (size_t i = 0; i < p_count; i++) {
		if (word == String(p_words[i])) {
			return true;
		}
	}
	return false;
}

bool is_reserved_word(const String &p_word) {
	return contains_word(p_word, TS_RESERVED_WORDS, sizeof(TS_RESERVED_WORDS) / sizeof(TS_RESERVED_WORDS[0]));
}

bool is_ascii_identifier_start(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

bool is_ascii_identifier_part(char c) {
	return is_ascii_identifier_start(c) || (c >= '0' && c <= '9');
}

String sanitize_typescript_identifier(const String &p_name, const String &p_fallback) {
	std::string input = p_name.utf8().get_data();
	std::string output;
	output.reserve(input.size() + 1);

	for (char c : input) {
		output.push_back(is_ascii_identifier_part(c) ? c : '_');
	}

	if (output.empty()) {
		output = p_fallback.utf8().get_data();
	}
	if (output.empty()) {
		output = "identifier";
	}
	if (!is_ascii_identifier_start(output[0])) {
		output.insert(output.begin(), '_');
	}

	String identifier(output.c_str());
	if (is_reserved_word(identifier)) {
		identifier += "_gd";
	}
	return identifier;
}

bool path_has_parent_segment(const String &p_path) {
	PackedStringArray segments = p_path.replace("\\", "/").split("/", false);
	for (int64_t i = 0; i < segments.size(); i++) {
		if (segments[i] == "..") {
			return true;
		}
	}
	return false;
}

String normalize_resource_script_path(const String &p_path) {
	String path = p_path.replace("\\", "/").simplify_path();
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

String render_default_template(const String &p_class_name, const String &p_base_name, bool p_tool) {
	String source;
	source += String("import { ") + p_base_name + String(" } from \"godot\";\n\n");
	if (p_tool) {
		source += String("@Tool\n");
	}
	source += String("export default class ") + p_class_name + String(" extends ") + p_base_name + String(" {\n");
	source += String("\t_ready(): void {\n");
	source += String("\t}\n");
	source += String("}\n");
	return source;
}

String render_template_source(const String &p_template, const String &p_class_name, const String &p_base_name) {
	if (p_template.is_empty()) {
		return render_default_template(p_class_name, p_base_name, false);
	}

	String source = p_template;
	source = source.replace("_CLASS_", p_class_name);
	source = source.replace("_BASE_", p_base_name);
	source = source.replace("%CLASS%", p_class_name);
	source = source.replace("%BASE%", p_base_name);
	source = source.replace("{CLASS_NAME}", p_class_name);
	source = source.replace("{BASE_CLASS_NAME}", p_base_name);
	source = source.replace("{{CLASS_NAME}}", p_class_name);
	source = source.replace("{{BASE_CLASS_NAME}}", p_base_name);
	return source;
}

Dictionary make_template_entry(const String &p_inherit, const String &p_name, const String &p_description, const String &p_content, int p_id) {
	Dictionary entry;
	entry["inherit"] = p_inherit;
	entry["name"] = p_name;
	entry["description"] = p_description;
	entry["content"] = p_content;
	entry["id"] = p_id;
	return entry;
}

String format_function_argument(const String &p_arg, int64_t p_index) {
	const String fallback = String("arg") + String::num_int64(p_index);
	String arg = p_arg.strip_edges();
	if (arg.is_empty()) {
		return fallback + String(": unknown");
	}

	int64_t colon = arg.find(":");
	if (colon >= 0) {
		String name = sanitize_typescript_identifier(arg.substr(0, colon).strip_edges(), fallback);
		String type = arg.substr(colon + 1).strip_edges();
		if (type.is_empty()) {
			type = "unknown";
		}
		return name + String(": ") + type;
	}

	return sanitize_typescript_identifier(arg, fallback) + String(": unknown");
}

String strip_typescript_method_modifiers(String p_line) {
	static const char *MODIFIERS[] = {
		"public", "private", "protected", "static", "override", "async", "declare", "readonly"
	};

	bool removed = true;
	while (removed) {
		removed = false;
		p_line = p_line.strip_edges();
		for (const char *modifier : MODIFIERS) {
			String token = String(modifier) + String(" ");
			if (p_line.begins_with(token)) {
				p_line = p_line.substr(token.length());
				removed = true;
				break;
			}
		}
	}
	return p_line.strip_edges();
}

String tree_sitter_node_text(TSNode node, const std::string &source) {
	if (ts_node_is_null(node)) {
		return String();
	}
	uint32_t start = ts_node_start_byte(node);
	uint32_t end = ts_node_end_byte(node);
	return String(source.substr(start, end - start).c_str());
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

StringName class_name_from_extends_node(TSNode node, const std::string &source) {
	const char *node_type = ts_node_type(node);
	if (strcmp(node_type, "identifier") != 0 &&
			strcmp(node_type, "member_expression") != 0 &&
			strcmp(node_type, "generic_type") != 0) {
		return StringName();
	}
	return StringName(class_name_tail(tree_sitter_node_text(node, source)));
}

StringName class_name_from_class_node(TSNode class_node, const std::string &source) {
	TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
	if (ts_node_is_null(name_node)) {
		return StringName();
	}
	return StringName(tree_sitter_node_text(name_node, source));
}

bool node_text_is_default(TSNode node, const std::string &source) {
	return !ts_node_is_null(node) && tree_sitter_node_text(node, source) == String("default");
}

StringName default_exported_name_from_clause(TSNode export_clause, const std::string &source) {
	for (uint32_t i = 0; i < ts_node_named_child_count(export_clause); i++) {
		TSNode specifier = ts_node_named_child(export_clause, i);
		if (strcmp(ts_node_type(specifier), "export_specifier") != 0) {
			continue;
		}

		TSNode alias_node = ts_node_child_by_field_name(specifier, "alias", 5);
		TSNode name_node = ts_node_child_by_field_name(specifier, "name", 4);
		if (!ts_node_is_null(alias_node)) {
			if (node_text_is_default(alias_node, source) && !ts_node_is_null(name_node)) {
				return StringName(tree_sitter_node_text(name_node, source));
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
			return StringName(tree_sitter_node_text(first_identifier, source));
		}
	}
	return StringName();
}

StringName default_exported_class_name_from_statement(TSNode export_statement, const std::string &source) {
	bool is_default = false;
	for (uint32_t i = 0; i < ts_node_child_count(export_statement); i++) {
		TSNode child = ts_node_child(export_statement, i);
		const char *child_type = ts_node_type(child);
		if (strcmp(child_type, "default") == 0) {
			is_default = true;
			continue;
		}
		if (is_default && strcmp(child_type, "identifier") == 0) {
			return StringName(tree_sitter_node_text(child, source));
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

TSNode find_class_declaration_by_name(TSNode root_node, uint32_t child_count, const std::string &source, const StringName &name) {
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

Dictionary make_validate_error(const String &path, TSNode node, const String &message) {
	TSPoint point = ts_node_start_point(node);

	Dictionary error;
	error["path"] = path;
	error["line"] = static_cast<int32_t>(point.row + 1);
	error["column"] = static_cast<int32_t>(point.column + 1);
	error["message"] = message;
	return error;
}

String describe_tree_sitter_error(TSNode node) {
	if (ts_node_is_missing(node)) {
		return String("Missing TypeScript syntax: ") + String(ts_node_type(node)) + String(".");
	}
	if (ts_node_is_error(node)) {
		return "Invalid TypeScript syntax.";
	}
	return String("Invalid TypeScript syntax near ") + String(ts_node_type(node)) + String(".");
}

void collect_tree_sitter_errors(TSNode node, const String &path, Array &errors) {
	if (ts_node_is_null(node) || !ts_node_has_error(node)) {
		return;
	}
	if (ts_node_is_error(node) || ts_node_is_missing(node)) {
		errors.append(make_validate_error(path, node, describe_tree_sitter_error(node)));
		return;
	}

	bool found_child_error = false;
	const uint32_t child_count = ts_node_child_count(node);
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(node, i);
		if (ts_node_has_error(child)) {
			found_child_error = true;
			collect_tree_sitter_errors(child, path, errors);
		}
	}
	if (!found_child_error) {
		errors.append(make_validate_error(path, node, describe_tree_sitter_error(node)));
	}
}

void append_validate_function_names(TSNode node, const std::string &source, PackedStringArray &functions) {
	if (ts_node_is_null(node)) {
		return;
	}

	const char *node_type = ts_node_type(node);
	if (strcmp(node_type, "function_declaration") == 0 ||
			strcmp(node_type, "method_definition") == 0 ||
			strcmp(node_type, "abstract_method_signature") == 0) {
		TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
		String name = tree_sitter_node_text(name_node, source);
		if (!name.is_empty()) {
			functions.push_back(name);
		}
	}

	const uint32_t child_count = ts_node_named_child_count(node);
	for (uint32_t i = 0; i < child_count; i++) {
		append_validate_function_names(ts_node_named_child(node, i), source, functions);
	}
}

TSNode find_default_class(TSNode root_node, uint32_t child_count, const std::string &source) {
	StringName exported_class_name;
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "export_statement") == 0) {
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
	}
	return find_class_declaration_by_name(root_node, child_count, source, exported_class_name);
}

bool class_has_tool_decorator(TSNode class_node, const std::string &source) {
	if (ts_node_is_null(class_node)) {
		return false;
	}
	for (uint32_t i = 0; i < ts_node_child_count(class_node); i++) {
		TSNode child = ts_node_child(class_node, i);
		if (strcmp(ts_node_type(child), "decorator") != 0) {
			continue;
		}
		std::string decorator = source.substr(ts_node_start_byte(child), ts_node_end_byte(child) - ts_node_start_byte(child));
		if (decorator == "@Tool" || decorator == "@tool") {
			return true;
		}
	}
	return false;
}

bool check_tool_decorator(TSNode root_node, uint32_t child_count, const std::string &source) {
	if (class_has_tool_decorator(find_default_class(root_node, child_count, source), source)) {
		return true;
	}

	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "export_statement") != 0) {
			continue;
		}

		for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
			TSNode node = ts_node_child(child, j);
			if (strcmp(ts_node_type(node), "decorator") == 0) {
				std::string decorator = source.substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node));
				if (decorator == "@Tool" || decorator == "@tool") {
					return true;
				}
			}
			if (strcmp(ts_node_type(node), "class_declaration") == 0) {
				for (uint32_t k = 0; k < ts_node_child_count(node); k++) {
					TSNode child_node = ts_node_child(node, k);
					if (strcmp(ts_node_type(child_node), "decorator") != 0) {
						continue;
					}
					std::string decorator = source.substr(ts_node_start_byte(child_node), ts_node_end_byte(child_node) - ts_node_start_byte(child_node));
					if (decorator == "@Tool" || decorator == "@tool") {
						return true;
					}
				}
			}
		}
	}
	return false;
}

void parse_global_class_metadata(TSNode class_node, const std::string &source, StringName &class_name, StringName &base_class_name) {
	class_name = class_name_from_class_node(class_node, source);

	for (uint32_t i = 0; i < ts_node_child_count(class_node); i++) {
		TSNode child = ts_node_child(class_node, i);
		child = ts_node_named_child(child, 0);
		if (ts_node_is_null(child) || strcmp(ts_node_type(child), "extends_clause") != 0) {
			continue;
		}
		for (uint32_t j = 0; j < ts_node_named_child_count(child); j++) {
			base_class_name = class_name_from_extends_node(ts_node_named_child(child, j), source);
			if (!base_class_name.is_empty()) {
				return;
			}
		}
	}
}

void reload_typescript_script_from_file(const Ref<Script> &p_script, bool p_keep_state) {
	Ref<TypeScriptScript> script = Ref(p_script);
	if (script.is_null()) {
		return;
	}

	String source_code;
	const String path = normalize_resource_script_path(script->get_path());
	if (!path.is_empty()) {
		source_code = FileAccess::get_file_as_string(path);
	}
	if (path.is_empty() || FileAccess::get_open_error() != OK) {
		source_code = script->_get_source_code();
	}
	script->reload_source_code(source_code, p_keep_state);
}

} // namespace

TypeScriptLanguage::~TypeScriptLanguage() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

TypeScriptLanguage *TypeScriptLanguage::get_singleton() {
	if (singleton) {
		return singleton;
	}
	singleton = memnew(TypeScriptLanguage);
	return singleton;
}

String TypeScriptLanguage::_get_name() const {
	return String("TypeScript");
}

void TypeScriptLanguage::_init() {
}

String TypeScriptLanguage::_get_type() const {
	return String("TypeScript");
}

String TypeScriptLanguage::_get_extension() const {
	return String("ts");
}

void TypeScriptLanguage::_finish() {
}

PackedStringArray TypeScriptLanguage::_get_reserved_words() const {
	PackedStringArray arr;
	for (const char *word : TS_RESERVED_WORDS) {
		arr.push_back(String(word));
	}
	return arr;
}

bool TypeScriptLanguage::_is_control_flow_keyword(const String &p_keyword) const {
	return contains_word(p_keyword, TS_CONTROL_FLOW_WORDS, sizeof(TS_CONTROL_FLOW_WORDS) / sizeof(TS_CONTROL_FLOW_WORDS[0]));
}

PackedStringArray TypeScriptLanguage::_get_comment_delimiters() const {
	PackedStringArray delimiters;
	delimiters.push_back("//");
	delimiters.push_back("/* */");
	return delimiters;
}

PackedStringArray TypeScriptLanguage::_get_doc_comment_delimiters() const {
	PackedStringArray delimiters;
	delimiters.push_back("/** */");
	return delimiters;
}

PackedStringArray TypeScriptLanguage::_get_string_delimiters() const {
	PackedStringArray delimiters;
	delimiters.push_back("\"\"");
	delimiters.push_back("''");
	delimiters.push_back("``");
	return delimiters;
}

PackedStringArray TypeScriptLanguage::_get_recognized_extensions() const {
	PackedStringArray arr;
	arr.push_back(String("ts"));
	arr.push_back(String("tsx"));
	return arr;
}

Ref<Script> TypeScriptLanguage::_make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	Ref<TypeScriptScript> script;
	script.instantiate();

	String class_name = p_class_name;
	if (class_name.is_empty()) {
		class_name = String("NewScript");
	}
	class_name = sanitize_typescript_identifier(class_name, "NewScript");

	String base_name = p_base_class_name;
	if (base_name.is_empty()) {
		base_name = String("Node");
	}
	base_name = sanitize_typescript_identifier(base_name, "Node");

	script->_set_source_code(render_template_source(p_template, class_name, base_name));
	return script;
}

TypedArray<Dictionary> TypeScriptLanguage::_get_built_in_templates(const StringName &p_object) const {
	TypedArray<Dictionary> arr;
	String inherit = String(p_object);
	if (inherit.is_empty()) {
		inherit = "Node";
	}

	arr.push_back(make_template_entry(
			inherit,
			"Default",
			"TypeScript class with a ready callback.",
			render_default_template("_CLASS_", "_BASE_", false),
			0));
	arr.push_back(make_template_entry(
			inherit,
			"Tool",
			"Editor tool TypeScript class with a ready callback.",
			render_default_template("_CLASS_", "_BASE_", true),
			1));
	return arr;
}

bool TypeScriptLanguage::_is_using_templates() {
	return true;
}

Dictionary TypeScriptLanguage::_validate(const String &p_script, const String &p_path, bool p_validate_functions, bool p_validate_errors, bool p_validate_warnings, bool p_validate_safe_lines) const {
	Dictionary d;
	d["valid"] = true;
	if (p_validate_errors) {
		d["errors"] = Array();
	}
	if (p_validate_warnings) {
		d["warnings"] = Array();
	}
	if (p_validate_safe_lines) {
		d["safe_lines"] = PackedInt32Array();
	}

	TSParser *parser = ts_parser_new();
	if (!parser) {
		d["valid"] = false;
		if (p_validate_errors) {
			Array errors = d["errors"];
			Dictionary error;
			error["path"] = p_path;
			error["line"] = 1;
			error["column"] = 1;
			error["message"] = "Failed to create TypeScript validation parser.";
			errors.append(error);
			d["errors"] = errors;
		}
		return d;
	}
	if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
		ts_parser_delete(parser);
		d["valid"] = false;
		if (p_validate_errors) {
			Array errors = d["errors"];
			Dictionary error;
			error["path"] = p_path;
			error["line"] = 1;
			error["column"] = 1;
			error["message"] = "Failed to configure TypeScript validation parser.";
			errors.append(error);
			d["errors"] = errors;
		}
		return d;
	}

	std::string source = p_script.utf8().get_data();
	TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(), static_cast<uint32_t>(source.size()));
	ts_parser_delete(parser);
	if (!tree) {
		d["valid"] = false;
		if (p_validate_errors) {
			Array errors = d["errors"];
			Dictionary error;
			error["path"] = p_path;
			error["line"] = 1;
			error["column"] = 1;
			error["message"] = "Failed to parse TypeScript source for validation.";
			errors.append(error);
			d["errors"] = errors;
		}
		return d;
	}

	TSNode root = ts_tree_root_node(tree);
	const bool has_error = ts_node_has_error(root);
	d["valid"] = !has_error;
	if (p_validate_errors && has_error) {
		Array errors = d["errors"];
		collect_tree_sitter_errors(root, p_path, errors);
		d["errors"] = errors;
	}
	if (p_validate_functions) {
		PackedStringArray functions;
		append_validate_function_names(root, source, functions);
		d["functions"] = functions;
	}
	ts_tree_delete(tree);
	return d;
}

String TypeScriptLanguage::_validate_path(const String &p_path) const {
	String path = normalize_resource_script_path(p_path);
	if (path.is_empty()) {
		return "Script path cannot be empty.";
	}
	if (!path.begins_with("res://")) {
		return "TypeScript scripts must be saved under res://.";
	}
	if (path_has_parent_segment(path)) {
		return "TypeScript script paths cannot contain parent-directory segments.";
	}
	String ext = path.get_extension().to_lower();
	if (ext != String("ts") && ext != String("tsx")) {
		return "TypeScript script paths must end with .ts or .tsx.";
	}
	return String();
}

Object *TypeScriptLanguage::_create_script() const {
	return memnew(TypeScriptScript);
}

bool TypeScriptLanguage::_has_named_classes() const {
	return true;
}

bool TypeScriptLanguage::_supports_builtin_mode() const {
	return false;
}

bool TypeScriptLanguage::_supports_documentation() const {
	return false;
}

bool TypeScriptLanguage::_can_inherit_from_file() const {
	return false;
}

int32_t TypeScriptLanguage::_find_function(const String &p_function, const String &p_code) const {
	String function_name = sanitize_typescript_identifier(p_function, "method");
	PackedStringArray lines = p_code.split("\n", true);
	for (int64_t i = 0; i < lines.size(); i++) {
		String line = strip_typescript_method_modifiers(lines[i]);
		if (line.begins_with(function_name + String("(")) ||
				line.begins_with(String("function ") + function_name + String("("))) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

String TypeScriptLanguage::_make_function(const String &p_class_name, const String &p_function_name, const PackedStringArray &p_function_args) const {
	String function_name = sanitize_typescript_identifier(p_function_name, "method");
	String code = String("\t") + function_name + String("(");
	for (int64_t i = 0; i < p_function_args.size(); i++) {
		if (i > 0) {
			code += ", ";
		}
		code += format_function_argument(p_function_args[i], i);
	}
	code += String("): void {\n\t}\n");
	return code;
}

bool TypeScriptLanguage::_can_make_function() const {
	return true;
}

Error TypeScriptLanguage::_open_in_external_editor(const Ref<Script> &p_script, int32_t p_line, int32_t p_column) {
	return Error::ERR_UNAVAILABLE;
}

bool TypeScriptLanguage::_overrides_external_editor() {
	return false;
}

ScriptLanguage::ScriptNameCasing TypeScriptLanguage::_preferred_file_name_casing() const {
	return ScriptLanguage::ScriptNameCasing::SCRIPT_NAME_CASING_PASCAL_CASE;
}

Dictionary TypeScriptLanguage::_complete_code(const String &p_code, const String &p_path, Object *p_owner) const {
	Dictionary d;
	d["result"] = Error::ERR_UNAVAILABLE;
	d["options"] = Array();
	d["force"] = false;
	d["call_hint"] = String();
	return d;
}

Dictionary TypeScriptLanguage::_lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner) const {
	Dictionary d;
	d["result"] = Error::ERR_UNAVAILABLE;
	d["type"] = ScriptLanguageExtension::LOOKUP_RESULT_SCRIPT_LOCATION;
	return d;
}

String TypeScriptLanguage::_auto_indent_code(const String &p_code, int32_t p_from_line, int32_t p_to_line) const {
	return p_code;
}

void TypeScriptLanguage::_add_global_constant(const StringName &p_name, const Variant &p_value) {
}

void TypeScriptLanguage::_add_named_global_constant(const StringName &p_name, const Variant &p_value) {
}

void TypeScriptLanguage::_remove_named_global_constant(const StringName &p_name) {
}

void TypeScriptLanguage::_thread_enter() {
}

void TypeScriptLanguage::_thread_exit() {
}

String TypeScriptLanguage::_debug_get_error() const {
	return String();
}

int32_t TypeScriptLanguage::_debug_get_stack_level_count() const {
	return 0;
}

int32_t TypeScriptLanguage::_debug_get_stack_level_line(int32_t p_level) const {
	return -1;
}

String TypeScriptLanguage::_debug_get_stack_level_function(int32_t p_level) const {
	return String();
}

String TypeScriptLanguage::_debug_get_stack_level_source(int32_t p_level) const {
	return String();
}

Dictionary TypeScriptLanguage::_debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) {
	Dictionary d;
	return d;
}

Dictionary TypeScriptLanguage::_debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) {
	Dictionary d;
	return d;
}

void *TypeScriptLanguage::_debug_get_stack_level_instance(int32_t p_level) {
	return nullptr;
}

Dictionary TypeScriptLanguage::_debug_get_globals(int32_t p_max_subitems, int32_t p_max_depth) {
	Dictionary d;
	return d;
}

String TypeScriptLanguage::_debug_parse_stack_level_expression(int32_t p_level, const String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) {
	return String();
}

TypedArray<Dictionary> TypeScriptLanguage::_debug_get_current_stack_info() {
	TypedArray<Dictionary> arr;
	return arr;
}

void TypeScriptLanguage::_reload_all_scripts() {
	TypeScriptLoader::get_singleton()->reload_cached_scripts();
}

void TypeScriptLanguage::_reload_scripts(const Array &p_scripts, bool p_soft_reload) {
	for (int64_t i = 0; i < p_scripts.size(); i++) {
		Ref<Script> script = p_scripts[i];
		reload_typescript_script_from_file(script, p_soft_reload);
	}
}

void TypeScriptLanguage::_reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
	reload_typescript_script_from_file(p_script, p_soft_reload);
}

TypedArray<Dictionary> TypeScriptLanguage::_get_public_functions() const {
	TypedArray<Dictionary> arr;
	return arr;
}

Dictionary TypeScriptLanguage::_get_public_constants() const {
	Dictionary d;
	return d;
}

TypedArray<Dictionary> TypeScriptLanguage::_get_public_annotations() const {
	TypedArray<Dictionary> arr;
	return arr;
}

void TypeScriptLanguage::_profiling_start() {
}

void TypeScriptLanguage::_profiling_stop() {
}

void TypeScriptLanguage::_profiling_set_save_native_calls(bool p_enable) {
}

int32_t TypeScriptLanguage::_profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) {
	return 0;
}

int32_t TypeScriptLanguage::_profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) {
	return 0;
}

void TypeScriptLanguage::_frame() {
}

bool TypeScriptLanguage::_handles_global_class_type(const String &p_type) const {
	return p_type == String("TypeScript") || p_type == String("ts") || p_type == String("tsx");
}

Dictionary TypeScriptLanguage::_get_global_class_name(const String &p_path) const {
	Dictionary d;
	String path = normalize_resource_script_path(p_path);
	if (!_validate_path(path).is_empty()) {
		return d;
	}

	String source_string = FileAccess::get_file_as_string(path);
	if (FileAccess::get_open_error() != OK) {
		return d;
	}

	std::string source = source_string.utf8().get_data();
	TSParser *parser = ts_parser_new();
	if (!parser) {
		return d;
	}
	if (!ts_parser_set_language(parser, tree_sitter_typescript())) {
		ts_parser_delete(parser);
		return d;
	}
	TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(), source.length());
	if (!tree) {
		ts_parser_delete(parser);
		return d;
	}

	TSNode root_node = ts_tree_root_node(tree);
	const uint32_t child_count = ts_node_child_count(root_node);
	TSNode class_node = find_default_class(root_node, child_count, source);
	if (!ts_node_is_null(class_node)) {
		StringName name;
		StringName base_type;
		parse_global_class_metadata(class_node, source, name, base_type);
		if (name != StringName()) {
			d["name"] = name;
			d["base_type"] = base_type == StringName() ? StringName("RefCounted") : base_type;
			d["icon_path"] = String();
			d["is_abstract"] = false;
			d["is_tool"] = check_tool_decorator(root_node, child_count, source);
		}
	}

	ts_tree_delete(tree);
	ts_parser_delete(parser);
	return d;
}
