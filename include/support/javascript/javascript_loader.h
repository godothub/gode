#ifndef GODOT_JAVASCRIPT_LOADER_H
#define GODOT_JAVASCRIPT_LOADER_H

#include "support/javascript/javascript.h"
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/templates/hash_map.hpp>

namespace gode {

class JavascriptLoader : public godot::ResourceFormatLoader {
	GDCLASS(JavascriptLoader, godot::ResourceFormatLoader);

private:
	JavascriptLoader() = default;

public:
	~JavascriptLoader();
	static JavascriptLoader *get_singleton();

private:
	static JavascriptLoader *singleton;
	mutable godot::HashMap<godot::StringName, godot::Ref<Javascript>> scripts;

protected:
	static void _bind_methods();

public:
	godot::PackedStringArray _get_recognized_extensions() const override;
	bool _recognize_path(const godot::String &p_path, const godot::StringName &p_type) const override;
	bool _handles_type(const godot::StringName &p_type) const override;
	godot::String _get_resource_type(const godot::String &p_path) const override;
	godot::String _get_resource_script_class(const godot::String &p_path) const override;
	int64_t _get_resource_uid(const godot::String &p_path) const override;
	godot::PackedStringArray _get_dependencies(const godot::String &p_path, bool p_add_types) const override;
	godot::Error _rename_dependencies(const godot::String &p_path, const godot::Dictionary &p_renames) const override;
	bool _exists(const godot::String &p_path) const override;
	godot::PackedStringArray _get_classes_used(const godot::String &p_path) const override;
	godot::Variant _load(const godot::String &p_path, const godot::String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const override;
};
} //namespace gode

#endif // GODOT_JAVASCRIPT_LOADER_H
