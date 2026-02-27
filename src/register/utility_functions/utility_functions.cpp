#include "register/utility_functions/utility_functions.h"
#include "napi.h"
#include "utils/func_utils.h"
#include <godot_cpp/variant/utility_functions.hpp>

Napi::Object gode::GD::init(Napi::Env env, Napi::Object exports) {
	// Define the class and its methods
	Napi::Function func = DefineClass(env, "GD", {
		InstanceMethod("print", &GD::print),
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
	return gode::call_builtin_static_vararg_method_no_ret(&godot::UtilityFunctions::print, info);
}