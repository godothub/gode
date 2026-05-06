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

inline std::vector<Napi::Value> to_args_array(const Napi::CallbackInfo &info) {
	std::size_t argc = info.Length();
	std::vector<Napi::Value> args;
	for (std::size_t i = 0; i < argc; ++i) {
		args.push_back(info[i]);
	}
	return args;
}

// Helper: Static Method Implementation
template <typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_impl(R (*Func)(P...), Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	if constexpr (std::is_void_v<R>) {
		Func(napi_to_godot<P>(args[Is])...);
		return env.Undefined();
	} else {
		return godot_to_napi(env, Func(napi_to_godot<P>(args[Is])...));
	}
}

// Helper: Instance Method Implementation (Non-Const)
template <typename T, typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_impl(R (T::*Func)(P...), T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	if constexpr (std::is_void_v<R>) {
		(instance->*Func)(napi_to_godot<P>(args[Is])...);
		return env.Undefined();
	} else {
		return godot_to_napi(env, (instance->*Func)(napi_to_godot<P>(args[Is])...));
	}
}

// Helper: Instance Method Implementation (Const)
template <typename T, typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_impl(R (T::*Func)(P...) const, T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	if constexpr (std::is_void_v<R>) {
		(instance->*Func)(napi_to_godot<P>(args[Is])...);
		return env.Undefined();
	} else {
		return godot_to_napi(env, (instance->*Func)(napi_to_godot<P>(args[Is])...));
	}
}

// 1. Static Method (Regular)
template <typename R, typename... P>
inline Napi::Value call_builtin_method(R (*Func)(P...), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (args.size() < sizeof...(P)) {
		args.resize(sizeof...(P), info.Env().Undefined());
	}
	return call_builtin_method_impl(Func, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

// 2. Static Method (Vararg)
// Matches specific signature: void (*)(const godot::Variant **, GDExtensionInt)
inline Napi::Value call_builtin_method(void (*Func)(const godot::Variant **, GDExtensionInt), const Napi::CallbackInfo &info) {
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

// 3. Static Method (Vararg with Return)
// Matches specific signature: R (*)(const godot::Variant **, GDExtensionInt)
template <typename R>
inline Napi::Value call_builtin_method(R (*Func)(const godot::Variant **, GDExtensionInt), const Napi::CallbackInfo &info) {
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


// 4. Instance Method (Vararg Helper - Static function taking T*)
// Matches specific signature: void (*)(T *, const godot::Variant **, GDExtensionInt)
template <typename T>
inline Napi::Value call_builtin_method(void (*Func)(T *, const godot::Variant **, GDExtensionInt), T *instance, const Napi::CallbackInfo &info) {
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

// 5. Instance Method (Vararg Helper with Return - Static function taking T*)
// Matches specific signature: R (*)(T *, const godot::Variant **, GDExtensionInt)
template <typename T, typename R>
inline Napi::Value call_builtin_method(R (*Func)(T *, const godot::Variant **, GDExtensionInt), T *instance, const Napi::CallbackInfo &info) {
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


// 6. Instance Method (Regular Non-Const)
template <typename T, typename R, typename... P>
inline Napi::Value call_builtin_method(R (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (args.size() < sizeof...(P)) {
		args.resize(sizeof...(P), info.Env().Undefined());
	}
	return call_builtin_method_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

// 7. Instance Method (Regular Const)
template <typename T, typename R, typename... P>
inline Napi::Value call_builtin_method(R (T::*Func)(P...) const, T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (args.size() < sizeof...(P)) {
		args.resize(sizeof...(P), info.Env().Undefined());
	}
	return call_builtin_method_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

// 8. Setter (Regular Non-Const)
template <typename T, typename... P>
inline Napi::Value call_builtin_method(void (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info, const Napi::Value &val) {
	(instance->*Func)(napi_to_godot<P>(val)...);
	return info.Env().Undefined();
}

} //namespace gode
#endif // GODE_FUNC_UTILS_H
