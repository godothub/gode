#include "runtime/gode_runtime_bridge.h"

#include "runtime/node_runtime.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;
using namespace gode;

void GodeRuntimeBridge::_bind_methods() {
#if GODE_EDITOR_BINDINGS
	ClassDB::bind_static_method(get_class_static(), D_METHOD("compile_typescript_project", "files"), &GodeRuntimeBridge::compile_typescript_project);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("collect_typescript_project_sources", "project_root"), &GodeRuntimeBridge::collect_typescript_project_sources);
#endif
}

#if GODE_EDITOR_BINDINGS
Dictionary GodeRuntimeBridge::compile_typescript_project(const Array &p_files) {
	return NodeRuntime::compile_typescript_project(p_files);
}

Dictionary GodeRuntimeBridge::collect_typescript_project_sources(const String &p_project_root) {
	return NodeRuntime::collect_typescript_project_sources(p_project_root);
}
#endif
