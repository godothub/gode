#include "utils/node_runtime.h"
#include "register/utility_functions/utility_functions.h"
#include "register_builtin.gen.h"
#include "register_classes.gen.h"
#include <cppgc/platform.h>
#include <node.h>
#include <node_api.h>
#include <uv.h>
#ifdef WIN32
#undef CONNECT_DEFERRED
#endif

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <memory>
#include <string>
#include <vector>

namespace gode {

static napi_env js_env = nullptr;
void JsEnvManager::init(Napi::Env env) {
	js_env = env;
}
Napi::Env JsEnvManager::get_env() {
	return Napi::Env(js_env);
}

static bool node_initialized = false;
static std::unique_ptr<node::MultiIsolatePlatform> platform;
static std::unique_ptr<node::ArrayBufferAllocator> allocator;
static v8::Isolate *isolate = nullptr;
static node::Environment *env = nullptr;
static node::IsolateData *isolate_data = nullptr;
static v8::Global<v8::Context> node_context;

static Napi::Value fs_readFile(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsString()) {
		return env.Null();
	}
	std::string path = info[0].As<Napi::String>().Utf8Value();
	if (path.find("res://") != 0) {
		return env.Null();
	}

	godot::Ref<godot::FileAccess> file = godot::FileAccess::open(path.c_str(), godot::FileAccess::READ);
	if (file.is_null()) {
		return env.Null();
	}

	uint64_t len = file->get_length();
	godot::PackedByteArray pba = file->get_buffer(len);
	return Napi::String::New(env, reinterpret_cast<const char *>(pba.ptr()), len);
}

static Napi::Value fs_stat(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsString()) {
		return Napi::Number::New(env, 0);
	}
	std::string path = info[0].As<Napi::String>().Utf8Value();
	if (path.find("res://") != 0) {
		return Napi::Number::New(env, 0);
	}

	godot::String gd_path = godot::String::utf8(path.c_str());
	if (godot::FileAccess::file_exists(gd_path)) {
		return Napi::Number::New(env, 1); // File
	}
	if (godot::DirAccess::dir_exists_absolute(gd_path)) {
		return Napi::Number::New(env, 2); // Directory
	}
	return Napi::Number::New(env, 0); // Not found
}

static Napi::Object InitGodeAddon(Napi::Env env, Napi::Object exports) {
	gode::JsEnvManager::init(env);
	gode::GD::init(env, exports);
	gode::register_builtin(env);
	gode::register_classes(env);

	exports.Set("fs_readFile", Napi::Function::New(env, fs_readFile));
	exports.Set("fs_stat", Napi::Function::New(env, fs_stat));

	return exports;
}

static napi_value InitGodeAddon_C(napi_env env, napi_value exports) {
	return Napi::RegisterModule(env, exports, InitGodeAddon);
}

void NodeRuntime::init_once() {
	if (node_initialized) {
		return;
	}

	std::vector<std::string> args;
	std::vector<std::string> exec_args;
	std::vector<std::string> errors;
	args.push_back("godot node");

	int flags = node::ProcessInitializationFlags::kNoInitializeV8 |
			node::ProcessInitializationFlags::kNoInitializeNodeV8Platform |
			node::ProcessInitializationFlags::kNoInitializeCppgc |
			node::ProcessInitializationFlags::kNoDefaultSignalHandling |
			node::ProcessInitializationFlags::kNoStdioInitialization;

#ifdef _MSC_VER
	auto init_result = node::InitializeOncePerProcess(args, static_cast<node::ProcessInitializationFlags::Flags>(flags));

	if (!init_result->errors().empty()) {
		for (const auto &err : init_result->errors()) {
			// printf("Node init error: %s\n", err.c_str());
		}
	}
#else
	// Warn about ABI incompatibility when using MinGW/GCC on Windows
	#pragma message("WARNING: Skipping node::InitializeOncePerProcess due to ABI incompatibility (Non-MSVC compiler). Node.js embedding may fail.")
#endif

	allocator = node::ArrayBufferAllocator::Create();

	platform = node::MultiIsolatePlatform::Create(4);

	v8::V8::InitializePlatform(platform.get());
	v8::V8::Initialize();

	cppgc::InitializeProcess(platform->GetPageAllocator());

	isolate = node::NewIsolate(allocator.get(), uv_default_loop(), platform.get());

	{
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = node::NewContext(isolate);
		v8::Context::Scope context_scope(context);

		isolate_data = node::CreateIsolateData(isolate, uv_default_loop(), platform.get(), allocator.get());

		env = node::CreateEnvironment(isolate_data, context, args, exec_args);

		node::AddLinkedBinding(env, "gode", InitGodeAddon_C, NODE_API_DEFAULT_MODULE_API_VERSION);

		std::string boot_script =
				"const Module = require('module');"
				"const path = require('path');"
				"const fs = require('fs');"
				"const gode = process._linkedBinding('gode');"
				""
				"const originalReadFileSync = fs.readFileSync;"
				"fs.readFileSync = function(p, options) {"
				"  if (typeof p === 'string' && p.startsWith('res://')) {"
				"    const content = gode.fs_readFile(p);"
				"    if (content !== null) return content;"
				"  }"
				"  return originalReadFileSync.call(fs, p, options);"
				"};"
				""
				"const originalExistsSync = fs.existsSync;"
				"fs.existsSync = function(p) {"
				"  if (typeof p === 'string' && p.startsWith('res://')) {"
				"    return gode.fs_stat(p) > 0;"
				"  }"
				"  return originalExistsSync ? originalExistsSync.call(fs, p) : false;"
				"};"
				""
				"const originalStatSync = fs.statSync;"
				"fs.statSync = function(p, options) {"
				"  if (typeof p === 'string' && p.startsWith('res://')) {"
				"    const type = gode.fs_stat(p);"
				"    if (type > 0) {"
				"      return {"
				"        isDirectory: () => type === 2,"
				"        isFile: () => type === 1,"
				"        mtime: new Date(),"
				"        size: 0"
				"      };"
				"    }"
				"  }"
				"  return originalStatSync.call(fs, p, options);"
				"};"
				""
				"const originalResolve = path.resolve;"
				"path.resolve = function(...args) {"
				"  if (args.length > 0 && typeof args[0] === 'string' && args[0].startsWith('res://')) {"
				"      return args.join('/').replace(/\\\\/g, '/');"
				"  }"
				"  return originalResolve.apply(path, args);"
				"};"
				""
				"const originalFindPath = Module._findPath;"
				"Module._findPath = function(request, paths, isMain) {"
				"  if (request.startsWith('res://')) {"
				"      return request;"
				"  }"
				"  return originalFindPath.call(Module, request, paths, isMain);"
				"};"
				""
				"globalThis.__gode_compile = function(code, filename) {"
				"  try {"
				"    const m = new Module(filename, module);"
				"    m.filename = filename;"
				"    m.paths = Module._nodeModulePaths(path.dirname(filename));"
				"    m._compile(code, filename);"
				"    return m.exports;"
				"  } catch (e) {"
				"    console.error('Gode compile error:', e);"
				"    return undefined;"
				"  }"
				"};"
				"globalThis.require = require;"
				"process._linkedBinding('gode');";

		node::LoadEnvironment(env, boot_script.c_str());

		node_context.Reset(isolate, context);
	}

	node_initialized = true;
}

