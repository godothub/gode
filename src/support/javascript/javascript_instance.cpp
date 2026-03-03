#include "support/javascript/javascript_instance.h"

#include "utils/node_runtime.h"
#include "utils/value_convert.h"
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>

using namespace godot;

namespace gode {

JavascriptInstance::JavascriptInstance(const Ref<Javascript> &p_javascript, Object *p_owner, bool p_placeholder) :
		javascript(p_javascript),
		owner(p_owner),
		placeholder(p_placeholder) {
	if (!placeholder) {
		if (!javascript.is_valid()) {
			return;
		}

		if (!javascript->compile()) {
			return;
		}

		v8::Locker locker(NodeRuntime::isolate);
		v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
		Napi::HandleScope scope(JsEnvManager::get_env());

		Napi::Function default_class = javascript->get_default_class();
		if (default_class.IsEmpty() || default_class.IsUndefined() || default_class.IsNull()) {
			return;
		}

		Napi::Value external_owner = Napi::External<godot::Object>::New(JsEnvManager::get_env(), owner);
		Napi::Object instance = default_class.New({ external_owner });
		js_instance = Napi::Persistent(instance);
	}
}

Object *JavascriptInstance::get_owner() const {
	return owner;
}

bool JavascriptInstance::is_placeholder() const {
	return placeholder;
}

bool JavascriptInstance::set(const StringName &p_name, const Variant &p_value) {
	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	return js_instance.Set(String(p_name).utf8().get_data(), godot_to_napi(JsEnvManager::get_env(), p_value));
}

bool JavascriptInstance::get(const StringName &p_name, Variant &r_value) const {
	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	Napi::Object obj = js_instance.Value();
	const char *prop_name = String(p_name).utf8().get_data();
	if (obj.Has(prop_name)) {
		Napi::Value val = js_instance.Get(prop_name);
		r_value = napi_to_godot(val);
		return true;
	}
	return false;
}

bool JavascriptInstance::has_method(const StringName &p_method) const {
	if (js_instance.IsEmpty()) {
		return false;
	}
	v8::Locker locker(NodeRuntime::isolate);
	v8::HandleScope scope(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	Napi::Object instance = js_instance.Value();
	std::string method_name = String(p_method).utf8().get_data();
	return instance.Has(method_name) && instance.Get(method_name).IsFunction();
}

int32_t JavascriptInstance::get_method_argument_count(const StringName &p_method, bool &r_is_valid) const {
	v8::Locker locker(NodeRuntime::isolate);
	(void)p_method;
	r_is_valid = false;
	return 0;
}

Variant JavascriptInstance::call(const StringName &p_method, const Variant *p_args, int32_t p_argcount, GDExtensionCallError &r_error) {
	if (!javascript->is_tool() && Engine::get_singleton()->is_editor_hint()) {
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
	r_is_valid = false;
	return String();
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
	(void)r_list;
	r_count = 0;
}

void JavascriptInstance::get_method_list(const GDExtensionMethodInfo *&r_list, uint32_t &r_count) const {
	(void)r_list;
	r_count = 0;
}

Ref<Javascript> JavascriptInstance::get_script() const {
	return javascript;
}
} //namespace gode