#ifndef GODE_UTILITY_FUNCTIONS_VARARG_METHOD_H
#define GODE_UTILITY_FUNCTIONS_VARARG_METHOD_H

#include <godot_cpp/variant/variant.hpp>
#include <vector>

namespace gode {
namespace utility {

inline void print_internal(const godot::Variant **p_args, GDExtensionInt p_arg_count) {
	static GDExtensionPtrUtilityFunction _gde_function = ::godot::gdextension_interface::variant_get_ptr_utility_function(godot::StringName("print")._native_ptr(), 2648703342);
	CHECK_METHOD_BIND(_gde_function);
	godot::Variant ret;
	_gde_function(&ret, reinterpret_cast<GDExtensionConstVariantPtr *>(p_args), p_arg_count);
}
inline void print(const godot::Variant &arg, const std::vector<godot::Variant> &args) {
	std::vector<godot::Variant> variant_args;
	std::vector<godot::Variant *> variant_args_ptr;
	variant_args.push_back(arg);
	for (int i = 0; i < args.size(); i++) {
		variant_args.push_back(args[i]);
	}
	for (int i = 0; i < variant_args.size(); i++) {
		variant_args_ptr.push_back(&variant_args[i]);
	}
	print_internal(const_cast<const godot::Variant **>(variant_args_ptr.data()), variant_args_ptr.size());
}
} //namespace utility
} //namespace gode

#endif // GODE_UTILITY_FUNCTIONS_VARARG_METHOD_H