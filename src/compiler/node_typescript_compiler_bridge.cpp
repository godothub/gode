#include "runtime/node_runtime.h"

#include "runtime/napi_error_utils.h"
#include "runtime/value_convert.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace gode {
namespace {

godot::Dictionary make_typescript_compile_failure(const godot::String &message) {
	godot::Dictionary result;
	godot::Array diagnostics;
	godot::Dictionary diagnostic;
	diagnostic["category"] = "error";
	diagnostic["code"] = 0;
	diagnostic["message"] = message;
	diagnostic["file"] = "";
	diagnostic["line"] = 0;
	diagnostic["column"] = 0;
	diagnostics.append(diagnostic);
	result["ok"] = false;
	result["outputs"] = godot::Array();
	result["diagnostics"] = diagnostics;
	return result;
}

Napi::Value read_project_directory(const Napi::CallbackInfo &info) {
	const auto path = godot::String::utf8(info[0].ToString().Utf8Value().c_str());
	godot::Array files;
	godot::Array directories;
	if (godot::DirAccess::dir_exists_absolute(path)) {
		for (const auto &file : godot::DirAccess::get_files_at(path)) {
			files.append(file);
		}
		for (const auto &directory : godot::DirAccess::get_directories_at(path)) {
			directories.append(directory);
		}
	}
	godot::Dictionary entries;
	entries["files"] = files;
	entries["directories"] = directories;
	return godot_to_napi(info.Env(), entries);
}

godot::Dictionary call_typescript_compiler(const char *function_name, const godot::Variant &argument, bool collect_sources = false) {
	if (!NodeRuntime::is_running()) {
		NodeRuntime::init_once();
	}

	if (NodeRuntime::napi_environment == nullptr) {
		return make_typescript_compile_failure("Node runtime is not ready for TypeScript compilation.");
	}

	auto *isolate = NodeRuntime::isolate;
	auto napi_environment = NodeRuntime::napi_environment;
	v8::Locker locker(isolate);
	v8::Isolate::Scope isolate_scope(isolate);
	v8::HandleScope handle_scope(isolate);

	v8::Local<v8::Context> context = NodeRuntime::node_context.Get(isolate);
	v8::Context::Scope context_scope(context);
	v8::TryCatch try_catch(isolate);

	v8::Local<v8::String> fn_name = v8::String::NewFromUtf8(isolate, function_name).ToLocalChecked();
	v8::Local<v8::Value> fn_val;
	if (!context->Global()->Get(context, fn_name).ToLocal(&fn_val) || !fn_val->IsFunction()) {
		v8::Local<v8::String> error_name = v8::String::NewFromUtf8Literal(isolate, "__gode_typescript_compiler_boot_error");
		v8::Local<v8::Value> boot_error;
		if (context->Global()->Get(context, error_name).ToLocal(&boot_error) && !boot_error->IsUndefined()) {
			v8::String::Utf8Value boot_error_text(isolate, boot_error);
			return make_typescript_compile_failure(godot::String("TypeScript compiler bootstrap failed: ") + (*boot_error_text ? *boot_error_text : "unknown error"));
		}
		return make_typescript_compile_failure("TypeScript compiler bootstrap function was not registered.");
	}

	Napi::Env napi_env(napi_environment);
	Napi::Value js_files = godot_to_napi(napi_env, argument);
	if (napi_env.IsExceptionPending()) {
		log_and_clear_pending_js_exception(napi_environment, "NodeRuntime TypeScript source conversion");
		return make_typescript_compile_failure("Failed to convert TypeScript source files for Node.");
	}

	Napi::Function fn(napi_env, reinterpret_cast<napi_value>(*fn_val));
	Napi::Value result_value = collect_sources
			? fn.Call(napi_env.Global(), { js_files, Napi::Function::New(napi_env, read_project_directory) })
			: fn.Call(napi_env.Global(), { js_files });
	if (try_catch.HasCaught() || napi_env.IsExceptionPending()) {
		log_and_clear_pending_js_exception(napi_environment, "NodeRuntime TypeScript compile");
		return make_typescript_compile_failure("TypeScript compiler threw an exception.");
	}

	godot::Variant converted = napi_to_godot(result_value);
	if (log_and_clear_pending_js_exception(napi_environment, "NodeRuntime TypeScript result conversion")) {
		return make_typescript_compile_failure("Failed to convert TypeScript compiler result.");
	}
	if (converted.get_type() != godot::Variant::DICTIONARY) {
		return make_typescript_compile_failure("TypeScript compiler returned an invalid result.");
	}
	return converted;
}

} // namespace

godot::Dictionary NodeRuntime::compile_typescript_project(const godot::Array &files) {
	return call_typescript_compiler("__gode_compile_typescript_project", files);
}

godot::Dictionary NodeRuntime::collect_typescript_project_sources(const godot::String &project_root) {
	return call_typescript_compiler("__gode_collect_typescript_project_sources", project_root, true);
}

} // namespace gode
