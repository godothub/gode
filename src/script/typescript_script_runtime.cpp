#include "runtime/node_runtime.h"
#include "runtime/napi_error_utils.h"
#include "runtime/value_convert.h"
#include "script/script_instance.h"
#include "script/script_instance_info.h"
#include "script/typescript_language.h"
#include "script/typescript_script.h"
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <exception>
#include <vector>

using namespace godot;
using namespace gode;

namespace {

HashSet<TypeScriptScript *> live_scripts;

Dictionary method_info_to_dictionary(const MethodInfo &p_method) {
	Dictionary method = p_method;
	return method;
}

void set_call_error(GDExtensionCallError &r_error, GDExtensionCallErrorType p_error, int32_t p_argument = 0, int32_t p_expected = 0) {
	r_error.error = p_error;
	r_error.argument = p_argument;
	r_error.expected = p_expected;
}

} // namespace

void TypeScriptScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("has_static_method", "method"), &TypeScriptScript::has_static_method);
	ClassDB::bind_method(D_METHOD("get_static_method_argument_count", "method"), &TypeScriptScript::get_static_method_argument_count);
	ClassDB::bind_vararg_method(
			METHOD_FLAGS_DEFAULT,
			"call_static",
			&TypeScriptScript::call_static,
			MethodInfo(Variant::NIL, "call_static", PropertyInfo(Variant::STRING_NAME, "method")));
}

TypeScriptScript::TypeScriptScript() {
	live_scripts.insert(this);
}

TypeScriptScript::~TypeScriptScript() {
	live_scripts.erase(this);
	release_runtime_state();
}

void TypeScriptScript::release_all_runtime_state() {
	std::vector<TypeScriptScript *> scripts;
	scripts.reserve(live_scripts.size());
	for (TypeScriptScript *script : live_scripts) {
		scripts.push_back(script);
	}

	for (TypeScriptScript *script : scripts) {
		if (script) {
			script->release_runtime_state();
		}
	}
}

void TypeScriptScript::cache_constructed_class(const Napi::Function &p_class) {
	if (default_class.IsEmpty()) {
		default_class = Napi::Persistent(p_class);
	}
}

void TypeScriptScript::release_runtime_state() {
	std::vector<ScriptInstance *> active_instances;
	active_instances.reserve(instances.size());
	for (ScriptInstance *instance : instances) {
		active_instances.push_back(instance);
	}
	for (ScriptInstance *instance : active_instances) {
		if (instance) {
			instance->release_runtime_state();
		}
	}

	if (default_class.IsEmpty()) {
		return;
	}
	if (!NodeRuntime::is_running()) {
		default_class.SuppressDestruct();
		return;
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	default_class.Reset();
}

bool TypeScriptScript::has_static_method(const StringName &p_method) const {
	return _has_static_method(p_method);
}

int32_t TypeScriptScript::get_static_method_argument_count(const StringName &p_method) const {
	compile();
	const MethodInfo *method = static_methods.getptr(p_method);
	return method ? method->arguments.size() : -1;
}

Variant TypeScriptScript::call_static(const Variant **p_args, GDExtensionInt p_argcount, GDExtensionCallError &r_error) {
	if (p_argcount < 1 || p_args == nullptr || p_args[0] == nullptr) {
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS, 0, 1);
		return Variant();
	}

	const Variant &method_arg = *p_args[0];
	if (method_arg.get_type() != Variant::STRING_NAME && method_arg.get_type() != Variant::STRING) {
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT, 0, static_cast<int32_t>(Variant::STRING_NAME));
		return Variant();
	}

	const StringName method = method_arg.get_type() == Variant::STRING_NAME ? StringName(method_arg) : StringName(String(method_arg));
	return call_static_method(method, p_args + 1, static_cast<int32_t>(p_argcount - 1), r_error);
}

