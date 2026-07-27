#include "runtime/node_runtime.h"
#include "register_builtin.gen.h"
#include "register_classes.gen.h"
#include "runtime/napi_error_utils.h"
#include "runtime/node_bootstrap_scripts.h"
#include "runtime/node_godot_bridge.h"
#include "runtime/node_inspector.h"
#include "runtime/node_module_resolver.h"
#include "utility_functions/utility_functions.h"

#include <node.h>
#include <node_api.h>
#include <uv.h>
#ifdef WIN32
#undef CONNECT_DEFERRED
#endif

#include "runtime/value_convert.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <memory>
#include <string>
#include <vector>

namespace gode {

static bool node_initialized = false;
static std::unique_ptr<node::MultiIsolatePlatform> platform;
static std::unique_ptr<node::ArrayBufferAllocator> allocator;
static node::IsolateData *isolate_data = nullptr;
v8::Isolate *NodeRuntime::isolate = nullptr;
node::Environment *NodeRuntime::env = nullptr;
v8::Global<v8::Context> NodeRuntime::node_context;
thread_local napi_env NodeRuntime::thread_local_env = nullptr;

static Napi::Object InitGodeAddon(Napi::Env env, Napi::Object exports) {
	node_runtime_bridge::preload_node_dll_stub();
	NodeRuntime::thread_local_env = env;
	gode::register_builtin(env, exports);
	gode::register_classes(env, exports);
	gode::GD::init(env, exports);

	node_runtime_bridge::install_exports(env, exports);
	node_runtime_bridge::install_global_decorators(env);

	return exports;
}

static napi_value InitGodeAddon_C(napi_env env, napi_value exports) {
	return Napi::RegisterModule(env, exports, InitGodeAddon);
}

void NodeRuntime::init_once() {
	if (node_initialized) {
		return;
	}

	node_inspector::Config inspector_config = node_inspector::load_config();

	std::vector<std::string> args;
	std::vector<std::string> exec_args;
	std::vector<std::string> errors;
	// args[0] is the executable name; the remaining entries are Node flags.
	args.push_back("godot node");
	args.push_back("--experimental-vm-modules");
	if (inspector_config.enabled && inspector_config.source_maps) {
		args.push_back("--enable-source-maps");
	}

	int flags = node::ProcessInitializationFlags::kNoInitializeV8 |
			node::ProcessInitializationFlags::kNoInitializeNodeV8Platform |
			node::ProcessInitializationFlags::kNoInitializeCppgc |
			node::ProcessInitializationFlags::kNoDefaultSignalHandling |
			node::ProcessInitializationFlags::kNoStdioInitialization;

	auto init_result = node::InitializeOncePerProcess(args, static_cast<node::ProcessInitializationFlags::Flags>(flags));

	if (!init_result->errors().empty()) {
		for (const auto &err : init_result->errors()) {
			godot::UtilityFunctions::printerr(godot::String("Node init error: ") + err.c_str());
		}
	}

	allocator = node::ArrayBufferAllocator::Create();

	platform = node::MultiIsolatePlatform::Create(4);

	v8::V8::InitializePlatform(platform.get());
	v8::V8::Initialize();

	isolate = node::NewIsolate(allocator.get(), uv_default_loop(), platform.get());

	{
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = node::NewContext(isolate);
		v8::Context::Scope context_scope(context);

		isolate_data = node::CreateIsolateData(isolate, uv_default_loop(), platform.get(), allocator.get());

		env = node::CreateEnvironment(isolate_data, context, args, exec_args);

		node::AddLinkedBinding(env, "godot", InitGodeAddon_C, NODE_API_DEFAULT_MODULE_API_VERSION);

		const std::string boot_script = node_bootstrap_scripts::commonjs_bootstrap_script();
		node::LoadEnvironment(env, boot_script.c_str());

		// Drive the event loop once to finish boot_script execution.
		isolate->PerformMicrotaskCheckpoint();
		uv_run(uv_default_loop(), UV_RUN_ONCE);

		// Execute ESM support after boot_script so the CommonJS bootstrap is already registered.
		const std::string esm_script = node_bootstrap_scripts::esm_bootstrap_script();
		{
			v8::Local<v8::String> esm_source = v8::String::NewFromUtf8(
					isolate, esm_script.c_str(), v8::NewStringType::kNormal)
													   .ToLocalChecked();
			v8::Local<v8::String> esm_name = v8::String::NewFromUtf8Literal(isolate, "<gode-esm-init>");
			v8::ScriptOrigin esm_origin(esm_name);
			v8::Local<v8::Script> esm_compiled;
			v8::TryCatch try_catch(isolate);
			if (v8::Script::Compile(context, esm_source, &esm_origin).ToLocal(&esm_compiled)) {
				v8::Local<v8::Value> ignored_result;
				if (!esm_compiled->Run(context).ToLocal(&ignored_result)) {
					log_v8_exception(isolate, try_catch, "[Gode ESM] Failed to run ESM init script");
				}
			} else {
				log_v8_exception(isolate, try_catch, "[Gode ESM] Failed to compile ESM init script");
			}
		}

		node_context.Reset(isolate, context);
		node_inspector::open_if_enabled(inspector_config);
	}

	node_initialized = true;
}

bool NodeRuntime::is_running() {
	return node_initialized && isolate != nullptr && env != nullptr && !node_context.IsEmpty();
}

void NodeRuntime::run_script(const std::string &code) {
	if (!node_initialized) {
		init_once();
	}

	v8::Locker locker(isolate);
	v8::Isolate::Scope isolate_scope(isolate);
	v8::HandleScope handle_scope(isolate);

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code.c_str(), v8::NewStringType::kNormal, static_cast<int>(code.size())).ToLocalChecked();
	v8::Local<v8::String> name = v8::String::NewFromUtf8(isolate, "<godot>", v8::NewStringType::kNormal).ToLocalChecked();

