#ifndef GODE_RUNTIME_BRIDGE_H
#define GODE_RUNTIME_BRIDGE_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace gode {

class GodeRuntimeBridge : public godot::Object {
	GDCLASS(GodeRuntimeBridge, godot::Object)

protected:
	static void _bind_methods();

public:
	static godot::Dictionary compile_typescript_project(const godot::Array &p_files);
	static godot::Dictionary collect_typescript_project_sources(const godot::String &p_project_root);
};

} // namespace gode

#endif // GODE_RUNTIME_BRIDGE_H
