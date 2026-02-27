#include "support/javascript/javascript.h"
#include "support/javascript/javascript_instance.h"
#include "support/javascript/javascript_instance_info.h"
#include "support/javascript/javascript_language.h"
#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>

using namespace godot;
using namespace gode;

void Javascript::_bind_methods() {
}

bool Javascript::compile() {
	if (!is_dirty) {
		return false;
	}

	is_dirty = false;
	return true;
}

bool Javascript::_editor_can_reload_from_file() {
	return false;
}

void Javascript::_placeholder_erased(void *p_placeholder) {
}

bool Javascript::_can_instantiate() const {
	return true;
}

Ref<Script> Javascript::_get_base_script() const {
	return Ref<Javascript>();
}

StringName Javascript::_get_global_name() const {
	return StringName();
}

bool Javascript::_inherits_script(const Ref<Script> &p_script) const {
	return false;
}

StringName Javascript::_get_instance_base_type() const {
	return StringName();
}

void *Javascript::_instance_create(Object *p_for_object) const {
	static auto fn = reinterpret_cast<GDExtensionInterfaceScriptInstanceCreate3>(gdextension_interface::get_proc_address("script_instance_create3"));
	const Ref self(const_cast<Javascript *>(this));
	JavascriptInstance *instance = memnew(JavascriptInstance(self, p_for_object, false));
	return fn(&javascript_instance_info, instance);
}

void *Javascript::_placeholder_instance_create(Object *p_for_object) const {
	static auto fn = reinterpret_cast<GDExtensionInterfaceScriptInstanceCreate3>(gdextension_interface::get_proc_address("script_instance_create3"));
	const Ref self(const_cast<Javascript *>(this));
	JavascriptInstance *instance = memnew(JavascriptInstance(self, p_for_object, true));
	return fn(&javascript_instance_info, instance);
}

bool Javascript::_instance_has(Object *p_object) const {
	(void)p_object;
	return true;
}

bool Javascript::_has_source_code() const {
	return false;
}

String Javascript::_get_source_code() const {
	return source_code;
}

void Javascript::_set_source_code(const String &p_code) {
	is_dirty = true;
	source_code = p_code;
}

Error Javascript::_reload(bool p_keep_state) {
	return Error::OK;
}

StringName Javascript::_get_doc_class_name() const {
	return StringName();
}

TypedArray<Dictionary> Javascript::_get_documentation() const {
	TypedArray<Dictionary> docs;
	return docs;
}

String Javascript::_get_class_icon_path() const {
	return String();
}

bool Javascript::_has_method(const StringName &p_method) const {
	return false;
}

bool Javascript::_has_static_method(const StringName &p_method) const {
	return false;
}

Variant Javascript::_get_script_method_argument_count(const StringName &p_method) const {
	return Variant();
}

Dictionary Javascript::_get_method_info(const StringName &p_method) const {
	Dictionary info;
	return info;
}

bool Javascript::_is_tool() const {
	return false;
}

bool Javascript::_is_valid() const {
	return false;
}

bool Javascript::_is_abstract() const {
	return false;
}

ScriptLanguage *Javascript::_get_language() const {
	return JavascriptLanguage::get_singleton();
}

bool Javascript::_has_script_signal(const StringName &p_signal) const {
	return false;
}

TypedArray<Dictionary> Javascript::_get_script_signal_list() const {
	TypedArray<Dictionary> list;
	return list;
}

bool Javascript::_has_property_default_value(const StringName &p_property) const {
	return false;
}

Variant Javascript::_get_property_default_value(const StringName &p_property) const {
	return Variant();
}

void Javascript::_update_exports() {
}

TypedArray<Dictionary> Javascript::_get_script_method_list() const {
	TypedArray<Dictionary> list;
	return list;
}

TypedArray<Dictionary> Javascript::_get_script_property_list() const {
	TypedArray<Dictionary> list;
	return list;
}

int32_t Javascript::_get_member_line(const StringName &p_member) const {
	return -1;
}

Dictionary Javascript::_get_constants() const {
	Dictionary constants;
	return constants;
}

TypedArray<StringName> Javascript::_get_members() const {
	TypedArray<StringName> members;
	return members;
}

bool Javascript::_is_placeholder_fallback_enabled() const {
	return false;
}

Variant Javascript::_get_rpc_config() const {
	return Variant();
}
