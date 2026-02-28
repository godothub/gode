#include "utils/value_convert.h"

#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

namespace gode {

Napi::Value godot_to_napi(Napi::Env env, const godot::Variant &variant) {
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
			return Napi::String::New(env, variant.operator String().utf8().get_data());
		case godot::Variant::Type::STRING_NAME:
			return Napi::String::New(env, variant.operator String().utf8().get_data());
		default:
			return env.Undefined();
	}
}

godot::Variant napi_to_godot(const Napi::Value &value) {
	if (value.IsNumber()) {
		return value.ToNumber().DoubleValue();
	} else if (value.IsBoolean()) {
		return value.ToBoolean().Value();
	} else if (value.IsString()) {
		return String::utf8(value.ToString().Utf8Value().c_str());
	} else {
		return godot::Variant();
	}
}

} //namespace gode
