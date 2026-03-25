#include "support/javascript/javascript_callable.h"

#include "utils/node_runtime.h"
#include "utils/value_convert.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <v8-isolate.h>
#include <v8-locker.h>

namespace gode {

JavascriptCallable::JavascriptCallable(Napi::Function p_function) {
	func_ref = Napi::Persistent(p_function);
}

JavascriptCallable::~JavascriptCallable() {
	if (!func_ref.IsEmpty()) {
		func_ref.Reset();
	}
}

Napi::Function JavascriptCallable::get_function() const {
	if (func_ref.IsEmpty()) {
		return Napi::Function();
	}
	return func_ref.Value();
}

uint32_t JavascriptCallable::hash() const {
    v8::Locker locker(NodeRuntime::isolate);
    v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
    v8::HandleScope handle_scope(NodeRuntime::isolate);

    if (func_ref.IsEmpty()) return 0;
    
    // Napi::Value has operator napi_value()
    napi_value nv = func_ref.Value();
    // In Node.js environment, napi_value is effectively v8::Local<v8::Value>
    v8::Local<v8::Value> v8_val = *reinterpret_cast<v8::Local<v8::Value>*>(&nv);
    
    if (v8_val.IsEmpty() || !v8_val->IsObject()) return 0;
    
    return v8_val.As<v8::Object>()->GetIdentityHash();
}

godot::String JavascriptCallable::get_as_text() const {
	return "JavascriptCallable";
}

bool JavascriptCallable::is_valid() const {
    // godot::UtilityFunctions::print("DEBUG: JavascriptCallable::is_valid invoked"); // Too spammy?
	return !func_ref.IsEmpty();
}

godot::ObjectID JavascriptCallable::get_object() const {
	return godot::ObjectID();
}

void JavascriptCallable::call(const godot::Variant **p_arguments, int p_argcount, godot::Variant &r_return_value, GDExtensionCallError &r_call_error) const {
	v8::Locker locker(NodeRuntime::isolate);
	v8::Isolate::Scope isolate_scope(NodeRuntime::isolate);
	v8::HandleScope handle_scope(NodeRuntime::isolate);
    
	if (func_ref.IsEmpty()) {
		r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return;
	}

	Napi::Function func = func_ref.Value();
	Napi::Env env = func.Env();
	std::vector<napi_value> args;
	for (int i = 0; i < p_argcount; ++i) {
		args.push_back(godot_to_napi(env, *p_arguments[i]));
	}

	try {
		Napi::Value result = func.Call(env.Global(), args);
		r_return_value = napi_to_godot(result);
		r_call_error.error = GDEXTENSION_CALL_OK;
	} catch (const Napi::Error &e) {
		godot::UtilityFunctions::printerr("JS Exception in Callable: ", e.Message().c_str());
		r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
	}
}

static bool javascript_callable_equal_func(const godot::CallableCustom *p_a, const godot::CallableCustom *p_b) {
	const JavascriptCallable *a = static_cast<const JavascriptCallable *>(p_a);
	const JavascriptCallable *b = static_cast<const JavascriptCallable *>(p_b);
    
    // Compare the underlying JS functions
    if (a->get_function().IsEmpty() || b->get_function().IsEmpty()) {
        return a->get_function().IsEmpty() && b->get_function().IsEmpty();
    }
    
    // Use StrictEquals to check object identity
    return a->get_function().StrictEquals(b->get_function());
}

static bool javascript_callable_less_than_func(const godot::CallableCustom *p_a, const godot::CallableCustom *p_b) {
    // For sorting, we can use hash or just address comparison of the JS object?
    // But hash collisions are possible.
    // Address comparison of wrapper is not enough.
    // Address comparison of underlying JS object is hard without handle scope.
    
    // Fallback to wrapper address for now, or hash comparison?
    // CallableCustom default behavior is address comparison if less_func returns false?
    // Actually, less_func is used for Map keys etc.
    
    // Let's use hash comparison first, then address as tie-breaker?
    // Or just use wrapper address if we don't care about stable sorting across different wrappers for same JS func.
    // BUT we DO care: same JS func should be "equal", so "not less" and "not greater".
    
    // If they are equal (same JS func), then neither is less than the other.
    if (javascript_callable_equal_func(p_a, p_b)) return false;
    
    // If not equal, use hash to order
    if (p_a->hash() != p_b->hash()) {
        return p_a->hash() < p_b->hash();
    }
    
    // If hashes collide but not equal, use address
    return p_a < p_b;
}

godot::CallableCustom::CompareEqualFunc JavascriptCallable::get_compare_equal_func() const {
	return javascript_callable_equal_func;
}

godot::CallableCustom::CompareLessFunc JavascriptCallable::get_compare_less_func() const {
	return javascript_callable_less_than_func;
}

} // namespace gode
