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
static node::IsolateData *isolate_data = nullptr;

v8::Isolate *NodeRuntime::isolate = nullptr;
node::Environment *NodeRuntime::env = nullptr;
v8::Global<v8::Context> NodeRuntime::node_context;

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
	// args[0] 是程序名，后面的是 node 标志
	args.push_back("godot node");
	args.push_back("--experimental-vm-modules");

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
	// 启用实验性 VM 模块支持
	// v8::V8::SetFlagsFromString("--experimental-vm-modules");
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

		node::AddLinkedBinding(env, "godot", InitGodeAddon_C, NODE_API_DEFAULT_MODULE_API_VERSION);

		std::string boot_script =
				"const Module = require('module');"
				"const path = require('path');"
				"const fs = require('fs');"
				"const gode = process._linkedBinding('godot');"
				"const isRes = p => typeof p === 'string' && p.startsWith('res://');"
				"const toPosix = p => isRes(p) ? '/' + p.substring(6) : p;"
				"const fromPosix = p => 'res://' + p.substring(1);"
				"const tryExtensions = (p) => {"
				"    const exts = ['.js', '.json', '.node'];"
				"    for (const ext of exts) {"
				"        if (fs.existsSync(p + ext)) return p + ext;"
				"    }"
				"    return null;"
				"};"
				"const findInRes = (base, req) => {"
				"    const p = path.join(base, req);"
				"    if (fs.existsSync(p) && fs.statSync(p).isFile()) return p;"
				"    const withExt = tryExtensions(p);"
				"    if (withExt) return withExt;"
				"    if (fs.existsSync(p) && fs.statSync(p).isDirectory()) {"
				"        const pkgPath = path.join(p, 'package.json');"
				"        if (fs.existsSync(pkgPath)) {"
				"            try {"
				"                const pkg = JSON.parse(fs.readFileSync(pkgPath, 'utf8'));"
				"                if (pkg.main) {"
				"                    const main = path.resolve(p, pkg.main);"
				"                    if (fs.existsSync(main) && fs.statSync(main).isFile()) return main;"
				"                    const mainExt = tryExtensions(main);"
				"                    if (mainExt) return mainExt;"
				"                    const mainIndex = path.join(main, 'index.js');"
				"                    if (fs.existsSync(mainIndex)) return mainIndex;"
				"                }"
				"            } catch(e){}"
				"        }"
				"        const index = path.join(p, 'index.js');"
				"        if (fs.existsSync(index)) return index;"
				"    }"
				"    return null;"
				"};"
				"try {"
				"  const _gode_module = new Module('godot');"
				"  _gode_module.id = 'godot';"
				"  _gode_module.exports = gode;"
				"  _gode_module.loaded = true;"
				"  _gode_module.filename = 'godot';"
				"  Module._cache['godot'] = _gode_module;"
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
				"const originalJoin = path.join;"
				"const originalResolve = path.resolve;"
				"const originalDirname = path.dirname;"
				""
				"path.join = function(...args) {"
				"  if (args.length > 0 && isRes(args[0])) {"
				"      const mapped = args.map(a => isRes(a) ? '/' + a.substring(6) : a);"
				"      return fromPosix(path.posix.join(...mapped));"
				"  }"
				"  return originalJoin.apply(path, args);"
				"};"
				""
				"path.resolve = function(...args) {"
				"  if (args.length > 0 && isRes(args[0])) {"
				"      const mapped = args.map(a => isRes(a) ? '/' + a.substring(6) : a);"
				"      return fromPosix(path.posix.resolve(...mapped));"
				"  }"
				"  return originalResolve.apply(path, args);"
				"};"
				""
				"path.dirname = function(p) {"
				"  if (isRes(p)) {"
				"      return fromPosix(path.posix.dirname('/' + p.substring(6)));"
				"  }"
				"  return originalDirname.call(path, p);"
				"};"
				""
				"const originalNodeModulePaths = Module._nodeModulePaths;"
				"Module._nodeModulePaths = function(from) {"
				"    if (isRes(from)) {"
				"        const paths = [];"
				"        let p = from;"
				"        while (true) {"
				"            paths.push(p + (p.endsWith('/') ? '' : '/') + 'node_modules');"
				"            const parent = path.dirname(p);"
				"            if (parent === p) break;"
				"            if (p === 'res://') break;" 
				"            p = parent;"
				"        }"
				"        return paths;"
				"    }"
				"    return originalNodeModulePaths.call(Module, from);"
				"};"
				""
				"const originalFindPath = Module._findPath;"
				"Module._findPath = function(request, paths, isMain) {"
				"  if (request.startsWith('res://')) {"
				"      return request;"
				"  }"
				"  const resPaths = paths.filter(p => typeof p === 'string' && p.startsWith('res://'));"
				"  for (const base of resPaths) {"
				"      const found = findInRes(base, request);"
				"      if (found) return found;"
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
				"    return m.exports;"
				"  } catch (e) {"
				"    console.error('Gode compile error:', e);"
				"    return e;"
				"  }"
				"};"
				"const originalRequire = Module.prototype.require;"
				"Module.prototype.require = function(id) {"
				"  if (id === 'godot') {"
				"    return gode;"
				"  }"
				"  return originalRequire.call(this, id);"
				"};"
				"const originalGlobalRequire = globalThis.require || Module.createRequire(process.cwd());"
				"globalThis.require = function(id) {"
				""
				"  if (id === 'godot') {"
				"    return gode;"
				"  }"
				"  return originalGlobalRequire.call(this, id);"
				"};"
				"if (originalGlobalRequire) Object.assign(globalThis.require, originalGlobalRequire);"
                ""
                "if (gode.GDObject && gode.GDObject.prototype) {"
                "  gode.GDObject.prototype.toSignal = function(signal, { timeoutMs, abortSignal } = {}) {"
                "    const obj = this;"
                "    return new Promise((resolve, reject) => {"
                "      let done = false;"
                "      let timer = null;"
                "      const cleanup = () => {"
                "        if (timer) clearTimeout(timer);"
                "        if (abortSignal) abortSignal.removeEventListener('abort', onAbort);"
                "        try { obj.disconnect(signal, callback); } catch (_) {}"
                "      };"
                "      const callback = (...args) => {"
                "        if (done) return;"
                "        done = true;"
                "        cleanup();"
                "        resolve(args.length <= 1 ? args[0] : args);"
                "      };"
                "      const onAbort = () => {"
                "        if (done) return;"
                "        done = true;"
                "        cleanup();"
                "        reject(new Error('toSignal: aborted'));"
                "      };"
                "      try {"
                "        obj.connect(signal, callback);"
                "      } catch (e) {"
                "        cleanup();"
                "        return reject(e);"
                "      }"
                "      if (abortSignal) abortSignal.addEventListener('abort', onAbort, { once: true });"
                "      if (typeof timeoutMs === 'number' && timeoutMs > 0) {"
                "        timer = setTimeout(() => {"
                "          if (done) return;"
                "          done = true;"
                "          cleanup();"
                "          reject(new Error(`toSignal: timeout waiting for '${signal}'`));"
                "        }, timeoutMs);"
                "      }"
                "    });"
                "  };"
                "  gode.GDObject.prototype.to_signal = gode.GDObject.prototype.toSignal;"
                "}";

		node::LoadEnvironment(env, boot_script.c_str());

		// 事件循环跑一次，确保 boot_script 执行完毕
		uv_run(uv_default_loop(), UV_RUN_NOWAIT);

		// ESM 支持代码单独执行，确保在 boot_script 之后注册
		std::string esm_script =
				"(function() {"
				"const vm = require('vm');"
				"const fs = require('fs');"
				"const path = require('path');"
				""
				"global.__gode_esm_supported = (typeof vm.SourceTextModule !== 'undefined');"
				""
				"global.__gode_esm_cache = new Map();"
				"global.__gode_esm_pending = new Map();"
				""
				"global.__gode_load_esm = async function(filepath, source) {"
				"  if (!global.__gode_esm_supported) {"
				"    throw new Error('vm.SourceTextModule is not available');"
				"  }"
				"  if (global.__gode_esm_cache.has(filepath)) {"
				"    return global.__gode_esm_cache.get(filepath);"
				"  }"
				"  if (global.__gode_esm_pending.has(filepath)) {"
				"    return global.__gode_esm_pending.get(filepath);"
				"  }"
				"  const loadPromise = (async () => {"
				"    const module = new vm.SourceTextModule(source, {"
				"      identifier: filepath,"
				"      initializeImportMeta(meta) { meta.url = 'file://' + filepath; },"
				"      importModuleDynamically: async (specifier, referrer) => {"
				"        return await global.__gode_resolve_and_load(specifier, referrer.identifier);"
				"      }"
				"    });"
				"    await module.link(async (specifier, referencingModule) => {"
				"      return await global.__gode_resolve_and_load(specifier, referencingModule.identifier);"
				"    });"
				"    await module.evaluate();"
				"    const ns = module.namespace;"
				"    global.__gode_esm_cache.set(filepath, ns);"
				"    global.__gode_esm_pending.delete(filepath);"
				"    return ns;"
				"  })();"
				"  global.__gode_esm_pending.set(filepath, loadPromise);"
				"  return loadPromise;"
				"};"
				""
				"global.__gode_resolve_and_load = async function(specifier, referrerPath) {"
				"  let resolvedPath;"
				"  if (specifier.startsWith('./') || specifier.startsWith('../')) {"
				"    const dir = path.dirname(referrerPath);"
				"    resolvedPath = path.resolve(dir, specifier);"
				"    if (!fs.existsSync(resolvedPath)) {"
				"      for (const ext of ['.mjs', '.js', '.json']) {"
				"        if (fs.existsSync(resolvedPath + ext)) { resolvedPath = resolvedPath + ext; break; }"
				"      }"
				"    }"
				"  } else if (specifier.startsWith('res://')) {"
				"    resolvedPath = specifier;"
				"  } else {"
				"    let builtinMod;"
				"    try { builtinMod = require(specifier); } catch(e) {}"
				"    if (builtinMod !== undefined) {"
				"      const exportNames = Object.keys(builtinMod);"
				"      const names = exportNames.includes('default') ? exportNames : ['default', ...exportNames];"
				"      const syntheticModule = new vm.SyntheticModule(names,"
				"        function() {"
				"          this.setExport('default', builtinMod);"
				"          for (const key of exportNames) {"
				"            this.setExport(key, builtinMod[key]);"
				"          }"
				"        }, { identifier: specifier });"
				"      await syntheticModule.link(() => {});"
				"      await syntheticModule.evaluate();"
				"      return syntheticModule;"
				"    }"
				"    try {"
				"      resolvedPath = require.resolve(specifier, { paths: [path.dirname(referrerPath)] });"
				"    } catch (e) {"
				"      throw new Error(`Cannot find module '${specifier}' from '${referrerPath}'`);"
				"    }"
				"  }"
				"  const source = fs.readFileSync(resolvedPath, 'utf8');"
				"  const isESM = resolvedPath.endsWith('.mjs') ||"
				"    (resolvedPath.endsWith('.js') && /^\\s*(import|export)\\s+/m.test(source));"
				"  if (isESM) {"
				"    return await global.__gode_load_esm(resolvedPath, source);"
				"  } else {"
				"    const cjsModule = require(resolvedPath);"
				"    const exportNames = Object.keys(cjsModule);"
				"    const syntheticModule = new vm.SyntheticModule("
				"      exportNames.length > 0 ? exportNames : ['default'],"
				"      function() {"
				"        if (exportNames.length > 0) {"
				"          for (const key of exportNames) { this.setExport(key, cjsModule[key]); }"
				"        } else { this.setExport('default', cjsModule); }"
				"      }, { identifier: resolvedPath });"
				"    await syntheticModule.link(() => {});"
				"    await syntheticModule.evaluate();"
				"    return syntheticModule;"
				"  }"
				"};"
				""
				"global.__gode_compile_esm = async function(code, filename) {"
				"  try {"
				"    const ns = await global.__gode_load_esm(filename, code);"
				"    return ns;"
				"  } catch (e) {"
				"    return e;"
				"  }"
				"};"
				"})();";

		// 执行 ESM 支持脚本
		{
			v8::Local<v8::String> esm_source = v8::String::NewFromUtf8(
					isolate, esm_script.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
			v8::Local<v8::String> esm_name = v8::String::NewFromUtf8Literal(isolate, "<gode-esm-init>");
			v8::ScriptOrigin esm_origin(esm_name);
			v8::Local<v8::Script> esm_compiled;
			if (v8::Script::Compile(context, esm_source, &esm_origin).ToLocal(&esm_compiled)) {
				esm_compiled->Run(context);
			} else {
				// printf("[Gode ESM] Failed to compile ESM init script\n");
			}
		}

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

	// 检测是否为 ESM
	if (is_esm_file(filename, code)) {
		return compile_esm_module(code, filename);
	} else {
		return compile_cjs_module(code, filename);
	}
}

