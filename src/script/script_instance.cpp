#include "script/script_instance.h"
#include "runtime/napi_error_utils.h"
#include "runtime/node_runtime.h"
#include "runtime/value_convert.h"
#include "script/typescript_script.h"
#include <v8-isolate.h>
#include <v8-locker.h>
#include <exception>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

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

void _attach_promise_rejection_handler(Napi::Value value, const std::string &method_name) {
	if (!value.IsPromise()) {
		return;
	}

	Napi::Env env = value.Env();
	Napi::Function on_rejected = Napi::Function::New(env, [method_name](const Napi::CallbackInfo &info) {
		std::string message = info.Length() > 0 ? js_error_to_string(info[0]) : "Unknown async JavaScript exception";
		log_js_error("Async JS exception in " + method_name, message);
		return info.Env().Undefined(); }, "__gode_async_rejection_handler");

	value.As<Napi::Promise>().Catch(on_rejected);
}

} // namespace
ScriptInstance::ScriptInstance(const Ref<TypeScriptScript> &p_script, Object *p_owner, bool p_placeholder) :
		script(p_script),
		owner(p_owner),
		placeholder(p_placeholder) {
	if (!placeholder) {
		if (!script.is_valid()) {
			return;
		}

		if (!script->compile()) {
			return;
		}

		// Register signals before creating the runtime module instance.
		for (const KeyValue<StringName, MethodInfo> &E : script->signals) {
			if (owner->has_user_signal(E.key)) {
				continue;
			}
			Array args;
			for (const PropertyInfo &arg : E.value.arguments) {
				Dictionary d;
				d["name"] = String(arg.name);
				d["type"] = (int)arg.type;
				args.push_back(d);
			}
			owner->add_user_signal(E.key, args);
		}

		if (!NodeRuntime::is_running()) {
			NodeRuntime::init_once();
		}
		if (!NodeRuntime::is_running()) {
			return;
		}

		v8::Locker locker(NodeRuntime::isolate);
		v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
		v8::HandleScope handle_scope(NodeRuntime::isolate);

		Napi::Function default_class = script->get_default_class();
		if (default_class.IsEmpty() || default_class.IsUndefined() || default_class.IsNull()) {
			return;
		}

		Napi::Env env = default_class.Env();
		Napi::Value external_owner = Napi::External<godot::Object>::New(env, owner);
		Napi::Object instance;
		try {
			instance = default_class.New({ external_owner });
			if (log_and_clear_pending_js_exception(env, "JS script constructor")) {
				return;
			}
		} catch (const Napi::Error &e) {
			log_js_error("JS script constructor", js_error_to_string(e));
			return;
		} catch (const std::exception &e) {
			UtilityFunctions::printerr("Native exception in JS script constructor: ", e.what());
			return;
		} catch (...) {
			UtilityFunctions::printerr("Unknown exception in JS script constructor");
			return;
		}

		js_instance = Napi::Persistent(instance);
	}
}

ScriptInstance::~ScriptInstance() {
	if (script.is_valid()) {
		if (placeholder) {
			script->placeholder_instances.erase(this);
		} else {
			script->instances.erase(this);
			if (owner) {
				script->instance_objects.erase(owner);
			}
		}
	}
	if (!js_instance.IsEmpty()) {
		if (NodeRuntime::is_running()) {
			v8::Locker locker(NodeRuntime::isolate);
			v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
			js_instance.Reset();
		} else {
			js_instance.SuppressDestruct();
		}
	}
}

Object *ScriptInstance::get_owner() const {
	return owner;
}

bool ScriptInstance::is_placeholder() const {
	return placeholder;
}

bool ScriptInstance::is_runtime_instance_valid() const {
	return placeholder || !js_instance.IsEmpty();
}