	v8::ScriptOrigin origin(name);
	v8::Local<v8::Script> script;
	v8::TryCatch try_catch(isolate);

	if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
		log_v8_exception(isolate, try_catch, "NodeRuntime run_script compile");
		return;
	}

	v8::MaybeLocal<v8::Value> result = script->Run(context);
	if (result.IsEmpty()) {
		log_v8_exception(isolate, try_catch, "NodeRuntime run_script execution");
		return;
	}
}

Napi::Value NodeRuntime::compile_script(const std::string &code, const std::string &filename) {
	if (!node_initialized) {
		init_once();
	}

	if (thread_local_env == nullptr) {
		godot::UtilityFunctions::printerr("[compile_script] Error: thread_local_env is not set");
		return Napi::Value();
	}

	v8::Locker locker(isolate);
	v8::Isolate::Scope isolate_scope(isolate);
	Napi::Env napi_env(thread_local_env);
	Napi::EscapableHandleScope handle_scope(napi_env);

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	node_inspector::maybe_break_on_user_script(filename);

	// Detect whether the file should be loaded as ESM.
	v8::Local<v8::Value> result;
	if (node_module_resolver::is_esm_file(filename, code)) {
		result = compile_esm_module(code, filename);
	} else {
		result = compile_cjs_module(code, filename);
	}

	if (result.IsEmpty()) {
		return Napi::Value();
	}

	return handle_scope.Escape(reinterpret_cast<napi_value>(*result));
}

