#ifndef GODE_UTILS_VALUE_CONVER_H
#define GODE_UTILS_VALUE_CONVER_H

#include <napi.h>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/core/type_info.hpp>

namespace gode {
extern Napi::Value godot_to_napi(Napi::Env env, const godot::Variant &variant);
extern godot::Variant napi_to_godot(const Napi::Value &value);

// Helper to detect BitField
template <typename T>
struct is_bitfield : std::false_type {};

template <typename T>
struct is_bitfield<godot::BitField<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_bitfield_v = is_bitfield<T>::value;

template <typename T>
std::remove_const_t<std::remove_reference_t<T>> napi_to_godot(const Napi::Value &value) {
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
	} else if constexpr (std::is_enum_v<ClearType>) {
		return static_cast<ClearType>(value.ToNumber().Int64Value());
	} else if constexpr (is_bitfield_v<ClearType>) {
		return ClearType(value.ToNumber().Int64Value());
	} else {
		// Default construction for other types
		return ClearType();
	}
}
} //namespace gode

#endif // GODE_UTILS_VALUE_CONVER_H
