#ifndef GODE_TYPESCRIPT_SCRIPT_H
#define GODE_TYPESCRIPT_SCRIPT_H

#include <napi.h>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace gode {

class ScriptInstance;

class TypeScriptScript : public godot::ScriptExtension {
	GDCLASS(TypeScriptScript, godot::ScriptExtension)

	friend ScriptInstance;

protected:
	mutable bool is_dirty = false;
	mutable bool is_valid = false;
	mutable bool is_tool_script = false;
	godot::String source_code;

	mutable godot::StringName class_name;
	mutable godot::StringName base_class_name;
	mutable godot::String base_script_path;
	mutable godot::HashMap<godot::StringName, godot::MethodInfo> methods;
	mutable godot::HashMap<godot::StringName, godot::Dictionary> rpc_configs;
	mutable godot::HashMap<godot::StringName, godot::MethodInfo> signals;
	mutable godot::HashMap<godot::StringName, godot::PropertyInfo> properties;
	mutable godot::Vector<godot::PropertyInfo> property_list;
	mutable godot::HashMap<godot::StringName, godot::Variant> property_defaults;
	mutable godot::HashMap<godot::StringName, godot::Variant> constants;
	mutable godot::HashMap<godot::StringName, int> member_lines;

	mutable Napi::FunctionReference default_class;

	mutable godot::HashSet<ScriptInstance *> instances;
	mutable godot::HashSet<ScriptInstance *> placeholder_instances;
	mutable godot::HashSet<godot::Object *> instance_objects;

protected:
	static void _bind_methods();

public:
	~TypeScriptScript();
	bool compile() const;
	godot::Error reload_source_code(const godot::String &p_code, bool p_keep_state);
	Napi::Function get_default_class() const;
	const godot::HashMap<godot::StringName, godot::PropertyInfo> &get_exported_properties() const { return properties; }
	const godot::Vector<godot::PropertyInfo> &get_property_list_ordered() const { return property_list; }
	const godot::HashMap<godot::StringName, godot::Variant> &get_property_defaults() const { return property_defaults; }
	godot::StringName get_base_class_name() const {
		compile();
		return base_class_name;
	}

	bool _editor_can_reload_from_file() override;
	void _placeholder_erased(void *p_placeholder) override;
	bool _can_instantiate() const override;
	godot::Ref<godot::Script> _get_base_script() const override;
	godot::StringName _get_global_name() const override;
	bool _inherits_script(const godot::Ref<godot::Script> &p_script) const override;
	godot::StringName _get_instance_base_type() const override;
	void *_instance_create(godot::Object *p_for_object) const override;
	void *_placeholder_instance_create(godot::Object *p_for_object) const override;
	bool _instance_has(godot::Object *p_object) const override;
	bool _has_source_code() const override;
	godot::String _get_source_code() const override;
	void _set_source_code(const godot::String &p_code) override;
	godot::Error _reload(bool p_keep_state) override;
	godot::StringName _get_doc_class_name() const override;
	godot::TypedArray<godot::Dictionary> _get_documentation() const override;
	godot::String _get_class_icon_path() const override;
	bool _has_method(const godot::StringName &p_method) const override;
	bool _has_static_method(const godot::StringName &p_method) const override;
	godot::Variant _get_script_method_argument_count(const godot::StringName &p_method) const override;
	godot::Dictionary _get_method_info(const godot::StringName &p_method) const override;
	bool _is_tool() const override;
	bool _is_valid() const override;
	bool _is_abstract() const override;
	godot::ScriptLanguage *_get_language() const override;
	godot::ScriptLanguage *get_script_language() const;
	godot::StringName get_global_name() const;
	bool _has_script_signal(const godot::StringName &p_signal) const override;
	godot::TypedArray<godot::Dictionary> _get_script_signal_list() const override;
	bool _has_property_default_value(const godot::StringName &p_property) const override;
	godot::Variant _get_property_default_value(const godot::StringName &p_property) const override;
	void _update_exports() override;
	godot::TypedArray<godot::Dictionary> _get_script_method_list() const override;
	godot::TypedArray<godot::Dictionary> _get_script_property_list() const override;
	int32_t _get_member_line(const godot::StringName &p_member) const override;
	godot::Dictionary _get_constants() const override;
	godot::TypedArray<godot::StringName> _get_members() const override;
	bool _is_placeholder_fallback_enabled() const override;
	godot::Variant _get_rpc_config() const override;
};

} // namespace gode

#endif // GODE_TYPESCRIPT_SCRIPT_H
