#include "runtime/value_convert.h"

#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include "godot_cpp/variant/utility_functions.hpp"
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/builtin_types.hpp>

#include "builtin/aabb_binding.gen.h"
#include "builtin/array_binding.gen.h"
#include "builtin/basis_binding.gen.h"
#include "builtin/callable_binding.gen.h"
#include "builtin/color_binding.gen.h"
#include "builtin/dictionary_binding.gen.h"
#include "builtin/node_path_binding.gen.h"
#include "builtin/packed_byte_array_binding.gen.h"
#include "builtin/packed_color_array_binding.gen.h"
#include "builtin/packed_float32_array_binding.gen.h"
#include "builtin/packed_float64_array_binding.gen.h"
#include "builtin/packed_int32_array_binding.gen.h"
#include "builtin/packed_int64_array_binding.gen.h"
#include "builtin/packed_string_array_binding.gen.h"
#include "builtin/packed_vector2_array_binding.gen.h"
#include "builtin/packed_vector3_array_binding.gen.h"
#include "builtin/packed_vector4_array_binding.gen.h"
#include "builtin/plane_binding.gen.h"
#include "builtin/projection_binding.gen.h"
#include "builtin/quaternion_binding.gen.h"
#include "builtin/rect2_binding.gen.h"
#include "builtin/rect2i_binding.gen.h"
#include "builtin/rid_binding.gen.h"
#include "builtin/signal_binding.gen.h"
#include "builtin/string_binding.gen.h"
#include "builtin/string_name_binding.gen.h"
#include "builtin/transform2d_binding.gen.h"
#include "builtin/transform3d_binding.gen.h"
#include "builtin/vector2_binding.gen.h"
#include "builtin/vector2i_binding.gen.h"
#include "builtin/vector3_binding.gen.h"
#include "builtin/vector3i_binding.gen.h"
#include "builtin/vector4_binding.gen.h"
#include "builtin/vector4i_binding.gen.h"
#include "runtime/node_runtime.h"
#include "script/script_callable.h"

// Helper macros for creating N-API objects from Godot variants
#define BIND_BUILTIN_TO_NAPI(VariantType, BindingClass)               \
	case godot::Variant::Type::VariantType: {                         \
		Napi::Object obj = BindingClass::constructor.Value().New({}); \
		BindingClass *binding = BindingClass::Unwrap(obj);            \
		binding->instance = variant;                                  \
		return obj;                                                   \
	}

using namespace godot;

