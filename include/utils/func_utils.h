#ifndef GODE_FUNC_UTILS_H
#define GODE_FUNC_UTILS_H

#include "godot_cpp/core/binder_common.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "utils/value_convert.h"
#include <napi.h>
#include <godot_cpp/variant/utility_functions.hpp>
#include <type_traits>
#include <vector>

namespace gode {

template <typename T>
inline T convert_arg(const godot::Variant &v) {
	if constexpr (std::is_enum_v<T>) {
		return reinterpret_cast<T>(static_cast<int64_t>(v));
	} else if constexpr (std::is_pointer_v<T> && std::is_base_of_v<godot::Object, std::remove_pointer_t<T>>) {
		return reinterpret_cast<T>(static_cast<godot::Object *>(v));
	} else if constexpr (std::is_same_v<T, char> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t> || std::is_same_v<T, wchar_t>) {
		godot::String s = v;
		if (s.length() == 0) {
			return static_cast<T>(0);
		}
		return static_cast<T>(s[0]);
	} else {
		return static_cast<T>(v);
	}
}

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
	Func(napi_to_godot<P>(args[Is])...);
	return env.Undefined();
}
template <typename... P>
inline Napi::Value call_builtin_static_method_no_ret(void (*Func)(P...), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_static_method_no_ret_impl(Func, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

template <typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_static_method_ret_impl(R (*Func)(P...), Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	return godot_to_napi(env, Func(napi_to_godot<P>(args[Is])...));
}
template <typename R, typename... P>
inline Napi::Value call_builtin_static_method_ret(R (*Func)(P...), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_static_method_ret_impl(Func, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

inline Napi::Value call_builtin_static_vararg_method_no_ret(void (*Func)(const godot::Variant **, GDExtensionInt), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> arg_ptrs;

	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	Func((const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size());
	return info.Env().Undefined();
}
template <typename R>
inline Napi::Value call_builtin_static_vararg_method_ret(R (*Func)(const godot::Variant **, GDExtensionInt), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> arg_ptrs;

	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	return godot_to_napi(info.Env(), Func((const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size()));
}

template <typename T, typename R>
inline Napi::Value call_builtin_vararg_method_ret(R (*Func)(T *, const godot::Variant **, GDExtensionInt), T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> arg_ptrs;

	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	return godot_to_napi(info.Env(), Func(instance, (const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size()));
}

template <typename T>
inline Napi::Value call_builtin_vararg_method_no_ret(void (*Func)(T *, const godot::Variant **, GDExtensionInt), T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> arg_ptrs;

	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	Func(instance, (const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size());
	return info.Env().Undefined();
}
//
// template <typename T, typename... P, std::size_t... Is>
// inline Napi::Value call_builtin_vararg_method_no_ret_impl(void (T::*Func)(const godot::Variant &, P...), T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
// 	if (args.empty()) {
// 		return env.Undefined();
// 	}
// 	godot::Variant first = napi_to_godot(args[0]);
// 	(instance->*Func)(first, convert_arg<P>(napi_to_godot(args[Is + 1]))...);
// 	return env.Undefined();
// }
// template <typename T, typename A1, typename... P>
// inline Napi::Value call_builtin_vararg_method_no_ret(void (T::*Func)(A1, P...), T *instance, const Napi::CallbackInfo &info) {
// 	std::vector<Napi::Value> args = to_args_array(info);
// 	return call_builtin_vararg_method_no_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
// }
//
// template <typename T, typename... P>
// inline Napi::Value call_builtin_vararg_method_no_ret(void (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info) {
// 	std::vector<Napi::Value> args = to_args_array(info);
// 	return call_builtin_vararg_method_no_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
// }
//
// template <typename T, typename R, typename A1, typename... P, std::size_t... Is>
// inline Napi::Value call_builtin_vararg_method_ret_impl(R (T::*Func)(A1, P...), T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
// 	if (args.empty()) {
// 		return env.Undefined();
// 	}
// 	godot::Variant first = napi_to_godot(args[0]);
// 	return godot_to_napi(env, (instance->*Func)(first, convert_arg<P>(napi_to_godot(args[Is + 1]))...));
// }
// template <typename T, typename R, typename A1, typename... P>
// inline Napi::Value call_builtin_vararg_method_ret(R (T::*Func)(A1, P...), T *instance, const Napi::CallbackInfo &info) {
// 	std::vector<Napi::Value> args = to_args_array(info);
// 	return call_builtin_vararg_method_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
// }
//
// template <typename T, typename R, typename... P, std::size_t... Is>
// inline Napi::Value call_builtin_vararg_method_ret_impl(R (T::*Func)(P...), T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
// 	return godot_to_napi(env, (instance->*Func)(napi_to_godot<P>(args[Is])...));
// }
// template <typename T, typename R, typename... P>
// inline Napi::Value call_builtin_vararg_method_ret(R (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info) {
// 	std::vector<Napi::Value> args = to_args_array(info);
// 	return call_builtin_vararg_method_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
// }
//
// template <typename T, typename A1, typename... P, std::size_t... Is>
// inline Napi::Value call_builtin_const_vararg_method_no_ret_impl(void (T::*Func)(A1, P...) const, T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
// 	if (args.empty()) {
// 		return env.Undefined();
// 	}
// 	godot::Variant first = napi_to_godot(args[0]);
// 	(instance->*Func)(first, convert_arg<P>(napi_to_godot(args[Is + 1]))...);
// 	return env.Undefined();
// }
// template <typename T, typename A1, typename... P>
// inline Napi::Value call_builtin_const_vararg_method_no_ret(void (T::*Func)(A1, P...) const, T *instance, const Napi::CallbackInfo &info) {
// 	std::vector<Napi::Value> args = to_args_array(info);
// 	return call_builtin_const_vararg_method_no_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
// }
//
// template <typename T, typename... P, std::size_t... Is>
// inline Napi::Value call_builtin_const_vararg_method_no_ret_impl(void (T::*Func)(P...) const, T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
// 	(instance->*Func)(napi_to_godot<P>(args[Is])...);
// 	return env.Undefined();
// }
// template <typename T, typename... P>
// inline Napi::Value call_builtin_const_vararg_method_no_ret(void (T::*Func)(P...) const, T *instance, const Napi::CallbackInfo &info) {
// 	std::vector<Napi::Value> args = to_args_array(info);
// 	return call_builtin_const_vararg_method_no_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
// }
//
// template <typename T, typename R, typename... P, std::size_t... Is>
// inline Napi::Value call_builtin_const_vararg_method_ret_impl(R (T::*Func)(P...) const, T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
// 	return godot_to_napi(env, (instance->*Func)(napi_to_godot<P>(args[Is])...));
// }
// template <typename T, typename R, typename... P>
// inline Napi::Value call_builtin_const_vararg_method_ret(R (T::*Func)(P...) const, T *instance, const Napi::CallbackInfo &info) {
// 	std::vector<Napi::Value> args = to_args_array(info);
// 	return call_builtin_const_vararg_method_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
// }
//
// template <typename T, typename R, typename A1, typename... P, std::size_t... Is>
// inline Napi::Value call_builtin_const_vararg_method_ret_impl(R (T::*Func)(A1, P...) const, T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
// 	if (args.empty()) {
// 		return env.Undefined();
// 	}
// 	godot::Variant first = napi_to_godot(args[0]);
// 	return godot_to_napi(env, (instance->*Func)(first, convert_arg<P>(napi_to_godot(args[Is + 1]))...));
// }
// template <typename T, typename R, typename A1, typename... P>
// inline Napi::Value call_builtin_const_vararg_method_ret(R (T::*Func)(A1, P...) const, T *instance, const Napi::CallbackInfo &info) {
// 	std::vector<Napi::Value> args = to_args_array(info);
// 	return call_builtin_const_vararg_method_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
// }

template <typename T, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_const_method_no_ret_impl(void (T::*Func)(P...) const, T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	(instance->*Func)(napi_to_godot<P>(args[Is])...);
	return env.Undefined();
}
template <typename T, typename... P>
inline Napi::Value call_builtin_const_method_no_ret(void (T::*Func)(P...) const, T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_const_method_no_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}
template <typename T, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_no_ret_impl(void (T::*Func)(P...), T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	(instance->*Func)(napi_to_godot<P>(args[Is])...);
	return env.Undefined();
}
template <typename T, typename... P>
inline Napi::Value call_builtin_method_no_ret(void (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_method_no_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

template <typename T, typename... P>
inline Napi::Value call_builtin_method_no_ret(void (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info, const Napi::Value &val) {
	(instance->*Func)(napi_to_godot<P>(val)...);
	return info.Env().Undefined();
}

template <typename T, typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_ret_impl(R (T::*Func)(P...), T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	return godot_to_napi(env, (instance->*Func)(napi_to_godot<P>(args[Is])...));
}
template <typename T, typename R, typename... P>
inline Napi::Value call_builtin_method_ret(R (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_method_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

template <typename T, typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_const_method_ret_impl(R (T::*Func)(P...) const, T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	return godot_to_napi(env, (instance->*Func)(napi_to_godot<P>(args[Is])...));
}
template <typename T, typename R, typename... P>
inline Napi::Value call_builtin_const_method_ret(R (T::*Func)(P...) const, T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	return call_builtin_const_method_ret_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

// Specializations for methods with 0 arguments to fix deduction issues
template <typename T>
inline Napi::Value call_builtin_const_method_no_ret(void (T::*Func)() const, T *instance, const Napi::CallbackInfo &info) {
	(instance->*Func)();
	return info.Env().Undefined();
}
template <typename T>
inline Napi::Value call_builtin_method_no_ret(void (T::*Func)(), T *instance, const Napi::CallbackInfo &info) {
	(instance->*Func)();
	return info.Env().Undefined();
}

template <typename T, typename R>
inline Napi::Value call_builtin_const_method_ret(R (T::*Func)() const, T *instance, const Napi::CallbackInfo &info) {
	return godot_to_napi(info.Env(), (instance->*Func)());
}
template <typename T, typename R>
inline Napi::Value call_builtin_method_ret(R (T::*Func)(), T *instance, const Napi::CallbackInfo &info) {
	return godot_to_napi(info.Env(), (instance->*Func)());
}
} //namespace gode
#endif // GODE_FUNC_UTILS_H
