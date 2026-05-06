#include "support/typescript/typescript.h"
#include "support/typescript/typescript_language.h"
#include "utils/node_runtime.h"
#include "utils/value_convert.h"

#include <tree_sitter/api.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

using namespace godot;
using namespace gode;

extern "C" const TSLanguage *tree_sitter_typescript();

static Variant::Type parse_type_string(const std::string &type_str) {
	if (type_str == "bool" || type_str == "boolean") {
		return Variant::BOOL;
	}
	if (type_str == "int") {
		return Variant::INT;
	}
	if (type_str == "float" || type_str == "number") {
		return Variant::FLOAT;
	}
	if (type_str == "string") {
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
	if (type_str == "Color") {
		return Variant::COLOR;
	}
	if (type_str == "NodePath") {
		return Variant::NODE_PATH;
	}
	return Variant::NIL;
}

static String get_js_path(const String &ts_path) {
	String rel = ts_path;
	if (rel.begins_with("res://")) {
		rel = rel.substr(6);
	}
	return "res://dist/" + rel.get_basename() + ".js";
}

static void collect_parent_properties(const StringName &parent_name, const std::string &source, TSNode root_node, uint32_t child_count, const String &file_path, HashMap<StringName, PropertyInfo> &properties, HashMap<StringName, Variant> &property_defaults) {
	if (parent_name.is_empty()) {
		return;
	}

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
					for (uint32_t j = 0; j < ts_node_child_count(parent_node); j++) {
						TSNode cn = ts_node_child(parent_node, j);
						cn = ts_node_named_child(cn, 0);
						if (!ts_node_is_null(cn) && strcmp(ts_node_type(cn), "extends_clause") == 0) {
							for (uint32_t k = 0; k < ts_node_child_count(cn); k++) {
								TSNode hn = ts_node_child(cn, k);
								if (strcmp(ts_node_type(hn), "identifier") == 0) {
									uint32_t s = ts_node_start_byte(hn);
									uint32_t e = ts_node_end_byte(hn);
									grandparent = StringName(source.substr(s, e - s).c_str());
									break;
								}
							}
						}
					}
					collect_parent_properties(grandparent, source, root_node, child_count, file_path, properties, property_defaults);
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
						TSNode fvalue = ts_node_child_by_field_name(field, "value", 5);
						if (!ts_node_is_null(fvalue)) {
							const char *vt = ts_node_type(fvalue);
							uint32_t vs = ts_node_start_byte(fvalue);
							uint32_t ve = ts_node_end_byte(fvalue);
							if (strcmp(vt, "string") == 0) {
								std::string str_val = source.substr(vs + 1, ve - vs - 2);
								property_defaults[prop_name] = String(str_val.c_str());
							} else if (strcmp(vt, "number") == 0) {
								property_defaults[prop_name] = std::stod(source.substr(vs, ve - vs));
							} else if (strcmp(vt, "true") == 0) {
								property_defaults[prop_name] = true;
							} else if (strcmp(vt, "false") == 0) {
								property_defaults[prop_name] = false;
							}
						}
					}
					return;
				}
			}
		}
	}

	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "import_statement") != 0) {
			continue;
		}
		TSNode clause = ts_node_child_by_field_name(child, "import_clause", 13);
		if (ts_node_is_null(clause)) {
			continue;
		}
		bool found = false;
		for (uint32_t j = 0; j < ts_node_child_count(clause); j++) {
			TSNode spec = ts_node_child(clause, j);
			if (strcmp(ts_node_type(spec), "named_imports") == 0) {
				for (uint32_t k = 0; k < ts_node_child_count(spec); k++) {
					TSNode imp = ts_node_child(spec, k);
					if (strcmp(ts_node_type(imp), "import_specifier") == 0) {
						TSNode name = ts_node_child_by_field_name(imp, "name", 4);
						if (!ts_node_is_null(name)) {
							uint32_t ns = ts_node_start_byte(name);
							uint32_t ne = ts_node_end_byte(name);
							if (source.substr(ns, ne - ns) == String(parent_name).utf8().get_data()) {
								found = true;
								break;
							}
						}
					}
				}
			}
			if (found) {
				break;
			}
		}
		if (found) {
			TSNode src = ts_node_child_by_field_name(child, "source", 6);
			if (!ts_node_is_null(src)) {
				uint32_t ss = ts_node_start_byte(src);
				uint32_t se = ts_node_end_byte(src);
				std::string import_path = source.substr(ss + 1, se - ss - 2);
				String ts_path = file_path.get_base_dir().path_join(String(import_path.c_str()) + ".ts");
				Ref<Script> parent_script = ResourceLoader::get_singleton()->load(ts_path);
				if (parent_script.is_valid()) {
					Ref<Typescript> parent_ts = parent_script;
					if (parent_ts.is_valid() && parent_ts->_is_valid()) {
						for (const KeyValue<StringName, PropertyInfo> &E : parent_ts->get_exported_properties()) {
							if (!properties.has(E.key)) {
								properties[E.key] = E.value;
							}
						}
						for (const KeyValue<StringName, Variant> &E : parent_ts->get_property_defaults()) {
							if (!property_defaults.has(E.key)) {
								property_defaults[E.key] = E.value;
							}
						}
					}
				}
				return;
			}
		}
	}
}

