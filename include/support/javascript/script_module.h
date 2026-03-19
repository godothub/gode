#ifndef GODE_SCRIPT_MODULE_H
#define GODE_SCRIPT_MODULE_H

#include <napi.h>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/classes/script_language.hpp>

namespace gode {

// Non-Godot interface shared by Javascript and Typescript.
// Provides the custom methods that JavascriptInstance needs beyond ScriptExtension virtuals.
class IScriptModule {
public:
	virtual bool compile() const = 0;
	virtual Napi::Function get_default_class() const = 0;
	virtual const godot::HashMap<godot::StringName, godot::PropertyInfo> &get_exported_properties() const = 0;
	virtual const godot::HashMap<godot::StringName, godot::Variant> &get_property_defaults() const = 0;
	virtual godot::ScriptLanguage *get_script_language() const = 0;
	virtual bool _has_method(const godot::StringName &p_method) const = 0;
	virtual bool _has_property_default_value(const godot::StringName &p_property) const = 0;
	virtual godot::Variant _get_property_default_value(const godot::StringName &p_property) const = 0;
	virtual godot::StringName get_global_name() const = 0;
	virtual ~IScriptModule() = default;
};

} // namespace gode

#endif // GODE_SCRIPT_MODULE_H
