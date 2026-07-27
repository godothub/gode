#include "runtime/napi_error_utils.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <v8.h>

namespace gode {
namespace {

std::string v8_value_to_string(v8::Isolate *isolate, v8::Local<v8::Value> value) {
	if (value.IsEmpty()) {
		return std::string();
	}
	v8::String::Utf8Value text(isolate, value);
	return *text ? *text : std::string();
}

} // namespace

std::string js_error_to_string(Napi::Value value) {
	try {
		if (value.IsObject()) {
			Napi::Object obj = value.As<Napi::Object>();
			if (obj.Has("stack")) {
				Napi::Value stack = obj.Get("stack");
				if (!stack.IsNull() && !stack.IsUndefined()) {
					return stack.ToString().Utf8Value();
				}
			}
			if (obj.Has("message")) {
				Napi::Value message = obj.Get("message");
				if (!message.IsNull() && !message.IsUndefined()) {
					return message.ToString().Utf8Value();
				}
			}
		}
		if (!value.IsNull() && !value.IsUndefined()) {
			return value.ToString().Utf8Value();
		}
	} catch (...) {
		if (!value.IsEmpty() && value.Env().IsExceptionPending()) {
			value.Env().GetAndClearPendingException();
		}
	}
	return "Unknown JavaScript exception";
}

std::string js_error_to_string(const Napi::Error &error) {
	std::string message = js_error_to_string(error.Value());
	if (message == "Unknown JavaScript exception" && !error.Message().empty()) {
		return error.Message();
	}
	return message;
}

void log_js_error(const std::string &context, const std::string &message) {
	godot::UtilityFunctions::printerr(context.c_str(), ": ", message.c_str());
}

void log_v8_exception(v8::Isolate *isolate, v8::TryCatch &try_catch, const std::string &context) {
	if (!try_catch.HasCaught()) {
		godot::UtilityFunctions::printerr(context.c_str(), ": Unknown V8 exception");
		return;
	}

	v8::Local<v8::Context> v8_context = isolate->GetCurrentContext();
	v8::Local<v8::Value> stack;
	if (try_catch.StackTrace(v8_context).ToLocal(&stack) && !stack->IsUndefined() && !stack->IsNull()) {
		const std::string stack_text = v8_value_to_string(isolate, stack);
		if (!stack_text.empty()) {
			godot::UtilityFunctions::printerr(context.c_str(), ": ", stack_text.c_str());
			return;
		}
	}

	const std::string exception_text = v8_value_to_string(isolate, try_catch.Exception());
	godot::UtilityFunctions::printerr(context.c_str(), ": ", exception_text.empty() ? "Unknown V8 exception" : exception_text.c_str());
}

bool log_and_clear_pending_js_exception(Napi::Env env, const std::string &context) {
	if (!env.IsExceptionPending()) {
		return false;
	}
	Napi::Error error = env.GetAndClearPendingException();
	log_js_error(context, js_error_to_string(error));
	return true;
}

} // namespace gode