namespace gode {

static std::unordered_map<std::string, ClassInfo> class_registry;
static std::vector<std::string> class_order;
static std::unordered_map<uint64_t, Napi::ObjectReference> object_cache;

constexpr const char *GODOT_OBJECT_ID_SYMBOL = "__gode.godot_object_id__";
constexpr const char *GODOT_OBJECT_PTR_SYMBOL = "__gode.godot_object_ptr__";
constexpr double JS_MAX_SAFE_INTEGER = 9007199254740991.0;
constexpr int64_t JS_MAX_SAFE_INTEGER_INT64 = 9007199254740991LL;
constexpr uint64_t JS_MAX_SAFE_INTEGER_UINT64 = 9007199254740991ULL;

static bool is_safe_js_integer(double number) {
	return std::isfinite(number) && std::trunc(number) == number && std::fabs(number) <= JS_MAX_SAFE_INTEGER;
}

static void throw_integer_type_error(Napi::Env env) {
	Napi::TypeError::New(env, "Expected a finite safe integer number or bigint").ThrowAsJavaScriptException();
}

static void throw_integer_range_error(Napi::Env env) {
	Napi::RangeError::New(env, "Integer value is outside the Godot 64-bit integer range").ThrowAsJavaScriptException();
}

Napi::Value godot_int_to_napi(Napi::Env env, int64_t value) {
	if (value >= -JS_MAX_SAFE_INTEGER_INT64 && value <= JS_MAX_SAFE_INTEGER_INT64) {
		return Napi::Number::New(env, static_cast<double>(value));
	}
	return Napi::BigInt::New(env, value);
}

Napi::Value godot_uint_to_napi(Napi::Env env, uint64_t value) {
	if (value <= JS_MAX_SAFE_INTEGER_UINT64) {
		return Napi::Number::New(env, static_cast<double>(value));
	}
	return Napi::BigInt::New(env, value);
}

bool godot_array_length_to_uint32(Napi::Env env, int64_t length, const char *context, uint32_t &out_length) {
	if (length < 0 || length > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
		std::string label = context && context[0] ? context : "Godot array";
		Napi::RangeError::New(env, label + " is too large to represent as a JavaScript Array").ThrowAsJavaScriptException();
		out_length = 0;
		return false;
	}

	out_length = static_cast<uint32_t>(length);
	return true;
}

int64_t napi_to_godot_int64(Napi::Value value) {
	if (value.IsBigInt()) {
		bool lossless = false;
		const int64_t integer = value.As<Napi::BigInt>().Int64Value(&lossless);
		if (!lossless) {
			throw_integer_range_error(value.Env());
			return 0;
		}
		return integer;
	}

	if (!value.IsNumber()) {
		throw_integer_type_error(value.Env());
		return 0;
	}

	const double number = value.ToNumber().DoubleValue();
	if (!is_safe_js_integer(number)) {
		throw_integer_type_error(value.Env());
		return 0;
	}
	return static_cast<int64_t>(number);
}

uint64_t napi_to_godot_uint64(Napi::Value value) {
	if (value.IsBigInt()) {
		bool lossless = false;
		const uint64_t integer = value.As<Napi::BigInt>().Uint64Value(&lossless);
		if (!lossless) {
			throw_integer_range_error(value.Env());
			return 0;
		}
		return integer;
	}

	if (!value.IsNumber()) {
		throw_integer_type_error(value.Env());
		return 0;
	}

	const double number = value.ToNumber().DoubleValue();
	if (!is_safe_js_integer(number) || number < 0.0) {
		throw_integer_type_error(value.Env());
		return 0;
	}
	return static_cast<uint64_t>(number);
}

static godot::Dictionary object_to_dictionary(const Napi::Object &obj) {
	godot::Dictionary dict;
	Napi::Array property_names = obj.GetPropertyNames();
	uint32_t property_count = property_names.Length();

	for (uint32_t i = 0; i < property_count; i++) {
		Napi::Value key = property_names.Get(i);
		Napi::Value val = obj.Get(key);
		godot::Variant godot_key = napi_to_godot(key);
		if (obj.Env().IsExceptionPending()) {
			return dict;
		}
		godot::Variant godot_value = napi_to_godot(val);
		if (obj.Env().IsExceptionPending()) {
			return dict;
		}
		dict[godot_key] = godot_value;
	}

	return dict;
}

godot::Array js_array_to_godot_array(const Napi::Array &js_array) {
	godot::Array array;
	const uint32_t length = js_array.Length();
	for (uint32_t i = 0; i < length; i++) {
		array.append(napi_to_godot(js_array.Get(i)));
		if (js_array.Env().IsExceptionPending()) {
			return array;
		}
	}
	return array;
}

void sync_godot_array_to_js_array(Napi::Env env, const Napi::Value &target, const godot::Array &array) {
	if (!target.IsArray()) {
		return;
	}

	uint32_t js_length = 0;
	if (!godot_array_length_to_uint32(env, array.size(), "Godot out array", js_length)) {
		return;
	}

	Napi::Array js_array = target.As<Napi::Array>();
	js_array.Set("length", Napi::Number::New(env, js_length));
	if (env.IsExceptionPending()) {
		return;
	}

	for (uint32_t i = 0; i < js_length; i++) {
		js_array.Set(i, godot_to_napi(env, array[i]));
		if (env.IsExceptionPending()) {
			return;
		}
	}
}

void sync_godot_variant_out_argument(Napi::Env env, const Napi::Value &target, const godot::Variant &variant) {
	if (!target.IsArray()) {
		return;
	}

	switch (variant.get_type()) {
		case godot::Variant::Type::ARRAY:
			sync_godot_array_to_js_array(env, target, variant.operator godot::Array());
			return;
		case godot::Variant::Type::PACKED_BYTE_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedByteArray());
			return;
		case godot::Variant::Type::PACKED_INT32_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedInt32Array());
			return;
		case godot::Variant::Type::PACKED_INT64_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedInt64Array());
			return;
		case godot::Variant::Type::PACKED_FLOAT32_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedFloat32Array());
			return;
		case godot::Variant::Type::PACKED_FLOAT64_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedFloat64Array());
			return;
		case godot::Variant::Type::PACKED_STRING_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedStringArray());
			return;
		case godot::Variant::Type::PACKED_VECTOR2_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedVector2Array());
			return;
		case godot::Variant::Type::PACKED_VECTOR3_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedVector3Array());
			return;
		case godot::Variant::Type::PACKED_VECTOR4_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedVector4Array());
			return;
		case godot::Variant::Type::PACKED_COLOR_ARRAY:
			sync_godot_packed_array_to_js_array(env, target, variant.operator godot::PackedColorArray());
			return;
		default:
			return;
	}
}

