#include "support/typescript/typescript_saver.h"
#include "support/typescript/typescript.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/resource_uid.hpp>

using namespace godot;
using namespace gode;

TypescriptSaver *TypescriptSaver::singleton = nullptr;

TypescriptSaver *TypescriptSaver::get_singleton() {
	if (singleton) {
		return singleton;
	}
	singleton = memnew(TypescriptSaver);
	return singleton;
}

TypescriptSaver::~TypescriptSaver() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

Error TypescriptSaver::_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Ref<Typescript> ts = p_resource;
	if (!ts.is_valid()) {
		return Error::ERR_INVALID_PARAMETER;
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (!file.is_valid()) {
		return Error::ERR_CANT_OPEN;
	}
	file->store_string(ts->_get_source_code());
	file->close();
	return Error::OK;
}

Error TypescriptSaver::_set_uid(const String &p_path, int64_t p_uid) {
	ResourceUID::get_singleton()->set_id(p_uid, p_path);
	return Error::OK;
}

bool TypescriptSaver::_recognize(const Ref<Resource> &p_resource) const {
	if (!p_resource.is_valid()) return false;
	String path = p_resource->get_path();
	String ext = path.get_extension().to_lower();
	return ext == "ts" || ext == "tsx";
}

PackedStringArray TypescriptSaver::_get_recognized_extensions(const Ref<Resource> &p_resource) const {
	PackedStringArray arr;
	arr.push_back(String("ts"));
	arr.push_back(String("tsx"));
	return arr;
}

bool TypescriptSaver::_recognize_path(const Ref<Resource> &p_resource, const String &p_path) const {
	String ext = p_path.get_extension().to_lower();
	return ext == String("ts") || ext == String("tsx");
}