void ScriptInstance::reload(bool p_keep_state) {
	if (placeholder) {
		return;
	}

	if (!script->compile()) {
		return;
	}

	// Register new signals during reload and skip existing ones to avoid duplicate registration errors.
	for (const KeyValue<StringName, MethodInfo> &E : script->signals) {
		if (owner->has_user_signal(E.key)) {
			continue;
		}
		Array args;
		for (const PropertyInfo &arg : E.value.arguments) {
			Dictionary d;
			d["name"] = String(arg.name);
			d["type"] = (int)arg.type;
			args.push_back(d);
		}
		owner->add_user_signal(E.key, args);
	}

	if (!NodeRuntime::is_running()) {
		NodeRuntime::init_once();
	}
	if (!NodeRuntime::is_running()) {
		return;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	v8::Local<v8::Context> context = NodeRuntime::node_context.Get(NodeRuntime::isolate);
	v8::Context::Scope context_scope(context);

	HashMap<StringName, Variant> old_state;
	if (p_keep_state && !js_instance.IsEmpty()) {
		for (const KeyValue<StringName, PropertyInfo> &E : script->get_exported_properties()) {
			Variant value;
			if (get(E.key, value)) {
				old_state[E.key] = value;
			}
		}
	}

	js_instance.Reset();

	Napi::Function default_class = script->get_default_class();
	if (default_class.IsEmpty()) {
		return;
	}

	Napi::Env env = default_class.Env();
	Napi::Value external_owner = Napi::External<Object>::New(env, owner);
	Napi::Object instance;
	try {
		instance = default_class.New({ external_owner });
		if (log_and_clear_pending_js_exception(env, "JS script reload constructor")) {
			return;
		}
	} catch (const Napi::Error &e) {
		log_js_error("JS script reload constructor", js_error_to_string(e));
		return;
	} catch (const std::exception &e) {
		UtilityFunctions::printerr("Native exception in JS script reload constructor: ", e.what());
		return;
	} catch (...) {
		UtilityFunctions::printerr("Unknown exception in JS script reload constructor");
		return;
	}

	if (p_keep_state) {
		for (const KeyValue<StringName, Variant> &E : old_state) {
			std::string key = String(E.key).utf8().get_data();
			size_t sep = key.find("::");
			if (sep != std::string::npos) {
				// Walk down the object chain for arbitrarily deep A::B::C::... keys.
				// Split into all segments first, then traverse to parent and set leaf.
				std::vector<std::string> segments;
				std::string remaining = key;
				size_t pos;
				while ((pos = remaining.find("::")) != std::string::npos) {
					segments.push_back(remaining.substr(0, pos));
					remaining = remaining.substr(pos + 2);
				}
				segments.push_back(remaining); // leaf key

				Napi::Object cur = instance;
				bool valid = true;
				for (size_t i = 0; i + 1 < segments.size(); ++i) {
					Napi::Value child = cur.Get(segments[i]);
					if (!child.IsObject()) {
						valid = false;
						break;
					}
					cur = child.As<Napi::Object>();
				}
				if (valid) {
					Napi::Value js_value = godot_to_napi(env, E.value);
					if (log_and_clear_pending_js_exception(env, "JS script reload state conversion " + key)) {
						continue;
					}
					cur.Set(segments.back(), js_value);
					log_and_clear_pending_js_exception(env, "JS script reload state restore " + key);
				}
			} else {
				Napi::Value js_value = godot_to_napi(env, E.value);
				if (log_and_clear_pending_js_exception(env, "JS script reload state conversion " + key)) {
					continue;
				}
				instance.Set(key, js_value);
				log_and_clear_pending_js_exception(env, "JS script reload state restore " + key);
			}
		}
	}

	js_instance = Napi::Persistent(instance);
}

bool ScriptInstance::set(const StringName &p_name, const Variant &p_value) {
	if (placeholder) {
		placeholder_properties[p_name] = p_value;
		return true;
	}
	if (js_instance.IsEmpty()) {
		return false;
	}
	if (!NodeRuntime::is_running()) {
		return false;
	}
	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	std::string property_name = String(p_name).utf8().get_data();
	if (!script->properties.has(property_name.c_str())) {
		return false;
	}
	try {
		Napi::Env env = js_instance.Value().Env();
		std::string context = "JS property set " + property_name;
		Napi::Value js_value = godot_to_napi(env, p_value);
		if (log_and_clear_pending_js_exception(env, context)) {
			return false;
		}
		size_t sep = property_name.find("::");
		if (sep != std::string::npos) {
			std::vector<std::string> segments;
			std::string remaining = property_name;
			size_t pos;
			while ((pos = remaining.find("::")) != std::string::npos) {
				segments.push_back(remaining.substr(0, pos));
				remaining = remaining.substr(pos + 2);
			}
			segments.push_back(remaining);
			Napi::Object cur = js_instance.Value();
			for (size_t i = 0; i + 1 < segments.size(); ++i) {
				Napi::Value child = cur.Get(segments[i]);
				if (log_and_clear_pending_js_exception(env, context)) {
					return false;
				}
				if (!child.IsObject()) {
					return false;
				}
				cur = child.As<Napi::Object>();
			}
			cur.Set(segments.back(), js_value);
			if (log_and_clear_pending_js_exception(env, context)) {
				return false;
			}
			return true;
		}
		bool ok = js_instance.Set(property_name, js_value);
		if (log_and_clear_pending_js_exception(env, context)) {
			return false;
		}
		return ok;
	} catch (const Napi::Error &e) {
		log_js_error("JS property set " + property_name, js_error_to_string(e));
		return false;
	} catch (const std::exception &e) {
		UtilityFunctions::printerr("Native exception in JS property set ", property_name.c_str(), ": ", e.what());
		return false;
	} catch (...) {
		UtilityFunctions::printerr("Unknown exception in JS property set ", property_name.c_str());
		return false;
	}
}

bool ScriptInstance::get(const StringName &p_name, Variant &r_value) const {
	if (placeholder) {
		if (placeholder_properties.has(p_name)) {
			r_value = placeholder_properties[p_name];
			return true;
		}
		if (script->_has_property_default_value(p_name)) {
			r_value = script->_get_property_default_value(p_name);
			return true;
		}
		return false;
	}

	if (js_instance.IsEmpty() || !NodeRuntime::is_running()) {
		return false;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	std::string prop_name = String(p_name).utf8().get_data();
	if (!script->properties.has(prop_name.c_str())) {
		return false;
	}
	try {
		Napi::Env env = js_instance.Value().Env();
		std::string context = "JS property get " + prop_name;
		Napi::Value val;
		size_t sep = prop_name.find("::");
		if (sep != std::string::npos) {
			std::vector<std::string> segments;
			std::string remaining = prop_name;
			size_t pos;
			while ((pos = remaining.find("::")) != std::string::npos) {
				segments.push_back(remaining.substr(0, pos));
				remaining = remaining.substr(pos + 2);
			}
			segments.push_back(remaining);
			Napi::Object cur = js_instance.Value();
			for (size_t i = 0; i + 1 < segments.size(); ++i) {
				Napi::Value child = cur.Get(segments[i]);
				if (log_and_clear_pending_js_exception(env, context)) {
					return false;
				}
				if (!child.IsObject()) {
					return false;
				}
				cur = child.As<Napi::Object>();
			}
			val = cur.Get(segments.back());
		} else {
			val = js_instance.Get(prop_name);
		}
		if (log_and_clear_pending_js_exception(env, context)) {
			return false;
		}
		Variant converted = napi_to_godot(val);
		if (log_and_clear_pending_js_exception(env, context)) {
			return false;
		}
		r_value = converted;
		return true;
	} catch (const Napi::Error &e) {
		log_js_error("JS property get " + prop_name, js_error_to_string(e));
		return false;
	} catch (const std::exception &e) {
		UtilityFunctions::printerr("Native exception in JS property get ", prop_name.c_str(), ": ", e.what());
		return false;
	} catch (...) {
		UtilityFunctions::printerr("Unknown exception in JS property get ", prop_name.c_str());
		return false;
	}
}

bool ScriptInstance::has_method(const StringName &p_method) const {
	if (placeholder) {
		return script->_has_method(p_method);
	}
	if (js_instance.IsEmpty()) {
		return false;
	}
	if (!NodeRuntime::is_running()) {
		return false;
	}
	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	Napi::Object instance = js_instance.Value();
	std::string method_name = String(p_method).utf8().get_data();
	return script->_has_method(method_name.c_str());
}

int32_t ScriptInstance::get_method_argument_count(const StringName &p_method, bool &r_is_valid) const {
	if (!script.is_valid()) {
		r_is_valid = false;
		return 0;
	}
	script->compile();
	if (!script->methods.has(p_method)) {
		r_is_valid = false;
		return 0;
	}
	r_is_valid = true;
	return script->methods[p_method].arguments.size();
}

Variant ScriptInstance::call(const StringName &p_method, const Variant *p_args, int32_t p_argcount, GDExtensionCallError &r_error) {
	if (placeholder) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	if (js_instance.IsEmpty()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}

	if (!NodeRuntime::is_running()) {
		r_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return Variant();
	}

	if (!script.is_valid() || (Engine::get_singleton()->is_editor_hint() && !script->_is_tool())) {
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return Variant();
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	v8::Context::Scope context_scope(NodeRuntime::node_context.Get(NodeRuntime::isolate));
	Napi::Object instance = js_instance.Value();
	Napi::Env env = instance.Env();
	std::string method_name = String(p_method).utf8().get_data();

	try {
		if (!instance.Has(method_name)) {
			r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
			return Variant();
		}

		Napi::Value method_value = instance.Get(method_name);
		if (!method_value.IsFunction()) {
			r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
			return Variant();
		}

		Napi::Function method = method_value.As<Napi::Function>();

		std::vector<napi_value> args;
		args.reserve(p_argcount);
		std::string context = "JS method " + method_name;
		for (int i = 0; i < p_argcount; ++i) {
			Napi::Value jsvalue = godot_to_napi(env, p_args[i]);
			if (log_and_clear_pending_js_exception(env, context + " argument conversion")) {
				r_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
				r_error.argument = i;
				r_error.expected = 0;
				return Variant();
			}
			args.push_back(jsvalue);
		}

		Napi::Value result = method.Call(instance, args);
		if (log_and_clear_pending_js_exception(env, context + " call")) {
			r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
			r_error.argument = 0;
			r_error.expected = 0;
			return Variant();
		}
		if (result.IsPromise()) {
			_attach_promise_rejection_handler(result, method_name);
			r_error.error = GDEXTENSION_CALL_OK;
			r_error.argument = 0;
			r_error.expected = 0;
			return Variant();
		}
		Variant converted = napi_to_godot(result);
		if (log_and_clear_pending_js_exception(env, context + " return conversion")) {
			r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
			r_error.argument = 0;
			r_error.expected = 0;
			return Variant();
		}
		r_error.error = GDEXTENSION_CALL_OK;
		r_error.argument = 0;
		r_error.expected = 0;
		return converted;
	} catch (const Napi::Error &e) {
		log_js_error("JS exception in " + method_name, js_error_to_string(e));
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		r_error.argument = 0;
		r_error.expected = 0;
		return Variant();
	} catch (const std::exception &e) {
		UtilityFunctions::printerr("Native exception in JS method ", method_name.c_str(), ": ", e.what());
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		r_error.argument = 0;
		r_error.expected = 0;
		return Variant();
	} catch (...) {
		UtilityFunctions::printerr("Unknown exception in JS method ", method_name.c_str());
		r_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		r_error.argument = 0;
		r_error.expected = 0;
		return Variant();
	}
}

void ScriptInstance::notification_bind(Napi::Object instance, int32_t p_what, bool p_reversed) {
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
			try {
				Napi::Value result = method.Call(instance, { Napi::Number::New(instance.Env(), p_what), Napi::Boolean::New(instance.Env(), p_reversed) });
				if (!log_and_clear_pending_js_exception(instance.Env(), "JS notification " + notification_method_name)) {
					_attach_promise_rejection_handler(result, notification_method_name);
				}
			} catch (const Napi::Error &e) {
				log_js_error("JS exception in " + notification_method_name, js_error_to_string(e));
			} catch (const std::exception &e) {
				UtilityFunctions::printerr("Native exception in JS notification: ", e.what());
			} catch (...) {
				UtilityFunctions::printerr("Unknown exception in JS notification");
			}

			if (p_reversed) {
				notification_bind(proto, p_what, p_reversed);
			}
		}
	}
}

void ScriptInstance::notification(int32_t p_what, bool p_reversed) {
	if (placeholder) {
		return;
	}
	if (js_instance.IsEmpty()) {
		return;
	}
	if (!NodeRuntime::is_running()) {
		return;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	v8::Context::Scope context_scope(NodeRuntime::node_context.Get(NodeRuntime::isolate));

	Napi::Object instance = js_instance.Value();
	notification_bind(instance, p_what, p_reversed);
}

String ScriptInstance::to_string(bool &r_is_valid) const {
	if (placeholder) {
		r_is_valid = true;
		return "ScriptInstance(Placeholder)";
	}
	if (js_instance.IsEmpty()) {
		r_is_valid = false;
		return String();
	}
	if (!NodeRuntime::is_running()) {
		r_is_valid = false;
		return String();
	}
	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	Napi::Object obj = js_instance.Value();
	Napi::Env env = obj.Env();
	try {
		Napi::Value proto_val = obj.Get("__proto__");
		if (log_and_clear_pending_js_exception(env, "JS toString prototype lookup")) {
			r_is_valid = false;
			return String();
		}
		if (proto_val.IsObject()) {
			Napi::Object proto = proto_val.As<Napi::Object>();
			if (proto.HasOwnProperty("toString")) {
				Napi::Value ts_val = proto.Get("toString");
				if (log_and_clear_pending_js_exception(env, "JS toString lookup")) {
					r_is_valid = false;
					return String();
				}
				if (ts_val.IsFunction()) {
					Napi::Value result = ts_val.As<Napi::Function>().Call(obj, {});
					if (log_and_clear_pending_js_exception(env, "JS toString call")) {
						r_is_valid = false;
						return String();
					}
					if (result.IsString()) {
						r_is_valid = true;
						return String(result.As<Napi::String>().Utf8Value().c_str());
					}
				}
			}
		}
	} catch (const Napi::Error &e) {
		log_js_error("JS toString", js_error_to_string(e));
		r_is_valid = false;
		return String();
	} catch (const std::exception &e) {
		UtilityFunctions::printerr("Native exception in JS toString: ", e.what());
		r_is_valid = false;
		return String();
	} catch (...) {
		UtilityFunctions::printerr("Unknown exception in JS toString");
		r_is_valid = false;
		return String();
	}
	r_is_valid = true;
	String cls_name = String(script->_get_global_name());
	if (cls_name.is_empty()) {
		cls_name = "ScriptInstance";
	}
	return cls_name + ":" + String::num_int64(owner ? owner->get_instance_id() : 0);
}

bool ScriptInstance::property_can_revert(const StringName &p_name) const {
	return script.is_valid() && script->_has_property_default_value(p_name);
}

bool ScriptInstance::property_get_revert(const StringName &p_name, Variant &r_ret) const {
	if (script.is_valid() && script->_has_property_default_value(p_name)) {
		r_ret = script->_get_property_default_value(p_name);
		return true;
	}
	return false;
}

void ScriptInstance::get_property_list(const GDExtensionPropertyInfo *&r_list, uint32_t &r_count) const {
	prop_list_cache.clear();
	prop_list_gde.clear();

	if (script.is_valid()) {
		const godot::Vector<godot::PropertyInfo> &ordered = script->get_property_list_ordered();
		if (!ordered.is_empty()) {
			prop_list_cache.reserve(ordered.size());
			for (const godot::PropertyInfo &pi : ordered) {
				prop_list_cache.push_back(pi);
			}
		} else {
			const godot::HashMap<godot::StringName, godot::PropertyInfo> &props = script->get_exported_properties();
			prop_list_cache.reserve(props.size());
			for (const godot::KeyValue<godot::StringName, godot::PropertyInfo> &kv : props) {
				prop_list_cache.push_back(kv.value);
			}
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

void ScriptInstance::free_property_list(const GDExtensionPropertyInfo *p_list) const {
	(void)p_list;
}

void ScriptInstance::get_method_list(const GDExtensionMethodInfo *&r_list, uint32_t &r_count) const {
	method_list_cache.clear();
	method_list_gde.clear();
	method_arg_cache.clear();
	method_arg_gde_cache.clear();
	method_return_cache.clear();
	method_return_gde_cache.clear();

	if (script.is_valid()) {
		script->compile();
		const godot::HashMap<godot::StringName, godot::MethodInfo> &methods = script->methods;
		method_list_cache.reserve(methods.size());
		for (const godot::KeyValue<godot::StringName, godot::MethodInfo> &kv : methods) {
			method_list_cache.push_back(kv.value);
		}
	}

	method_list_gde.resize(method_list_cache.size());
	method_arg_cache.resize(method_list_cache.size());
	method_arg_gde_cache.resize(method_list_cache.size());
	method_return_cache.resize(method_list_cache.size());
	method_return_gde_cache.resize(method_list_cache.size());

	for (size_t i = 0; i < method_list_cache.size(); i++) {
		const godot::MethodInfo &mi = method_list_cache[i];
		GDExtensionMethodInfo &gde = method_list_gde[i];

		method_return_cache[i] = mi.return_val;
		GDExtensionPropertyInfo &return_gde = method_return_gde_cache[i];
		return_gde.type = (GDExtensionVariantType)method_return_cache[i].type;
		return_gde.name = (GDExtensionStringNamePtr)&method_return_cache[i].name;
		return_gde.class_name = (GDExtensionStringNamePtr)&method_return_cache[i].class_name;
		return_gde.hint = (uint32_t)method_return_cache[i].hint;
		return_gde.hint_string = (GDExtensionStringPtr)&method_return_cache[i].hint_string;
		return_gde.usage = (uint32_t)method_return_cache[i].usage;
		gde.name = (GDExtensionStringNamePtr)&mi.name;
		gde.return_value = return_gde;
		gde.flags = (uint32_t)mi.flags;
		gde.id = mi.id;

		method_arg_cache[i].reserve(mi.arguments.size());
		for (const godot::PropertyInfo &arg : mi.arguments) {
			method_arg_cache[i].push_back(arg);
		}

		method_arg_gde_cache[i].resize(method_arg_cache[i].size());
		for (size_t j = 0; j < method_arg_cache[i].size(); j++) {
			const godot::PropertyInfo &arg = method_arg_cache[i][j];
			GDExtensionPropertyInfo &arg_gde = method_arg_gde_cache[i][j];
			arg_gde.type = (GDExtensionVariantType)arg.type;
			arg_gde.name = (GDExtensionStringNamePtr)&arg.name;
			arg_gde.class_name = (GDExtensionStringNamePtr)&arg.class_name;
			arg_gde.hint = (uint32_t)arg.hint;
			arg_gde.hint_string = (GDExtensionStringPtr)&arg.hint_string;
			arg_gde.usage = (uint32_t)arg.usage;
		}
		gde.arguments = method_arg_gde_cache[i].empty() ? nullptr : method_arg_gde_cache[i].data();
		gde.argument_count = (uint32_t)method_arg_gde_cache[i].size();
		gde.default_arguments = nullptr;
		gde.default_argument_count = 0;
	}

	r_list = method_list_gde.empty() ? nullptr : method_list_gde.data();
	r_count = (uint32_t)method_list_gde.size();
}

void ScriptInstance::free_method_list(const GDExtensionMethodInfo *p_list) const {
	(void)p_list;
}

Ref<TypeScriptScript> ScriptInstance::get_script() const {
	return script;
}

} // namespace gode