bool NodeRuntime::is_esm_file(const std::string &filename, const std::string &code) {
	// 1. 检查文件扩展名（最高优先级）
	if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".mjs") {
		return true;
	}
	if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".cjs") {
		return false;
	}

	// 2. 内容检测：如果有 CommonJS 特征，直接判定为 CJS
	if (code.find("module.exports") != std::string::npos ||
		code.find("exports.") != std::string::npos ||
		code.find("require(") != std::string::npos) {
		return false;
	}

	// 3. 内容检测：如果有 ESM 特征，判定为 ESM
	if (code.find("import ") != std::string::npos ||
		code.find("export ") != std::string::npos ||
		code.find("import{") != std::string::npos ||
		code.find("export{") != std::string::npos ||
		code.find("export default") != std::string::npos) {
		return true;
	}

	// 4. 对于 .js 文件，检查 package.json 的 "type" 字段
	if (filename.size() >= 3 && filename.substr(filename.size() - 3) == ".js") {
		std::string dir = filename;
		size_t last_slash = dir.find_last_of("/\\");
		if (last_slash != std::string::npos) {
			dir = dir.substr(0, last_slash);
		}

		while (!dir.empty()) {
			std::string pkg_path = dir + "/package.json";

			godot::String gd_pkg_path = godot::String::utf8(pkg_path.c_str());
			if (godot::FileAccess::file_exists(gd_pkg_path)) {
				godot::Ref<godot::FileAccess> file = godot::FileAccess::open(gd_pkg_path, godot::FileAccess::READ);
				if (file.is_valid()) {
					godot::String content = file->get_as_text();
					std::string json_str = content.utf8().get_data();

					size_t type_pos = json_str.find("\"type\"");
					if (type_pos != std::string::npos) {
						size_t colon_pos = json_str.find(":", type_pos);
						if (colon_pos != std::string::npos) {
							size_t value_start = json_str.find_first_not_of(" \t\n\r", colon_pos + 1);
							if (value_start != std::string::npos) {
								if (json_str.substr(value_start, 8) == "\"module\"") {
									return true;
								} else if (json_str.substr(value_start, 12) == "\"commonjs\"") {
									return false;
								}
							}
						}
					}
				}
			}

			size_t parent_slash = dir.find_last_of("/\\");
			if (parent_slash != std::string::npos && parent_slash > 0) {
				dir = dir.substr(0, parent_slash);
			} else {
				break;
			}
		}
	}

	// 5. 默认为 CommonJS
	return false;
}

