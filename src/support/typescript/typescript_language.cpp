#include "support/typescript/typescript_language.h"
#include "support/typescript/typescript.h"
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

using namespace godot;
using namespace gode;

TypescriptLanguage *TypescriptLanguage::singleton = nullptr;

TypescriptLanguage::~TypescriptLanguage() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

TypescriptLanguage *TypescriptLanguage::get_singleton() {
	if (singleton) {
		return singleton;
	}
	singleton = memnew(TypescriptLanguage);
	return singleton;
}

String TypescriptLanguage::_get_name() const {
	return String("TypeScript");
}

String TypescriptLanguage::_get_type() const {
	return String("TypeScript");
}

String TypescriptLanguage::_get_extension() const {
	return String("ts");
}

PackedStringArray TypescriptLanguage::_get_recognized_extensions() const {
	PackedStringArray arr;
	arr.push_back(String("ts"));
	arr.push_back(String("tsx"));
	return arr;
}

Ref<Script> TypescriptLanguage::_make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	Ref<Typescript> script;
	script.instantiate();

	String class_name = p_class_name;
	if (class_name.is_empty()) {
		class_name = String("NewScript");
	}

	String base_name = p_base_class_name;
	if (base_name.is_empty()) {
		base_name = String("Node");
	}

	String code;
	code += String("import { ") + base_name + String(" } from \"godot\";\n\n");
	code += String("export default class ") + class_name + String(" extends ") + base_name + String(" {\n");
	code += String("\t_ready(): void {\n");
	code += String("\t}\n");
	code += String("}\n");

	script->_set_source_code(code);
	return script;
}

Object *TypescriptLanguage::_create_script() const {
	return memnew(Typescript);
}

Dictionary TypescriptLanguage::_get_global_class_name(const String &p_path) const {
	Dictionary d;
	Ref<Typescript> script = ResourceLoader::get_singleton()->load(p_path, "", ResourceLoader::CACHE_MODE_REUSE);
	if (script.is_null()) {
		return d;
	}
	StringName name = script->_get_global_name();
	if (name == StringName()) {
		return d;
	}
	d["name"] = name;
	d["base_type"] = script->get_base_class_name();
	return d;
}
