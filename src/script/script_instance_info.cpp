#include "script/script_instance_info.h"
#include "script/script_instance.h"
#include "script/typescript_language.h"
#include "script/typescript_script.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/memory.hpp>
#include <vector>

using namespace godot;

namespace gode {

static ScriptInstance *cast_instance(GDExtensionScriptInstanceDataPtr p_instance) {
	return reinterpret_cast<ScriptInstance *>(p_instance);
}

static GDExtensionBool script_instance_set(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	const Variant &value = *reinterpret_cast<const Variant *>(p_value);
	return instance->set(name, value);
}

static GDExtensionBool script_instance_get(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	Variant &ret = *reinterpret_cast<Variant *>(r_ret);
	return instance->get(name, ret);
}

static const GDExtensionPropertyInfo *script_instance_get_property_list(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		if (r_count) {
			*r_count = 0;
		}
		return nullptr;
	}
	const GDExtensionPropertyInfo *list = nullptr;
	uint32_t count = 0;
	instance->get_property_list(list, count);
	if (r_count) {
		*r_count = count;
	}
	return list;
}

static void script_instance_free_property_list(GDExtensionScriptInstanceDataPtr p_instance, const GDExtensionPropertyInfo *p_list, uint32_t p_count) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return;
	}
	instance->free_property_list(p_list);
}

static GDExtensionBool script_instance_get_class_category(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_class_category) {
	(void)p_instance;
	(void)p_class_category;
	return false;
}

static GDExtensionVariantType script_instance_get_property_type(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		if (r_is_valid) {
			*r_is_valid = false;
		}
		return GDEXTENSION_VARIANT_TYPE_NIL;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	Ref<TypeScriptScript> script = instance->get_script();
	if (script.is_valid()) {
		const HashMap<StringName, PropertyInfo> &properties = script->get_exported_properties();
		const PropertyInfo *property = properties.getptr(name);
		if (property) {
			if (r_is_valid) {
				*r_is_valid = true;
			}
			return (GDExtensionVariantType)property->type;
		}
	}
	if (r_is_valid) {
		*r_is_valid = false;
	}
	return GDEXTENSION_VARIANT_TYPE_NIL;
}

static GDExtensionBool script_instance_validate_property(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_property) {
	(void)p_instance;
	(void)p_property;
	return false;
}

static GDExtensionBool script_instance_property_can_revert(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	return instance->property_can_revert(name);
}

static GDExtensionBool script_instance_property_get_revert(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	Variant &ret = *reinterpret_cast<Variant *>(r_ret);
	return instance->property_get_revert(name, ret);
}

static GDExtensionObjectPtr script_instance_get_owner(GDExtensionScriptInstanceDataPtr p_instance) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return nullptr;
	}
	Object *owner = instance->get_owner();
	return owner ? owner->_owner : nullptr;
}

static void script_instance_get_property_state(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
	(void)p_instance;
	(void)p_add_func;
	(void)p_userdata;
}

static const GDExtensionMethodInfo *script_instance_get_method_list(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		if (r_count) {
			*r_count = 0;
		}
		return nullptr;
	}
	const GDExtensionMethodInfo *list = nullptr;
	uint32_t count = 0;
	instance->get_method_list(list, count);
	if (r_count) {
		*r_count = count;
	}
	return list;
}

static void script_instance_free_method_list(GDExtensionScriptInstanceDataPtr p_instance, const GDExtensionMethodInfo *p_list, uint32_t p_count) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return;
	}
	instance->free_method_list(p_list);
}

static GDExtensionBool script_instance_has_method(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	return instance->has_method(name);
}

static GDExtensionInt script_instance_get_method_argument_count(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		if (r_is_valid) {
			*r_is_valid = false;
		}
		return 0;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	bool valid = false;
	int32_t count = instance->get_method_argument_count(name, valid);
	if (r_is_valid) {
		*r_is_valid = valid;
	}
	return count;
}

