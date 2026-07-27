#ifndef GODE_SCRIPT_CALLABLE_H
#define GODE_SCRIPT_CALLABLE_H

#include <napi.h>
#include <godot_cpp/variant/callable_custom.hpp>

namespace gode {

class ScriptCallable : public godot::CallableCustom {
	Napi::FunctionReference func_ref;

public:
	ScriptCallable(Napi::Function p_function);
	virtual ~ScriptCallable();

	Napi::Function get_function() const;

	virtual uint32_t hash() const override;
	virtual godot::String get_as_text() const override;
	virtual CompareEqualFunc get_compare_equal_func() const override;
	virtual CompareLessFunc get_compare_less_func() const override;
	virtual bool is_valid() const override;
	virtual godot::ObjectID get_object() const override;
	virtual void call(const godot::Variant **p_arguments, int p_argcount, godot::Variant &r_return_value, GDExtensionCallError &r_call_error) const override;
};

} // namespace gode

#endif // GODE_SCRIPT_CALLABLE_H