Napi::Value NodeRuntime::compile_esm_module(const std::string &code, const std::string &filename) {
	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);
	v8::EscapableHandleScope escapable_scope(isolate);

	v8::Local<v8::String> fn_name = v8::String::NewFromUtf8Literal(isolate, "__gode_compile_esm");
	v8::Local<v8::Value> fn_val;
	if (!context->Global()->Get(context, fn_name).ToLocal(&fn_val) || !fn_val->IsFunction()) {
		godot::UtilityFunctions::print("compile_esm_module: __gode_compile_esm not found");
		return Napi::Value();
	}

	v8::Local<v8::Function> fn = fn_val.As<v8::Function>();

	v8::Local<v8::Value> args[] = {
		v8::String::NewFromUtf8(isolate, code.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
		v8::String::NewFromUtf8(isolate, filename.c_str(), v8::NewStringType::kNormal).ToLocalChecked()
	};

	v8::MaybeLocal<v8::Value> result = fn->Call(context, context->Global(), 2, args);

	if (result.IsEmpty()) {
		godot::UtilityFunctions::print("compile_esm_module: fn->Call returned empty");
		return Napi::Value();
	}

	v8::Local<v8::Value> promise_val = result.ToLocalChecked();

	// 检查是否是 Promise
	if (!promise_val->IsPromise()) {
		godot::UtilityFunctions::print("compile_esm_module: result is not a Promise");
		return Napi::Value();
	}

	v8::Local<v8::Promise> promise = promise_val.As<v8::Promise>();

	// 等待 Promise 完成（需要运行事件循环）
	int max_iterations = 1000; // 降低最大迭代次数
	int iterations = 0;

	while (promise->State() == v8::Promise::kPending && iterations < max_iterations) {
		// 使用 UV_RUN_NOWAIT 并添加微任务处理
		isolate->PerformMicrotaskCheckpoint();
		uv_run(uv_default_loop(), UV_RUN_NOWAIT);
		iterations++;

		// 每 100 次迭代打印一次调试信息
		if (iterations % 100 == 0) {
			godot::UtilityFunctions::print("compile_esm_module: Still waiting... iteration ", iterations);
		}
	}

	if (iterations >= max_iterations) {
		godot::UtilityFunctions::print("compile_esm_module: Timeout waiting for Promise after ", iterations, " iterations");
		return Napi::Value();
	}

	if (promise->State() == v8::Promise::kRejected) {
		v8::Local<v8::Value> error = promise->Result();

		// 尝试获取错误信息
		v8::String::Utf8Value error_str(isolate, error);
		godot::UtilityFunctions::print("compile_esm_module: Promise rejected: ", *error_str);
		return Napi::Value();
	}

	v8::Local<v8::Value> final_exports = promise->Result();

	if (final_exports->IsUndefined()) {
		v8::Local<v8::Value> undefined_val = v8::Undefined(isolate);
		return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*escapable_scope.Escape(undefined_val)));
	}

	return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*escapable_scope.Escape(final_exports)));
}