void NodeRuntime::run_script(const std::string &code) {
	if (!node_initialized) {
		init_once();
	}

	v8::Isolate::Scope isolate_scope(isolate);
	v8::HandleScope handle_scope(isolate);

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code.c_str(), v8::NewStringType::kNormal, static_cast<int>(code.size())).ToLocalChecked();
	v8::Local<v8::String> name = v8::String::NewFromUtf8(isolate, "<godot>", v8::NewStringType::kNormal).ToLocalChecked();

	v8::ScriptOrigin origin(name);
	v8::Local<v8::Script> script;

	if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
		return;
	}

	v8::MaybeLocal<v8::Value> result = script->Run(context);
	if (result.IsEmpty()) {
		return;
	}

	// Spin the event loop to ensure async operations complete if needed
	// Note: In a real game engine integration, you'd likely want to integrate
	// the event loop with the game loop rather than blocking here.
	// node::SpinEventLoop(env).ToChecked();
}

Napi::Value NodeRuntime::compile_script(const std::string &code, const std::string &filename) {
	if (!node_initialized) {
		init_once();
	}

	v8::Isolate::Scope isolate_scope(isolate);
	v8::EscapableHandleScope handle_scope(isolate);

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	v8::Local<v8::String> fn_name = v8::String::NewFromUtf8Literal(isolate, "__gode_compile");
	v8::Local<v8::Value> fn_val;
	if (!context->Global()->Get(context, fn_name).ToLocal(&fn_val) || !fn_val->IsFunction()) {
		return Napi::Value();
	}

	v8::Local<v8::Function> fn = fn_val.As<v8::Function>();

	v8::Local<v8::Value> args[] = {
		v8::String::NewFromUtf8(isolate, code.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
		v8::String::NewFromUtf8(isolate, filename.c_str(), v8::NewStringType::kNormal).ToLocalChecked()
	};

	v8::MaybeLocal<v8::Value> result = fn->Call(context, context->Global(), 2, args);

	if (result.IsEmpty()) {
		return Napi::Value();
	}

	v8::Local<v8::Value> final_exports = result.ToLocalChecked();
	return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*handle_scope.Escape(final_exports)));
}

Napi::Function NodeRuntime::get_default_class(Napi::Value module_exports) {
	if (module_exports.IsEmpty() || !module_exports.IsObject()) {
		return Napi::Function();
	}

	Napi::Object exports_obj = module_exports.As<Napi::Object>();
	if (exports_obj.Has("default")) {
		Napi::Value default_export = exports_obj.Get("default");
		if (default_export.IsFunction()) {
			return default_export.As<Napi::Function>();
		}
	}

	return Napi::Function();
}

void NodeRuntime::shutdown() {
	if (!node_initialized) {
		return;
	}

	if (env) {
		// Stop the environment first (this stops worker threads)
		// We need to be in the isolate scope to perform cleanup
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);

		node::Stop(env);
		node::FreeEnvironment(env);
		env = nullptr;
	}

	node_context.Reset(); // Dispose context AFTER freeing environment

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

	cppgc::ShutdownProcess();

	platform.reset();
	allocator.reset();

	node_initialized = false;
}

} // namespace gode
