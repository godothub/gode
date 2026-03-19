#include "support/typescript/typescript_language.h"
#include "support/typescript/typescript.h"
#include <godot_cpp/core/memory.hpp>

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
	return String("Typescript");
}

String TypescriptLanguage::_get_type() const {
	return String("Typescript");
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

Object *TypescriptLanguage::_create_script() const {
	return memnew(Typescript);
}

Dictionary TypescriptLanguage::_get_global_class_name(const String &p_path) const {
	Dictionary d;
	return d;
}
