#include "runtime/gode_runtime_bridge.h"

#include "runtime/node_runtime.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;
using namespace gode;

void GodeRuntimeBridge::_bind_methods() {
#if GODE_EDITOR_BINDINGS
	ClassDB::bind_static_method(get_class_static(), D_METHOD("compile_typescript_project", "files"), &GodeRuntimeBridge::compile_typescript_project);
#endif
}

#if GODE_EDITOR_BINDINGS
Dictionary GodeRuntimeBridge::compile_typescript_project(const Array &p_files) {
	return NodeRuntime::compile_typescript_project(p_files);
}
#endif
