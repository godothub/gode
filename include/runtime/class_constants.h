#ifndef GODE_CLASS_CONSTANTS_H
#define GODE_CLASS_CONSTANTS_H

#include <napi.h>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace gode {

struct ClassConstant {
	const char *name;
	int64_t value;
};

struct ClassEnum {
	const char *name;
	const ClassConstant *values;
	std::size_t count;
};

void install_class_constants(Napi::Env env, Napi::Object target,
		const ClassConstant *constants, std::size_t constant_count,
		const ClassEnum *enums, std::size_t enum_count);

} // namespace gode

#endif
