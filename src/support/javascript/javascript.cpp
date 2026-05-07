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
#include <godot_cpp/variant/dictionary.hpp>
#include <cctype>

using namespace godot;
using namespace gode;

extern "C" const TSLanguage *tree_sitter_javascript();

void Javascript::_bind_methods() {
}

namespace {

static std::string node_text(const std::string &p_source, TSNode p_node) {
	if (ts_node_is_null(p_node)) {
		return std::string();
	}
	uint32_t start = ts_node_start_byte(p_node);
	uint32_t end = ts_node_end_byte(p_node);
	return p_source.substr(start, end - start);
}

static std::string strip_quotes(std::string p_text) {
	if (p_text.size() >= 2 && ((p_text.front() == '"' && p_text.back() == '"') || (p_text.front() == '\'' && p_text.back() == '\''))) {
		return p_text.substr(1, p_text.size() - 2);
	}
	return p_text;
}

static Variant::Type parse_variant_type(const std::string &p_type) {
	if (p_type == "bool" || p_type == "boolean" || p_type == "Boolean") return Variant::BOOL;
	if (p_type == "int") return Variant::INT;
	if (p_type == "float" || p_type == "number" || p_type == "Number") return Variant::FLOAT;
	if (p_type == "String" || p_type == "string") return Variant::STRING;
	if (p_type == "Vector2") return Variant::VECTOR2;
	if (p_type == "Vector2i") return Variant::VECTOR2I;
	if (p_type == "Vector3") return Variant::VECTOR3;
	if (p_type == "Vector3i") return Variant::VECTOR3I;
	if (p_type == "Vector4") return Variant::VECTOR4;
	if (p_type == "Vector4i") return Variant::VECTOR4I;
	if (p_type == "Color") return Variant::COLOR;
	if (p_type == "NodePath") return Variant::NODE_PATH;
	if (p_type == "Object") return Variant::OBJECT;
	return Variant::NIL;
}

static void parse_signal_entry(const std::string &p_signal_name, TSNode p_value, const std::string &p_source, HashMap<StringName, MethodInfo> &r_signals) {
	MethodInfo mi;
	mi.name = StringName(p_signal_name.c_str());

	TSNode args_node;
	if (strcmp(ts_node_type(p_value), "array") == 0) {
		args_node = p_value;
	} else if (strcmp(ts_node_type(p_value), "object") == 0) {
		for (uint32_t i = 0; i < ts_node_named_child_count(p_value); i++) {
			TSNode pair = ts_node_named_child(p_value, i);
			if (strcmp(ts_node_type(pair), "pair") != 0) continue;
			TSNode key = ts_node_child_by_field_name(pair, "key", 3);
			if (strip_quotes(node_text(p_source, key)) == "args") {
				args_node = ts_node_child_by_field_name(pair, "value", 5);
				break;
			}
		}
	}

	if (!ts_node_is_null(args_node) && strcmp(ts_node_type(args_node), "array") == 0) {
		for (uint32_t i = 0; i < ts_node_named_child_count(args_node); i++) {
			TSNode arg_node = ts_node_named_child(args_node, i);
			PropertyInfo arg;
			arg.type = Variant::NIL;
			if (strcmp(ts_node_type(arg_node), "string") == 0) {
				arg.name = StringName(strip_quotes(node_text(p_source, arg_node)).c_str());
			} else if (strcmp(ts_node_type(arg_node), "object") == 0) {
				for (uint32_t j = 0; j < ts_node_named_child_count(arg_node); j++) {
					TSNode pair = ts_node_named_child(arg_node, j);
					if (strcmp(ts_node_type(pair), "pair") != 0) continue;
					TSNode key = ts_node_child_by_field_name(pair, "key", 3);
					TSNode val = ts_node_child_by_field_name(pair, "value", 5);
					std::string key_text = strip_quotes(node_text(p_source, key));
					if (key_text == "name") {
						arg.name = StringName(strip_quotes(node_text(p_source, val)).c_str());
					} else if (key_text == "type") {
						arg.type = parse_variant_type(strip_quotes(node_text(p_source, val)));
					}
				}
			}
			mi.arguments.push_back(arg);
		}
	}

	r_signals[mi.name] = mi;
}

static int parse_rpc_mode(const std::string &p_mode) {
	if (p_mode == "any_peer" || p_mode == "any") return 1;
	if (p_mode == "authority" || p_mode == "master") return 2;
	if (p_mode == "disabled") return 0;
	return std::atoi(p_mode.c_str());
}

static int parse_transfer_mode(const std::string &p_mode) {
	if (p_mode == "unreliable") return 0;
	if (p_mode == "unreliable_ordered") return 1;
	if (p_mode == "reliable") return 2;
	return std::atoi(p_mode.c_str());
}

static void parse_static_metadata(const std::string &p_name, TSNode p_value, const std::string &p_source, HashMap<StringName, MethodInfo> &r_signals, HashMap<StringName, Dictionary> &r_rpc_configs) {
	if (p_name == "signals" && strcmp(ts_node_type(p_value), "object") == 0) {
		for (uint32_t i = 0; i < ts_node_named_child_count(p_value); i++) {
			TSNode pair = ts_node_named_child(p_value, i);
			if (strcmp(ts_node_type(pair), "pair") != 0) continue;
			TSNode key = ts_node_child_by_field_name(pair, "key", 3);
			TSNode val = ts_node_child_by_field_name(pair, "value", 5);
			parse_signal_entry(strip_quotes(node_text(p_source, key)), val, p_source, r_signals);
		}
		return;
	}

	if ((p_name == "rpc_config" || p_name == "rpcs") && strcmp(ts_node_type(p_value), "object") == 0) {
		for (uint32_t i = 0; i < ts_node_named_child_count(p_value); i++) {
			TSNode pair = ts_node_named_child(p_value, i);
			if (strcmp(ts_node_type(pair), "pair") != 0) continue;
			StringName method(strip_quotes(node_text(p_source, ts_node_child_by_field_name(pair, "key", 3))).c_str());
			TSNode cfg_node = ts_node_child_by_field_name(pair, "value", 5);
			Dictionary cfg;
			cfg["rpc_mode"] = 2;
			cfg["transfer_mode"] = 2;
			cfg["call_local"] = false;
			cfg["channel"] = 0;
			if (strcmp(ts_node_type(cfg_node), "object") == 0) {
				for (uint32_t j = 0; j < ts_node_named_child_count(cfg_node); j++) {
					TSNode cfg_pair = ts_node_named_child(cfg_node, j);
					if (strcmp(ts_node_type(cfg_pair), "pair") != 0) continue;
					std::string key = strip_quotes(node_text(p_source, ts_node_child_by_field_name(cfg_pair, "key", 3)));
					TSNode val = ts_node_child_by_field_name(cfg_pair, "value", 5);
					std::string val_text = strip_quotes(node_text(p_source, val));
					if (key == "rpc_mode" || key == "mode") {
						cfg["rpc_mode"] = parse_rpc_mode(val_text);
					} else if (key == "transfer_mode" || key == "transferMode") {
						cfg["transfer_mode"] = parse_transfer_mode(val_text);
					} else if (key == "call_local" || key == "callLocal") {
						cfg["call_local"] = val_text == "true";
					} else if (key == "channel") {
						cfg["channel"] = std::atoi(val_text.c_str());
					}
				}
			}
			r_rpc_configs[method] = cfg;
		}
	}
}

} // namespace

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
	v8::HandleScope handle_scope(NodeRuntime::isolate);

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
	rpc_configs.clear();
	properties.clear();
	property_defaults.clear();
	constants.clear();
	member_lines.clear();
	is_tool_script = false;

	// Parse exports using tree-sitter (will be done in class body iteration below)

	// Read static tool — equivalent to @tool in GDScript
	// JS usage: static tool = true
	if (cls.Has("tool") && cls.Get("tool").IsBoolean()) {
		is_tool_script = cls.Get("tool").As<Napi::Boolean>().Value();
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

			// Get base class (extends). tree-sitter-javascript exposes this as
			// "superclass"; older/other grammars may wrap it in a heritage clause.
			TSNode base_node = ts_node_child_by_field_name(child, "superclass", strlen("superclass"));
			if (ts_node_is_null(base_node)) {
				TSNode heritage = ts_node_child_by_field_name(child, "heritage", strlen("heritage"));
				if (!ts_node_is_null(heritage)) {
					uint32_t heritage_child_count = ts_node_child_count(heritage);
					for (uint32_t j = 0; j < heritage_child_count; j++) {
						TSNode heritage_child = ts_node_child(heritage, j);
						if (strcmp(ts_node_type(heritage_child), "extends_clause") == 0 && ts_node_child_count(heritage_child) > 0) {
							base_node = ts_node_child(heritage_child, ts_node_child_count(heritage_child) - 1);
							break;
						}
					}
				}
			}
			if (!ts_node_is_null(base_node)) {
				uint32_t start = ts_node_start_byte(base_node);
				uint32_t end = ts_node_end_byte(base_node);
				std::string base = source.substr(start, end - start);
				size_t dot = base.rfind('.');
				if (dot != std::string::npos) {
					base = base.substr(dot + 1);
				}
				base_class_name = StringName(base.c_str());
			}

			// Iterate class body for methods and properties
			TSNode body = ts_node_child_by_field_name(child, "body", strlen("body"));
			uint32_t member_count = ts_node_child_count(body);
			for (uint32_t j = 0; j < member_count; j++) {
				TSNode member = ts_node_child(body, j);
				const char *member_type = ts_node_type(member);

				if (strcmp(member_type, "field_definition") == 0) {
					bool is_static = false;
					TSNode prop_name_node;
					TSNode value_node;

					uint32_t field_child_count = ts_node_child_count(member);
					for (uint32_t k = 0; k < field_child_count; k++) {
						TSNode field_child = ts_node_child(member, k);
						const char *child_type = ts_node_type(field_child);
						if (strcmp(child_type, "static") == 0) {
							is_static = true;
						} else if (strcmp(child_type, "property_identifier") == 0) {
							prop_name_node = field_child;
						} else if (strcmp(child_type, "object") == 0 || strcmp(child_type, "string") == 0 || strcmp(child_type, "number") == 0 || strcmp(child_type, "true") == 0 || strcmp(child_type, "false") == 0) {
							value_node = field_child;
						}
					}

					if (is_static && !ts_node_is_null(prop_name_node) && !ts_node_is_null(value_node)) {
						uint32_t name_start = ts_node_start_byte(prop_name_node);
						uint32_t name_end = ts_node_end_byte(prop_name_node);
						std::string prop_name = source.substr(name_start, name_end - name_start);
						parse_static_metadata(prop_name, value_node, source, signals, rpc_configs);

						if (prop_name == "exports") {
							uint32_t obj_child_count = ts_node_child_count(value_node);
							for (uint32_t m = 0; m < obj_child_count; m++) {
								TSNode obj_child = ts_node_child(value_node, m);
								if (strcmp(ts_node_type(obj_child), "pair") == 0) {
									TSNode key_node = ts_node_child_by_field_name(obj_child, "key", strlen("key"));
									TSNode val_node = ts_node_child_by_field_name(obj_child, "value", strlen("value"));

									if (!ts_node_is_null(key_node) && !ts_node_is_null(val_node)) {
										uint32_t key_start = ts_node_start_byte(key_node);
										uint32_t key_end = ts_node_end_byte(key_node);
										std::string key_str = source.substr(key_start, key_end - key_start);

										if (key_str.length() >= 2 && (key_str[0] == '"' || key_str[0] == '\'')) {
											key_str = key_str.substr(1, key_str.length() - 2);
										}

										PropertyInfo pi;
										pi.name = StringName(key_str.c_str());
										pi.usage = PROPERTY_USAGE_DEFAULT;
										pi.hint = PROPERTY_HINT_NONE;
										pi.type = Variant::NIL;

										uint32_t pc = ts_node_child_count(val_node);
										for (uint32_t n = 0; n < pc; n++) {
											TSNode prop_pair = ts_node_child(val_node, n);
											if (strcmp(ts_node_type(prop_pair), "pair") != 0) continue;

											TSNode pk = ts_node_child_by_field_name(prop_pair, "key", 3);
											TSNode pv = ts_node_child_by_field_name(prop_pair, "value", 5);
											if (ts_node_is_null(pk) || ts_node_is_null(pv)) continue;

											uint32_t pks = ts_node_start_byte(pk);
											uint32_t pke = ts_node_end_byte(pk);
											std::string field_key = source.substr(pks, pke - pks);

											if (field_key == "type") {
												const char *pv_type = ts_node_type(pv);
												if (strcmp(pv_type, "string") == 0) {
													uint32_t pvs = ts_node_start_byte(pv) + 1;
													uint32_t pve = ts_node_end_byte(pv) - 1;
													std::string type_str = source.substr(pvs, pve - pvs);
													pi.type = parse_variant_type(type_str);
												} else if (strcmp(pv_type, "identifier") == 0) {
													uint32_t pvs = ts_node_start_byte(pv);
													uint32_t pve = ts_node_end_byte(pv);
													std::string type_str = source.substr(pvs, pve - pvs);
													pi.type = parse_variant_type(type_str);
												}
											} else if (field_key == "hint" && strcmp(ts_node_type(pv), "number") == 0) {
												uint32_t pvs = ts_node_start_byte(pv);
												uint32_t pve = ts_node_end_byte(pv);
												pi.hint = (PropertyHint)std::stoi(source.substr(pvs, pve - pvs));
											} else if (field_key == "hint_string" && strcmp(ts_node_type(pv), "string") == 0) {
												uint32_t pvs = ts_node_start_byte(pv) + 1;
												uint32_t pve = ts_node_end_byte(pv) - 1;
												pi.hint_string = String(source.substr(pvs, pve - pvs).c_str());
											}
										}

										properties[pi.name] = pi;
									}
								}
							}
						}
					} else if (!is_static && !ts_node_is_null(prop_name_node) && !ts_node_is_null(value_node)) {
						uint32_t name_start = ts_node_start_byte(prop_name_node);
						uint32_t name_end = ts_node_end_byte(prop_name_node);
						std::string prop_name = source.substr(name_start, name_end - name_start);
						StringName field_name(prop_name.c_str());

						if (properties.has(field_name)) {
							const char *vt = ts_node_type(value_node);
							uint32_t vs = ts_node_start_byte(value_node);
							uint32_t ve = ts_node_end_byte(value_node);

							if (strcmp(vt, "string") == 0) {
								property_defaults[field_name] = String(source.substr(vs + 1, ve - vs - 2).c_str());
							} else if (strcmp(vt, "number") == 0) {
								std::string num_str = source.substr(vs, ve - vs);
								if (properties[field_name].type == Variant::INT) property_defaults[field_name] = std::stoi(num_str);
								else property_defaults[field_name] = std::stod(num_str);
							} else if (strcmp(vt, "true") == 0) {
								property_defaults[field_name] = true;
							} else if (strcmp(vt, "false") == 0) {
								property_defaults[field_name] = false;
							} else if (strcmp(vt, "new_expression") == 0) {
								property_defaults[field_name] = NodeRuntime::eval_expression(source.substr(vs, ve - vs));
							}
						}
					}
				}

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

	if (base_class_name == StringName()) {
		size_t class_pos = source.find("class");
		size_t extends_pos = class_pos == std::string::npos ? std::string::npos : source.find("extends", class_pos);
		if (extends_pos != std::string::npos) {
			size_t start = extends_pos + strlen("extends");
			while (start < source.size() && std::isspace(static_cast<unsigned char>(source[start]))) {
				start++;
			}
			size_t end = start;
			while (end < source.size()) {
				unsigned char c = static_cast<unsigned char>(source[end]);
				if (!(std::isalnum(c) || c == '_' || c == '$' || c == '.')) {
					break;
				}
				end++;
			}
			if (end > start) {
				std::string base = source.substr(start, end - start);
				size_t dot = base.rfind('.');
				if (dot != std::string::npos) {
					base = base.substr(dot + 1);
				}
				base_class_name = StringName(base.c_str());
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
	if (!default_class.IsEmpty()) {
		return default_class.Value();
	}

	if (!compile()) {
		return Napi::Function();
	}

	return default_class.IsEmpty() ? Napi::Function() : default_class.Value();
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
	compile();
	return class_name;
}

bool Javascript::_inherits_script(const Ref<Script> &p_script) const {
	compile();
	Ref<Javascript> base_script = Ref(p_script);
	if (p_script.is_valid() && base_script->class_name == base_class_name) {
		return true;
	}
	return false;
}

StringName Javascript::_get_instance_base_type() const {
	compile();
	return base_class_name;
}

void *Javascript::_instance_create(Object *p_for_object) const {
	Ref<Javascript> self(const_cast<Javascript *>(this));
	JavascriptInstance *instance = memnew(JavascriptInstance(self, p_for_object, false));
	instances.insert(instance);
	instance_objects.insert(p_for_object);
	return gdextension_interface::script_instance_create3(&javascript_instance_info, instance);
}

void *Javascript::_placeholder_instance_create(Object *p_for_object) const {
	Ref<Javascript> self(const_cast<Javascript *>(this));
	JavascriptInstance *instance = memnew(JavascriptInstance(self, p_for_object, true));
	placeholder_instances.insert(instance);
	return gdextension_interface::script_instance_create3(&javascript_instance_info, instance);
}

bool Javascript::_instance_has(Object *p_object) const {
	return instance_objects.has(p_object);
}

bool Javascript::_has_source_code() const {
	return !source_code.is_empty();
}

String Javascript::_get_source_code() const {
	return source_code;
}

void Javascript::_set_source_code(const String &p_code) {
	is_dirty = true;
	source_code = p_code;

	if (compile()) {
		for (JavascriptInstance *instance : instances) {
			if (instance && !instance->is_placeholder()) {
				instance->reload(true);
			}
		}
	}
}

Error Javascript::_reload(bool p_keep_state) {
	compile();

	// Reload all instances
	for (JavascriptInstance *instance : instances) {
		if (instance && !instance->is_placeholder()) {
			instance->reload(p_keep_state);
		}
	}

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
	compile();
	return methods.has(p_method);
}

bool Javascript::_has_static_method(const StringName &p_method) const {
	compile();
	if (methods.has(p_method)) {
		return methods[p_method].flags & METHOD_FLAG_STATIC;
	}
	return false;
}

Variant Javascript::_get_script_method_argument_count(const StringName &p_method) const {
	compile();
	return Variant();
}

Dictionary Javascript::_get_method_info(const StringName &p_method) const {
	compile();
	return methods.get(p_method);
}

bool Javascript::_is_tool() const {
	compile();
	return is_tool_script;
}

bool Javascript::_is_valid() const {
	return compile();
}

bool Javascript::_is_abstract() const {
	compile();
	return false;
}

ScriptLanguage *Javascript::_get_language() const {
	return JavascriptLanguage::get_singleton();
}

ScriptLanguage *Javascript::get_script_language() const {
	return JavascriptLanguage::get_singleton();
}

StringName Javascript::get_global_name() const {
	compile();
	return class_name;
}

bool Javascript::_has_script_signal(const StringName &p_signal) const {
	compile();
	return signals.has(p_signal);
}

TypedArray<Dictionary> Javascript::_get_script_signal_list() const {
	compile();
	TypedArray<Dictionary> list;
	for (const KeyValue<StringName, MethodInfo> &E : signals) {
		Dictionary d;
		d["name"] = String(E.key);
		Array args;
		for (const PropertyInfo &arg : E.value.arguments) {
			Dictionary ad;
			ad["name"] = String(arg.name);
			ad["type"] = (int)arg.type;
			ad["class_name"] = String(arg.class_name);
			ad["hint"] = (int)arg.hint;
			ad["hint_string"] = arg.hint_string;
			ad["usage"] = (int)arg.usage;
			args.push_back(ad);
		}
		d["args"] = args;
		list.push_back(d);
	}
	return list;
}

bool Javascript::_has_property_default_value(const StringName &p_property) const {
	compile();
	return property_defaults.has(p_property);
}

Variant Javascript::_get_property_default_value(const StringName &p_property) const {
	compile();
	if (property_defaults.has(p_property)) {
		return property_defaults[p_property];
	}
	return Variant();
}

void Javascript::_update_exports() {
	compile();
}

TypedArray<Dictionary> Javascript::_get_script_method_list() const {
	compile();
	TypedArray<Dictionary> list;
	return list;
}

TypedArray<Dictionary> Javascript::_get_script_property_list() const {
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

int32_t Javascript::_get_member_line(const StringName &p_member) const {
	compile();
	const int32_t *line = member_lines.getptr(p_member);
	return line ? *line : -1;
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
	compile();
	Dictionary config;
	for (const KeyValue<StringName, Dictionary> &E : rpc_configs) {
		config[E.key] = E.value;
	}
	return config;
}
