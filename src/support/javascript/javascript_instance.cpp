#include "support/javascript/javascript_instance.h"

#include "utils/node_runtime.h"
#include "utils/value_convert.h"
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>

using namespace godot;

namespace gode {

JavascriptInstance::JavascriptInstance(IScriptModule *p_module, Ref<Script> p_script, Object *p_owner, bool p_placeholder) :
		module(p_module),
		script(p_script),
		owner(p_owner),
		placeholder(p_placeholder) {
	if (!placeholder) {
		if (!module) {
			return;
		}

		if (!module->compile()) {
			return;
		}

		v8::Locker locker(NodeRuntime::isolate);
		v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
		Napi::HandleScope scope(JsEnvManager::get_env());

		Napi::Function default_class = module->get_default_class();
		if (default_class.IsEmpty() || default_class.IsUndefined() || default_class.IsNull()) {
			return;
		}

		Napi::Value external_owner = Napi::External<godot::Object>::New(JsEnvManager::get_env(), owner);
		Napi::Object instance = default_class.New({ external_owner });
		js_instance = Napi::Persistent(instance);
	}
}

JavascriptInstance::~JavascriptInstance() {
	if (!js_instance.IsEmpty()) {
		js_instance.Reset();
	}
}

Object *JavascriptInstance::get_owner() const {
	return owner;
}

bool JavascriptInstance::is_placeholder() const {
	return placeholder;
}

bool JavascriptInstance::set(const StringName &p_name, const Variant &p_value) {
	if (placeholder) {
		placeholder_properties[p_name] = p_value;
		return true;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	return js_instance.Set(String(p_name).utf8().get_data(), godot_to_napi(JsEnvManager::get_env(), p_value));
}

bool JavascriptInstance::get(const StringName &p_name, Variant &r_value) const {
	if (placeholder) {
		if (placeholder_properties.has(p_name)) {
			r_value = placeholder_properties[p_name];
			return true;
		}
		if (module && module->_has_property_default_value(p_name)) {
			r_value = module->_get_property_default_value(p_name);
			return true;
		}
		return false;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	Napi::Object obj = js_instance.Value();
	const char *prop_name = String(p_name).utf8().get_data();
	if (obj.HasOwnProperty(prop_name)) {
		Napi::Value val = js_instance.Get(prop_name);
		r_value = napi_to_godot(val);
		return true;
	}
	return false;
}

bool JavascriptInstance::has_method(const StringName &p_method) const {
	if (placeholder) {
		return module ? module->_has_method(p_method) : false;
	}
	if (js_instance.IsEmpty()) {
		return false;
	}
	v8::Locker locker(NodeRuntime::isolate);
	v8::HandleScope scope(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	Napi::Function cls = module ? module->get_default_class() : Napi::Function();
	if (cls.IsEmpty() || !cls.IsFunction()) {
		return false;
	}
	Napi::Value proto_val = cls.Get("prototype");
	if (!proto_val.IsObject()) {
		return false;
	}
	Napi::Object proto = proto_val.As<Napi::Object>();
	std::string method_name = String(p_method).utf8().get_data();
	return proto.HasOwnProperty(method_name) && proto.Get(method_name).IsFunction();
}

int32_t JavascriptInstance::get_method_argument_count(const StringName &p_method, bool &r_is_valid) const {
	v8::Locker locker(NodeRuntime::isolate);
	(void)p_method;
	r_is_valid = false;
	return 0;
}

Variant JavascriptInstance::call(const StringName &p_method, const Variant *p_args, int32_t p_argcount, GDExtensionCallError &r_error) {
	if (placeholder) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	if (Engine::get_singleton()->is_editor_hint()) {
		r_error.error = GDExtensionCallErrorType::GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	if (js_instance.IsEmpty()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}

	v8::Locker locker(NodeRuntime::isolate);

	Napi::HandleScope scope(JsEnvManager::get_env());
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	Napi::Object instance = js_instance.Value();
	std::string method_name = String(p_method).utf8().get_data();

	if (!instance.Has(method_name)) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	Napi::Value method_val = instance.Get(method_name);
	if (!method_val.IsFunction()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	Napi::Function method = method_val.As<Napi::Function>();
	std::vector<napi_value> args;
	for (int i = 0; i < p_argcount; ++i) {
		args.push_back(godot_to_napi(JsEnvManager::get_env(), p_args[i]));
	}

	Napi::Value result = method.Call(instance, args);
	r_error.error = GDEXTENSION_CALL_OK;
	return napi_to_godot(result);
}

String JavascriptInstance::to_string(bool &r_is_valid) const {
	if (placeholder) {
		r_is_valid = true;
		return "JavascriptInstance(Placeholder)";
	}
	if (js_instance.IsEmpty()) {
		r_is_valid = false;
		return String();
	}
	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	Napi::HandleScope scope(JsEnvManager::get_env());
	Napi::Object obj = js_instance.Value();
	Napi::Value proto_val = obj.Get("__proto__");
	if (proto_val.IsObject()) {
		Napi::Object proto = proto_val.As<Napi::Object>();
		if (proto.HasOwnProperty("toString")) {
			Napi::Value ts_val = proto.Get("toString");
			if (ts_val.IsFunction()) {
				Napi::Value result = ts_val.As<Napi::Function>().Call(obj, {});
				if (result.IsString()) {
					r_is_valid = true;
					return String(result.As<Napi::String>().Utf8Value().c_str());
				}
			}
		}
	}
	r_is_valid = true;
	String cls_name = module ? String(module->get_global_name()) : String();
	if (cls_name.is_empty()) {
		cls_name = "JavascriptInstance";
	}
	return cls_name + ":" + String::num_int64(owner ? owner->get_instance_id() : 0);
}

bool JavascriptInstance::property_can_revert(const StringName &p_name) const {
	(void)p_name;
	return false;
}

bool JavascriptInstance::property_get_revert(const StringName &p_name, Variant &r_ret) const {
	(void)p_name;
	(void)r_ret;
	return false;
}

void JavascriptInstance::get_property_list(const GDExtensionPropertyInfo *&r_list, uint32_t &r_count) const {
	prop_list_cache.clear();
	prop_list_gde.clear();

	if (module) {
		const godot::HashMap<godot::StringName, godot::PropertyInfo> &props = module->get_exported_properties();
		prop_list_cache.reserve(props.size());
		for (const godot::KeyValue<godot::StringName, godot::PropertyInfo> &kv : props) {
			prop_list_cache.push_back(kv.value);
		}
	}

	prop_list_gde.resize(prop_list_cache.size());
	for (size_t i = 0; i < prop_list_cache.size(); i++) {
		const godot::PropertyInfo &pi = prop_list_cache[i];
		GDExtensionPropertyInfo &gde = prop_list_gde[i];
		gde.type = (GDExtensionVariantType)pi.type;
		gde.name = (GDExtensionStringNamePtr)&pi.name;
		gde.class_name = (GDExtensionStringNamePtr)&pi.class_name;
		gde.hint = (uint32_t)pi.hint;
		gde.hint_string = (GDExtensionStringPtr)&pi.hint_string;
		gde.usage = (uint32_t)pi.usage;
	}

	r_list = prop_list_gde.data();
	r_count = (uint32_t)prop_list_gde.size();
}

void JavascriptInstance::free_property_list(const GDExtensionPropertyInfo *p_list) const {
	(void)p_list;
}

void JavascriptInstance::get_method_list(const GDExtensionMethodInfo *&r_list, uint32_t &r_count) const {
	(void)r_list;
	r_count = 0;
}

void JavascriptInstance::free_method_list(const GDExtensionMethodInfo *p_list) const {
	(void)p_list;
}

Ref<Script> JavascriptInstance::get_script() const {
	return script;
}

IScriptModule *JavascriptInstance::get_module() const {
	return module;
}

} //namespace gode
