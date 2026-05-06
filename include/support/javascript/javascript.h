#ifndef GODOT_GODE_JAVASCRIPT_H
#define GODOT_GODE_JAVASCRIPT_H

#include "javascript_instance.h"

#include <napi.h>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>

namespace gode {

class JavascriptInstance;

class Javascript : public godot::ScriptExtension {
	GDCLASS(Javascript, godot::ScriptExtension)

	friend JavascriptInstance;

protected:
	mutable bool is_dirty = false;
	mutable bool is_valid = false;
	mutable bool is_tool_script = false;
	godot::String source_code;

	mutable godot::StringName class_name;
	mutable godot::StringName base_class_name;
	mutable godot::HashMap<godot::StringName, godot::MethodInfo> methods;
	mutable godot::HashMap<godot::StringName, godot::MethodInfo> signals;
	mutable godot::HashMap<godot::StringName, godot::PropertyInfo> properties;
	mutable godot::Vector<godot::PropertyInfo> property_list; // ordered, may include GROUP entries
	mutable godot::HashMap<godot::StringName, godot::Variant> property_defaults;
	mutable godot::HashMap<godot::StringName, godot::Variant> constants;
	mutable godot::HashMap<godot::StringName, int> member_lines;

	mutable Napi::FunctionReference default_class;

	mutable godot::HashSet<JavascriptInstance *> instances;
	mutable godot::HashSet<JavascriptInstance *> placeholder_instances;
	mutable godot::HashSet<godot::Object *> instance_objects;

public:
	virtual bool compile() const;
	Napi::Function get_default_class() const;
	const godot::HashMap<godot::StringName, godot::PropertyInfo> &get_exported_properties() const { return properties; }
	const godot::Vector<godot::PropertyInfo> &get_property_list_ordered() const { return property_list; }
	const godot::HashMap<godot::StringName, godot::Variant> &get_property_defaults() const { return property_defaults; }
	godot::StringName get_base_class_name() const { compile(); return base_class_name; }

protected:
	static void _bind_methods();

public:
	bool _editor_can_reload_from_file();
	void _placeholder_erased(void *p_placeholder);
	bool _can_instantiate() const;
	godot::Ref<godot::Script> _get_base_script() const;
	godot::StringName _get_global_name() const;
	bool _inherits_script(const godot::Ref<godot::Script> &p_script) const;
	godot::StringName _get_instance_base_type() const;
	void *_instance_create(godot::Object *p_for_object) const;
	void *_placeholder_instance_create(godot::Object *p_for_object) const;
	bool _instance_has(godot::Object *p_object) const;
	bool _has_source_code() const;
	godot::String _get_source_code() const;
	void _set_source_code(const godot::String &p_code);
	godot::Error _reload(bool p_keep_state);
	godot::StringName _get_doc_class_name() const;
	godot::TypedArray<godot::Dictionary> _get_documentation() const;
	godot::String _get_class_icon_path() const;
	bool _has_method(const godot::StringName &p_method) const;
	bool _has_static_method(const godot::StringName &p_method) const;
	godot::Variant _get_script_method_argument_count(const godot::StringName &p_method) const;
	godot::Dictionary _get_method_info(const godot::StringName &p_method) const;
	bool _is_tool() const;
	bool _is_valid() const;
	bool _is_abstract() const;
	godot::ScriptLanguage *_get_language() const;
	godot::ScriptLanguage *get_script_language() const;
	godot::StringName get_global_name() const;
	bool _has_script_signal(const godot::StringName &p_signal) const;
	godot::TypedArray<godot::Dictionary> _get_script_signal_list() const;
	bool _has_property_default_value(const godot::StringName &p_property) const;
	godot::Variant _get_property_default_value(const godot::StringName &p_property) const;
	void _update_exports();
	godot::TypedArray<godot::Dictionary> _get_script_method_list() const;
	godot::TypedArray<godot::Dictionary> _get_script_property_list() const;
	int32_t _get_member_line(const godot::StringName &p_member) const;
	godot::Dictionary _get_constants() const;
	godot::TypedArray<godot::StringName> _get_members() const;
	bool _is_placeholder_fallback_enabled() const;
	godot::Variant _get_rpc_config() const;
};
} //namespace gode

#endif // GODE_JAVASCRIPT_H