static bool is_js_map(const Napi::Object &obj) {
	Napi::Value map_constructor = obj.Env().Global().Get("Map");
	return map_constructor.IsFunction() && obj.InstanceOf(map_constructor.As<Napi::Function>());
}

static godot::Dictionary js_map_to_godot_dictionary(const Napi::Object &map) {
	godot::Dictionary dict;
	Napi::Value entries_value = map.Get("entries");
	if (!entries_value.IsFunction()) {
		return dict;
	}

	Napi::Value iterator_value = entries_value.As<Napi::Function>().Call(map, {});
	if (!iterator_value.IsObject()) {
		return dict;
	}

	Napi::Object iterator = iterator_value.As<Napi::Object>();
	Napi::Value next_value = iterator.Get("next");
	if (!next_value.IsFunction()) {
		return dict;
	}

	Napi::Function next = next_value.As<Napi::Function>();
	while (true) {
		Napi::Value step_value = next.Call(iterator, {});
		if (!step_value.IsObject()) {
			break;
		}

		Napi::Object step = step_value.As<Napi::Object>();
		if (step.Get("done").ToBoolean().Value()) {
			break;
		}

		Napi::Value entry_value = step.Get("value");
		if (!entry_value.IsArray()) {
			continue;
		}

		Napi::Array entry = entry_value.As<Napi::Array>();
		if (entry.Length() < 2) {
			continue;
		}

		godot::Variant godot_key = napi_to_godot(entry.Get(static_cast<uint32_t>(0)));
		if (map.Env().IsExceptionPending()) {
			return dict;
		}
		godot::Variant godot_value = napi_to_godot(entry.Get(static_cast<uint32_t>(1)));
		if (map.Env().IsExceptionPending()) {
			return dict;
		}
		dict[godot_key] = godot_value;
	}

	return dict;
}

static bool dictionary_key_fits_js_object(const godot::Variant &key) {
	const godot::Variant::Type type = key.get_type();
	return type == godot::Variant::Type::STRING || type == godot::Variant::Type::STRING_NAME;
}