Variant TypeScriptScript::call_static_method(const StringName &p_method, const Variant **p_args, int32_t p_argcount, GDExtensionCallError &r_error) const {
	if (p_argcount < 0 || (p_argcount > 0 && p_args == nullptr)) {
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT);
		return Variant();
	}

	if (!compile() || !static_methods.has(p_method)) {
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
		return Variant();
	}

	if (!ensure_default_class_loaded() || !NodeRuntime::is_running()) {
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL);
		return Variant();
	}

	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
	v8::Context::Scope context_scope(NodeRuntime::node_context.Get(NodeRuntime::isolate));

	Napi::Function default_class = get_cached_default_class();
	if (default_class.IsEmpty() || default_class.IsUndefined() || default_class.IsNull()) {
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL);
		return Variant();
	}

	Napi::Env env = default_class.Env();
	Napi::Object class_object = default_class.As<Napi::Object>();
	std::string method_name = String(p_method).utf8().get_data();
	std::string context = "JS static method " + method_name;

	try {
		if (!class_object.HasOwnProperty(method_name)) {
			set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
			return Variant();
		}

		Napi::Value method_value = class_object.Get(method_name);
		if (log_and_clear_pending_js_exception(env, context + " lookup")) {
			set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
			return Variant();
		}
		if (!method_value.IsFunction()) {
			set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
			return Variant();
		}

		Napi::Function method = method_value.As<Napi::Function>();
		std::vector<napi_value> args;
		args.reserve(p_argcount);
		for (int32_t i = 0; i < p_argcount; ++i) {
			if (p_args[i] == nullptr) {
				set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT, i, 0);
				return Variant();
			}
			Napi::Value js_value = godot_to_napi(env, *p_args[i]);
			if (log_and_clear_pending_js_exception(env, context + " argument conversion")) {
				set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT, i, 0);
				return Variant();
			}
			args.push_back(js_value);
		}

		Napi::Value result = method.Call(class_object, args);
		if (log_and_clear_pending_js_exception(env, context + " call")) {
			set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
			return Variant();
		}
		if (result.IsPromise()) {
			attach_promise_rejection_handler(result, context);
			set_call_error(r_error, GDEXTENSION_CALL_OK);
			return Variant();
		}

		Variant converted = napi_to_godot(result);
		if (log_and_clear_pending_js_exception(env, context + " return conversion")) {
			set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
			return Variant();
		}

		set_call_error(r_error, GDEXTENSION_CALL_OK);
		return converted;
	} catch (const Napi::Error &e) {
		log_js_error("JS exception in static method " + method_name, js_error_to_string(e));
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
		return Variant();
	} catch (const std::exception &e) {
		UtilityFunctions::printerr("Native exception in JS static method ", method_name.c_str(), ": ", e.what());
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
		return Variant();
	} catch (...) {
		UtilityFunctions::printerr("Unknown exception in JS static method ", method_name.c_str());
		set_call_error(r_error, GDEXTENSION_CALL_ERROR_INVALID_METHOD);
		return Variant();
	}
}

bool TypeScriptScript::_editor_can_reload_from_file() {
	return true;
}

void TypeScriptScript::_placeholder_erased(void *p_placeholder) {
	placeholder_instances.erase(static_cast<ScriptInstance *>(p_placeholder));
}

bool TypeScriptScript::_can_instantiate() const {
	return compile();
}

Ref<Script> TypeScriptScript::_get_base_script() const {
	compile();
	if (base_script_path.is_empty() || base_script_path == get_path()) {
		return Ref<Script>();
	}
	Ref<Resource> resource = ResourceLoader::get_singleton()->load(base_script_path);
	if (resource.is_null()) {
		return Ref<Script>();
	}
	return Ref<Script>(resource);
}

StringName TypeScriptScript::_get_global_name() const {
	compile();
	return global_class_name;
}

bool TypeScriptScript::_inherits_script(const Ref<Script> &p_script) const {
	compile();
	Ref<TypeScriptScript> base_script = Ref(p_script);
	if (base_script.is_valid() && base_script->class_name == base_class_name) {
		return true;
	}
	Ref<Script> direct_base = _get_base_script();
	if (direct_base.is_valid()) {
		if (direct_base == p_script) {
			return true;
		}
		Ref<TypeScriptScript> direct_base_ts = direct_base;
		if (direct_base_ts.is_valid() && direct_base_ts->_inherits_script(p_script)) {
			return true;
		}
	}
	return false;
}