static void collect_interfaces_from_node(TSNode root_node, uint32_t child_count, const std::string &source, HashMap<StringName, Vector<PropertyInfo>> &interfaces);

// 递归解析 object 字面量，将 prefix::key 写入 property_defaults
static void parse_object_defaults(TSNode obj_node, const std::string &source, const std::string &prefix, HashMap<StringName, Variant> &property_defaults) {
	for (uint32_t i = 0; i < ts_node_child_count(obj_node); i++) {
		TSNode pair = ts_node_child(obj_node, i);
		if (strcmp(ts_node_type(pair), "pair") != 0) continue;

		TSNode key = ts_node_child_by_field_name(pair, "key", 3);
		TSNode val = ts_node_child_by_field_name(pair, "value", 5);
		if (ts_node_is_null(key) || ts_node_is_null(val)) continue;

		uint32_t ks = ts_node_start_byte(key);
		uint32_t ke = ts_node_end_byte(key);
		std::string full_key = prefix + source.substr(ks, ke - ks);
		StringName prop_name(full_key.c_str());

		const char *vt = ts_node_type(val);
		uint32_t vs = ts_node_start_byte(val);
		uint32_t ve = ts_node_end_byte(val);

		if (strcmp(vt, "object") == 0) {
			parse_object_defaults(val, source, full_key + "::", property_defaults);
		} else if (strcmp(vt, "string") == 0) {
			property_defaults[prop_name] = String(source.substr(vs + 1, ve - vs - 2).c_str());
		} else if (strcmp(vt, "number") == 0) {
			property_defaults[prop_name] = std::stod(source.substr(vs, ve - vs));
		} else if (strcmp(vt, "true") == 0) {
			property_defaults[prop_name] = true;
		} else if (strcmp(vt, "false") == 0) {
			property_defaults[prop_name] = false;
		}
	}
}

static HashMap<StringName, Vector<PropertyInfo>> parse_interfaces(TSNode root_node, uint32_t child_count, const std::string &source, const String &file_path) {
	HashMap<StringName, Vector<PropertyInfo>> interfaces;

	// 解析当前文件的 interface
	collect_interfaces_from_node(root_node, child_count, source, interfaces);

	// 扫描 import 语句，从外部文件加载 interface
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "import_statement") != 0) continue;

		TSNode src_node = ts_node_child_by_field_name(child, "source", 6);
		if (ts_node_is_null(src_node)) continue;

		uint32_t ss = ts_node_start_byte(src_node);
		uint32_t se = ts_node_end_byte(src_node);
		std::string import_path = source.substr(ss + 1, se - ss - 2);
		if (import_path.find("./") != 0 && import_path.find("../") != 0) continue;

		String ts_path = file_path.get_base_dir().path_join(String(import_path.c_str()) + ".ts");
		if (!FileAccess::file_exists(ts_path)) continue;

		String ext_src_str = FileAccess::get_file_as_string(ts_path);
		std::string ext_src = ext_src_str.utf8().get_data();

		TSParser *ext_parser = ts_parser_new();
		ts_parser_set_language(ext_parser, tree_sitter_typescript());
		TSTree *ext_tree = ts_parser_parse_string(ext_parser, nullptr, ext_src.c_str(), ext_src.length());
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
					// 若 type 未能解析为已知 Variant 类型，保存原始类型名供嵌套检测
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