static Napi::Value dictionary_to_map(Napi::Env env, const godot::Dictionary &godot_dictionary, const godot::Array &keys) {
	Napi::Value map_constructor_value = env.Global().Get("Map");
	if (!map_constructor_value.IsFunction()) {
		Napi::Object fallback = Napi::Object::New(env);
		const int64_t key_count = keys.size();
		for (int64_t i = 0; i < key_count; i++) {
			const godot::Variant key = keys[i];
			const godot::String key_string = key.operator godot::String();
			Napi::Value js_value = godot_to_napi(env, godot_dictionary.get(key, Variant()));
			if (env.IsExceptionPending()) {
				return env.Undefined();
			}
			fallback.Set(
					Napi::String::New(env, key_string.utf8().get_data()),
					js_value);
			if (env.IsExceptionPending()) {
				return env.Undefined();
			}
		}
		return fallback;
	}

	Napi::Object map = map_constructor_value.As<Napi::Function>().New({});
	Napi::Value set_value = map.Get("set");
	if (!set_value.IsFunction()) {
		return map;
	}

	Napi::Function set = set_value.As<Napi::Function>();
	const int64_t key_count = keys.size();
	for (int64_t i = 0; i < key_count; i++) {
		const godot::Variant key = keys[i];
		Napi::Value js_key = godot_to_napi(env, key);
		if (env.IsExceptionPending()) {
			return env.Undefined();
		}
		Napi::Value js_value = godot_to_napi(env, godot_dictionary.get(key, Variant()));
		if (env.IsExceptionPending()) {
			return env.Undefined();
		}
		set.Call(map, {
							  js_key,
							  js_value,
					  });
		if (env.IsExceptionPending()) {
			return env.Undefined();
		}
	}

	return map;
}

static Napi::Value godot_dictionary_to_napi(Napi::Env env, const godot::Dictionary &godot_dictionary) {
	const godot::Array keys = godot_dictionary.keys();
	const int64_t key_count = keys.size();
	bool object_safe = true;
	for (int64_t i = 0; i < key_count; i++) {
		if (!dictionary_key_fits_js_object(keys[i])) {
			object_safe = false;
			break;
		}
	}

	if (!object_safe) {
		return dictionary_to_map(env, godot_dictionary, keys);
	}

	Napi::Object js_object = Napi::Object::New(env);
	for (int64_t i = 0; i < key_count; i++) {
		const godot::Variant key = keys[i];
		const godot::String key_string = key.operator godot::String();
		Napi::Value js_value = godot_to_napi(env, godot_dictionary.get(key, Variant()));
		if (env.IsExceptionPending()) {
			return env.Undefined();
		}
		js_object.Set(
				Napi::String::New(env, key_string.utf8().get_data()),
				js_value);
		if (env.IsExceptionPending()) {
			return env.Undefined();
		}
	}
	return js_object;
}

void register_class(const std::string &name, const std::string &godot_class_name, Napi::FunctionReference *ref, UnwrapFunc unwrapper, WrapFunc wrapper, CreateFunc creator) {
	const bool is_new_class = class_registry.find(name) == class_registry.end();
	ClassInfo info = { godot_class_name, ref, unwrapper, wrapper, creator };
	class_registry[name] = info;
	if (is_new_class) {
		class_order.push_back(name);
	}
}

static void release_object_reference(Napi::ObjectReference &ref) {
	if (ref.IsEmpty()) {
		return;
	}

	if (NodeRuntime::is_running()) {
		ref.Reset();
	} else {
		ref.SuppressDestruct();
	}
}

void register_godot_instance(godot::Object *obj, Napi::Object js_obj) {
	if (!obj) {
		return;
	}
	Napi::Env env = js_obj.Env();
	uint64_t id = obj->get_instance_id();
	js_obj.Set(Napi::Symbol::For(env, GODOT_OBJECT_ID_SYMBOL), Napi::BigInt::New(env, id));
	js_obj.Set(Napi::Symbol::For(env, GODOT_OBJECT_PTR_SYMBOL), Napi::External<godot::Object>::New(env, obj));
	object_cache[id] = Napi::Persistent(js_obj);
}

void clear_godot_instance_cache() {
	for (auto &entry : object_cache) {
		release_object_reference(entry.second);
	}
	object_cache.clear();
}

void clear_registered_godot_classes() {
	class_registry.clear();
	class_order.clear();
}