Napi::Value NodeRuntime::compile_cjs_module(const std::string &code, const std::string &filename) {
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

	if (result.IsEmpty()) {
		godot::UtilityFunctions::print("compile_cjs_module: fn->Call returned empty (exception?)");
		return Napi::Value();
	}

	v8::Local<v8::Value> final_exports = result.ToLocalChecked();

	if (final_exports->IsUndefined()) {
		v8::Local<v8::Value> undefined_val = v8::Undefined(isolate);
		return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*escapable_scope.Escape(undefined_val)));
	}

	return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*escapable_scope.Escape(final_exports)));
}

Napi::Function NodeRuntime::get_default_class(Napi::Value module_exports) {
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
		v8::Local<v8::Object> export_obj = reinterpret_cast<v8::Value *>(static_cast<napi_value>(default_export))->ToObject(context).ToLocalChecked();
		v8::Local<v8::Function> export_func = export_obj.As<v8::Function>();
		if (default_export.IsFunction()) {
			v8::Local<v8::Value> escaped_val = escapable_scope.Escape(export_func);
			return Napi::Value(JsEnvManager::get_env(), reinterpret_cast<napi_value>(*escaped_val)).As<Napi::Function>();
		}
	}

	return Napi::Function();
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

	uv_run(uv_default_loop(), UV_RUN_NOWAIT);
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
