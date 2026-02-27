#ifndef GODOT_JAVASCRIPT_INSTANCE_H
#define GODOT_JAVASCRIPT_INSTANCE_H

#include "support/javascript/javascript.h"
#include <napi.h>

namespace gode {

class JavascriptInstance {
	godot::Ref<Javascript> javascript;
	godot::Object *owner = nullptr;
	bool placeholder = false;
	Napi::ObjectReference js_instance;

	friend class JavascriptInstanceInfo;

private:
	bool compile_module();

public:
	JavascriptInstance(const godot::Ref<Javascript> &p_javascript, godot::Object *p_owner, bool p_placeholder);

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
	void get_method_list(const GDExtensionMethodInfo *&r_list, uint32_t &r_count) const;

	godot::Ref<Javascript> get_script() const;

};
} // namespace gode


#endif // GODOT_JAVASCRIPT_INSTANCE_H