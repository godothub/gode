#ifndef GODE_FUNC_UTILS_H
#define GODE_FUNC_UTILS_H

#include "godot_cpp/variant/variant.hpp"
#include "utils/value_convert.h"
#include <napi.h>
#include <vector>

namespace gode {

// 把 Napi::CallbackInfo 转换为 Napi::Array
inline std::vector<Napi::Value> to_args_array(const Napi::CallbackInfo &info) {
	std::size_t argc = info.Length();
	std::vector<Napi::Value> args;
	for (std::size_t i = 0; i < argc; ++i) {
		args.push_back(info[i]);
	}
	return args;
}

// 调用 C++ 静态方法
template <typename... P, std::size_t... Is>
inline Napi::Value call_builtin_static_method_no_ret_impl(void (*Func)(P...), Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	Func(napi_to_godot(args[Is])...);
	return env.Undefined();
}
template <typename... P>
inline Napi::Value call_builtin_static_method_no_ret(void (*Func)(P...), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_static_method_no_ret_impl(Func, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

template <typename... P, std::size_t... Is>
inline Napi::Value call_builtin_static_vararg_method_no_ret_impl(void (*Func)(const godot::Variant &, P...), Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	if (args.empty()) {
		return env.Undefined();
	}
	godot::Variant first = napi_to_godot(args[0]);
	Func(first, napi_to_godot(args[Is + 1])...);
	return env.Undefined();
}
template <typename... P>
inline Napi::Value call_builtin_static_vararg_method_no_ret(void (*Func)(const godot::Variant &, P...), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_static_vararg_method_no_ret_impl(Func, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

template <typename T, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_no_ret_impl(void (T::*Func)(P...), T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	(instance->*Func)(napi_to_godot(args[Is])...);
	return env.Undefined();
}
template <typename T, typename... P>
inline Napi::Value call_builtin_method_no_ret(void (T::*Func)(P...), const Napi::CallbackInfo &info) {
	T *instance = Napi::ObjectWrap<T>::Unwrap(info.This().As<Napi::Object>());
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_method_no_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}
} //namespace gode
#endif // GODE_FUNC_UTILS_H
