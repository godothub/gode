#ifndef GODE_UTILITY_FUNCTIONS_H
#define GODE_UTILITY_FUNCTIONS_H

#include <napi.h>
#include <godot_cpp/variant/utility_functions.hpp>

namespace gode {
class GD : public Napi::ObjectWrap<GD> {
public:
	static Napi::Object init(Napi::Env env, Napi::Object exports);
	GD(const Napi::CallbackInfo &info);

private:
	Napi::Value print(const Napi::CallbackInfo &info);
	Napi::Value print_rich(const Napi::CallbackInfo &info);
	Napi::Value printerr(const Napi::CallbackInfo &info);
	Napi::Value printt(const Napi::CallbackInfo &info);
	Napi::Value prints(const Napi::CallbackInfo &info);
	Napi::Value printraw(const Napi::CallbackInfo &info);
	Napi::Value print_verbose(const Napi::CallbackInfo &info);
	Napi::Value push_error(const Napi::CallbackInfo &info);
	Napi::Value push_warning(const Napi::CallbackInfo &info);
	Napi::Value max(const Napi::CallbackInfo &info);
	Napi::Value min(const Napi::CallbackInfo &info);
	Napi::Value str(const Napi::CallbackInfo &info);
};
} //namespace gode

#endif // GODE_UTILITY_FUNCTIONS_H
