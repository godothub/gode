#include "support/javascript/javascript_instance_info.h"
#include "godot_cpp/core/memory.hpp"
#include "support/javascript/javascript_instance.h"
#include "support/javascript/javascript_language.h"
#include <godot_cpp/classes/script_language.hpp>

using namespace godot;

namespace gode {

static JavascriptInstance *cast_instance(GDExtensionScriptInstanceDataPtr p_instance) {
	return reinterpret_cast<JavascriptInstance *>(p_instance);
}

static GDExtensionBool javascript_instance_set(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	const Variant &value = *reinterpret_cast<const Variant *>(p_value);
	return instance->set(name, value);
}

static GDExtensionBool javascript_instance_get(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	Variant &ret = *reinterpret_cast<Variant *>(r_ret);
	return instance->get(name, ret);
}

static const GDExtensionPropertyInfo *javascript_instance_get_property_list(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
	JavascriptInstance *instance = cast_instance(p_instance);
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

static void javascript_instance_free_property_list(GDExtensionScriptInstanceDataPtr p_instance, const GDExtensionPropertyInfo *p_list, uint32_t p_count) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return;
	}
	instance->free_property_list(p_list);
}

static GDExtensionBool javascript_instance_get_class_category(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_class_category) {
	(void)p_instance;
	(void)p_class_category;
	return false;
}

static GDExtensionVariantType javascript_instance_get_property_type(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	JavascriptInstance *instance = cast_instance(p_instance);
	(void)instance;
	(void)p_name;
	if (r_is_valid) {
		*r_is_valid = false;
	}
	return GDEXTENSION_VARIANT_TYPE_NIL;
}

static GDExtensionBool javascript_instance_validate_property(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionPropertyInfo *p_property) {
	(void)p_instance;
	(void)p_property;
	return false;
}

static GDExtensionBool javascript_instance_property_can_revert(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	return instance->property_can_revert(name);
}

static GDExtensionBool javascript_instance_property_get_revert(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	Variant &ret = *reinterpret_cast<Variant *>(r_ret);
	return instance->property_get_revert(name, ret);
}

static GDExtensionObjectPtr javascript_instance_get_owner(GDExtensionScriptInstanceDataPtr p_instance) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return nullptr;
	}
	Object *owner = instance->get_owner();
	return owner ? owner->_owner : nullptr;
}

static void javascript_instance_get_property_state(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionScriptInstancePropertyStateAdd p_add_func, void *p_userdata) {
	(void)p_instance;
	(void)p_add_func;
	(void)p_userdata;
}

static const GDExtensionMethodInfo *javascript_instance_get_method_list(GDExtensionScriptInstanceDataPtr p_instance, uint32_t *r_count) {
	JavascriptInstance *instance = cast_instance(p_instance);
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

static void javascript_instance_free_method_list(GDExtensionScriptInstanceDataPtr p_instance, const GDExtensionMethodInfo *p_list, uint32_t p_count) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return;
	}
	instance->free_method_list(p_list);
}

static GDExtensionBool javascript_instance_has_method(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	const StringName &name = *reinterpret_cast<const StringName *>(p_name);
	return instance->has_method(name);
}

static GDExtensionInt javascript_instance_get_method_argument_count(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	JavascriptInstance *instance = cast_instance(p_instance);
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

static void javascript_instance_call(GDExtensionScriptInstanceDataPtr p_self, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argument_count, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
	JavascriptInstance *instance = cast_instance(p_self);
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
	if (p_args && p_argument_count > 0) {
		args = *reinterpret_cast<const Variant *const *>(p_args);
		argc = (int32_t)p_argument_count;
	}
	GDExtensionCallError local_error;
	GDExtensionCallError &err = r_error ? *r_error : local_error;
	Variant ret = instance->call(method, args, argc, err);
	if (r_return) {
		*reinterpret_cast<Variant *>(r_return) = ret;
	}
}

static void javascript_instance_notification(GDExtensionScriptInstanceDataPtr p_instance, int32_t p_what, GDExtensionBool p_reversed) {
	(void)p_instance;
	(void)p_what;
	(void)p_reversed;
}

static void javascript_instance_to_string(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
	JavascriptInstance *instance = cast_instance(p_instance);
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

static void javascript_instance_refcount_incremented(GDExtensionScriptInstanceDataPtr p_instance) {
	(void)p_instance;
}

static GDExtensionBool javascript_instance_refcount_decremented(GDExtensionScriptInstanceDataPtr p_instance) {
	(void)p_instance;
	return false;
}

static GDExtensionObjectPtr javascript_instance_get_script(GDExtensionScriptInstanceDataPtr p_instance) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return nullptr;
	}
	Ref<Script> script = instance->get_script();
	if (!script.is_valid()) {
		return nullptr;
	}
	return script->_owner;
}

static GDExtensionBool javascript_instance_is_placeholder(GDExtensionScriptInstanceDataPtr p_instance) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (!instance) {
		return false;
	}
	return instance->is_placeholder();
}

static GDExtensionBool javascript_instance_set_fallback(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	return true;
}

static GDExtensionBool javascript_instance_get_fallback(GDExtensionScriptInstanceDataPtr p_instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	return true;
}

static GDExtensionScriptLanguagePtr javascript_instance_get_language(GDExtensionScriptInstanceDataPtr p_instance) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (instance && instance->get_module()) {
		ScriptLanguage *lang = instance->get_module()->get_script_language();
		if (lang) return lang->_owner;
	}
	return JavascriptLanguage::get_singleton()->_owner;
}

static void javascript_instance_free(GDExtensionScriptInstanceDataPtr p_instance) {
	JavascriptInstance *instance = cast_instance(p_instance);
	if (instance) {
		memdelete(instance);
	}
}

GDExtensionScriptInstanceInfo3 javascript_instance_info = {
	javascript_instance_set,
	javascript_instance_get,
	javascript_instance_get_property_list,
	javascript_instance_free_property_list,
	javascript_instance_get_class_category,
	javascript_instance_property_can_revert,
	javascript_instance_property_get_revert,
	javascript_instance_get_owner,
	javascript_instance_get_property_state,
	javascript_instance_get_method_list,
	javascript_instance_free_method_list,
	javascript_instance_get_property_type,
	javascript_instance_validate_property,
	javascript_instance_has_method,
	javascript_instance_get_method_argument_count,
	javascript_instance_call,
	javascript_instance_notification,
	javascript_instance_to_string,
	javascript_instance_refcount_incremented,
	javascript_instance_refcount_decremented,
	javascript_instance_get_script,
	javascript_instance_is_placeholder,
	javascript_instance_set_fallback,
	javascript_instance_get_fallback,
	javascript_instance_get_language,
	javascript_instance_free,
};

} // namespace gode
