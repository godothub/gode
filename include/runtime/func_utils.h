#ifndef GODE_FUNC_UTILS_H
#define GODE_FUNC_UTILS_H

#include "godot_cpp/core/binder_common.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "runtime/value_convert.h"
#include <napi.h>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <initializer_list>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace gode {

inline std::vector<Napi::Value> to_args_array(const Napi::CallbackInfo &info) {
	std::size_t argc = info.Length();
	std::vector<Napi::Value> args;
	args.reserve(argc);
	for (std::size_t i = 0; i < argc; ++i) {
		args.push_back(info[i]);
	}
	return args;
}

inline std::string argument_word(std::size_t count) {
	return count == 1 ? "argument" : "arguments";
}

inline std::size_t required_arg_count(std::size_t target_count, const std::vector<Napi::Value> &default_args) {
	std::size_t required_count = target_count;
	while (required_count > 0) {
		const std::size_t index = required_count - 1;
		if (index < default_args.size() && !default_args[index].IsUndefined()) {
			required_count--;
			continue;
		}
		break;
	}
	return required_count;
}

inline void throw_arg_count_error(Napi::Env env, std::size_t actual_count, std::size_t min_count, std::size_t max_count) {
	std::string expected;
	if (min_count == max_count) {
		expected = "exactly " + std::to_string(max_count) + " " + argument_word(max_count);
	} else if (actual_count < min_count) {
		expected = "at least " + std::to_string(min_count) + " " + argument_word(min_count);
	} else {
		expected = "at most " + std::to_string(max_count) + " " + argument_word(max_count);
	}

	Napi::TypeError::New(
			env,
			"Godot API call expected " + expected + ", got " + std::to_string(actual_count))
			.ThrowAsJavaScriptException();
}

inline bool prepare_fixed_args(std::vector<Napi::Value> &args, std::size_t target_count, const std::vector<Napi::Value> &default_args, Napi::Env env) {
	const std::size_t min_count = required_arg_count(target_count, default_args);
	if (args.size() < min_count || args.size() > target_count) {
		throw_arg_count_error(env, args.size(), min_count, target_count);
		return false;
	}

	for (std::size_t i = args.size(); i < target_count; ++i) {
		if (i < default_args.size() && !default_args[i].IsUndefined()) {
			args.push_back(default_args[i]);
		} else {
			throw_arg_count_error(env, args.size(), min_count, target_count);
			return false;
		}
	}

	return true;
}

inline const char *godot_call_error_name(GDExtensionCallErrorType error) {
	switch (error) {
		case GDEXTENSION_CALL_OK:
			return "OK";
		case GDEXTENSION_CALL_ERROR_INVALID_METHOD:
			return "INVALID_METHOD";
		case GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT:
			return "INVALID_ARGUMENT";
		case GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS:
			return "TOO_MANY_ARGUMENTS";
		case GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS:
			return "TOO_FEW_ARGUMENTS";
		case GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL:
			return "INSTANCE_IS_NULL";
		case GDEXTENSION_CALL_ERROR_METHOD_NOT_CONST:
			return "METHOD_NOT_CONST";
		default:
			return "UNKNOWN";
	}
}

inline bool throw_if_godot_call_failed(Napi::Env env, const GDExtensionCallError &error, const char *context) {
	if (error.error == GDEXTENSION_CALL_OK) {
		return false;
	}

	std::string message = std::string(context) + " failed: " + godot_call_error_name(error.error) +
			" (argument=" + std::to_string(error.argument) +
			", expected=" + std::to_string(error.expected) + ")";
	Napi::TypeError::New(env, message).ThrowAsJavaScriptException();
	return true;
}