v8::Local<v8::Value> NodeRuntime::compile_esm_module(const std::string &code, const std::string &filename) {
	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);
	v8::EscapableHandleScope escapable_scope(isolate);

	v8::Local<v8::String> fn_name = v8::String::NewFromUtf8Literal(isolate, "__gode_compile_esm");
	v8::Local<v8::Value> fn_val;
	v8::TryCatch try_catch(isolate);
	if (!context->Global()->Get(context, fn_name).ToLocal(&fn_val) || !fn_val->IsFunction()) {
		if (try_catch.HasCaught()) {
			log_v8_exception(isolate, try_catch, "NodeRuntime ESM compiler lookup");
		}
		godot::UtilityFunctions::print("compile_esm_module: __gode_compile_esm not found");
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Function> fn = fn_val.As<v8::Function>();

	v8::Local<v8::Value> args[] = {
		v8::String::NewFromUtf8(isolate, code.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
		v8::String::NewFromUtf8(isolate, filename.c_str(), v8::NewStringType::kNormal).ToLocalChecked()
	};

	v8::MaybeLocal<v8::Value> result = fn->Call(context, context->Global(), 2, args);

	if (result.IsEmpty()) {
		log_v8_exception(isolate, try_catch, "NodeRuntime ESM compile call");
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Value> promise_val = result.ToLocalChecked();

	// Check whether the result is a Promise.
	if (!promise_val->IsPromise()) {
		godot::UtilityFunctions::print("compile_esm_module: result is not a Promise");
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Promise> promise = promise_val.As<v8::Promise>();

	// ESM loading is async; keep driving the event loop until the Promise settles.
	while (promise->State() == v8::Promise::kPending) {
		isolate->PerformMicrotaskCheckpoint();
		uv_run(uv_default_loop(), UV_RUN_ONCE);
		if (promise->State() != v8::Promise::kPending) {
			break;
		}
	}

	if (promise->State() == v8::Promise::kRejected) {
		v8::Local<v8::Value> error = promise->Result();
		Napi::Value js_error(thread_local_env, reinterpret_cast<napi_value>(*error));
		log_js_error("NodeRuntime ESM compile rejected", js_error_to_string(js_error));
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Value> final_exports = promise->Result();

	if (final_exports->IsUndefined()) {
		v8::Local<v8::Value> undefined_val = v8::Undefined(isolate);
		return escapable_scope.Escape(undefined_val);
	}

	return escapable_scope.Escape(final_exports);
}

v8::Local<v8::Value> NodeRuntime::compile_cjs_module(const std::string &code, const std::string &filename) {
	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);
	v8::EscapableHandleScope escapable_scope(isolate);

	v8::Local<v8::String> fn_name = v8::String::NewFromUtf8Literal(isolate, "__gode_compile");
	v8::Local<v8::Value> fn_val;
	v8::TryCatch try_catch(isolate);
	if (!context->Global()->Get(context, fn_name).ToLocal(&fn_val) || !fn_val->IsFunction()) {
		if (try_catch.HasCaught()) {
			log_v8_exception(isolate, try_catch, "NodeRuntime CJS compiler lookup");
		}
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Function> fn = fn_val.As<v8::Function>();

	v8::Local<v8::Value> args[] = {
		v8::String::NewFromUtf8(isolate, code.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
		v8::String::NewFromUtf8(isolate, filename.c_str(), v8::NewStringType::kNormal).ToLocalChecked()
	};

	v8::MaybeLocal<v8::Value> result = fn->Call(context, context->Global(), 2, args);

	if (result.IsEmpty()) {
		log_v8_exception(isolate, try_catch, "NodeRuntime CJS compile call");
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Value> final_exports = result.ToLocalChecked();

	if (final_exports->IsUndefined()) {
		v8::Local<v8::Value> undefined_val = v8::Undefined(isolate);
		return escapable_scope.Escape(undefined_val);
	}

	return escapable_scope.Escape(final_exports);
}

Napi::Function NodeRuntime::get_default_class(Napi::Value module_exports) {
	if (module_exports.IsEmpty() || module_exports.IsUndefined()) {
		return Napi::Function();
	}

	v8::Locker locker(isolate);
	v8::Isolate::Scope isolate_scope(isolate);
	Napi::Env napi_env = module_exports.Env();
	Napi::EscapableHandleScope handle_scope(napi_env);

	// A function export is already a valid default class.
	if (module_exports.IsFunction()) {
		return handle_scope.Escape(module_exports).As<Napi::Function>();
	}

	// Try to read the default export.
	if (module_exports.IsObject()) {
		Napi::Object exports_obj = module_exports.As<Napi::Object>();
		if (exports_obj.Has("default")) {
			Napi::Value default_export = exports_obj.Get("default");
			if (default_export.IsFunction()) {
				return handle_scope.Escape(default_export).As<Napi::Function>();
			}
		}
	}

	return Napi::Function();
}

godot::Variant NodeRuntime::eval_expression(const std::string &expr) {
	if (!node_initialized || !env) {
		return godot::Variant();
	}

	std::string code = "(function(godot) { with (godot) { return (" + expr + "); } })(process._linkedBinding('godot'))";

	v8::Locker locker(isolate);
	v8::Isolate::Scope isolate_scope(isolate);
	v8::HandleScope handle_scope(isolate);

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);
	v8::TryCatch try_catch(isolate);

	v8::MaybeLocal<v8::String> maybe_source = v8::String::NewFromUtf8(isolate, code.c_str());
	if (maybe_source.IsEmpty() || try_catch.HasCaught()) {
		return godot::Variant();
	}

	v8::MaybeLocal<v8::Script> maybe_script = v8::Script::Compile(context, maybe_source.ToLocalChecked());
	if (maybe_script.IsEmpty() || try_catch.HasCaught()) {
		return godot::Variant();
	}

	v8::MaybeLocal<v8::Value> maybe_result = maybe_script.ToLocalChecked()->Run(context);
	if (maybe_result.IsEmpty() || try_catch.HasCaught()) {
		return godot::Variant();
	}

	v8::Local<v8::Value> result = maybe_result.ToLocalChecked();
	Napi::Value napi_result(thread_local_env, reinterpret_cast<napi_value>(*result));
	godot::Variant converted = napi_to_godot(napi_result);
	if (log_and_clear_pending_js_exception(thread_local_env, "NodeRuntime eval expression result conversion")) {
		return godot::Variant();
	}
	return converted;
}

void NodeRuntime::spin_loop() {
	if (!node_initialized || !env) {
		return;
	}

	v8::Locker locker(isolate);
	v8::Isolate::Scope isolate_scope(isolate);
	v8::HandleScope handle_scope(isolate);

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	isolate->PerformMicrotaskCheckpoint();
	uv_run(uv_default_loop(), UV_RUN_NOWAIT);
	isolate->PerformMicrotaskCheckpoint();
}

void NodeRuntime::shutdown() {
	if (!node_initialized) {
		return;
	}

	if (env) {
		v8::Locker locker(isolate);
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);

		{
			v8::Context::Scope context_scope(node_context.Get(isolate));
			node_inspector::close_if_open();
			clear_godot_instance_cache();
			reset_class_references();
			reset_builtin_references();
			clear_registered_godot_classes();
			isolate->LowMemoryNotification();
			node::SpinEventLoop(env).ToChecked();
		}

		node::Stop(env);
		node::FreeEnvironment(env);
		env = nullptr;
	}

	node_context.Reset();

	if (isolate_data) {
		node::FreeIsolateData(isolate_data);
		isolate_data = nullptr;
	}

	if (isolate) {
		if (platform) {
			platform->UnregisterIsolate(isolate);
		}
		isolate->Dispose();
		isolate = nullptr;
	}

	v8::V8::Dispose();
	v8::V8::DisposePlatform();

	platform.reset();
	allocator.reset();

	node_initialized = false;
}

} // namespace gode
