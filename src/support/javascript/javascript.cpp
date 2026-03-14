#include "support/javascript/javascript.h"
#include "support/javascript/javascript_instance.h"
#include "support/javascript/javascript_instance_info.h"
#include "support/javascript/javascript_language.h"
#include "utils/node_runtime.h"
#include "utils/value_convert.h"
#include <tree_sitter/api.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>

using namespace godot;
using namespace gode;

extern "C" const TSLanguage *tree_sitter_javascript();

void Javascript::_bind_methods() {
}

bool Javascript::compile() const {
	if (!is_dirty) {
		return true;
	}

	// Compile JS code
	String path = get_path();
	if (path.is_empty()) {
		return false;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	Napi::HandleScope scope(JsEnvManager::get_env());

	Napi::Value exports = NodeRuntime::compile_script(source_code.utf8().get_data(), path.utf8().get_data());
	Napi::Function cls = NodeRuntime::get_default_class(exports);

	if (cls.IsEmpty() || cls.IsUndefined() || cls.IsNull()) {
		// Clear cached class if compilation fails
		default_class.Reset();
		return false;
	}

	// Cache the default class
	default_class = Napi::Persistent(cls);

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
	property_defaults.clear();
	constants.clear();
	member_lines.clear();

	// Read static exports — equivalent to @export in GDScript
	// JS usage: static exports = { speed: { type: "float", default: 100.0 } }
	if (cls.Has("exports")) {
		Napi::Value exp_val = cls.Get("exports");
		if (exp_val.IsObject()) {
			Napi::Object exp_obj = exp_val.As<Napi::Object>();
			Napi::Array keys = exp_obj.GetPropertyNames();
			for (uint32_t i = 0; i < keys.Length(); i++) {
				std::string key = keys.Get(i).As<Napi::String>().Utf8Value();
				Napi::Value entry = exp_obj.Get(key);
				if (!entry.IsObject()) {
					continue;
				}
				Napi::Object entry_obj = entry.As<Napi::Object>();

				PropertyInfo pi;
				pi.name = StringName(key.c_str());
				pi.usage = PROPERTY_USAGE_DEFAULT;
				pi.hint = PROPERTY_HINT_NONE;

				// type
				if (entry_obj.Has("type") && entry_obj.Get("type").IsString()) {
					std::string type_str = entry_obj.Get("type").As<Napi::String>().Utf8Value();
					if (type_str == "bool") {
						pi.type = Variant::BOOL;
					} else if (type_str == "int") {
						pi.type = Variant::INT;
					} else if (type_str == "float" || type_str == "number") {
						pi.type = Variant::FLOAT;
					} else if (type_str == "String" || type_str == "string") {
						pi.type = Variant::STRING;
					} else if (type_str == "Vector2") {
						pi.type = Variant::VECTOR2;
					} else if (type_str == "Vector2i") {
						pi.type = Variant::VECTOR2I;
					} else if (type_str == "Vector3") {
						pi.type = Variant::VECTOR3;
					} else if (type_str == "Vector3i") {
						pi.type = Variant::VECTOR3I;
					} else if (type_str == "Vector4") {
						pi.type = Variant::VECTOR4;
					} else if (type_str == "Vector4i") {
						pi.type = Variant::VECTOR4I;
					} else if (type_str == "Color") {
						pi.type = Variant::COLOR;
					} else if (type_str == "NodePath") {
						pi.type = Variant::NODE_PATH;
					} else if (type_str == "Object") {
						pi.type = Variant::OBJECT;
					} else {
						pi.type = Variant::NIL;
					}
				}

				// hint (integer constant, e.g. PROPERTY_HINT_RANGE = 1)
				if (entry_obj.Has("hint") && entry_obj.Get("hint").IsNumber()) {
					pi.hint = (PropertyHint)entry_obj.Get("hint").As<Napi::Number>().Int32Value();
				}

				// hint_string (e.g. "0,200,1" for range)
				if (entry_obj.Has("hint_string") && entry_obj.Get("hint_string").IsString()) {
					std::string hs = entry_obj.Get("hint_string").As<Napi::String>().Utf8Value();
					pi.hint_string = String(hs.c_str());
				}

				properties[pi.name] = pi;

				// default value
				if (entry_obj.Has("default")) {
					property_defaults[pi.name] = napi_to_godot(entry_obj.Get("default"));
				}
			}
		}
	}

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

					// Check for static modifier
					bool is_static = false;
					TSNode static_node = ts_node_child_by_field_name(member, "static", strlen("static"));
					if (!ts_node_is_null(static_node)) {
						// Wait, Tree-sitter JS grammar usually puts 'static' keyword as a child of method_definition?
						// Or it's a field "static" (boolean)? No, it's a child node "static".
						// Let's check child nodes for "static" keyword.
						uint32_t method_child_count = ts_node_child_count(member);
						for (uint32_t k = 0; k < method_child_count; k++) {
							TSNode method_child = ts_node_child(member, k);
							const char *child_type = ts_node_type(method_child);
							if (strcmp(child_type, "static") == 0) {
								is_static = true;
								break;
							}
						}
					}

					MethodInfo mi;
					mi.name = method_name;
					if (is_static) {
						mi.flags |= METHOD_FLAG_STATIC;
					}
					methods[method_name] = mi;
					member_lines[method_name] = ts_node_start_point(member).row + 1;
				}
			}
		}
	}

	// Clean up Tree-sitter
	ts_tree_delete(tree);
	ts_parser_delete(parser);

	is_dirty = false;
	return true;
}