inline Napi::Value call_class_method_bind(
		godot::Object *instance,
		const char *godot_class_name,
		const char *method_name,
		uint32_t method_hash,
		bool has_return,
		const Napi::CallbackInfo &info,
		std::size_t target_count,
		const std::vector<Napi::Value> &default_args,
		std::initializer_list<std::size_t> out_arg_indices) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (!prepare_fixed_args(args, target_count, default_args, info.Env())) {
		return info.Env().Undefined();
	}

	std::vector<godot::Variant> variant_args;
	std::vector<const godot::Variant *> arg_ptrs;
	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	static_assert(sizeof(uint32_t) <= sizeof(GDExtensionInt), "method hash must fit the GDExtension method bind lookup");
	GDExtensionMethodBindPtr method_bind = ::godot::gdextension_interface::classdb_get_method_bind(
			godot::StringName(godot_class_name)._native_ptr(),
			godot::StringName(method_name)._native_ptr(),
			method_hash);
	if (!method_bind) {
		Napi::Error::New(info.Env(), std::string("Godot MethodBind not found: ") + godot_class_name + "." + method_name).ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	godot::Variant return_value;
	GDExtensionCallError error = { GDEXTENSION_CALL_OK, 0, 0 };
	::godot::gdextension_interface::object_method_bind_call(
			method_bind,
			instance ? instance->_owner : nullptr,
			reinterpret_cast<GDExtensionConstVariantPtr *>(arg_ptrs.data()),
			static_cast<GDExtensionInt>(arg_ptrs.size()),
			has_return ? &return_value : nullptr,
			&error);
	if (throw_if_godot_call_failed(info.Env(), error, "Godot fixed MethodBind call")) {
		return info.Env().Undefined();
	}

	for (std::size_t out_index : out_arg_indices) {
		if (out_index < args.size() && out_index < variant_args.size()) {
			sync_godot_variant_out_argument(info.Env(), args[out_index], variant_args[out_index]);
			if (info.Env().IsExceptionPending()) {
				return info.Env().Undefined();
			}
		}
	}

	return has_return ? godot_to_napi(info.Env(), return_value) : info.Env().Undefined();
}

template <typename T>
using ConvertedArg = std::remove_const_t<std::remove_reference_t<T>>;

template <typename T>
using ConvertedArgSlot = std::optional<ConvertedArg<T>>;

template <typename... P>
using ConvertedArgTuple = std::tuple<ConvertedArgSlot<P>...>;

template <std::size_t I, typename... P>
inline bool convert_args(Napi::Env env, const std::vector<Napi::Value> &args, ConvertedArgTuple<P...> &converted) {
	if constexpr (I == sizeof...(P)) {
		return true;
	} else {
		using ParamTuple = std::tuple<P...>;
		using Param = std::tuple_element_t<I, ParamTuple>;
		std::get<I>(converted).emplace(napi_to_godot<Param>(args[I]));
		if (env.IsExceptionPending()) {
			return false;
		}
		return convert_args<I + 1, P...>(env, args, converted);
	}
}

template <typename Param, typename Converted>
inline void sync_out_arg(Napi::Env env, const Napi::Value &js_arg, const Converted &converted_arg) {
	using ReferencedParam = std::remove_reference_t<Param>;
	if constexpr (std::is_lvalue_reference_v<Param> && !std::is_const_v<ReferencedParam>) {
		sync_godot_out_argument(env, js_arg, converted_arg);
	}
}

template <std::size_t I, typename... P>
inline bool sync_out_args(Napi::Env env, const std::vector<Napi::Value> &args, const ConvertedArgTuple<P...> &converted) {
	if constexpr (I == sizeof...(P)) {
		return !env.IsExceptionPending();
	} else {
		using ParamTuple = std::tuple<P...>;
		using Param = std::tuple_element_t<I, ParamTuple>;
		if (I < args.size() && std::get<I>(converted).has_value()) {
			sync_out_arg<Param>(env, args[I], *std::get<I>(converted));
			if (env.IsExceptionPending()) {
				return false;
			}
		}
		return sync_out_args<I + 1, P...>(env, args, converted);
	}
}

