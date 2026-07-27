#include "runtime/node_runtime.h"
#include "runtime/value_convert.h"
#include "script/script_instance.h"
#include "script/script_instance_info.h"
#include "script/typescript_language.h"
#include "script/typescript_script.h"
#include <v8-isolate.h>
#include <v8-locker.h>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;
using namespace gode;

void TypeScriptScript::_bind_methods() {
}

TypeScriptScript::~TypeScriptScript() {
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
	return class_name;
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

Error TypeScriptScript::reload_source_code(const String &p_code, bool p_keep_state) {
	is_dirty = true;
	source_code = p_code;
	return _reload(p_keep_state);
}

void TypeScriptScript::_set_source_code(const String &p_code) {
	reload_source_code(p_code, true);
}

Error TypeScriptScript::_reload(bool p_keep_state) {
	if (!compile()) {
		return Error::ERR_INVALID_PARAMETER;
	}

	// Reload all instances
	for (ScriptInstance *instance : instances) {
		if (instance && !instance->is_placeholder()) {
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
	if (methods.has(p_method)) {
		return methods[p_method].flags & METHOD_FLAG_STATIC;
	}
	return false;
}

Variant TypeScriptScript::_get_script_method_argument_count(const StringName &p_method) const {
	compile();
	if (!methods.has(p_method)) {
		return Variant();
	}
	return methods[p_method].arguments.size();
}

Dictionary TypeScriptScript::_get_method_info(const StringName &p_method) const {
	compile();
	return methods.get(p_method);
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
	return class_name;
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
		Dictionary d;
		d["name"] = String(E.key);
		d["flags"] = (int)E.value.flags;
		d["id"] = E.value.id;

		Dictionary ret;
		ret["name"] = String(E.value.return_val.name);
		ret["type"] = (int)E.value.return_val.type;
		ret["class_name"] = String(E.value.return_val.class_name);
		ret["hint"] = (int)E.value.return_val.hint;
		ret["hint_string"] = E.value.return_val.hint_string;
		ret["usage"] = (int)E.value.return_val.usage;
		d["return"] = ret;

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
