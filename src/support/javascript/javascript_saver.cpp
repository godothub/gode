#include "support/javascript/javascript_saver.h"
#include "support/javascript/javascript.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/resource_uid.hpp>

using namespace godot;
using namespace gode;

JavascriptSaver *JavascriptSaver::singleton = nullptr;

JavascriptSaver *JavascriptSaver::get_singleton() {
	if (singleton) {
		return singleton;
	}
	singleton = memnew(JavascriptSaver);
	// if (likely(singleton)) {
	// 	ClassDB::_register_engine_singleton(JavascriptSaver::get_class_static(), singleton);
	// }
	return singleton;
}

void JavascriptSaver::_bind_methods() {
}

JavascriptSaver::~JavascriptSaver() {
	if (singleton == this) {
		// ClassDB::_unregister_engine_singleton(JavascriptSaver::get_class_static());
		// memdelete(singleton);
		singleton = nullptr;
	}
}

Error JavascriptSaver::_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Javascript *js = reinterpret_cast<Javascript *>(p_resource.ptr());
	if (!js) {
		return Error::ERR_INVALID_PARAMETER;
	}

	String source_code = js->_get_source_code();

	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (!file.is_valid()) {
		return Error::ERR_CANT_OPEN;
	}

	if (p_flags & ResourceSaver::SaverFlags::FLAG_RELATIVE_PATHS) {
		// 当前脚本没有依赖路径表，这里暂时不做特殊处理
	}

	if (p_flags & ResourceSaver::SaverFlags::FLAG_BUNDLE_RESOURCES) {
		// Javascript 资源本身就是文本，不打包子资源
	}

	if (p_flags & ResourceSaver::SaverFlags::FLAG_CHANGE_PATH) {
		// Godot 负责更新资源内部路径，这里只负责写文件
	}

	file->store_string(source_code);
	file->close();

	return Error::OK;
}

Error JavascriptSaver::_set_uid(const String &p_path, int64_t p_uid) {
	ResourceUID::get_singleton()->set_id(p_uid, p_path);
	return Error::OK;
}

bool JavascriptSaver::_recognize(const Ref<Resource> &p_resource) const {
	if (!p_resource.is_valid()) return false;
	String path = p_resource->get_path();
	String ext = path.get_extension().to_lower();
	return ext == "js";
}

PackedStringArray JavascriptSaver::_get_recognized_extensions(const Ref<Resource> &p_resource) const {
	PackedStringArray arr;
	arr.push_back(String("js"));
	return arr;
}

bool JavascriptSaver::_recognize_path(const Ref<Resource> &p_resource, const String &p_path) const {
	return p_path.get_extension().to_lower() == String("js");
}
