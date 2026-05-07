#ifndef GODE_UTILS_VALUE_CONVER_H
#define GODE_UTILS_VALUE_CONVER_H

#include <napi.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <string>

namespace gode {
extern Napi::Value godot_to_napi(Napi::Env env, godot::Variant variant);
extern godot::Variant napi_to_godot(Napi::Value value);

typedef godot::Object *(*UnwrapFunc)(const Napi::Object &);
typedef void (*WrapFunc)(const Napi::Object &, godot::Object *);

struct ClassInfo {
	std::string godot_class_name;
	Napi::FunctionReference *constructor;
	UnwrapFunc unwrapper;
	WrapFunc wrapper;
};

void register_class(const std::string &name, const std::string &godot_class_name, Napi::FunctionReference *ref, UnwrapFunc unwrapper, WrapFunc wrapper);
godot::Object *unwrap_godot_object(const Napi::Object &value);
void register_godot_instance(godot::Object *obj, Napi::Object js_obj);
void bind_builtin_owner_property(const Napi::Value &value, godot::Object *owner, const godot::StringName &property);
void bind_builtin_parent_property(const Napi::Value &value, const Napi::Object &parent, const godot::StringName &property);

// Helper to detect BitField
template <typename T>
struct is_bitfield : std::false_type {};

template <typename T>
struct is_bitfield<godot::BitField<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_bitfield_v = is_bitfield<T>::value;

template <typename T>
std::remove_const_t<std::remove_reference_t<T>> napi_to_godot(Napi::Value value) {
	using ClearType = std::remove_const_t<std::remove_reference_t<T>>;

	if constexpr (std::is_same_v<ClearType, bool>) {
		return value.ToBoolean().Value();
	} else if constexpr (std::is_integral_v<ClearType>) {
		return static_cast<ClearType>(value.ToNumber().Int64Value());
	} else if constexpr (std::is_floating_point_v<ClearType>) {
		return static_cast<ClearType>(value.ToNumber().DoubleValue());
	} else if constexpr (std::is_same_v<ClearType, godot::String> || std::is_same_v<ClearType, godot::StringName>) {
		if (value.IsString()) {
			return godot::String::utf8(value.ToString().Utf8Value().c_str());
		}
		return godot::String();
	} else if constexpr (std::is_same_v<ClearType, godot::Basis>) {
		godot::Variant variant = napi_to_godot(value);
		if (variant.get_type() == godot::Variant::Type::QUATERNION) {
			return godot::Basis(variant.operator godot::Quaternion());
		}
		return variant;
	} else if constexpr (std::is_enum_v<ClearType>) {
		return static_cast<ClearType>(value.ToNumber().Int64Value());
	} else if constexpr (is_bitfield_v<ClearType>) {
		return ClearType(value.ToNumber().Int64Value());
	} else if constexpr (std::is_pointer_v<ClearType> && std::is_base_of_v<godot::Object, std::remove_pointer_t<ClearType>>) {
		godot::Variant variant = napi_to_godot(value);
		return godot::Object::cast_to<std::remove_pointer_t<ClearType>>(variant.operator godot::Object *());
	} else {
		// Handle builtin types by calling the non-template napi_to_godot
		return napi_to_godot(value);
	}
}
} //namespace gode

#endif // GODE_UTILS_VALUE_CONVER_H
