#include "support/javascript/javascript_instance.h"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "utils/node_runtime.h"
#include "utils/value_convert.h"

using namespace godot;

namespace gode {

bool JavascriptInstance::compile_module() {
	if (!javascript.is_valid()) {
		return false;
	}

	String source_code = javascript->_get_source_code();
	String path = javascript->get_path();
	if (path.is_empty()) {
		return false;
	}

    // Convert res:// path to absolute file system path
    // Node.js module system requires absolute paths to handle dependencies and caching correctly
    // especially when dealing with ESM/CJS interop which might try to convert paths to URLs.
    // While users might prefer res:// in stack traces, Node.js internals (pathToFileURL) will crash
    // if given a non-file path when ESM logic is triggered.
    // The safest way is to use the real OS path.
    String abs_path = ProjectSettings::get_singleton()->globalize_path(path);

    // We MUST have a HandleScope here because compile_script returns a Napi::Value that wraps a v8::Local handle.
    // That handle must be allocated in a scope that outlives the call.
    // Since compile_script creates its own scopes and then escapes the handle, 
    // the escaped handle lands in the CURRENT HandleScope.
    // If there is no current HandleScope, the handle is invalid or leaks (depending on V8 version).
    Napi::HandleScope scope(JsEnvManager::get_env());
	Napi::Value exports = NodeRuntime::compile_script(source_code.utf8().get_data(), abs_path.utf8().get_data());
	Napi::Function default_class = NodeRuntime::get_default_class(exports);
	if (default_class.IsEmpty()) {
		return false;
	}

    Napi::Value external_owner = Napi::External<godot::Object>::New(JsEnvManager::get_env(), owner);
	Napi::Object instance = default_class.New({external_owner});
	js_instance = Napi::Persistent(instance);
	return true;
}

JavascriptInstance::JavascriptInstance(const Ref<Javascript> &p_javascript, Object *p_owner, bool p_placeholder) {
	javascript = p_javascript;
	owner = p_owner;
	placeholder = p_placeholder;
	if (!placeholder) {
		if (!compile_module()) {
			ERR_PRINT("Failed to compile module.");
		}
	}
}

Object *JavascriptInstance::get_owner() const {
	return owner;
}

bool JavascriptInstance::is_placeholder() const {
	return placeholder;
}

bool JavascriptInstance::set(const StringName &p_name, const Variant &p_value) {
	(void)p_name;
	(void)p_value;
	return false;
}

bool JavascriptInstance::get(const StringName &p_name, Variant &r_value) const {
	(void)p_name;
	(void)r_value;
	return false;
}

bool JavascriptInstance::has_method(const StringName &p_method) const {
	if (js_instance.IsEmpty()) {
		return false;
	}
	Napi::HandleScope scope(JsEnvManager::get_env());
	Napi::Object instance = js_instance.Value();
	std::string method_name = String(p_method).utf8().get_data();
	return instance.Has(method_name) && instance.Get(method_name).IsFunction();
}

int32_t JavascriptInstance::get_method_argument_count(const StringName &p_method, bool &r_is_valid) const {
	(void)p_method;
	r_is_valid = false;
	return 0;
}

Variant JavascriptInstance::call(const StringName &p_method, const Variant *p_args, int32_t p_argcount, GDExtensionCallError &r_error) {
	if (js_instance.IsEmpty()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}

	Napi::HandleScope scope(JsEnvManager::get_env());
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

} // namespace gode
