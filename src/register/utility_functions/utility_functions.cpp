#include "register/utility_functions/utility_functions.h"
#include "register/utility_functions/utility_functions_vararg_method.h"
#include "napi.h"
#include "utils/func_utils.h"
#include <godot_cpp/variant/utility_functions.hpp>

Napi::Object gode::GD::init(Napi::Env env, Napi::Object exports) {
	// Define the class and its methods
	Napi::Function func = DefineClass(env, "GD", {
		InstanceMethod("print", &GD::print),
		InstanceMethod("print_rich", &GD::print_rich),
		InstanceMethod("printerr", &GD::printerr),
		InstanceMethod("printt", &GD::printt),
		InstanceMethod("prints", &GD::prints),
		InstanceMethod("printraw", &GD::printraw),
		InstanceMethod("print_verbose", &GD::print_verbose),
		InstanceMethod("push_error", &GD::push_error),
		InstanceMethod("push_warning", &GD::push_warning),
		InstanceMethod("max", &GD::max),
		InstanceMethod("min", &GD::min),
		InstanceMethod("str", &GD::str),
	});

	// Create an instance of the class
	Napi::Object gd_instance = func.New({});

	// Register it as a global object named "GD"
	Napi::Object global = env.Global();
	global.Set("GD", gd_instance);
	
	// Also export it just in case
	exports.Set("GD", gd_instance);
	
	return exports;
}

gode::GD::GD(const Napi::CallbackInfo &info) :
		Napi::ObjectWrap<GD>(info) {
	this->SuppressDestruct();
}

Napi::Value gode::GD::print(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::print_internal, info);
}

Napi::Value gode::GD::print_rich(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::print_rich_internal, info);
}

Napi::Value gode::GD::printerr(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::printerr_internal, info);
}

Napi::Value gode::GD::printt(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::printt_internal, info);
}

Napi::Value gode::GD::prints(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::prints_internal, info);
}

Napi::Value gode::GD::printraw(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::printraw_internal, info);
}

Napi::Value gode::GD::print_verbose(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::print_verbose_internal, info);
}

Napi::Value gode::GD::push_error(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::push_error_internal, info);
}

Napi::Value gode::GD::push_warning(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_no_ret(&gode::utility::push_warning_internal, info);
}

Napi::Value gode::GD::max(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_ret(&gode::utility::max_internal, info);
}

Napi::Value gode::GD::min(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_ret(&gode::utility::min_internal, info);
}

Napi::Value gode::GD::str(const Napi::CallbackInfo &info) {
	return gode::call_builtin_static_vararg_method_ret(&gode::utility::str_internal, info);
}