// Helper: Static Method Implementation
template <typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_impl(R (*Func)(P...), Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	ConvertedArgTuple<P...> converted_args;
	if (!convert_args<0, P...>(env, args, converted_args)) {
		return env.Undefined();
	}

	if constexpr (std::is_void_v<R>) {
		Func((*std::get<Is>(converted_args))...);
		if (!sync_out_args<0, P...>(env, args, converted_args)) {
			return env.Undefined();
		}
		return env.Undefined();
	} else {
		R result = Func((*std::get<Is>(converted_args))...);
		if (!sync_out_args<0, P...>(env, args, converted_args)) {
			return env.Undefined();
		}
		return godot_result_to_napi(env, result);
	}
}

// Helper: Instance Method Implementation (Non-Const)
template <typename T, typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_impl(R (T::*Func)(P...), T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	ConvertedArgTuple<P...> converted_args;
	if (!convert_args<0, P...>(env, args, converted_args)) {
		return env.Undefined();
	}

	if constexpr (std::is_void_v<R>) {
		(instance->*Func)((*std::get<Is>(converted_args))...);
		if (!sync_out_args<0, P...>(env, args, converted_args)) {
			return env.Undefined();
		}
		return env.Undefined();
	} else {
		R result = (instance->*Func)((*std::get<Is>(converted_args))...);
		if (!sync_out_args<0, P...>(env, args, converted_args)) {
			return env.Undefined();
		}
		return godot_result_to_napi(env, result);
	}
}

// Helper: Instance Method Implementation (Const)
template <typename T, typename R, typename... P, std::size_t... Is>
inline Napi::Value call_builtin_method_impl(R (T::*Func)(P...) const, T *instance, Napi::Env env, std::vector<Napi::Value> args, std::index_sequence<Is...>) {
	ConvertedArgTuple<P...> converted_args;
	if (!convert_args<0, P...>(env, args, converted_args)) {
		return env.Undefined();
	}

	if constexpr (std::is_void_v<R>) {
		(instance->*Func)((*std::get<Is>(converted_args))...);
		if (!sync_out_args<0, P...>(env, args, converted_args)) {
			return env.Undefined();
		}
		return env.Undefined();
	} else {
		R result = (instance->*Func)((*std::get<Is>(converted_args))...);
		if (!sync_out_args<0, P...>(env, args, converted_args)) {
			return env.Undefined();
		}
		return godot_result_to_napi(env, result);
	}
}

