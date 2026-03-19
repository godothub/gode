#include "support/typescript/typescript.h"
#include "support/typescript/typescript_language.h"
#include "support/javascript/javascript_instance.h"
#include "support/javascript/javascript_instance_info.h"
#include "utils/node_runtime.h"
#include <tree_sitter/api.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>

using namespace godot;
using namespace gode;

extern "C" const TSLanguage *tree_sitter_typescript();

// 把 res://path/to/Foo.ts 映射到 res://dist/path/to/Foo.js
static String get_js_path(const String &ts_path) {
	String rel = ts_path;
	if (rel.begins_with("res://")) {
		rel = rel.substr(6);
	}
	return "res://dist/" + rel.get_basename() + ".js";
}

bool Typescript::compile() const {
	if (!is_dirty) {
		return true;
	}

	String path = get_path();
	if (path.is_empty()) {
		return false;
	}

	// 读取 dist/ 下的编译产物
	String js_path = get_js_path(path);
	String js_code;
	if (FileAccess::file_exists(js_path)) {
		js_code = FileAccess::get_file_as_string(js_path);
	} else {
		return false;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	Napi::HandleScope scope(JsEnvManager::get_env());

	Napi::Value exports = NodeRuntime::compile_script(js_code.utf8().get_data(), js_path.utf8().get_data());
	Napi::Function cls = NodeRuntime::get_default_class(exports);

	if (cls.IsEmpty() || cls.IsUndefined() || cls.IsNull()) {
		default_class.Reset();
		return false;
	}

	default_class = Napi::Persistent(cls);

	// Initialize Tree-sitter with TypeScript grammar
	TSParser *parser = ts_parser_new();
	ts_parser_set_language(parser, tree_sitter_typescript());

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

	// Read static exports
	if (cls.Has("exports")) {
		Napi::Value exp_val = cls.Get("exports");
		if (exp_val.IsObject()) {
			Napi::Object exp_obj = exp_val.As<Napi::Object>();
			Napi::Array keys = exp_obj.GetPropertyNames();
			for (uint32_t i = 0; i < keys.Length(); i++) {
				std::string key = keys.Get(i).As<Napi::String>().Utf8Value();
				Napi::Value entry = exp_obj.Get(key);
				if (!entry.IsObject()) continue;
				Napi::Object entry_obj = entry.As<Napi::Object>();

				PropertyInfo pi;
				pi.name = StringName(key.c_str());
				pi.usage = PROPERTY_USAGE_DEFAULT;
				pi.hint = PROPERTY_HINT_NONE;

				if (entry_obj.Has("type") && entry_obj.Get("type").IsString()) {
					std::string type_str = entry_obj.Get("type").As<Napi::String>().Utf8Value();
					if (type_str == "bool") pi.type = Variant::BOOL;
					else if (type_str == "int") pi.type = Variant::INT;
					else if (type_str == "float" || type_str == "number") pi.type = Variant::FLOAT;
					else if (type_str == "String" || type_str == "string") pi.type = Variant::STRING;
					else if (type_str == "Vector2") pi.type = Variant::VECTOR2;
					else if (type_str == "Vector2i") pi.type = Variant::VECTOR2I;
					else if (type_str == "Vector3") pi.type = Variant::VECTOR3;
					else if (type_str == "Vector3i") pi.type = Variant::VECTOR3I;
					else if (type_str == "Vector4") pi.type = Variant::VECTOR4;
					else if (type_str == "Vector4i") pi.type = Variant::VECTOR4I;
					else if (type_str == "Color") pi.type = Variant::COLOR;
					else if (type_str == "NodePath") pi.type = Variant::NODE_PATH;
					else if (type_str == "Object") pi.type = Variant::OBJECT;
					else pi.type = Variant::NIL;
				}

				if (entry_obj.Has("hint") && entry_obj.Get("hint").IsNumber()) {
					pi.hint = (PropertyHint)entry_obj.Get("hint").As<Napi::Number>().Int32Value();
				}
				if (entry_obj.Has("hint_string") && entry_obj.Get("hint_string").IsString()) {
					pi.hint_string = String::utf8(entry_obj.Get("hint_string").As<Napi::String>().Utf8Value().c_str());
				}

				properties[pi.name] = pi;

				if (entry_obj.Has("default")) {
					Napi::Value def = entry_obj.Get("default");
					if (def.IsNumber()) property_defaults[pi.name] = def.As<Napi::Number>().DoubleValue();
					else if (def.IsString()) property_defaults[pi.name] = String::utf8(def.As<Napi::String>().Utf8Value().c_str());
					else if (def.IsBoolean()) property_defaults[pi.name] = def.As<Napi::Boolean>().Value();
				}
			}
		}
	}

	// Parse class declaration with tree-sitter-typescript
	uint32_t child_count = ts_node_child_count(root_node);
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		const char *node_type = ts_node_type(child);

		// TS uses "class_declaration" or "export_statement" wrapping a class
		TSNode class_node = { 0 };
		if (strcmp(node_type, "class_declaration") == 0) {
			class_node = child;
		} else if (strcmp(node_type, "export_statement") == 0) {
			uint32_t export_child_count = ts_node_child_count(child);
			for (uint32_t j = 0; j < export_child_count; j++) {
				TSNode export_child = ts_node_child(child, j);
				if (strcmp(ts_node_type(export_child), "class_declaration") == 0) {
					class_node = export_child;
					break;
				}
			}
		}

		if (ts_node_is_null(class_node)) continue;

		// Get class name
		TSNode name_node = ts_node_child_by_field_name(class_node, "name", strlen("name"));
		if (!ts_node_is_null(name_node)) {
			uint32_t start = ts_node_start_byte(name_node);
			uint32_t end = ts_node_end_byte(name_node);
			class_name = StringName(source.substr(start, end - start).c_str());
		}

		// Get base class (extends)
		TSNode heritage = ts_node_child_by_field_name(class_node, "heritage", strlen("heritage"));
		if (!ts_node_is_null(heritage)) {
			uint32_t heritage_child_count = ts_node_child_count(heritage);
			for (uint32_t j = 0; j < heritage_child_count; j++) {
				TSNode heritage_child = ts_node_child(heritage, j);
				if (strcmp(ts_node_type(heritage_child), "extends_clause") == 0) {
					TSNode value_node = ts_node_child(heritage_child, ts_node_child_count(heritage_child) - 1);
					uint32_t start = ts_node_start_byte(value_node);
					uint32_t end = ts_node_end_byte(value_node);
					base_class_name = StringName(source.substr(start, end - start).c_str());
				}
			}
		}

		// Iterate class body
		TSNode body = ts_node_child_by_field_name(class_node, "body", strlen("body"));
		if (ts_node_is_null(body)) continue;

		uint32_t member_count = ts_node_child_count(body);
		for (uint32_t j = 0; j < member_count; j++) {
			TSNode member = ts_node_child(body, j);
			const char *member_type = ts_node_type(member);

			if (strcmp(member_type, "method_definition") == 0) {
				TSNode method_name_node = ts_node_child_by_field_name(member, "name", strlen("name"));
				if (ts_node_is_null(method_name_node)) continue;

				uint32_t start = ts_node_start_byte(method_name_node);
				uint32_t end = ts_node_end_byte(method_name_node);
				StringName method_name = StringName(String::utf8(source.substr(start, end - start).c_str()));

				bool is_static = false;
				uint32_t method_child_count = ts_node_child_count(member);
				for (uint32_t k = 0; k < method_child_count; k++) {
					if (strcmp(ts_node_type(ts_node_child(member, k)), "static") == 0) {
						is_static = true;
						break;
					}
				}

				MethodInfo mi;
				mi.name = method_name;
				if (is_static) mi.flags |= METHOD_FLAG_STATIC;
				methods[method_name] = mi;
				member_lines[method_name] = ts_node_start_point(member).row + 1;
			}
		}

		break; // 只处理第一个 export default class
	}

	ts_tree_delete(tree);
	ts_parser_delete(parser);

	is_dirty = false;
	return true;
}

