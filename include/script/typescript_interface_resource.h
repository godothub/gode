#ifndef GODE_TYPESCRIPT_INTERFACE_RESOURCE_H
#define GODE_TYPESCRIPT_INTERFACE_RESOURCE_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace gode {

class TypeScriptInterfaceResource : public godot::Resource {
	GDCLASS(TypeScriptInterfaceResource, godot::Resource)

	struct Schema {
		godot::StringName interface_name;
		godot::HashMap<godot::StringName, godot::Vector<godot::PropertyInfo>> interfaces;
	};

	static godot::HashMap<godot::StringName, Schema> schemas;

	godot::StringName schema_id;
	godot::Vector<godot::PropertyInfo> fields;
	godot::HashMap<godot::StringName, godot::StringName> nested_interfaces;
	godot::HashMap<godot::StringName, godot::StringName> nested_interface_arrays;
	godot::HashMap<godot::StringName, godot::Variant> default_values;
	godot::HashMap<godot::StringName, godot::Variant> values;

	void apply_schema();
	void configure_nested_value(const godot::StringName &p_name, const godot::Variant &p_value);

protected:
	static void _bind_methods();
	bool _set(const godot::StringName &p_name, const godot::Variant &p_value);
	bool _get(const godot::StringName &p_name, godot::Variant &r_value) const;
	void _get_property_list(godot::List<godot::PropertyInfo> *p_list) const;

public:
	static void register_schema(
			const godot::StringName &p_schema_id,
			const godot::StringName &p_interface_name,
			const godot::HashMap<godot::StringName, godot::Vector<godot::PropertyInfo>> &p_interfaces);
	void configure(const godot::StringName &p_schema_id);
	int64_t get_interface_field_count() const { return fields.size(); }
	godot::StringName get_interface_field_name(int64_t p_index) const;
};

} // namespace gode

#endif // GODE_TYPESCRIPT_INTERFACE_RESOURCE_H
