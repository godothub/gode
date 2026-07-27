#ifndef GODE_SCRIPT_INSTANCE_H
#define GODE_SCRIPT_INSTANCE_H

#include <napi.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <vector>

namespace gode {

class TypeScriptScript;

class ScriptInstance {
	godot::Ref<TypeScriptScript> script;
	godot::Object *owner = nullptr;
	Napi::ObjectReference js_instance;
	bool placeholder = false;
	godot::HashMap<godot::StringName, godot::Variant> placeholder_properties;

	mutable std::vector<godot::PropertyInfo> prop_list_cache;
	mutable std::vector<GDExtensionPropertyInfo> prop_list_gde;
	mutable std::vector<godot::MethodInfo> method_list_cache;
	mutable std::vector<GDExtensionMethodInfo> method_list_gde;
	mutable std::vector<std::vector<godot::PropertyInfo>> method_arg_cache;
	mutable std::vector<std::vector<GDExtensionPropertyInfo>> method_arg_gde_cache;
	mutable std::vector<godot::PropertyInfo> method_return_cache;
	mutable std::vector<GDExtensionPropertyInfo> method_return_gde_cache;

private:
	void notification_bind(Napi::Object instance, int32_t p_what, bool p_reversed);

public:
	ScriptInstance(const godot::Ref<TypeScriptScript> &p_script, godot::Object *p_owner, bool p_placeholder);
	~ScriptInstance();

	godot::Object *get_owner() const;
	bool is_placeholder() const;
	bool is_runtime_instance_valid() const;

	bool set(const godot::StringName &p_name, const godot::Variant &p_value);
	bool get(const godot::StringName &p_name, godot::Variant &r_value) const;

	bool has_method(const godot::StringName &p_method) const;
	int32_t get_method_argument_count(const godot::StringName &p_method, bool &r_is_valid) const;
	godot::Variant call(const godot::StringName &p_method, const godot::Variant *p_args, int32_t p_argcount, GDExtensionCallError &r_error);
	void notification(int32_t p_what, bool p_reversed);
	godot::String to_string(bool &r_is_valid) const;

	bool property_can_revert(const godot::StringName &p_name) const;
	bool property_get_revert(const godot::StringName &p_name, godot::Variant &r_ret) const;

	void reload(bool p_keep_state);

	void get_property_list(const GDExtensionPropertyInfo *&r_list, uint32_t &r_count) const;
	void free_property_list(const GDExtensionPropertyInfo *p_list) const;
	void get_method_list(const GDExtensionMethodInfo *&r_list, uint32_t &r_count) const;
	void free_method_list(const GDExtensionMethodInfo *p_list) const;

	godot::Ref<TypeScriptScript> get_script() const;
};
} // namespace gode

#endif // GODE_SCRIPT_INSTANCE_H