godot::Object *unwrap_godot_object(const Napi::Object &obj) {
	Napi::Env env = obj.Env();
	Napi::Symbol id_symbol = Napi::Symbol::For(env, GODOT_OBJECT_ID_SYMBOL);
	if (obj.Has(id_symbol)) {
		Napi::Value id_value = obj.Get(id_symbol);
		uint64_t id = 0;
		bool lossless = true;
		if (id_value.IsBigInt()) {
			id = id_value.As<Napi::BigInt>().Uint64Value(&lossless);
		} else if (id_value.IsNumber()) {
			id = static_cast<uint64_t>(id_value.As<Napi::Number>().Int64Value());
		}
		if (id != 0) {
			godot::Object *stored = godot::ObjectDB::get_instance(id);
			if (stored) {
				return stored;
			}
			return nullptr;
		}
	}

	Napi::Symbol ptr_symbol = Napi::Symbol::For(env, GODOT_OBJECT_PTR_SYMBOL);
	if (obj.Has(ptr_symbol)) {
		Napi::Value ptr_value = obj.Get(ptr_symbol);
		if (ptr_value.IsExternal()) {
			return ptr_value.As<Napi::External<godot::Object>>().Data();
		}
	}

	for (const std::string &registered_name : class_order) {
		auto registered = class_registry.find(registered_name);
		if (registered == class_registry.end()) {
			continue;
		}
		const ClassInfo &info = registered->second;
		if (!info.constructor || info.constructor->IsEmpty()) {
			continue;
		}
		if (obj.InstanceOf(info.constructor->Value())) {
			return info.unwrapper(obj);
		}
	}
	return nullptr;
}

#define BIND_OWNER_TO_BUILTIN(BindingClass)                  \
	if (obj.InstanceOf(BindingClass::constructor.Value())) { \
		BindingClass *binding = BindingClass::Unwrap(obj);   \
		binding->bind_owner_property(owner, property);       \
		return;                                              \
	}

#define BIND_PARENT_TO_BUILTIN(BindingClass)                 \
	if (obj.InstanceOf(BindingClass::constructor.Value())) { \
		BindingClass *binding = BindingClass::Unwrap(obj);   \
		binding->bind_parent_property(parent, property);     \
		return;                                              \
	}

void bind_builtin_owner_property(const Napi::Value &value, godot::Object *owner, const godot::StringName &property) {
	if (!owner || property == godot::StringName() || !value.IsObject()) {
		return;
	}
	Napi::Object obj = value.As<Napi::Object>();

	BIND_OWNER_TO_BUILTIN(Vector2Binding)
	BIND_OWNER_TO_BUILTIN(Vector2iBinding)
	BIND_OWNER_TO_BUILTIN(Rect2Binding)
	BIND_OWNER_TO_BUILTIN(Rect2iBinding)
	BIND_OWNER_TO_BUILTIN(Vector3Binding)
	BIND_OWNER_TO_BUILTIN(Vector3iBinding)
	BIND_OWNER_TO_BUILTIN(Transform2DBinding)
	BIND_OWNER_TO_BUILTIN(Vector4Binding)
	BIND_OWNER_TO_BUILTIN(Vector4iBinding)
	BIND_OWNER_TO_BUILTIN(PlaneBinding)
	BIND_OWNER_TO_BUILTIN(QuaternionBinding)
	BIND_OWNER_TO_BUILTIN(AABBBinding)
	BIND_OWNER_TO_BUILTIN(BasisBinding)
	BIND_OWNER_TO_BUILTIN(Transform3DBinding)
	BIND_OWNER_TO_BUILTIN(ProjectionBinding)
	BIND_OWNER_TO_BUILTIN(ColorBinding)
	BIND_OWNER_TO_BUILTIN(NodePathBinding)
	BIND_OWNER_TO_BUILTIN(RIDBinding)
}