Napi::Function Javascript::get_default_class() const {
	if (default_class.IsEmpty()) {
		return Napi::Function();
	}
	return default_class.Value();
}

bool Javascript::_editor_can_reload_from_file() {
	return true;
}

void Javascript::_placeholder_erased(void *p_placeholder) {
	placeholder_instances.erase(static_cast<JavascriptInstance *>(p_placeholder));
}

bool Javascript::_can_instantiate() const {
	return compile();
}

Ref<Script> Javascript::_get_base_script() const {
	String base = String(base_class_name);
	if (base.is_empty()) {
		return Ref<Javascript>();
	}
	String req = base;
	{
		int64_t p1 = base.find("require(");
		if (p1 >= 0) {
			int64_t s1 = base.find("'", p1);
			int64_t s2 = base.find("\"", p1);
			int64_t s = -1;
			int64_t e = -1;
			if (s1 >= 0 && (s2 < 0 || s1 < s2)) {
				s = s1 + 1;
				e = base.find("'", s);
			} else if (s2 >= 0) {
				s = s2 + 1;
				e = base.find("\"", s);
			}
			if (s >= 0 && e > s) {
				req = base.substr(s, e - s);
			}
		}
	}
	if (req.is_empty()) {
		return Ref<Javascript>();
	}
	String cur = get_path();
	String cur_dir = cur;
	{
		int64_t p = cur_dir.rfind("/");
		if (p >= 0) {
			cur_dir = cur_dir.substr(0, p);
		}
	}
	String resolved = req;
	if (!req.begins_with("res://")) {
		if (req.begins_with("./") || req.begins_with("../")) {
			PackedStringArray left = cur_dir.replace("res://", "").split("/", false);
			PackedStringArray right = req.split("/", false);
			Vector<String> parts;
			for (int i = 0; i < left.size(); i++) {
				if (!left[i].is_empty()) {
					parts.push_back(left[i]);
				}
			}
			for (int i = 0; i < right.size(); i++) {
				String seg = right[i];
				if (seg == ".") {
					continue;
				}
				if (seg == "..") {
					if (parts.size() > 0) {
						parts.remove_at(parts.size() - 1);
					}
					continue;
				}
				if (!seg.is_empty()) {
					parts.push_back(seg);
				}
			}
			String joined = "res://";
			for (int i = 0; i < parts.size(); i++) {
				if (i > 0) {
					joined += "/";
				}
				joined += parts[i];
			}
			resolved = joined;
		} else {
			String base_nm = "res://node_modules/" + req;
			String pkg_json = base_nm + "/package.json";
			if (FileAccess::file_exists(pkg_json)) {
				String main_rel;
				String pkg = FileAccess::get_file_as_string(pkg_json);
				int64_t mpos = pkg.find("\"main\"");
				if (mpos >= 0) {
					int64_t cpos = pkg.find(":", mpos);
					if (cpos >= 0) {
						int64_t q1 = pkg.find("\"", cpos);
						int64_t q2 = pkg.find("\"", q1 + 1);
						if (q1 >= 0 && q2 > q1) {
							main_rel = pkg.substr(q1 + 1, q2 - q1 - 1);
						}
					}
				}
				if (main_rel.is_empty()) {
					String idx = base_nm + "/index.js";
					if (FileAccess::file_exists(idx)) {
						resolved = idx;
					} else {
						resolved = String();
					}
				} else {
					String main_path = base_nm + "/" + main_rel;
					if (FileAccess::file_exists(main_path)) {
						resolved = main_path;
					} else if (FileAccess::file_exists(main_path + ".js")) {
						resolved = main_path + ".js";
					} else if (FileAccess::file_exists(main_path + ".json")) {
						resolved = main_path + ".json";
					} else {
						String idx = base_nm + "/index.js";
						if (FileAccess::file_exists(idx)) {
							resolved = idx;
						} else {
							resolved = String();
						}
					}
				}
			} else {
				String idx = base_nm + "/index.js";
				if (FileAccess::file_exists(idx)) {
					resolved = idx;
				} else {
					resolved = String();
				}
			}
		}
	}
	if (resolved.is_empty()) {
		return Ref<Javascript>();
	}
	Ref<Javascript> base_script = ResourceLoader::get_singleton()->load(resolved);
	return base_script;
}

