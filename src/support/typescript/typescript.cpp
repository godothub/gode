#include "support/typescript/typescript.h"
#include "support/typescript/typescript_language.h"
#include "utils/node_runtime.h"
#include "utils/value_convert.h"

#include <tree_sitter/api.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/file_access.hpp>

using namespace godot;
using namespace gode;

extern "C" const TSLanguage *tree_sitter_typescript();

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

	String js_path = get_js_path(path);
	String js_code;
	if (FileAccess::file_exists(js_path)) {
		js_code = FileAccess::get_file_as_string(js_path);
	} else {
		return false;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);

	Napi::Value exports = NodeRuntime::compile_script(js_code.utf8().get_data(), js_path.utf8().get_data());
	Napi::Function cls = NodeRuntime::get_default_class(exports);

	if (cls.IsEmpty() || cls.IsUndefined() || cls.IsNull()) {
		default_class.Reset();
		return false;
	}

	default_class = Napi::Persistent(cls);

	// tree-sitter 解析 .ts 源码元数据
	TSParser *parser = ts_parser_new();
	ts_parser_set_language(parser, tree_sitter_typescript());

	std::string source = source_code.utf8().get_data();
	TSTree *tree = ts_parser_parse_string(parser, NULL, source.c_str(), source.length());
	TSNode root_node = ts_tree_root_node(tree);

	class_name = StringName();
	base_class_name = StringName();
	methods.clear();
	signals.clear();
	properties.clear();
	property_defaults.clear();
	constants.clear();
	member_lines.clear();
	is_tool_script = false;

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
					pi.hint_string = String(entry_obj.Get("hint_string").As<Napi::String>().Utf8Value().c_str());
				}
				properties[pi.name] = pi;
				if (entry_obj.Has("default")) {
					property_defaults[pi.name] = napi_to_godot(entry_obj.Get("default"));
				}
			}
		}
	}

	// Read static tool — equivalent to @tool in GDScript
	// TS usage: static tool = true
	if (cls.Has("tool") && cls.Get("tool").IsBoolean()) {
		is_tool_script = cls.Get("tool").As<Napi::Boolean>().Value();
	}

	// 解析 AST：export_statement > export default class / class_declaration
	uint32_t child_count = ts_node_child_count(root_node);
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		const char *node_type = ts_node_type(child);

		TSNode class_node = { 0 };
		if (strcmp(node_type, "export_statement") == 0) {
			uint32_t ec = ts_node_child_count(child);
			for (uint32_t j = 0; j < ec; j++) {
				TSNode en = ts_node_child(child, j);
				if (strcmp(ts_node_type(en), "class_declaration") == 0) {
					class_node = en;
					break;
				}
			}
		} else if (strcmp(node_type, "class_declaration") == 0) {
			class_node = child;
		}

		if (ts_node_is_null(class_node)) continue;

		// class name
		TSNode name_node = ts_node_child_by_field_name(class_node, "name", strlen("name"));
		if (!ts_node_is_null(name_node)) {
			uint32_t start = ts_node_start_byte(name_node);
			uint32_t end = ts_node_end_byte(name_node);
			class_name = StringName(source.substr(start, end - start).c_str());
		}

		// base class (extends clause)
		TSNode body_node = ts_node_child_by_field_name(class_node, "body", strlen("body"));
		uint32_t cc = ts_node_child_count(class_node);
		for (uint32_t j = 0; j < cc; j++) {
			TSNode cn = ts_node_child(class_node, j);
			if (strcmp(ts_node_type(cn), "class_heritage") == 0) {
				uint32_t hc = ts_node_child_count(cn);
				for (uint32_t k = 0; k < hc; k++) {
					TSNode hn = ts_node_child(cn, k);
					if (strcmp(ts_node_type(hn), "identifier") == 0) {
						uint32_t s = ts_node_start_byte(hn);
						uint32_t e = ts_node_end_byte(hn);
						base_class_name = StringName(source.substr(s, e - s).c_str());
						break;
					}
				}
			}
		}

		// methods from class body
		if (!ts_node_is_null(body_node)) {
			uint32_t mc = ts_node_child_count(body_node);
			for (uint32_t j = 0; j < mc; j++) {
				TSNode member = ts_node_child(body_node, j);
				if (strcmp(ts_node_type(member), "method_definition") != 0) continue;

				TSNode mn = ts_node_child_by_field_name(member, "name", strlen("name"));
				if (ts_node_is_null(mn)) continue;
				uint32_t s = ts_node_start_byte(mn);
				uint32_t e = ts_node_end_byte(mn);
				StringName method_name(source.substr(s, e - s).c_str());

				bool is_static = false;
				uint32_t mcc = ts_node_child_count(member);
				for (uint32_t k = 0; k < mcc; k++) {
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

		break;
	}

	ts_tree_delete(tree);
	ts_parser_delete(parser);

	is_valid = true;
	is_dirty = false;
	return true;
}

ScriptLanguage *Typescript::_get_language() const {
	return TypescriptLanguage::get_singleton();
}