StringName TypeScriptScript::_get_instance_base_type() const {
	compile();
	return base_class_name;
}

void *TypeScriptScript::_instance_create(Object *p_for_object) const {
	if (!p_for_object || !compile()) {
		return nullptr;
	}

	Ref<TypeScriptScript> self(const_cast<TypeScriptScript *>(this));
	ScriptInstance *instance = memnew(ScriptInstance(self, p_for_object, false));
	if (!instance->is_runtime_instance_valid()) {
		memdelete(instance);
		return nullptr;
	}

	void *gd_instance = gdextension_interface::script_instance_create3(&script_instance_info, instance);
	if (!gd_instance) {
		memdelete(instance);
		return nullptr;
	}

	instances.insert(instance);
	instance_objects.insert(p_for_object);
	return gd_instance;
}

void *TypeScriptScript::_placeholder_instance_create(Object *p_for_object) const {
	Ref<TypeScriptScript> self(const_cast<TypeScriptScript *>(this));
	ScriptInstance *instance = memnew(ScriptInstance(self, p_for_object, true));
	placeholder_instances.insert(instance);
	return gdextension_interface::script_instance_create3(&script_instance_info, instance);
}

bool TypeScriptScript::_instance_has(Object *p_object) const {
	return instance_objects.has(p_object);
}

bool TypeScriptScript::_has_source_code() const {
	return !source_code.is_empty();
}

String TypeScriptScript::_get_source_code() const {
	return source_code;
}

Error TypeScriptScript::reload_source_code(const String &p_code, bool p_keep_state, bool p_force_dirty) {
	const bool source_changed = !source_code_loaded || source_code != p_code;
	source_code_loaded = true;
	if (source_changed) {
		source_code = p_code;
	}
	if (source_changed || p_force_dirty) {
		is_dirty = true;
	}
	if (!source_changed && !p_force_dirty && !is_dirty) {
		return is_valid ? Error::OK : Error::ERR_INVALID_PARAMETER;
	}
	return _reload(p_keep_state);
}

void TypeScriptScript::_set_source_code(const String &p_code) {
	reload_source_code(p_code, true);
}

Error TypeScriptScript::_reload(bool p_keep_state) {
	if (!compile()) {
		return Error::ERR_INVALID_PARAMETER;
	}

	// Constructors can add same-script instances or destroy other owners while
	// reloading. Keep owner IDs instead of iterating the mutable instance set or
	// retaining ScriptInstance pointers across calls into user code.
	std::vector<ObjectID> owners;
	owners.reserve(instances.size());
	for (ScriptInstance *instance : instances) {
		if (instance && instance->get_owner()) {
			owners.push_back(ObjectID(instance->get_owner()->get_instance_id()));
		}
	}
	for (ObjectID owner_id : owners) {
		Object *owner = ObjectDB::get_instance(owner_id);
		if (!owner) {
			continue;
		}
		ScriptInstance *instance = static_cast<ScriptInstance *>(gdextension_interface::object_get_script_instance(
				owner->_owner, TypeScriptLanguage::get_singleton()->_owner));
		if (instance && instances.has(instance) && !instance->is_placeholder()) {
			instance->reload(p_keep_state);
		}
	}

	return Error::OK;
}

StringName TypeScriptScript::_get_doc_class_name() const {
	return StringName();
}

TypedArray<Dictionary> TypeScriptScript::_get_documentation() const {
	TypedArray<Dictionary> docs;
	return docs;
}

String TypeScriptScript::_get_class_icon_path() const {
	return String();
}

bool TypeScriptScript::_has_method(const StringName &p_method) const {
	compile();
	return methods.has(p_method);
}

bool TypeScriptScript::_has_static_method(const StringName &p_method) const {
	compile();
	return static_methods.has(p_method);
}

Variant TypeScriptScript::_get_script_method_argument_count(const StringName &p_method) const {
	compile();
	if (methods.has(p_method)) {
		return methods[p_method].arguments.size();
	}
	if (static_methods.has(p_method)) {
		return static_methods[p_method].arguments.size();
	}
	return Variant();
}

