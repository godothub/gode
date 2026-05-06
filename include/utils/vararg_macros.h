#ifndef GODE_VARARG_MACROS_H
#define GODE_VARARG_MACROS_H

#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <vector>

// Macro to define the internal function for UtilityFunctions (static, lookup by hash)
#define DEFINE_UTILITY_INTERNAL_VOID(m_name, m_hash) \
inline void m_name##_internal(const godot::Variant **p_args, GDExtensionInt p_arg_count) { \
	static GDExtensionPtrUtilityFunction _gde_function = ::godot::gdextension_interface::variant_get_ptr_utility_function(godot::StringName(#m_name)._native_ptr(), m_hash); \
	CHECK_METHOD_BIND(_gde_function); \
	godot::Variant ret; \
	_gde_function(&ret, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count); \
}

#define DEFINE_UTILITY_INTERNAL_RET(m_name, m_hash, m_type) \
inline godot::Variant m_name##_internal(const godot::Variant **p_args, GDExtensionInt p_arg_count) { \
	static GDExtensionPtrUtilityFunction _gde_function = ::godot::gdextension_interface::variant_get_ptr_utility_function(godot::StringName(#m_name)._native_ptr(), m_hash); \
	CHECK_METHOD_BIND_RET(_gde_function, godot::Variant()); \
	m_type ret; \
	_gde_function(&ret, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count); \
	return godot::Variant(ret); \
}

// Macro to define the vector wrapper (converts vector to array and calls internal)
// This expects m_name##_internal to exist.
#define DEFINE_VECTOR_WRAPPER_VOID(m_name) \
inline void m_name(const godot::Variant &arg, const std::vector<godot::Variant> &args) { \
	std::vector<godot::Variant> variant_args; \
	std::vector<godot::Variant *> variant_args_ptr; \
	variant_args.reserve(1 + args.size()); \
	variant_args_ptr.reserve(1 + args.size()); \
	variant_args.push_back(arg); \
	for (const auto &v : args) { \
		variant_args.push_back(v); \
	} \
	for (size_t i = 0; i < variant_args.size(); i++) { \
		variant_args_ptr.push_back(&variant_args[i]); \
	} \
	m_name##_internal(const_cast<const godot::Variant **>(variant_args_ptr.data()), (GDExtensionInt)variant_args_ptr.size()); \
}

#define DEFINE_VECTOR_WRAPPER_RET(m_name) \
inline godot::Variant m_name(const godot::Variant &arg, const std::vector<godot::Variant> &args) { \
	std::vector<godot::Variant> variant_args; \
	std::vector<godot::Variant *> variant_args_ptr; \
	variant_args.reserve(1 + args.size()); \
	variant_args_ptr.reserve(1 + args.size()); \
	variant_args.push_back(arg); \
	for (const auto &v : args) { \
		variant_args.push_back(v); \
	} \
	for (size_t i = 0; i < variant_args.size(); i++) { \
		variant_args_ptr.push_back(&variant_args[i]); \
	} \
	return m_name##_internal(const_cast<const godot::Variant **>(variant_args_ptr.data()), (GDExtensionInt)variant_args_ptr.size()); \
}

// Combined macros
#define DEFINE_VARARG_FUNC_VOID(m_name, m_hash) \
    DEFINE_UTILITY_INTERNAL_VOID(m_name, m_hash) \

#define DEFINE_VARARG_FUNC_RET(m_name, m_hash, m_type) \
    DEFINE_UTILITY_INTERNAL_RET(m_name, m_hash, m_type) \

#define DEFINE_BUILTIN_VARARG_METHOD_VOID(m_class, m_name, m_hash, m_type_constant) \
inline void m_name##_internal(godot::m_class *p_instance, const godot::Variant **p_args, GDExtensionInt p_arg_count) { \
	static GDExtensionPtrBuiltInMethod _gde_method = ::godot::gdextension_interface::variant_get_ptr_builtin_method(m_type_constant, godot::StringName(#m_name)._native_ptr(), m_hash); \
	CHECK_METHOD_BIND(_gde_method); \
	_gde_method(p_instance->_native_ptr(), reinterpret_cast<GDExtensionConstTypePtr *>(p_args), nullptr, p_arg_count); \
}

#define DEFINE_BUILTIN_VARARG_METHOD_RET(m_class, m_name, m_hash, m_type_constant, m_ret_type) \
inline m_ret_type m_name##_internal(godot::m_class *p_instance, const godot::Variant **p_args, GDExtensionInt p_arg_count) { \
	static GDExtensionPtrBuiltInMethod _gde_method = ::godot::gdextension_interface::variant_get_ptr_builtin_method(m_type_constant, godot::StringName(#m_name)._native_ptr(), m_hash); \
	CHECK_METHOD_BIND_RET(_gde_method, m_ret_type()); \
	m_ret_type ret; \
	_gde_method(p_instance->_native_ptr(), reinterpret_cast<GDExtensionConstTypePtr *>(p_args), ret._native_ptr(), p_arg_count); \
	return ret; \
}

#define DEFINE_CLASS_VARARG_METHOD_VOID(m_cpp_type, m_godot_class, m_name, m_hash) \
inline void m_name##_internal(godot::m_cpp_type *p_instance, const godot::Variant **p_args, GDExtensionInt p_arg_count) { \
	static GDExtensionMethodBindPtr _gde_method_bind = ::godot::gdextension_interface::classdb_get_method_bind(godot::StringName(#m_godot_class)._native_ptr(), godot::StringName(#m_name)._native_ptr(), m_hash); \
	CHECK_METHOD_BIND(_gde_method_bind); \
	GDExtensionCallError error; \
	::godot::gdextension_interface::object_method_bind_call(_gde_method_bind, p_instance->_owner, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count, nullptr, &error); \
}

// Helper templates for return value casting
template<typename T>
inline T variant_cast(const godot::Variant& v) {
    return static_cast<T>(v);
}

// Specialization for Error (enum)
template<>
inline godot::Error variant_cast<godot::Error>(const godot::Variant& v) {
    return (godot::Error)(int64_t)v;
}

#define DEFINE_CLASS_VARARG_METHOD_RET(m_cpp_type, m_godot_class, m_name, m_hash, m_ret_type) \
inline m_ret_type m_name##_internal(godot::m_cpp_type *p_instance, const godot::Variant **p_args, GDExtensionInt p_arg_count) { \
	static GDExtensionMethodBindPtr _gde_method_bind = ::godot::gdextension_interface::classdb_get_method_bind(godot::StringName(#m_godot_class)._native_ptr(), godot::StringName(#m_name)._native_ptr(), m_hash); \
	CHECK_METHOD_BIND_RET(_gde_method_bind, m_ret_type()); \
	GDExtensionCallError error; \
	godot::Variant ret_variant; \
	::godot::gdextension_interface::object_method_bind_call(_gde_method_bind, p_instance->_owner, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count, &ret_variant, &error); \
	return variant_cast<m_ret_type>(ret_variant); \
}

#define DEFINE_CLASS_STATIC_VARARG_METHOD_VOID(m_cpp_type, m_godot_class, m_name, m_hash) \
inline void m_name##_internal(const godot::Variant **p_args, GDExtensionInt p_arg_count) { \
	static GDExtensionMethodBindPtr _gde_method_bind = ::godot::gdextension_interface::classdb_get_method_bind(godot::StringName(#m_godot_class)._native_ptr(), godot::StringName(#m_name)._native_ptr(), m_hash); \
	CHECK_METHOD_BIND(_gde_method_bind); \
	GDExtensionCallError error; \
	::godot::gdextension_interface::object_method_bind_call(_gde_method_bind, nullptr, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count, nullptr, &error); \
}

#define DEFINE_CLASS_STATIC_VARARG_METHOD_RET(m_cpp_type, m_godot_class, m_name, m_hash, m_ret_type) \
inline m_ret_type m_name##_internal(const godot::Variant **p_args, GDExtensionInt p_arg_count) { \
	static GDExtensionMethodBindPtr _gde_method_bind = ::godot::gdextension_interface::classdb_get_method_bind(godot::StringName(#m_godot_class)._native_ptr(), godot::StringName(#m_name)._native_ptr(), m_hash); \
	CHECK_METHOD_BIND_RET(_gde_method_bind, m_ret_type()); \
	GDExtensionCallError error; \
	m_ret_type ret; \
	::godot::gdextension_interface::object_method_bind_call(_gde_method_bind, nullptr, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count, &ret, &error); \
	return ret; \
}

#endif // GODE_VARARG_MACROS_H