// 检查默认导出类是否带有 @Tool 装饰器（大小写均支持），运行时为空操作，由 tree-sitter 静态解析
static bool check_tool_decorator(TSNode root_node, uint32_t child_count, const std::string &source) {
	for (uint32_t i = 0; i < child_count; i++) {
		TSNode child = ts_node_child(root_node, i);
		if (strcmp(ts_node_type(child), "export_statement") != 0) {
			continue;
		}

		// 确认是 export default ... class
		bool is_default_export = false;
		for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
			if (strcmp(ts_node_type(ts_node_child(child, j)), "default") == 0) {
				is_default_export = true;
				break;
			}
		}
		if (!is_default_export) {
			continue;
		}

		for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
			TSNode en = ts_node_child(child, j);
			const char *en_type = ts_node_type(en);
			// 装饰器在 export_statement 上
			if (strcmp(en_type, "decorator") == 0) {
				uint32_t ds = ts_node_start_byte(en);
				uint32_t de = ts_node_end_byte(en);
				std::string deco = source.substr(ds, de - ds);
				if (deco == "@Tool" || deco == "@tool") {
					return true;
				}
			}
			// 装饰器在 class_declaration 内部
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

static TSNode find_default_class(TSNode root_node, uint32_t child_count) {
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
		}
	}
	return {};
}

