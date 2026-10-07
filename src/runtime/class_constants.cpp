#include "runtime/class_constants.h"
#include "runtime/value_convert.h"

namespace gode {

void install_class_constants(Napi::Env env, Napi::Object target,
		const ClassConstant *constants, std::size_t constant_count,
		const ClassEnum *enums, std::size_t enum_count) {
	for (std::size_t i = 0; i < constant_count; ++i) {
		target.Set(constants[i].name, godot_int_to_napi(env, constants[i].value));
	}
	for (std::size_t i = 0; i < enum_count; ++i) {
		Napi::Object values = Napi::Object::New(env);
		for (std::size_t j = 0; j < enums[i].count; ++j) {
			const ClassConstant &entry = enums[i].values[j];
			Napi::Value value = godot_int_to_napi(env, entry.value);
			values.Set(entry.name, value);
			values.Set(Napi::Number::New(env, static_cast<double>(entry.value)), Napi::String::New(env, entry.name));
			target.Set(entry.name, value);
		}
		target.Set(enums[i].name, values);
	}
}

} // namespace gode
