#include "support/javascript/javascript_instance.h"
#include "support/javascript/javascript.h"
#include "utils/node_runtime.h"
#include "utils/value_convert.h"
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>

using namespace godot;

namespace gode {

namespace {

constexpr const char *CPP_BINDING_PROTO_MARKER = "__gode.cpp_binding__";
constexpr const char *PROTO_PROP_NAME = "__proto__";

bool _is_cpp_binding_prototype(const Napi::Object &p_proto, const Napi::Symbol &p_marker_symbol) {
	if (!p_proto.HasOwnProperty(p_marker_symbol)) {
		return false;
	}

	Napi::Value marker_value = p_proto.Get(p_marker_symbol);
	if (marker_value.IsBoolean()) {
		return marker_value.As<Napi::Boolean>().Value();
	}
	return !marker_value.IsNull() && !marker_value.IsUndefined();
}

bool _find_script_method_from_prototype_chain(const Napi::Object &p_instance, const std::string &p_method_name, Napi::Function *r_method = nullptr) {
	Napi::Env env = p_instance.Env();
	Napi::Symbol marker_symbol = Napi::Symbol::For(env, CPP_BINDING_PROTO_MARKER);

	Napi::Value proto_val = p_instance.Get(PROTO_PROP_NAME);
	while (proto_val.IsObject()) {
		Napi::Object proto = proto_val.As<Napi::Object>();

		// Stop before entering generated C++ binding prototypes.
		if (_is_cpp_binding_prototype(proto, marker_symbol)) {
			return false;
		}

		if (proto.HasOwnProperty(p_method_name)) {
			Napi::Value method_val = proto.Get(p_method_name);
			if (!method_val.IsFunction()) {
				return false;
			}
			if (r_method != nullptr) {
				*r_method = method_val.As<Napi::Function>();
			}
			return true;
		}

		proto_val = proto.Get(PROTO_PROP_NAME);
	}

	return false;
}

} // namespace
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
		v8::HandleScope handle_scope(NodeRuntime::isolate);

		Napi::Function default_class = javascript->get_default_class();
		if (default_class.IsEmpty() || default_class.IsUndefined() || default_class.IsNull()) {
			return;
		}

		Napi::Env env = default_class.Env();
		Napi::Value external_owner = Napi::External<godot::Object>::New(env, owner);
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
	std::string property_name = String(p_name).utf8().get_data();
	if (js_instance.Value().HasOwnProperty(property_name)) {
		Napi::Env env = js_instance.Value().Env();
		return js_instance.Set(property_name, godot_to_napi(env, p_value));
	}
	return false;
}

bool JavascriptInstance::get(const StringName &p_name, Variant &r_value) const {
	if (placeholder) {
		if (placeholder_properties.has(p_name)) {
			r_value = placeholder_properties[p_name];
			return true;
		}
		if (javascript->_has_property_default_value(p_name)) {
			r_value = javascript->_get_property_default_value(p_name);
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
		return javascript->_has_method(p_method);
	}
	if (js_instance.IsEmpty()) {
		return false;
	}
	v8::Locker locker(NodeRuntime::isolate);
	v8::HandleScope scope(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	Napi::Object instance = js_instance.Value();
	std::string method_name = String(p_method).utf8().get_data();
	return instance.Has(method_name);
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

	if (js_instance.IsEmpty()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	Napi::Object instance = js_instance.Value();
	Napi::Env env = instance.Env();
	std::string method_name = String(p_method).utf8().get_data();

	bool ret = false;
	napi_is_exception_pending(env, &ret);

	if (!instance.Has(method_name)) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	if (!instance.Get(method_name).IsFunction()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	Napi::Function method = js_instance.Get(method_name).As<Napi::Function>();

	std::vector<napi_value> args;
	for (int i = 0; i < p_argcount; ++i) {
		Napi::Value jsvalue = godot_to_napi(env, p_args[i]);
		args.push_back(jsvalue);
	}

	Napi::Value result = method.Call(instance, args);
	r_error.error = GDEXTENSION_CALL_OK;
	return napi_to_godot(result);
}

void JavascriptInstance::notification_bind(Napi::Object instance, int32_t p_what, bool p_reversed) {
	static std::string notification_method_name = "_notification";
	Napi::Value method_val;
	if (!instance.IsUndefined() && instance.HasOwnProperty(notification_method_name)) {
		method_val = instance.Get(notification_method_name);
		if (method_val.IsFunction()) {
			Napi::Object globalObject = instance.Env().Global().Get("Object").As<Napi::Object>();
			Napi::Function getPrototypeOf = globalObject.Get("getPrototypeOf").As<Napi::Function>();
			Napi::Object proto = getPrototypeOf.Call(globalObject, { instance }).As<Napi::Object>();

			if (!p_reversed) {
				notification_bind(proto, p_what, p_reversed);
			}

			Napi::Function method = method_val.As<Napi::Function>();
			Napi::Value result = method.Call(instance, { Napi::Number::New(instance.Env(), p_what), Napi::Boolean::New(instance.Env(), p_reversed) });

			if (p_reversed) {
				notification_bind(proto, p_what, p_reversed);
			}
		}
	}
}

void JavascriptInstance::notification(int32_t p_what, bool p_reversed) {
	if (placeholder) {
		return;
	}
	if (js_instance.IsEmpty()) {
		return;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::Context::Scope context_scope(NodeRuntime::node_context.Get(NodeRuntime::isolate));

	Napi::Object instance = js_instance.Value();
	notification_bind(instance, p_what, p_reversed);
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
	v8::HandleScope handle_scope(NodeRuntime::isolate);
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
	String cls_name = String(javascript->_get_global_name());
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

	if (javascript.is_valid()) {
		const godot::HashMap<godot::StringName, godot::PropertyInfo> &props = javascript->get_exported_properties();
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

Ref<Javascript> JavascriptInstance::get_script() const {
	return javascript;
}

} // namespace gode