void bind_builtin_parent_property(const Napi::Value &value, const Napi::Object &parent, const godot::StringName &property) {
	if (property == godot::StringName() || !value.IsObject()) {
		return;
	}
	Napi::Object obj = value.As<Napi::Object>();

	BIND_PARENT_TO_BUILTIN(Vector2Binding)
	BIND_PARENT_TO_BUILTIN(Vector2iBinding)
	BIND_PARENT_TO_BUILTIN(Rect2Binding)
	BIND_PARENT_TO_BUILTIN(Rect2iBinding)
	BIND_PARENT_TO_BUILTIN(Vector3Binding)
	BIND_PARENT_TO_BUILTIN(Vector3iBinding)
	BIND_PARENT_TO_BUILTIN(Transform2DBinding)
	BIND_PARENT_TO_BUILTIN(Vector4Binding)
	BIND_PARENT_TO_BUILTIN(Vector4iBinding)
	BIND_PARENT_TO_BUILTIN(PlaneBinding)
	BIND_PARENT_TO_BUILTIN(QuaternionBinding)
	BIND_PARENT_TO_BUILTIN(AABBBinding)
	BIND_PARENT_TO_BUILTIN(BasisBinding)
	BIND_PARENT_TO_BUILTIN(Transform3DBinding)
	BIND_PARENT_TO_BUILTIN(ProjectionBinding)
	BIND_PARENT_TO_BUILTIN(ColorBinding)
	BIND_PARENT_TO_BUILTIN(NodePathBinding)
	BIND_PARENT_TO_BUILTIN(RIDBinding)
}

static ClassInfo *find_class_info_for_object(godot::Object *obj) {
	if (!obj) {
		return nullptr;
	}

	const godot::StringName object_class = obj->get_class();
	const std::string exact_name = godot::String(object_class).utf8().get_data();
	auto exact = class_registry.find(exact_name);
	if (exact != class_registry.end()) {
		return &exact->second;
	}

	godot::ClassDBSingleton *class_db = godot::ClassDBSingleton::get_singleton();
	ClassInfo *best = nullptr;
	int best_distance = INT32_MAX;
	for (const std::string &registered_name : class_order) {
		auto registered = class_registry.find(registered_name);
		if (registered == class_registry.end()) {
			continue;
		}
		ClassInfo &info = registered->second;
		if (info.godot_class_name.empty()) {
			continue;
		}
		const godot::StringName candidate(info.godot_class_name.c_str());
		if (!class_db->is_parent_class(object_class, candidate)) {
			continue;
		}

		int distance = 0;
		godot::StringName current = object_class;
		while (current != candidate && current != godot::StringName()) {
			current = class_db->get_parent_class(current);
			distance++;
		}
		if (distance < best_distance) {
			best = &info;
			best_distance = distance;
		}
	}
	return best;
}

Napi::Value wrap_godot_object(Napi::Env env, godot::Object *obj, const std::string &registered_class_name) {
	if (!obj) {
		return env.Null();
	}

	uint64_t id = obj->get_instance_id();
	auto cached_it = object_cache.find(id);
	if (cached_it != object_cache.end()) {
		if (godot::ObjectDB::get_instance(id) && !cached_it->second.IsEmpty()) {
			if (cached_it->second.Env() == env) {
				Napi::Object cached = cached_it->second.Value();
				if (!cached.IsEmpty()) {
					return cached;
				}
			}
			release_object_reference(cached_it->second);
		}
		object_cache.erase(cached_it);
	}

	ClassInfo *class_info = nullptr;
	if (!registered_class_name.empty()) {
		auto registered = class_registry.find(registered_class_name);
		if (registered != class_registry.end()) {
			class_info = &registered->second;
		}
	}
	if (!class_info) {
		class_info = find_class_info_for_object(obj);
	}

	if (!class_info || !class_info->constructor || class_info->constructor->IsEmpty()) {
		return env.Null();
	}

	if (class_info->constructor->Env() != env) {
		return env.Null();
	}

	Napi::Object js_obj;
	if (class_info->creator) {
		js_obj = class_info->creator(env, obj);
	} else {
		js_obj = class_info->constructor->Value().New({});
	}
	if (js_obj.IsEmpty()) {
		return env.Null();
	}
	if (!class_info->creator && class_info->wrapper) {
		class_info->wrapper(js_obj, obj);
	}

	register_godot_instance(obj, js_obj);
	return js_obj;
}