Napi::Function Typescript::get_default_class() const {
	compile();
	if (!default_class.IsEmpty()) {
		return default_class.Value();
	}
	return Napi::Function();
}

bool Typescript::_editor_can_reload_from_file() {
	return true;
}

void Typescript::_placeholder_erased(void *p_placeholder) {
	JavascriptInstance *instance = reinterpret_cast<JavascriptInstance *>(p_placeholder);
	placeholder_instances.erase(instance);
}

bool Typescript::_can_instantiate() const {
	return compile();
}

Ref<Script> Typescript::_get_base_script() const {
	compile();
	return Ref<Script>();
}

StringName Typescript::_get_global_name() const {
	compile();
	return class_name;
}

bool Typescript::_inherits_script(const Ref<Script> &p_script) const {
	return false;
}

StringName Typescript::_get_instance_base_type() const {
	compile();
	return base_class_name;
}

void *Typescript::_instance_create(Object *p_for_object) const {
	Ref<Script> self(const_cast<Typescript *>(this));
	IScriptModule *module = const_cast<Typescript *>(this);
	JavascriptInstance *instance = memnew(JavascriptInstance(module, self, p_for_object, false));
	instances.insert(instance);
	instance_objects.insert(p_for_object);
	return gdextension_interface::script_instance_create3(&javascript_instance_info, instance);
}

