#ifndef GODOT_JAVASCRIPT_INSTANCE_H
#define GODOT_JAVASCRIPT_INSTANCE_H

#include "script_module.h"
#include <napi.h>
#include <vector>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/templates/hash_map.hpp>

namespace gode {

class Javascript;

class JavascriptInstance {
	IScriptModule *module = nullptr;
	godot::Ref<godot::Script> script;
	godot::Object *owner = nullptr;
	Napi::ObjectReference js_instance;
	bool placeholder = false;
	godot::HashMap<godot::StringName, godot::Variant> placeholder_properties;

	// Caches for get_property_list — keeps StringName/String alive while the GDExtension array is in use
	mutable std::vector<godot::PropertyInfo> prop_list_cache;
	mutable std::vector<GDExtensionPropertyInfo> prop_list_gde;

	friend class JavascriptInstanceInfo;

private:
	bool compile_module();

public:
	JavascriptInstance(IScriptModule *p_module, godot::Ref<godot::Script> p_script, godot::Object *p_owner, bool p_placeholder);
	~JavascriptInstance();

	godot::Object *get_owner() const;
	bool is_placeholder() const;

	bool set(const godot::StringName &p_name, const godot::Variant &p_value);
	bool get(const godot::StringName &p_name, godot::Variant &r_value) const;

	bool has_method(const godot::StringName &p_method) const;
	int32_t get_method_argument_count(const godot::StringName &p_method, bool &r_is_valid) const;
	godot::Variant call(const godot::StringName &p_method, const godot::Variant *p_args, int32_t p_argcount, GDExtensionCallError &r_error);

	godot::String to_string(bool &r_is_valid) const;

	bool property_can_revert(const godot::StringName &p_name) const;
	bool property_get_revert(const godot::StringName &p_name, godot::Variant &r_ret) const;

	void get_property_list(const GDExtensionPropertyInfo *&r_list, uint32_t &r_count) const;
	void free_property_list(const GDExtensionPropertyInfo *p_list) const;
	void get_method_list(const GDExtensionMethodInfo *&r_list, uint32_t &r_count) const;
	void free_method_list(const GDExtensionMethodInfo *p_list) const;

	godot::Ref<godot::Script> get_script() const;
	IScriptModule *get_module() const;
};
} // namespace gode

#endif // GODOT_JAVASCRIPT_INSTANCE_H