Dictionary TypeScriptScript::_get_method_info(const StringName &p_method) const {
	compile();
	if (methods.has(p_method)) {
		return method_info_to_dictionary(methods[p_method]);
	}
	if (static_methods.has(p_method)) {
		return method_info_to_dictionary(static_methods[p_method]);
	}
	return Dictionary();
}

bool TypeScriptScript::_is_tool() const {
	compile();
	return is_tool_script;
}

bool TypeScriptScript::_is_valid() const {
	return compile();
}

bool TypeScriptScript::_is_abstract() const {
	compile();
	return false;
}

ScriptLanguage *TypeScriptScript::get_script_language() const {
	return TypeScriptLanguage::get_singleton();
}

StringName TypeScriptScript::get_global_name() const {
	compile();
	return global_class_name;
}

bool TypeScriptScript::_has_script_signal(const StringName &p_signal) const {
	compile();
	return signals.has(p_signal);
}

TypedArray<Dictionary> TypeScriptScript::_get_script_signal_list() const {
	compile();
	TypedArray<Dictionary> list;
	for (const KeyValue<StringName, MethodInfo> &E : signals) {
		Dictionary d;
		d["name"] = String(E.key);
		Array args;
		for (const PropertyInfo &arg : E.value.arguments) {
			Dictionary ad;
			ad["name"] = String(arg.name);
			ad["type"] = (int)arg.type;
			ad["class_name"] = String(arg.class_name);
			ad["hint"] = (int)arg.hint;
			ad["hint_string"] = arg.hint_string;
			ad["usage"] = (int)arg.usage;
			args.push_back(ad);
		}
		d["args"] = args;
		list.push_back(d);
	}
	return list;
}

bool TypeScriptScript::_has_property_default_value(const StringName &p_property) const {
	compile();
	return property_defaults.has(p_property);
}

Variant TypeScriptScript::_get_property_default_value(const StringName &p_property) const {
	compile();
	if (property_defaults.has(p_property)) {
		return property_defaults[p_property];
	}
	return Variant();
}

void TypeScriptScript::_update_exports() {
	compile();
}

TypedArray<Dictionary> TypeScriptScript::_get_script_method_list() const {
	compile();
	TypedArray<Dictionary> list;
	for (const KeyValue<StringName, MethodInfo> &E : methods) {
		list.push_back(method_info_to_dictionary(E.value));
	}
	for (const KeyValue<StringName, MethodInfo> &E : static_methods) {
		list.push_back(method_info_to_dictionary(E.value));
	}
	return list;
}

TypedArray<Dictionary> TypeScriptScript::_get_script_property_list() const {
	compile();
	TypedArray<Dictionary> list;
	for (const KeyValue<StringName, PropertyInfo> &kv : properties) {
		const PropertyInfo &pi = kv.value;
		Dictionary d;
		d["name"] = String(pi.name);
		d["class_name"] = String(pi.class_name);
		d["type"] = (int)pi.type;
		d["hint"] = (int)pi.hint;
		d["hint_string"] = pi.hint_string;
		d["usage"] = (int)pi.usage;
		list.push_back(d);
	}
	return list;
}

int32_t TypeScriptScript::_get_member_line(const StringName &p_member) const {
	compile();
	const int32_t *line = member_lines.getptr(p_member);
	return line ? *line : -1;
}

Dictionary TypeScriptScript::_get_constants() const {
	Dictionary constants;
	compile();
	for (const KeyValue<StringName, Variant> &E : this->constants) {
		constants[E.key] = E.value;
	}
	return constants;
}

TypedArray<StringName> TypeScriptScript::_get_members() const {
	TypedArray<StringName> members;
	return members;
}

bool TypeScriptScript::_is_placeholder_fallback_enabled() const {
	return true;
}

Variant TypeScriptScript::_get_rpc_config() const {
	compile();
	Dictionary config;
	for (const KeyValue<StringName, Dictionary> &E : rpc_configs) {
		config[E.key] = E.value;
	}
	return config;
}
