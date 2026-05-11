#include "support/javascript/javascript_loader.h"

#include "godot_cpp/classes/resource_uid.hpp"
#include "support/javascript/javascript.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <regex>
#include <unordered_set>

using namespace godot;
using namespace gode;

namespace {

String resolve_script_dependency(const String &p_owner_path, const String &p_dependency, const char *p_default_extension) {
	if (p_dependency.begins_with("res://") || p_dependency.begins_with("uid://")) {
		return p_dependency;
	}
	if (p_dependency.begins_with("./") || p_dependency.begins_with("../")) {
		String resolved = p_owner_path.get_base_dir().path_join(p_dependency).simplify_path();
		if (resolved.get_extension().is_empty()) {
			resolved += ".";
			resolved += p_default_extension;
		}
		return resolved;
	}
	return String();
}

PackedStringArray scan_script_dependencies(const String &p_path, const char *p_default_extension) {
	PackedStringArray deps;
	if (!FileAccess::file_exists(p_path)) {
		return deps;
	}

	std::string source = FileAccess::get_file_as_string(p_path).utf8().get_data();
	std::unordered_set<std::string> seen;
	const std::regex patterns[] = {
		std::regex(R"((?:preload|load|require)\s*\(\s*["']([^"']+)["'])"),
		std::regex(R"(ResourceLoader\s*\.\s*load\s*\(\s*["']([^"']+)["'])"),
		std::regex(R"(import\s+(?:[^"']+\s+from\s+)?["']([^"']+)["'])")
	};

	for (const std::regex &pattern : patterns) {
		for (std::sregex_iterator it(source.begin(), source.end(), pattern), end; it != end; ++it) {
			String dep = resolve_script_dependency(p_path, String((*it)[1].str().c_str()), p_default_extension);
			if (dep.is_empty()) {
				continue;
			}
			std::string key = dep.utf8().get_data();
			if (seen.insert(key).second) {
				deps.push_back(dep);
			}
		}
	}

	return deps;
}

} // namespace

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
	String ext = p_path.get_extension().to_lower();
	return ext == String("js");
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
	Ref<Javascript> script;
	if (scripts.has(p_path)) {
		script = scripts.get(p_path);
	} else if (FileAccess::file_exists(p_path)) {
		Javascript *loaded = memnew(Javascript);
		loaded->_set_source_code(FileAccess::get_file_as_string(p_path));
		script = Ref<Javascript>(loaded);
		scripts[p_path] = script;
	}
	if (script.is_valid()) {
		StringName global_name = script->_get_global_name();
		if (global_name != StringName()) {
			return String(global_name);
		}
	}
	return String();
}

int64_t JavascriptLoader::_get_resource_uid(const String &p_path) const {
	return ResourceUID::get_singleton()->text_to_id(p_path);
}

PackedStringArray JavascriptLoader::_get_dependencies(const String &p_path, bool p_add_types) const {
	return scan_script_dependencies(p_path, "js");
}

Error JavascriptLoader::_rename_dependencies(const String &p_path, const Dictionary &p_renames) const {
	return Error::OK;
}

bool JavascriptLoader::_exists(const String &p_path) const {
	return FileAccess::file_exists(p_path);
}

PackedStringArray JavascriptLoader::_get_classes_used(const String &p_path) const {
	PackedStringArray classes;
	String class_name = _get_resource_script_class(p_path);
	if (!class_name.is_empty()) {
		classes.push_back(class_name);
	}
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
