#include "support/javascript/javascript.h"
#include "support/javascript/javascript_instance.h"
#include "support/javascript/javascript_instance_info.h"
#include "support/javascript/javascript_language.h"
#include <gdextension_interface.h>
#include <tree_sitter/api.h>
#include <godot_cpp/godot.hpp>

using namespace godot;
using namespace gode;

extern "C" const TSLanguage *tree_sitter_javascript();

void Javascript::_bind_methods() {
}

bool Javascript::compile() {
	if (!is_dirty) {
		return false;
	}

	_update_metadata();
	is_dirty = false;
	return true;
}

void Javascript::_update_metadata() {
	// Initialize Tree-sitter
	TSParser *parser = ts_parser_new();
	ts_parser_set_language(parser, tree_sitter_javascript());

	std::string source = source_code.utf8().get_data();
	TSTree *tree = ts_parser_parse_string(parser, NULL, source.c_str(), source.length());
	TSNode root_node = ts_tree_root_node(tree);

	// Clear previous metadata
	class_name = StringName();
	base_class_name = StringName();
	methods.clear();
	signals.clear();
	properties.clear();
	constants.clear();
	member_lines.clear();

	// Basic query to find class declaration and methods
	// Note: This is a simplified implementation. A robust one would use TSQuery.

	// Iterate over top-level nodes to find class declaration
	uint32_t child_count = ts_node_child_count(root_node);
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		const char *node_type = ts_node_type(child);

		if (strcmp(node_type, "class_declaration") == 0) {
			// Get class name
			TSNode name_node = ts_node_child_by_field_name(child, "name", strlen("name"));
			if (!ts_node_is_null(name_node)) {
				uint32_t start = ts_node_start_byte(name_node);
				uint32_t end = ts_node_end_byte(name_node);
				class_name = StringName(source.substr(start, end - start).c_str());
			}

			// Get base class (extends)
			TSNode heritage = ts_node_child_by_field_name(child, "heritage", strlen("heritage"));
			if (!ts_node_is_null(heritage)) {
				// heritage structure is typically "extends BaseClass"
				// We iterate children of heritage clause
				uint32_t heritage_child_count = ts_node_child_count(heritage);
				for (uint32_t j = 0; j < heritage_child_count; j++) {
					TSNode heritage_child = ts_node_child(heritage, j);
					if (strcmp(ts_node_type(heritage_child), "extends_clause") == 0) {
						TSNode base_node = ts_node_child_by_field_name(heritage_child, "value", strlen("value")); // or just the identifier
						if (ts_node_child_count(base_node) > 0) {
							// Sometimes it's a member expression like godot.Node
							// For simplicity, we just grab the text
						}
						// Actually, extends_clause child at index 1 is usually the class
						// Let's just grab the text of the last child of extends_clause for now
						TSNode value_node = ts_node_child(heritage_child, ts_node_child_count(heritage_child) - 1);
						uint32_t start = ts_node_start_byte(value_node);
						uint32_t end = ts_node_end_byte(value_node);
						base_class_name = StringName(source.substr(start, end - start).c_str());
					}
				}
			}

			// Iterate class body for methods and properties
			TSNode body = ts_node_child_by_field_name(child, "body", strlen("body"));
			uint32_t member_count = ts_node_child_count(body);
			for (uint32_t j = 0; j < member_count; j++) {
				TSNode member = ts_node_child(body, j);
				const char *member_type = ts_node_type(member);

				if (strcmp(member_type, "method_definition") == 0) {
					TSNode method_name_node = ts_node_child_by_field_name(member, "name", strlen("name"));
					uint32_t start = ts_node_start_byte(method_name_node);
					uint32_t end = ts_node_end_byte(method_name_node);
					String method_name_str = String::utf8(source.substr(start, end - start).c_str());
					StringName method_name = StringName(method_name_str);

					Dictionary method_info;
					method_info["name"] = method_name;

					// Determine if static
					// In JS, 'static' is a modifier. We'd need to check children for 'static' keyword node
					// Simplified: check text starts with static? No, that's unreliable.
					// TS structure: modifiers are children before name?
					// Let's assume instance method for now.

					// Parameters
					TSNode params_node = ts_node_child_by_field_name(member, "parameters", strlen("parameters"));
					TypedArray<Dictionary> args;
					uint32_t param_count = ts_node_child_count(params_node);
					for (uint32_t k = 0; k < param_count; k++) {
						TSNode param = ts_node_child(params_node, k);
						// Skip parentheses and commas
						if (strcmp(ts_node_type(param), "identifier") == 0) {
							uint32_t p_start = ts_node_start_byte(param);
							uint32_t p_end = ts_node_end_byte(param);
							String param_name = String::utf8(source.substr(p_start, p_end - p_start).c_str());
							Dictionary arg;
							arg["name"] = param_name;
							arg["type"] = Variant::NIL;
							args.push_back(arg);
						}
					}
					method_info["args"] = args;
					method_info["default_args"] = Array();
					method_info["return"] = Dictionary();
					method_info["flags"] = METHOD_FLAG_NORMAL;

					methods.push_back(method_info);

					// Record line number (0-based in TS, Godot uses 1-based usually? Check ScriptEditor)
					TSPoint start_point = ts_node_start_point(method_name_node);
					member_lines[method_name] = start_point.row + 1;
				}
				// TODO: Handle field_definition for properties
			}
		}
	}

	ts_tree_delete(tree);
	ts_parser_delete(parser);
}