Napi::Value godot_to_napi(Napi::Env env, godot::Variant variant) {
	switch (variant.get_type()) {
		case godot::Variant::Type::NIL:
			return env.Null();
		case godot::Variant::Type::INT:
			return godot_int_to_napi(env, variant.operator int64_t());
		case godot::Variant::Type::FLOAT:
			return Napi::Number::New(env, variant.operator double());
		case godot::Variant::Type::BOOL:
			return Napi::Boolean::New(env, variant.operator bool());
		case godot::Variant::Type::STRING:
		case godot::Variant::Type::STRING_NAME:
			return Napi::String::New(env, variant.operator String().utf8().get_data());
		case godot::Variant::Type::ARRAY: {
			const godot::Array godot_array = variant.operator godot::Array();
			uint32_t array_length = 0;
			if (!godot_array_length_to_uint32(env, godot_array.size(), "Godot Array", array_length)) {
				return env.Undefined();
			}
			Napi::Array js_array = Napi::Array::New(env, array_length);
			for (uint32_t i = 0; i < array_length; i++) {
				js_array.Set(i, godot_to_napi(env, godot_array[i]));
				if (env.IsExceptionPending()) {
					return env.Undefined();
				}
			}
			return js_array;
		}
		case godot::Variant::Type::DICTIONARY: {
			const godot::Dictionary godot_dictionary = variant.operator godot::Dictionary();
			return godot_dictionary_to_napi(env, godot_dictionary);
		}

			BIND_BUILTIN_TO_NAPI(VECTOR2, Vector2Binding)
			BIND_BUILTIN_TO_NAPI(VECTOR2I, Vector2iBinding)
			BIND_BUILTIN_TO_NAPI(RECT2, Rect2Binding)
			BIND_BUILTIN_TO_NAPI(RECT2I, Rect2iBinding)
			BIND_BUILTIN_TO_NAPI(VECTOR3, Vector3Binding)
			BIND_BUILTIN_TO_NAPI(VECTOR3I, Vector3iBinding)
			BIND_BUILTIN_TO_NAPI(TRANSFORM2D, Transform2DBinding)
			BIND_BUILTIN_TO_NAPI(VECTOR4, Vector4Binding)
			BIND_BUILTIN_TO_NAPI(VECTOR4I, Vector4iBinding)
			BIND_BUILTIN_TO_NAPI(PLANE, PlaneBinding)
			BIND_BUILTIN_TO_NAPI(QUATERNION, QuaternionBinding)
			BIND_BUILTIN_TO_NAPI(AABB, AABBBinding)
			BIND_BUILTIN_TO_NAPI(BASIS, BasisBinding)
			BIND_BUILTIN_TO_NAPI(TRANSFORM3D, Transform3DBinding)
			BIND_BUILTIN_TO_NAPI(PROJECTION, ProjectionBinding)
			BIND_BUILTIN_TO_NAPI(COLOR, ColorBinding)
			BIND_BUILTIN_TO_NAPI(NODE_PATH, NodePathBinding)
			BIND_BUILTIN_TO_NAPI(RID, RIDBinding)
			BIND_BUILTIN_TO_NAPI(SIGNAL, SignalBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_BYTE_ARRAY, PackedByteArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_INT32_ARRAY, PackedInt32ArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_INT64_ARRAY, PackedInt64ArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_FLOAT32_ARRAY, PackedFloat32ArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_FLOAT64_ARRAY, PackedFloat64ArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_STRING_ARRAY, PackedStringArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_VECTOR2_ARRAY, PackedVector2ArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_VECTOR3_ARRAY, PackedVector3ArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_VECTOR4_ARRAY, PackedVector4ArrayBinding)
			BIND_BUILTIN_TO_NAPI(PACKED_COLOR_ARRAY, PackedColorArrayBinding)

		case godot::Variant::Type::CALLABLE: {
			godot::Callable callable = variant;
			if (callable.is_custom()) {
				gode::ScriptCallable *js_callable = dynamic_cast<gode::ScriptCallable *>(callable.get_custom());
				if (js_callable) {
					return js_callable->get_function();
				}
			}
			Napi::Object obj = CallableBinding::constructor.Value().New({});
			CallableBinding *binding = CallableBinding::Unwrap(obj);
			binding->instance = variant;

			// Register custom callable properties
			if (callable.is_custom()) {
				obj.Set("callable", Napi::External<void>::New(env, callable.get_custom()));
			}

			return obj;
		}

		case godot::Variant::Type::OBJECT: {
			godot::Object *obj = variant.operator godot::Object *();
			return wrap_godot_object(env, obj);
		}
		default:
			return env.Undefined();
	}
}

