#include "utils/node_runtime.h"
#include "register_builtin.gen.h"
#include "register_classes.gen.h"
#include "utility_functions/utility_functions.h"
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
	gode::register_builtin(env);
	gode::register_classes(env, exports);
	gode::GD::init(env, exports);

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
				"try {"
				"  const _gode_module = new Module('gode');"
				"  _gode_module.id = 'gode';"
				"  _gode_module.exports = gode;"
				"  _gode_module.loaded = true;"
				"  _gode_module.filename = 'gode';"
				"  Module._cache['gode'] = _gode_module;"
				"} catch (e) {"
				"  console.error('[Gode] Injection error:', e);"
				"}"
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
				"const originalResolveFilename = Module._resolveFilename;"
				"Module._resolveFilename = function(request, parent, isMain, options) {"
				"  if (request.startsWith('res://')) {"
				"    return request;"
				"  }"
				"  if (parent && parent.filename && parent.filename.startsWith('res://')) {"
				"    if (request.startsWith('./') || request.startsWith('../')) {"
				"      const parentDir = path.dirname(parent.filename);"
				"      return path.resolve(parentDir, request);"
				"    }"
				"  }"
				"  return originalResolveFilename.call(Module, request, parent, isMain, options);"
				"};"
				""
				"globalThis.__gode_compile = function(code, filename) {"
				"  try {"
				"    const m = new Module(filename, module);"
				"    m.filename = filename;"
				"    m.paths = Module._nodeModulePaths(path.dirname(filename));"
				"    m._compile(code, filename);"
				"    console.log('Gode compiled exports:', m.exports);"
				"    return m.exports;"
				"  } catch (e) {"
				"    console.error('Gode compile error:', e);"
				"    return e;" // Return error object on failure? No, caller expects undefined or exports.
				// If we return undefined, C++ sees it as empty?
				// compile_script checks result.IsEmpty().
				// But fn->Call returns undefined (as Value) if the function returns undefined.
				// Wait, if function returns undefined, Call returns a Local<Value> wrapping undefined.
				// It is NOT Empty.
				"  }"
				"};"
				"const originalRequire = Module.prototype.require;"
				"Module.prototype.require = function(id) {"
				"  if (id === 'gode') {"
				"    return gode;"
				"  }"
				"  return originalRequire.call(this, id);"
				"};"
				"const originalGlobalRequire = globalThis.require || Module.createRequire(process.cwd());"
				"globalThis.require = function(id) {"
				"  if (id === 'gode') {"
				"    return gode;"
				"  }"
				"  return originalGlobalRequire.call(this, id);"
				"};"
				"if (originalGlobalRequire) Object.assign(globalThis.require, originalGlobalRequire);";
		// "globalThis.require = require;";

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
}

Napi::Value NodeRuntime::compile_script(const std::string &code, const std::string &filename) {
	if (!node_initialized) {
		init_once();
	}
	// Removed v8::Locker because it causes memory errors in some configurations
	v8::Isolate::Scope isolate_scope(isolate);

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	v8::EscapableHandleScope escapable_scope(isolate);

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

	// Check if Call succeeded
	if (result.IsEmpty()) {
		godot::UtilityFunctions::print("compile_script: fn->Call returned empty (exception?)");
		return Napi::Value();
	}

	v8::Local<v8::Value> final_exports = result.ToLocalChecked();

	// Debug result type
	// godot::UtilityFunctions::print("compile_script: final_exports IsUndefined: ", final_exports->IsUndefined());
	// godot::UtilityFunctions::print("compile_script: final_exports IsNull: ", final_exports->IsNull());
	// godot::UtilityFunctions::print("compile_script: final_exports IsObject: ", final_exports->IsObject());
	// godot::UtilityFunctions::print("compile_script: final_exports IsFunction: ", final_exports->IsFunction());

	if (final_exports->IsUndefined()) {
		v8::Local<v8::Value> undefined_val = v8::Undefined(isolate);
		// If we are here, it means JS returned undefined, BUT JS log said it returned a class!
		// This is extremely weird.

		// Maybe the HandleScope in JavascriptInstance::compile_module is not enough?
		// No, that handles the return value lifetime.

		// Maybe `fn->Call` logic is somehow flawed?
		// We pass context->Global() as receiver. `globalThis.__gode_compile` expects global as `this`?
		// Yes.

		// Maybe `args` are wrong?
		// code and filename strings. They seem fine.

		// Is it possible that `result` handle is somehow invalidated immediately?
		// We are inside `escapable_scope`.

		return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*escapable_scope.Escape(undefined_val)));
	}

	return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*escapable_scope.Escape(final_exports)));
}

Napi::Function NodeRuntime::get_default_class(Napi::Value module_exports) {
	v8::Isolate::Scope isolate_scope(isolate);
	v8::EscapableHandleScope escapable_scope(isolate);
	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	if (module_exports.IsEmpty()) {
		godot::UtilityFunctions::print("get_default_class: module_exports is Empty");
		return Napi::Function();
	}

	if (module_exports.IsUndefined()) {
		godot::UtilityFunctions::print("get_default_class: module_exports is Undefined");
		return Napi::Function();
	}

	Napi::Object exports_obj = module_exports.As<Napi::Object>();

	if (module_exports.IsFunction()) {
		return module_exports.As<Napi::Function>();
	}

	if (exports_obj.Has("default")) {
		Napi::Value default_export = exports_obj.Get("default");
		if (default_export.IsFunction()) {
			v8::Local<v8::Value> v8_val = *reinterpret_cast<v8::Local<v8::Value> *>(static_cast<napi_value>(default_export));
			v8::Local<v8::Value> escaped_val = escapable_scope.Escape(v8_val);
			return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*escaped_val)).As<Napi::Function>();
		}
	}

	return Napi::Function();
}

void NodeRuntime::spin_loop() {
	if (!node_initialized || !env) {
		return;
	}

	v8::Isolate::Scope isolate_scope(isolate);
	v8::HandleScope handle_scope(isolate);

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	uv_run(uv_default_loop(), UV_RUN_NOWAIT);
}

void NodeRuntime::shutdown() {
	if (!node_initialized) {
		return;
	}

	if (env) {
		v8::Isolate::Scope isolate_scope(isolate);
		v8::HandleScope handle_scope(isolate);

		{
			v8::Context::Scope context_scope(node_context.Get(isolate));
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

	cppgc::ShutdownProcess();

	platform.reset();
	allocator.reset();

	node_initialized = false;
}

} // namespace gode