static void parse_class_metadata(TSNode class_node, const std::string &source, StringName &class_name, StringName &base_class_name) {
	TSNode name_node = ts_node_child_by_field_name(class_node, "name", 4);
	if (!ts_node_is_null(name_node)) {
		uint32_t start = ts_node_start_byte(name_node);
		uint32_t end = ts_node_end_byte(name_node);
		class_name = StringName(source.substr(start, end - start).c_str());
	}

	for (uint32_t j = 0; j < ts_node_child_count(class_node); j++) {
		TSNode cn = ts_node_child(class_node, j);
		cn = ts_node_named_child(cn, 0);
		if (!ts_node_is_null(cn) && strcmp(ts_node_type(cn), "extends_clause") == 0) {
			for (uint32_t k = 0; k < ts_node_child_count(cn); k++) {
				TSNode hn = ts_node_child(cn, k);
				if (strcmp(ts_node_type(hn), "identifier") == 0) {
					uint32_t s = ts_node_start_byte(hn);
					uint32_t e = ts_node_end_byte(hn);
					base_class_name = StringName(source.substr(s, e - s).c_str());
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
	if (visited.has(iface_name)) return;
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
	// func_type_node 即为 function_type 节点：(params) => void
	if (strcmp(ts_node_type(func_type_node), "function_type") != 0) return;

	// 找 formal_parameters 子节点
	TSNode params = { 0 };
	for (uint32_t i = 0; i < ts_node_child_count(func_type_node); i++) {
		TSNode c = ts_node_child(func_type_node, i);
		if (strcmp(ts_node_type(c), "formal_parameters") == 0) {
			params = c;
			break;
		}
	}
	if (ts_node_is_null(params)) return;

	for (uint32_t i = 0; i < ts_node_child_count(params); i++) {
		TSNode param = ts_node_child(params, i);
		const char *ptype = ts_node_type(param);
		if (strcmp(ptype, "required_parameter") != 0 && strcmp(ptype, "optional_parameter") != 0) {
			continue;
		}

		TSNode pattern = ts_node_child_by_field_name(param, "pattern", 7);
		if (ts_node_is_null(pattern)) continue;
		uint32_t ps = ts_node_start_byte(pattern);
		uint32_t pe = ts_node_end_byte(pattern);

		PropertyInfo arg_pi;
		arg_pi.name = StringName(source.substr(ps, pe - ps).c_str());
		arg_pi.usage = PROPERTY_USAGE_DEFAULT;
		arg_pi.hint = PROPERTY_HINT_NONE;
		arg_pi.type = Variant::NIL;

		// 遍历子节点找 type_annotation（不依赖 field name）
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

static void parse_class_members(TSNode class_node, const std::string &source, HashMap<StringName, PropertyInfo> &properties, Vector<PropertyInfo> &property_list, HashMap<StringName, Variant> &property_defaults, HashMap<StringName, MethodInfo> &methods, HashMap<StringName, MethodInfo> &signals, HashMap<StringName, int> &member_lines, const HashMap<StringName, Vector<PropertyInfo>> &interfaces) {
	TSNode body_node = ts_node_child_by_field_name(class_node, "body", 4);
	if (ts_node_is_null(body_node)) {
		return;
	}

	for (uint32_t j = 0; j < ts_node_child_count(body_node); j++) {
		TSNode member = ts_node_child(body_node, j);
		const char *member_type = ts_node_type(member);

		if (strcmp(member_type, "public_field_definition") == 0) {
			// 扫描装饰器，检测 @Export 并解析其参数
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
				// 解析 @Export(...) 的参数：找 call_expression / decorator_call_expression
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
					TSNode first_arg = ts_node_named_child(args, 0);
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
							std::string key_str = source.substr(ts_node_start_byte(key), ts_node_end_byte(key) - ts_node_start_byte(key));
							uint32_t vs_b = ts_node_start_byte(val);
							uint32_t ve_b = ts_node_end_byte(val);
							std::string val_str = source.substr(vs_b, ve_b - vs_b);
							if (key_str == "hint") {
								Variant v = NodeRuntime::eval_expression(val_str);
								if (v.get_type() != Variant::NIL) {
									export_hint = (PropertyHint)(int)v;
								}
							} else if (key_str == "hintString") {
								if (val_str.size() >= 2 && (val_str.front() == '"' || val_str.front() == '\'')) {
									export_hint_string = String(val_str.substr(1, val_str.size() - 2).c_str());
								}
							}
						}
					} else {
						// @Export(hint) 或 @Export(hint, "hintString")
						uint32_t vs_b = ts_node_start_byte(first_arg);
						uint32_t ve_b = ts_node_end_byte(first_arg);
						Variant v = NodeRuntime::eval_expression(source.substr(vs_b, ve_b - vs_b));
						if (v.get_type() != Variant::NIL) {
							export_hint = (PropertyHint)(int)v;
						}
						if (nargs >= 2) {
							TSNode second_arg = ts_node_named_child(args, 1);
							if (strcmp(ts_node_type(second_arg), "string") == 0) {
								uint32_t ss = ts_node_start_byte(second_arg);
								uint32_t se = ts_node_end_byte(second_arg);
								std::string hs = source.substr(ss, se - ss);
								if (hs.size() >= 2) {
									export_hint_string = String(hs.substr(1, hs.size() - 2).c_str());
								}
							}
						}
					}
					break;
				}
			}

			// Signal<T> 类型注解：检测 fieldName!: Signal<(...) => void> 形式
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
								// 从 Signal<T> 的类型参数中取出 function_type 解析参数列表
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
				// 解析字段初始化器中的默认值
				if (!ts_node_is_null(field_value_node) && strcmp(ts_node_type(field_value_node), "object") == 0) {
					parse_object_defaults(field_value_node, source, prefix, property_defaults);
				}
			} else {
				properties[field_name] = pi;
				property_list.push_back(pi);

				if (!ts_node_is_null(field_value_node)) {
					const char *vt = ts_node_type(field_value_node);
					uint32_t vs = ts_node_start_byte(field_value_node);
					uint32_t ve = ts_node_end_byte(field_value_node);

					if (strcmp(vt, "string") == 0) {
						property_defaults[field_name] = String(source.substr(vs + 1, ve - vs - 2).c_str());
					} else if (strcmp(vt, "number") == 0) {
						std::string num_str = source.substr(vs, ve - vs);
						if (pi.type == Variant::INT) {
							property_defaults[field_name] = std::stoi(num_str);
						} else {
							property_defaults[field_name] = std::stod(num_str);
						}
					} else if (strcmp(vt, "true") == 0) {
						property_defaults[field_name] = true;
					} else if (strcmp(vt, "false") == 0) {
						property_defaults[field_name] = false;
					} else if (strcmp(vt, "new_expression") == 0) {
						property_defaults[field_name] = NodeRuntime::eval_expression(source.substr(vs, ve - vs));
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
			methods[method_name] = mi;
			member_lines[method_name] = ts_node_start_point(member).row + 1;
		}
	}
}

static void parse_exports_object(TSNode obj_node, const std::string &source, HashMap<StringName, PropertyInfo> &properties, HashMap<StringName, Variant> &property_defaults) {
	// obj_node 是外层 object，每个 pair 的 key 是属性名，value 是描述对象 { type, default, ... }
	for (uint32_t j = 0; j < ts_node_child_count(obj_node); j++) {
		TSNode pair = ts_node_child(obj_node, j);
		if (strcmp(ts_node_type(pair), "pair") != 0) {
			continue;
		}

		TSNode key = ts_node_child_by_field_name(pair, "key", 3);
		TSNode value = ts_node_child_by_field_name(pair, "value", 5);
		if (ts_node_is_null(key) || ts_node_is_null(value)) {
			continue;
		}
		if (strcmp(ts_node_type(value), "object") != 0) {
			continue;
		}

		uint32_t ks = ts_node_start_byte(key);
		uint32_t ke = ts_node_end_byte(key);
		StringName prop_name(source.substr(ks, ke - ks).c_str());

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
			TSNode fval = ts_node_child_by_field_name(field, "value", 5);
			if (ts_node_is_null(fkey) || ts_node_is_null(fval)) {
				continue;
			}

			uint32_t fks = ts_node_start_byte(fkey);
			uint32_t fke = ts_node_end_byte(fkey);
			std::string field_key = source.substr(fks, fke - fks);

			if (field_key == "type") {
				uint32_t vs = ts_node_start_byte(fval);
				uint32_t ve = ts_node_end_byte(fval);
				// string literal: strip quotes
				pi.type = parse_type_string(source.substr(vs + 1, ve - vs - 2));
			} else if (field_key == "default") {
				const char *vt = ts_node_type(fval);
				uint32_t vs = ts_node_start_byte(fval);
				uint32_t ve = ts_node_end_byte(fval);
				if (strcmp(vt, "string") == 0) {
					property_defaults[prop_name] = String(source.substr(vs + 1, ve - vs - 2).c_str());
				} else if (strcmp(vt, "number") == 0) {
					property_defaults[prop_name] = std::stod(source.substr(vs, ve - vs));
				} else if (strcmp(vt, "true") == 0) {
					property_defaults[prop_name] = true;
				} else if (strcmp(vt, "false") == 0) {
					property_defaults[prop_name] = false;
				}
			}
		}

		properties[prop_name] = pi;
	}
}

// static exports = {...} 在 TS 中是 class body 内的 public_field_definition（带 static 修饰）
static void parse_static_exports(TSNode class_node, const std::string &source, HashMap<StringName, PropertyInfo> &properties, HashMap<StringName, Variant> &property_defaults) {
	TSNode body = ts_node_child_by_field_name(class_node, "body", 4);
	if (ts_node_is_null(body)) {
		return;
	}

	for (uint32_t i = 0; i < ts_node_child_count(body); i++) {
		TSNode member = ts_node_child(body, i);
		if (strcmp(ts_node_type(member), "public_field_definition") != 0) {
			continue;
		}

		// 检查是否有 static 修饰
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

		// 检查字段名是否为 "exports"
		TSNode name = ts_node_child_by_field_name(member, "name", 4);
		if (ts_node_is_null(name)) {
			continue;
		}
		uint32_t ns = ts_node_start_byte(name);
		uint32_t ne = ts_node_end_byte(name);
		if (source.substr(ns, ne - ns) != "exports") {
			continue;
		}

		// 解析 value（object 字面量）
		TSNode value = ts_node_child_by_field_name(member, "value", 5);
		if (ts_node_is_null(value) || strcmp(ts_node_type(value), "object") != 0) {
			continue;
		}

		parse_exports_object(value, source, properties, property_defaults);
		return;
	}
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
	if (!FileAccess::file_exists(js_path)) {
		return false;
	}

	TSParser *parser = ts_parser_new();
	ts_parser_set_language(parser, tree_sitter_typescript());

	std::string source = source_code.utf8().get_data();
	TSTree *tree = ts_parser_parse_string(parser, NULL, source.c_str(), source.length());
	TSNode root_node = ts_tree_root_node(tree);

	class_name = StringName();
	base_class_name = StringName();
	property_list.clear();
	methods.clear();
	signals.clear();
	properties.clear();
	property_defaults.clear();
	constants.clear();
	member_lines.clear();

	uint32_t child_count = ts_node_child_count(root_node);
	is_tool_script = check_tool_decorator(root_node, child_count, source);
	TSNode class_node = find_default_class(root_node, child_count);

	if (ts_node_is_null(class_node)) {
		ts_tree_delete(tree);
		ts_parser_delete(parser);
		is_valid = false;
		return false;
	}

	HashMap<StringName, Vector<PropertyInfo>> interfaces = parse_interfaces(root_node, child_count, source, get_path());
	parse_class_metadata(class_node, source, class_name, base_class_name);
	parse_class_members(class_node, source, properties, property_list, property_defaults, methods, signals, member_lines, interfaces);
	parse_static_exports(class_node, source, properties, property_defaults);
	collect_parent_properties(base_class_name, source, root_node, child_count, get_path(), properties, property_defaults);

	ts_tree_delete(tree);
	ts_parser_delete(parser);

	is_valid = true;
	is_dirty = false;
	return true;
}

ScriptLanguage *Typescript::_get_language() const {
	return TypescriptLanguage::get_singleton();
}
