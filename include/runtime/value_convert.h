#ifndef GODE_UTILS_VALUE_CONVER_H
#define GODE_UTILS_VALUE_CONVER_H

#include <napi.h>
#include <cmath>
#include <cstdint>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/variant/builtin_types.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/typed_dictionary.hpp>
#include <limits>
#include <string>
#include <type_traits>

namespace gode {
extern Napi::Value godot_to_napi(Napi::Env env, godot::Variant variant);
extern godot::Variant napi_to_godot(Napi::Value value);
Napi::Value godot_int_to_napi(Napi::Env env, int64_t value);
Napi::Value godot_uint_to_napi(Napi::Env env, uint64_t value);
bool godot_array_length_to_uint32(Napi::Env env, int64_t length, const char *context, uint32_t &out_length);
godot::Array js_array_to_godot_array(const Napi::Array &js_array);
void sync_godot_array_to_js_array(Napi::Env env, const Napi::Value &target, const godot::Array &array);
void sync_godot_variant_out_argument(Napi::Env env, const Napi::Value &target, const godot::Variant &variant);
int64_t napi_to_godot_int64(Napi::Value value);
uint64_t napi_to_godot_uint64(Napi::Value value);

template <typename T>
Napi::Value godot_result_to_napi(Napi::Env env, const T &value) {
	using ClearType = std::remove_const_t<std::remove_reference_t<T>>;

	if constexpr (std::is_same_v<ClearType, bool>) {
		return Napi::Boolean::New(env, value);
	} else if constexpr (std::is_integral_v<ClearType> && std::is_signed_v<ClearType>) {
		return godot_int_to_napi(env, static_cast<int64_t>(value));
	} else if constexpr (std::is_integral_v<ClearType> && std::is_unsigned_v<ClearType>) {
		return godot_uint_to_napi(env, static_cast<uint64_t>(value));
	} else if constexpr (std::is_enum_v<ClearType>) {
		return godot_int_to_napi(env, static_cast<int64_t>(value));
	} else {
		return godot_to_napi(env, value);
	}
}

typedef godot::Object *(*UnwrapFunc)(const Napi::Object &);
typedef void (*WrapFunc)(const Napi::Object &, godot::Object *);
typedef Napi::Object (*CreateFunc)(Napi::Env, godot::Object *);

struct ClassInfo {
	std::string godot_class_name;
	Napi::FunctionReference *constructor;
	UnwrapFunc unwrapper;
	WrapFunc wrapper;
	CreateFunc creator;
};

void register_class(const std::string &name, const std::string &godot_class_name, Napi::FunctionReference *ref, UnwrapFunc unwrapper, WrapFunc wrapper, CreateFunc creator);
godot::Object *unwrap_godot_object(const Napi::Object &value);
void register_godot_instance(godot::Object *obj, Napi::Object js_obj);
Napi::Value wrap_godot_object(Napi::Env env, godot::Object *obj, const std::string &registered_class_name = "");
void clear_godot_instance_cache();
void clear_registered_godot_classes();
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
std::remove_const_t<std::remove_reference_t<T>> napi_to_godot(Napi::Value value);

inline void throw_bool_type_error(Napi::Env env) {
	Napi::TypeError::New(env, "Expected a boolean").ThrowAsJavaScriptException();
}

inline void throw_float_type_error(Napi::Env env) {
	Napi::TypeError::New(env, "Expected a number").ThrowAsJavaScriptException();
}

inline void throw_nan_type_error(Napi::Env env) {
	Napi::TypeError::New(env, "Expected a number that is not NaN").ThrowAsJavaScriptException();
}

inline void throw_string_type_error(Napi::Env env) {
	Napi::TypeError::New(env, "Expected a string, GDString, or StringName").ThrowAsJavaScriptException();
}

inline void throw_node_path_type_error(Napi::Env env) {
	Napi::TypeError::New(env, "Expected a string or NodePath").ThrowAsJavaScriptException();
}

inline void throw_object_type_error(Napi::Env env) {
	Napi::TypeError::New(env, "Expected a Godot object wrapper or null").ThrowAsJavaScriptException();
}

inline void throw_object_class_type_error(Napi::Env env) {
	Napi::TypeError::New(env, "Expected a Godot object wrapper compatible with the API parameter type").ThrowAsJavaScriptException();
}

inline bool napi_to_godot_bool(Napi::Value value) {
	if (!value.IsBoolean()) {
		throw_bool_type_error(value.Env());
		return false;
	}
	return value.As<Napi::Boolean>().Value();
}

template <typename T>
T napi_to_godot_float(Napi::Value value) {
	if (!value.IsNumber()) {
		throw_float_type_error(value.Env());
		return T{};
	}

	const double number = value.As<Napi::Number>().DoubleValue();
	if (std::isnan(number)) {
		throw_nan_type_error(value.Env());
		return T{};
	}
	return static_cast<T>(number);
}

inline godot::Object *napi_to_godot_object_value(Napi::Value value) {
	if (value.IsNull() || value.IsUndefined()) {
		return nullptr;
	}
	if (!value.IsObject()) {
		throw_object_type_error(value.Env());
		return nullptr;
	}

	godot::Object *object = unwrap_godot_object(value.As<Napi::Object>());
	if (!object) {
		throw_object_type_error(value.Env());
		return nullptr;
	}
	return object;
}

template <typename T>
T *napi_to_godot_object_pointer(Napi::Value value) {
	godot::Object *object = napi_to_godot_object_value(value);
	if (value.Env().IsExceptionPending() || !object) {
		return nullptr;
	}

	T *typed_object = godot::Object::cast_to<T>(object);
	if (!typed_object) {
		throw_object_class_type_error(value.Env());
		return nullptr;
	}
	return typed_object;
}

template <typename T>
struct is_godot_typed_array : std::false_type {};

template <typename T>
struct is_godot_typed_array<godot::TypedArray<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_godot_typed_array_v = is_godot_typed_array<T>::value;

template <typename T>
struct packed_array_element {};

template <>
struct packed_array_element<godot::PackedByteArray> {
	using type = uint8_t;
};
template <>
struct packed_array_element<godot::PackedInt32Array> {
	using type = int32_t;
};
template <>
struct packed_array_element<godot::PackedInt64Array> {
	using type = int64_t;
};
template <>
struct packed_array_element<godot::PackedFloat32Array> {
	using type = float;
};
template <>
struct packed_array_element<godot::PackedFloat64Array> {
	using type = double;
};
template <>
struct packed_array_element<godot::PackedStringArray> {
	using type = godot::String;
};
template <>
struct packed_array_element<godot::PackedVector2Array> {
	using type = godot::Vector2;
};
template <>
struct packed_array_element<godot::PackedVector3Array> {
	using type = godot::Vector3;
};
template <>
struct packed_array_element<godot::PackedVector4Array> {
	using type = godot::Vector4;
};
template <>
struct packed_array_element<godot::PackedColorArray> {
	using type = godot::Color;
};

template <typename T, typename = void>
struct is_godot_packed_array : std::false_type {};

template <typename T>
struct is_godot_packed_array<T, std::void_t<typename packed_array_element<T>::type>> : std::true_type {};

template <typename T>
inline constexpr bool is_godot_packed_array_v = is_godot_packed_array<T>::value;

template <typename PackedArray>
void sync_godot_packed_array_to_js_array(Napi::Env env, const Napi::Value &target, const PackedArray &value) {
	if (!target.IsArray()) {
		return;
	}

	uint32_t js_length = 0;
	if (!godot_array_length_to_uint32(env, value.size(), "Godot out packed array", js_length)) {
		return;
	}

	Napi::Array js_array = target.As<Napi::Array>();
	js_array.Set("length", Napi::Number::New(env, js_length));
	if (env.IsExceptionPending()) {
		return;
	}

	for (uint32_t i = 0; i < js_length; i++) {
		js_array.Set(i, godot_to_napi(env, godot::Variant(value[i])));
		if (env.IsExceptionPending()) {
			return;
		}
	}
}

template <typename T>
struct is_godot_ref : std::false_type {};

template <typename T>
struct is_godot_ref<godot::Ref<T>> : std::true_type {
	using element_type = T;
};

template <typename T>
inline constexpr bool is_godot_ref_v = is_godot_ref<T>::value;

template <typename T>
void sync_godot_out_argument(Napi::Env env, const Napi::Value &target, const T &value) {
	using ClearType = std::remove_const_t<std::remove_reference_t<T>>;

	if constexpr (std::is_same_v<ClearType, godot::Array>) {
		sync_godot_array_to_js_array(env, target, value);
	} else if constexpr (is_godot_typed_array_v<ClearType>) {
		sync_godot_array_to_js_array(env, target, value);
	} else if constexpr (is_godot_packed_array_v<ClearType>) {
		sync_godot_packed_array_to_js_array(env, target, value);
	}
}

template <typename PackedArray>
PackedArray js_array_to_packed_array(const Napi::Array &js_array) {
	using ElementType = typename packed_array_element<PackedArray>::type;

	PackedArray array;
	const uint32_t length = js_array.Length();
	for (uint32_t i = 0; i < length; i++) {
		ElementType element = napi_to_godot<ElementType>(js_array.Get(i));
		if (js_array.Env().IsExceptionPending()) {
			return array;
		}
		array.append(element);
	}
	return array;
}

template <typename TypedArray>
TypedArray js_array_to_typed_array(const Napi::Array &js_array) {
	return TypedArray(js_array_to_godot_array(js_array));
}

template <typename T>
std::remove_const_t<std::remove_reference_t<T>> napi_to_godot(Napi::Value value) {
	using ClearType = std::remove_const_t<std::remove_reference_t<T>>;

	if constexpr (std::is_same_v<ClearType, bool>) {
		return napi_to_godot_bool(value);
	} else if constexpr (std::is_integral_v<ClearType>) {
		if constexpr (std::is_signed_v<ClearType>) {
			const int64_t integer = napi_to_godot_int64(value);
			if (integer < static_cast<int64_t>(std::numeric_limits<ClearType>::lowest()) ||
					integer > static_cast<int64_t>(std::numeric_limits<ClearType>::max())) {
				Napi::RangeError::New(value.Env(), "Integer value is outside the target Godot API type range").ThrowAsJavaScriptException();
				return ClearType{};
			}
			return static_cast<ClearType>(integer);
		} else {
			const uint64_t integer = napi_to_godot_uint64(value);
			if (integer > static_cast<uint64_t>(std::numeric_limits<ClearType>::max())) {
				Napi::RangeError::New(value.Env(), "Integer value is outside the target Godot API type range").ThrowAsJavaScriptException();
				return ClearType{};
			}
			return static_cast<ClearType>(integer);
		}
	} else if constexpr (std::is_floating_point_v<ClearType>) {
		return napi_to_godot_float<ClearType>(value);
	} else if constexpr (std::is_same_v<ClearType, godot::String> || std::is_same_v<ClearType, godot::StringName>) {
		if (value.IsString()) {
			const godot::String string = godot::String::utf8(value.ToString().Utf8Value().c_str());
			return ClearType(string);
		}
		if (value.IsObject()) {
			godot::Variant variant = napi_to_godot(value);
			if (value.Env().IsExceptionPending()) {
				return ClearType();
			}
			if (variant.get_type() == godot::Variant::Type::STRING || variant.get_type() == godot::Variant::Type::STRING_NAME) {
				return variant;
			}
		}
		throw_string_type_error(value.Env());
		return ClearType();
	} else if constexpr (std::is_same_v<ClearType, godot::NodePath>) {
		if (value.IsString()) {
			const godot::String string = godot::String::utf8(value.ToString().Utf8Value().c_str());
			return godot::NodePath(string);
		}
		if (value.IsObject()) {
			godot::Variant variant = napi_to_godot(value);
			if (value.Env().IsExceptionPending()) {
				return godot::NodePath();
			}
			if (variant.get_type() == godot::Variant::Type::NODE_PATH) {
				return variant;
			}
			if (variant.get_type() == godot::Variant::Type::STRING) {
				return godot::NodePath(variant.operator godot::String());
			}
		}
		throw_node_path_type_error(value.Env());
		return godot::NodePath();
	} else if constexpr (std::is_same_v<ClearType, godot::Array>) {
		if (value.IsArray()) {
			return js_array_to_godot_array(value.As<Napi::Array>());
		}
		return napi_to_godot(value);
	} else if constexpr (is_godot_typed_array_v<ClearType>) {
		if (value.IsArray()) {
			return js_array_to_typed_array<ClearType>(value.As<Napi::Array>());
		}
		return napi_to_godot(value);
	} else if constexpr (is_godot_packed_array_v<ClearType>) {
		if (value.IsArray()) {
			return js_array_to_packed_array<ClearType>(value.As<Napi::Array>());
		}
		return napi_to_godot(value);
	} else if constexpr (std::is_same_v<ClearType, godot::Basis>) {
		godot::Variant variant = napi_to_godot(value);
		if (variant.get_type() == godot::Variant::Type::QUATERNION) {
			return godot::Basis(variant.operator godot::Quaternion());
		}
		return variant;
	} else if constexpr (std::is_enum_v<ClearType>) {
		return static_cast<ClearType>(napi_to_godot_int64(value));
	} else if constexpr (is_bitfield_v<ClearType>) {
		return ClearType(napi_to_godot_int64(value));
	} else if constexpr (is_godot_ref_v<ClearType>) {
		using RefTarget = typename is_godot_ref<ClearType>::element_type;
		RefTarget *typed_object = napi_to_godot_object_pointer<RefTarget>(value);
		if (value.Env().IsExceptionPending() || !typed_object) {
			return ClearType();
		}
		return ClearType(godot::Variant(typed_object));
	} else if constexpr (std::is_pointer_v<ClearType> && std::is_base_of_v<godot::Object, std::remove_pointer_t<ClearType>>) {
		return napi_to_godot_object_pointer<std::remove_pointer_t<ClearType>>(value);
	} else {
		// Handle builtin types by calling the non-template napi_to_godot
		return napi_to_godot(value);
	}
}
} //namespace gode

#endif // GODE_UTILS_VALUE_CONVER_H