// 1. Static Method (Regular)
template <typename R, typename... P>
inline Napi::Value call_builtin_method(R (*Func)(P...), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (!prepare_fixed_args(args, sizeof...(P), {}, info.Env())) {
		return info.Env().Undefined();
	}
	return call_builtin_method_impl(Func, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

template <typename R, typename... P>
inline Napi::Value call_builtin_method(R (*Func)(P...), const Napi::CallbackInfo &info, const std::vector<Napi::Value> &default_args) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (!prepare_fixed_args(args, sizeof...(P), default_args, info.Env())) {
		return info.Env().Undefined();
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
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
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
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	R result = Func((const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size());
	return godot_result_to_napi(info.Env(), result);
}

// Static class MethodBind vararg with GDExtensionCallError reporting.
inline Napi::Value call_builtin_method(void (*Func)(const godot::Variant **, GDExtensionInt, GDExtensionCallError *), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> arg_ptrs;

	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	GDExtensionCallError error = { GDEXTENSION_CALL_OK, 0, 0 };
	Func((const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size(), &error);
	if (throw_if_godot_call_failed(info.Env(), error, "Godot static vararg MethodBind call")) {
		return info.Env().Undefined();
	}
	return info.Env().Undefined();
}

template <typename R>
inline Napi::Value call_builtin_method(R (*Func)(const godot::Variant **, GDExtensionInt, GDExtensionCallError *), const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> arg_ptrs;

	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	GDExtensionCallError error = { GDEXTENSION_CALL_OK, 0, 0 };
	R result = Func((const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size(), &error);
	if (throw_if_godot_call_failed(info.Env(), error, "Godot static vararg MethodBind call")) {
		return info.Env().Undefined();
	}
	return godot_result_to_napi(info.Env(), result);
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
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
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
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	R result = Func(instance, (const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size());
	return godot_result_to_napi(info.Env(), result);
}

// Instance class MethodBind vararg with GDExtensionCallError reporting.
template <typename T>
inline Napi::Value call_builtin_method(void (*Func)(T *, const godot::Variant **, GDExtensionInt, GDExtensionCallError *), T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> arg_ptrs;

	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	GDExtensionCallError error = { GDEXTENSION_CALL_OK, 0, 0 };
	Func(instance, (const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size(), &error);
	if (throw_if_godot_call_failed(info.Env(), error, "Godot vararg MethodBind call")) {
		return info.Env().Undefined();
	}
	return info.Env().Undefined();
}

template <typename T, typename R>
inline Napi::Value call_builtin_method(R (*Func)(T *, const godot::Variant **, GDExtensionInt, GDExtensionCallError *), T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> arg_ptrs;

	variant_args.reserve(args.size());
	arg_ptrs.reserve(args.size());

	for (const auto &arg : args) {
		variant_args.push_back(napi_to_godot(arg));
		if (info.Env().IsExceptionPending()) {
			return info.Env().Undefined();
		}
	}
	for (size_t i = 0; i < variant_args.size(); i++) {
		arg_ptrs.push_back(&variant_args[i]);
	}

	GDExtensionCallError error = { GDEXTENSION_CALL_OK, 0, 0 };
	R result = Func(instance, (const godot::Variant **)arg_ptrs.data(), (GDExtensionInt)arg_ptrs.size(), &error);
	if (throw_if_godot_call_failed(info.Env(), error, "Godot vararg MethodBind call")) {
		return info.Env().Undefined();
	}
	return godot_result_to_napi(info.Env(), result);
}

// 6. Instance Method (Regular Non-Const)
template <typename T, typename R, typename... P>
inline Napi::Value call_builtin_method(R (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (!prepare_fixed_args(args, sizeof...(P), {}, info.Env())) {
		return info.Env().Undefined();
	}
	return call_builtin_method_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

template <typename T, typename R, typename... P>
inline Napi::Value call_builtin_method(R (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info, const std::vector<Napi::Value> &default_args) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (!prepare_fixed_args(args, sizeof...(P), default_args, info.Env())) {
		return info.Env().Undefined();
	}
	return call_builtin_method_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

// 7. Instance Method (Regular Const)
template <typename T, typename R, typename... P>
inline Napi::Value call_builtin_method(R (T::*Func)(P...) const, T *instance, const Napi::CallbackInfo &info) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (!prepare_fixed_args(args, sizeof...(P), {}, info.Env())) {
		return info.Env().Undefined();
	}
	return call_builtin_method_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

template <typename T, typename R, typename... P>
inline Napi::Value call_builtin_method(R (T::*Func)(P...) const, T *instance, const Napi::CallbackInfo &info, const std::vector<Napi::Value> &default_args) {
	std::vector<Napi::Value> args = to_args_array(info);
	if (!prepare_fixed_args(args, sizeof...(P), default_args, info.Env())) {
		return info.Env().Undefined();
	}
	return call_builtin_method_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

// 8. Setter (Regular Non-Const)
template <typename T, typename... P>
inline Napi::Value call_builtin_method(void (T::*Func)(P...), T *instance, const Napi::CallbackInfo &info, const Napi::Value &val) {
	std::vector<Napi::Value> args(sizeof...(P), val);
	return call_builtin_method_impl(Func, instance, info.Env(), args, std::make_index_sequence<sizeof...(P)>());
}

} //namespace gode
#endif // GODE_FUNC_UTILS_H