bool Javascript::_editor_can_reload_from_file() {
	return false;
}

void Javascript::_placeholder_erased(void *p_placeholder) {
}

bool Javascript::_can_instantiate() const {
	return true;
}

Ref<Script> Javascript::_get_base_script() const {
	return Ref<Javascript>();
}

StringName Javascript::_get_global_name() const {
	return StringName();
}

bool Javascript::_inherits_script(const Ref<Script> &p_script) const {
	return false;
}

StringName Javascript::_get_instance_base_type() const {
	return StringName();
}

void *Javascript::_instance_create(Object *p_for_object) const {
	static auto fn = reinterpret_cast<GDExtensionInterfaceScriptInstanceCreate3>(gdextension_interface::get_proc_address("script_instance_create3"));
	const Ref self(const_cast<Javascript *>(this));
	JavascriptInstance *instance = memnew(JavascriptInstance(self, p_for_object, false));
	return fn(&javascript_instance_info, instance);
}

void *Javascript::_placeholder_instance_create(Object *p_for_object) const {
	static auto fn = reinterpret_cast<GDExtensionInterfaceScriptInstanceCreate3>(gdextension_interface::get_proc_address("script_instance_create3"));
	const Ref self(const_cast<Javascript *>(this));
	JavascriptInstance *instance = memnew(JavascriptInstance(self, p_for_object, true));
	return fn(&javascript_instance_info, instance);
}

bool Javascript::_instance_has(Object *p_object) const {
	(void)p_object;
	return true;
}

bool Javascript::_has_source_code() const {
	return false;
}

String Javascript::_get_source_code() const {
	return source_code;
}

void Javascript::_set_source_code(const String &p_code) {
	is_dirty = true;
	source_code = p_code;
}

Error Javascript::_reload(bool p_keep_state) {
	return Error::OK;
}

StringName Javascript::_get_doc_class_name() const {
	return StringName();
}

TypedArray<Dictionary> Javascript::_get_documentation() const {
	TypedArray<Dictionary> docs;
	return docs;
}

String Javascript::_get_class_icon_path() const {
	return String();
}

bool Javascript::_has_method(const StringName &p_method) const {
	return false;
}

bool Javascript::_has_static_method(const StringName &p_method) const {
	return false;
}

Variant Javascript::_get_script_method_argument_count(const StringName &p_method) const {
	return Variant();
}

Dictionary Javascript::_get_method_info(const StringName &p_method) const {
	Dictionary info;
	return info;
}

bool Javascript::_is_tool() const {
	return false;
}

bool Javascript::_is_valid() const {
	return false;
}

bool Javascript::_is_abstract() const {
	return false;
}

ScriptLanguage *Javascript::_get_language() const {
	return JavascriptLanguage::get_singleton();
}

bool Javascript::_has_script_signal(const StringName &p_signal) const {
	return false;
}

TypedArray<Dictionary> Javascript::_get_script_signal_list() const {
	TypedArray<Dictionary> list;
	return list;
}

bool Javascript::_has_property_default_value(const StringName &p_property) const {
	return false;
}

Variant Javascript::_get_property_default_value(const StringName &p_property) const {
	return Variant();
}

void Javascript::_update_exports() {
}

TypedArray<Dictionary> Javascript::_get_script_method_list() const {
	TypedArray<Dictionary> list;
	return list;
}

TypedArray<Dictionary> Javascript::_get_script_property_list() const {
	TypedArray<Dictionary> list;
	return list;
}

int32_t Javascript::_get_member_line(const StringName &p_member) const {
	return -1;
}

Dictionary Javascript::_get_constants() const {
	Dictionary constants;
	return constants;
}

TypedArray<StringName> Javascript::_get_members() const {
	TypedArray<StringName> members;
	return members;
}

bool Javascript::_is_placeholder_fallback_enabled() const {
	return true;
}

Variant Javascript::_get_rpc_config() const {
	return Variant();
}