static void script_instance_call(GDExtensionScriptInstanceDataPtr p_self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
	ScriptInstance *instance = cast_instance(p_self);
	if (!instance) {
		if (r_error) {
			r_error->error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
			r_error->argument = 0;
			r_error->expected = 0;
		}
		return;
	}
	const StringName &method = *reinterpret_cast<const StringName *>(p_method);
	const Variant *args = nullptr;
	int32_t argc = 0;
	std::vector<Variant> arg_storage;
	if (p_args && p_argument_count > 0) {
		arg_storage.reserve((size_t)p_argument_count);
		for (GDExtensionInt i = 0; i < p_argument_count; i++) {
			arg_storage.push_back(*reinterpret_cast<const Variant *>(p_args[i]));
		}
		args = arg_storage.data();
		argc = (int32_t)p_argument_count;
	}
	GDExtensionCallError local_error;
	GDExtensionCallError &err = r_error ? *r_error : local_error;
	Variant ret = instance->call(method, args, argc, err);
	if (r_return) {
		*reinterpret_cast<Variant *>(r_return) = ret;
	}
}

static void script_instance_notification(GDExtensionScriptInstanceDataPtr p_instance, int32_t p_what, GDExtensionBool p_reversed) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return;
	}
	instance->notification(p_what, p_reversed);
}

static void script_instance_to_string(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		if (r_is_valid) {
			*r_is_valid = false;
		}
		return;
	}
	bool valid = false;
	String text = instance->to_string(valid);
	if (r_is_valid) {
		*r_is_valid = valid;
	}
	if (valid && r_out) {
		*reinterpret_cast<String *>(r_out) = text;
	}
}

static void script_instance_refcount_incremented(GDExtensionScriptInstanceDataPtr p_instance) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (instance) {
		RefCounted *ref_counted = Object::cast_to<RefCounted>(instance->get_owner());
		if (ref_counted) {
			ref_counted->reference();
		}
	}
}

static GDExtensionBool script_instance_refcount_decremented(GDExtensionScriptInstanceDataPtr p_instance) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (instance) {
		RefCounted *ref_counted = Object::cast_to<RefCounted>(instance->get_owner());
		if (ref_counted) {
			return ref_counted->unreference();
		}
		return false;
	}
	return true;
}

static GDExtensionObjectPtr script_instance_get_script(GDExtensionScriptInstanceDataPtr p_instance) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return nullptr;
	}
	Ref<TypeScriptScript> script = instance->get_script();
	if (!script.is_valid()) {
		return nullptr;
	}
	return script->_owner;
}

static GDExtensionBool script_instance_is_placeholder(GDExtensionScriptInstanceDataPtr p_instance) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	return instance->is_placeholder();
}

static GDExtensionBool script_instance_set_fallback(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	return script_instance_set(p_instance, p_name, p_value);
}

static GDExtensionBool script_instance_get_fallback(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	return script_instance_get(p_instance, p_name, r_ret);
}

static GDExtensionScriptLanguagePtr script_instance_get_language(GDExtensionScriptInstanceDataPtr p_instance) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (instance) {
		Ref<TypeScriptScript> script = instance->get_script();
		if (script.is_valid()) {
			return script->_get_language()->_owner;
		}
	}
	return TypeScriptLanguage::get_singleton()->_owner;
}

static void script_instance_free(GDExtensionScriptInstanceDataPtr p_instance) {
	ScriptInstance *instance = cast_instance(p_instance);
	if (instance) {
		memdelete(instance);
	}
}

GDExtensionScriptInstanceInfo3 script_instance_info = {
	script_instance_set,
	script_instance_get,
	script_instance_get_property_list,
	script_instance_free_property_list,
	script_instance_get_class_category,
	script_instance_property_can_revert,
	script_instance_property_get_revert,
	script_instance_get_owner,
	script_instance_get_property_state,
	script_instance_get_method_list,
	script_instance_free_method_list,
	script_instance_get_property_type,
	script_instance_validate_property,
	script_instance_has_method,
	script_instance_get_method_argument_count,
	script_instance_call,
	script_instance_notification,
	script_instance_to_string,
	script_instance_refcount_incremented,
	script_instance_refcount_decremented,
	script_instance_get_script,
	script_instance_is_placeholder,
	script_instance_set_fallback,
	script_instance_get_fallback,
	script_instance_get_language,
	script_instance_free,
};

} // namespace gode