#define BIND_NAPI_TO_BUILTIN(BindingClass)                   \
	if (obj.InstanceOf(BindingClass::constructor.Value())) { \
		BindingClass *binding = BindingClass::Unwrap(obj);   \
		return binding->instance;                            \
	}

godot::Variant napi_to_godot(Napi::Value value) {
	if (value.IsNumber()) {
		const double number = value.ToNumber().DoubleValue();
		if (is_safe_js_integer(number)) {
			return static_cast<int64_t>(number);
		}
		return number;
	} else if (value.IsBigInt()) {
		return napi_to_godot_int64(value);
	} else if (value.IsBoolean()) {
		return value.ToBoolean().Value();
	} else if (value.IsString()) {
		return String::utf8(value.ToString().Utf8Value().c_str());
	} else if (value.IsFunction()) {
		ScriptCallable *callable = memnew(ScriptCallable(value.As<Napi::Function>()));
		return godot::Callable(callable);
	} else if (value.IsArray()) {
		return js_array_to_godot_array(value.As<Napi::Array>());
	} else if (value.IsObject()) {
		Napi::Object obj = value.As<Napi::Object>();

		BIND_NAPI_TO_BUILTIN(StringBinding)
		BIND_NAPI_TO_BUILTIN(StringNameBinding)
		BIND_NAPI_TO_BUILTIN(NodePathBinding)
		BIND_NAPI_TO_BUILTIN(Vector2Binding)
		BIND_NAPI_TO_BUILTIN(Vector2iBinding)
		BIND_NAPI_TO_BUILTIN(Rect2Binding)
		BIND_NAPI_TO_BUILTIN(Rect2iBinding)
		BIND_NAPI_TO_BUILTIN(Vector3Binding)
		BIND_NAPI_TO_BUILTIN(Vector3iBinding)
		BIND_NAPI_TO_BUILTIN(Transform2DBinding)
		BIND_NAPI_TO_BUILTIN(Vector4Binding)
		BIND_NAPI_TO_BUILTIN(Vector4iBinding)
		BIND_NAPI_TO_BUILTIN(PlaneBinding)
		BIND_NAPI_TO_BUILTIN(QuaternionBinding)
		BIND_NAPI_TO_BUILTIN(AABBBinding)
		BIND_NAPI_TO_BUILTIN(BasisBinding)
		BIND_NAPI_TO_BUILTIN(Transform3DBinding)
		BIND_NAPI_TO_BUILTIN(ProjectionBinding)
		BIND_NAPI_TO_BUILTIN(ColorBinding)
		BIND_NAPI_TO_BUILTIN(NodePathBinding)
		BIND_NAPI_TO_BUILTIN(RIDBinding)
		BIND_NAPI_TO_BUILTIN(CallableBinding)
		BIND_NAPI_TO_BUILTIN(SignalBinding)
		BIND_NAPI_TO_BUILTIN(DictionaryBinding)
		BIND_NAPI_TO_BUILTIN(ArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedByteArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedInt32ArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedInt64ArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedFloat32ArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedFloat64ArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedStringArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedVector2ArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedVector3ArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedVector4ArrayBinding)
		BIND_NAPI_TO_BUILTIN(PackedColorArrayBinding)

		godot::Object *obj_inst = unwrap_godot_object(obj);
		if (obj_inst) {
			return godot::Variant(obj_inst);
		}

		if (is_js_map(obj)) {
			return js_map_to_godot_dictionary(obj);
		}

		return object_to_dictionary(obj);
	} else {
		return godot::Variant();
	}
}

} //namespace gode
