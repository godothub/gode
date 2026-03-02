#include "support/javascript/javascript_loader.h"

#include "godot_cpp/classes/resource_uid.hpp"
#include "support/javascript/javascript.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

using namespace godot;
using namespace gode;

JavascriptLoader *JavascriptLoader::singleton = nullptr;

JavascriptLoader *JavascriptLoader::get_singleton() {
	if (singleton) {
		return singleton;
	}
	singleton = memnew(JavascriptLoader);
	// if (likely(singleton)) {
	// 	ClassDB::_register_engine_singleton(JavascriptLoader::get_class_static(), singleton);
	// }
	return singleton;
}

void JavascriptLoader::_bind_methods() {
}

JavascriptLoader::~JavascriptLoader() {
	if (singleton == this) {
		// ClassDB::_unregister_engine_singleton(JavascriptLoader::get_class_static());
		// memdelete(singleton);
		singleton = nullptr;
	}
}

PackedStringArray JavascriptLoader::_get_recognized_extensions() const {
	PackedStringArray arr;
	arr.push_back(String("js"));
	return arr;
}

bool JavascriptLoader::_recognize_path(const String &p_path, const StringName &p_type) const {
	return p_path.get_extension().to_lower() == String("js");
}

bool JavascriptLoader::_handles_type(const StringName &p_type) const {
	return p_type == StringName("Script");
}

String JavascriptLoader::_get_resource_type(const String &p_path) const {
	if (p_path.get_extension().to_lower() == String("js")) {
		return String("Script");
	}
	return String();
}

String JavascriptLoader::_get_resource_script_class(const String &p_path) const {
	return String();
}

int64_t JavascriptLoader::_get_resource_uid(const String &p_path) const {
	return ResourceUID::get_singleton()->text_to_id(p_path);
}

PackedStringArray JavascriptLoader::_get_dependencies(const String &p_path, bool p_add_types) const {
	PackedStringArray deps;
	return deps;
}

Error JavascriptLoader::_rename_dependencies(const String &p_path, const Dictionary &p_renames) const {
	return Error::OK;
}

bool JavascriptLoader::_exists(const String &p_path) const {
	return FileAccess::file_exists(p_path);
}

PackedStringArray JavascriptLoader::_get_classes_used(const String &p_path) const {
	PackedStringArray classes;
	return classes;
}

Variant JavascriptLoader::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
	if (p_cache_mode == ResourceLoader::CacheMode::CACHE_MODE_REUSE && scripts.has(p_path)) {
		return scripts.get(p_path);
	}
	String source_code = FileAccess::get_file_as_string(p_original_path);
	Javascript *script = memnew(Javascript);
	script->_set_source_code(source_code);
	scripts[p_path] = Ref(script);
	return script;
}
