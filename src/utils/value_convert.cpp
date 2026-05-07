#include "utils/value_convert.h"

#include <climits>
#include <unordered_map>
#include <vector>

#include "godot_cpp/variant/utility_functions.hpp"
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
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
#include "support/javascript/javascript_callable.h"
#include "utils/node_runtime.h"

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
static std::vector<ClassInfo> class_list;
static std::unordered_map<uint64_t, Napi::ObjectReference> object_cache;

constexpr const char *GODOT_OBJECT_ID_SYMBOL = "__gode.godot_object_id__";
constexpr const char *GODOT_OBJECT_PTR_SYMBOL = "__gode.godot_object_ptr__";

static godot::Dictionary object_to_dictionary(const Napi::Object &obj) {
	godot::Dictionary dict;
	Napi::Array property_names = obj.GetPropertyNames();
	uint32_t property_count = property_names.Length();

	for (uint32_t i = 0; i < property_count; i++) {
		Napi::Value key = property_names.Get(i);
		Napi::Value val = obj.Get(key);
		dict[napi_to_godot(key)] = napi_to_godot(val);
	}

	return dict;
}

static godot::Array js_array_to_godot_array(const Napi::Array &js_array) {
	godot::Array array;
	const uint32_t length = js_array.Length();
	for (uint32_t i = 0; i < length; i++) {
		array.append(napi_to_godot(js_array.Get(i)));
	}
	return array;
}

void register_class(const std::string &name, const std::string &godot_class_name, Napi::FunctionReference *ref, UnwrapFunc unwrapper, WrapFunc wrapper) {
	ClassInfo info = { godot_class_name, ref, unwrapper, wrapper };
	class_registry[name] = info;
	class_list.push_back(info);
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

	for (const auto &info : class_list) {
		if (!info.constructor || info.constructor->IsEmpty()) {
			continue;
		}
		if (obj.InstanceOf(info.constructor->Value())) {
			return info.unwrapper(obj);
		}
	}
	return nullptr;
}

#define BIND_OWNER_TO_BUILTIN(BindingClass)                   \
	if (obj.InstanceOf(BindingClass::constructor.Value())) {  \
		BindingClass *binding = BindingClass::Unwrap(obj);    \
		binding->bind_owner_property(owner, property);        \
		return;                                               \
	}

#define BIND_PARENT_TO_BUILTIN(BindingClass)                  \
	if (obj.InstanceOf(BindingClass::constructor.Value())) {  \
		BindingClass *binding = BindingClass::Unwrap(obj);    \
		binding->bind_parent_property(parent, property);      \
		return;                                               \
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
	for (ClassInfo &info : class_list) {
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

Napi::Value godot_to_napi(Napi::Env env, godot::Variant variant) {
	switch (variant.get_type()) {
		case godot::Variant::Type::NIL:
			return env.Null();
		case godot::Variant::Type::INT:
			return Napi::Number::New(env, variant.operator int64_t());
		case godot::Variant::Type::FLOAT:
			return Napi::Number::New(env, variant.operator double());
		case godot::Variant::Type::BOOL:
			return Napi::Boolean::New(env, variant.operator bool());
		case godot::Variant::Type::STRING:
		case godot::Variant::Type::STRING_NAME:
			return Napi::String::New(env, variant.operator String().utf8().get_data());
		case godot::Variant::Type::ARRAY: {
			const godot::Array godot_array = variant.operator godot::Array();
			const uint32_t array_length = static_cast<uint32_t>(godot_array.size());
			Napi::Array js_array = Napi::Array::New(env, array_length);
			for (uint32_t i = 0; i < array_length; i++) {
				js_array.Set(i, godot_to_napi(env, godot_array[i]));
			}
			return js_array;
		}
		case godot::Variant::Type::DICTIONARY: {
			const godot::Dictionary godot_dictionary = variant.operator godot::Dictionary();
			Napi::Object js_object = Napi::Object::New(env);

			const godot::Array keys = godot_dictionary.keys();
			const int64_t key_count = keys.size();
			for (int64_t i = 0; i < key_count; i++) {
				const godot::Variant key = keys[i];
				const godot::String key_string = key.operator godot::String();
				js_object.Set(
					Napi::String::New(env, key_string.utf8().get_data()),
					godot_to_napi(env, godot_dictionary.get(key, Variant())));
			}
			return js_object;
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
				gode::JavascriptCallable *js_callable = dynamic_cast<gode::JavascriptCallable *>(callable.get_custom());
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
			if (!obj) {
				return env.Null();
			}

			uint64_t id = obj->get_instance_id();
			auto it = object_cache.find(id);
			if (it != object_cache.end()) {
				if (godot::ObjectDB::get_instance(id) && !it->second.IsEmpty()) {
					Napi::Object cached = it->second.Value();
					if (!cached.IsEmpty()) {
						return cached;
					}
				}
				object_cache.erase(it);
			}

			ClassInfo *class_info = find_class_info_for_object(obj);
			if (class_info) {
				ClassInfo &info = *class_info;
				if (info.constructor && !info.constructor->IsEmpty()) {
					Napi::Object js_obj = info.constructor->Value().New({});
					if (info.wrapper) {
						info.wrapper(js_obj, obj);
					}

					register_godot_instance(obj, js_obj);

					return js_obj;
				}
			}
			return env.Null();
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
		return value.ToNumber().DoubleValue();
	} else if (value.IsBoolean()) {
		return value.ToBoolean().Value();
	} else if (value.IsString()) {
		return String::utf8(value.ToString().Utf8Value().c_str());
	} else if (value.IsFunction()) {
		JavascriptCallable *callable = memnew(JavascriptCallable(value.As<Napi::Function>()));
		return godot::Callable(callable);
	} else if (value.IsArray()) {
		return js_array_to_godot_array(value.As<Napi::Array>());
	} else if (value.IsObject()) {
		Napi::Object obj = value.As<Napi::Object>();

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
		
		return object_to_dictionary(obj);
	} else {
		return godot::Variant();
	}
}

} //namespace gode