StringName Javascript::_get_global_name() const {
	return class_name;
}

bool Javascript::_inherits_script(const Ref<Script> &p_script) const {
	Ref<Javascript> base_script = Ref(p_script);
	if (p_script.is_valid() && base_script->class_name == base_class_name) {
		return true;
	}
	return false;
}

StringName Javascript::_get_instance_base_type() const {
	return base_class_name;
}

void *Javascript::_instance_create(Object *p_for_object) const {
	const Ref self(const_cast<Javascript *>(this));
	JavascriptInstance *instance = memnew(JavascriptInstance(self, p_for_object, false));
	instances.insert(instance);
	instance_objects.insert(p_for_object);
	return gdextension_interface::script_instance_create3(&javascript_instance_info, instance);
}

void *Javascript::_placeholder_instance_create(Object *p_for_object) const {
	const Ref self(const_cast<Javascript *>(this));
	JavascriptInstance *instance = memnew(JavascriptInstance(self, p_for_object, true));
	placeholder_instances.insert(instance);
	return gdextension_interface::script_instance_create3(&javascript_instance_info, instance);
}

bool Javascript::_instance_has(Object *p_object) const {
	return instance_objects.has(p_object);
}

bool Javascript::_has_source_code() const {
	return source_code.is_empty();
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
	return methods.has(p_method);
}

bool Javascript::_has_static_method(const StringName &p_method) const {
	if (methods.has(p_method)) {
		return methods[p_method].flags & METHOD_FLAG_STATIC;
	}
	return false;
}

Variant Javascript::_get_script_method_argument_count(const StringName &p_method) const {
	return Variant();
}

Dictionary Javascript::_get_method_info(const StringName &p_method) const {
	return methods.get(p_method);
}

bool Javascript::_is_tool() const {
	return false;
}

bool Javascript::_is_valid() const {
	return is_valid;
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
	return property_defaults.has(p_property);
}

Variant Javascript::_get_property_default_value(const StringName &p_property) const {
	if (property_defaults.has(p_property)) {
		return property_defaults[p_property];
	}
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
	compile();
	for (const KeyValue<StringName, PropertyInfo> &kv : properties) {
		const PropertyInfo &pi = kv.value;
		Dictionary d;
		d["name"] = String(pi.name);
		d["class_name"] = String(pi.class_name);
		d["type"] = (int)pi.type;
		d["hint"] = (int)pi.hint;
		d["hint_string"] = pi.hint_string;
		d["usage"] = (int)pi.usage;
		list.push_back(d);
	}
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