void *Typescript::_placeholder_instance_create(Object *p_for_object) const {
	Ref<Script> self(const_cast<Typescript *>(this));
	IScriptModule *module = const_cast<Typescript *>(this);
	JavascriptInstance *instance = memnew(JavascriptInstance(module, self, p_for_object, true));
	placeholder_instances.insert(instance);
	return gdextension_interface::script_instance_create3(&javascript_instance_info, instance);
}

bool Typescript::_instance_has(Object *p_object) const {
	return instance_objects.has(p_object);
}

bool Typescript::_has_source_code() const {
	return !source_code.is_empty();
}

String Typescript::_get_source_code() const {
	return source_code;
}

void Typescript::_set_source_code(const String &p_code) {
	is_dirty = true;
	source_code = p_code;
}

Error Typescript::_reload(bool p_keep_state) {
	compile();
	return Error::OK;
}

StringName Typescript::_get_doc_class_name() const {
	return StringName();
}

TypedArray<Dictionary> Typescript::_get_documentation() const {
	return TypedArray<Dictionary>();
}

String Typescript::_get_class_icon_path() const {
	return String();
}

bool Typescript::_has_method(const StringName &p_method) const {
	compile();
	return methods.has(p_method);
}

bool Typescript::_has_static_method(const StringName &p_method) const {
	compile();
	if (methods.has(p_method)) {
		return methods[p_method].flags & METHOD_FLAG_STATIC;
	}
	return false;
}

Variant Typescript::_get_script_method_argument_count(const StringName &p_method) const {
	compile();
	return Variant();
}

Dictionary Typescript::_get_method_info(const StringName &p_method) const {
	compile();
	return methods.get(p_method);
}

bool Typescript::_is_tool() const {
	compile();
	return false;
}

bool Typescript::_is_valid() const {
	compile();
	return is_valid;
}

bool Typescript::_is_abstract() const {
	compile();
	return false;
}

ScriptLanguage *Typescript::_get_language() const {
	return TypescriptLanguage::get_singleton();
}

ScriptLanguage *Typescript::get_script_language() const {
	return TypescriptLanguage::get_singleton();
}

StringName Typescript::get_global_name() const {
	compile();
	return class_name;
}

bool Typescript::_has_script_signal(const StringName &p_signal) const {
	compile();
	return signals.has(p_signal);
}

TypedArray<Dictionary> Typescript::_get_script_signal_list() const {
	compile();
	return TypedArray<Dictionary>();
}

bool Typescript::_has_property_default_value(const StringName &p_property) const {
	compile();
	return property_defaults.has(p_property);
}

Variant Typescript::_get_property_default_value(const StringName &p_property) const {
	compile();
	if (property_defaults.has(p_property)) {
		return property_defaults[p_property];
	}
	return Variant();
}

void Typescript::_update_exports() {
}

TypedArray<Dictionary> Typescript::_get_script_method_list() const {
	compile();
	return TypedArray<Dictionary>();
}

TypedArray<Dictionary> Typescript::_get_script_property_list() const {
	compile();
	TypedArray<Dictionary> list;
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

int32_t Typescript::_get_member_line(const StringName &p_member) const {
	compile();
	if (member_lines.has(p_member)) {
		return member_lines[p_member];
	}
	return -1;
}

Dictionary Typescript::_get_constants() const {
	compile();
	return Dictionary();
}

TypedArray<StringName> Typescript::_get_members() const {
	compile();
	return TypedArray<StringName>();
}

bool Typescript::_is_placeholder_fallback_enabled() const {
	return true;
}

Variant Typescript::_get_rpc_config() const {
	compile();
	return Variant();
}
