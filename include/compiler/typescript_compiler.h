#ifndef GODE_TYPESCRIPT_COMPILER_H
#define GODE_TYPESCRIPT_COMPILER_H

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace gode {

class GodeTypeScriptCompiler : public godot::Object {
	GDCLASS(GodeTypeScriptCompiler, godot::Object)

protected:
	static void _bind_methods();

public:
	godot::Dictionary compile_project(bool p_force = false);
	godot::Dictionary compile_script(const godot::String &p_source_path, bool p_force = false);
	godot::String get_compiled_path(const godot::String &p_source_path) const;
	godot::String get_exported_path(const godot::String &p_source_path) const;
	godot::String get_cache_root() const;

	static godot::String compiled_path_for_source(const godot::String &p_source_path);
	static godot::String exported_path_for_source(const godot::String &p_source_path);
	static bool ensure_script_compiled(const godot::String &p_source_path, godot::String *r_compiled_path = nullptr);
};

} // namespace gode

#endif // GODE_TYPESCRIPT_COMPILER_H
