#include "utils/node_runtime.h"
#include "register_builtin.gen.h"
#include "register_classes.gen.h"
#include "utility_functions/utility_functions.h"

#include <node.h>
#include <node_api.h>
#include <uv.h>
#ifdef WIN32
#undef CONNECT_DEFERRED
#endif

#include "utils/value_convert.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <memory>
#include <string>
#include <vector>
#ifdef WIN32
#include <windows.h>
#endif

namespace gode {

static bool node_initialized = false;
static std::unique_ptr<node::MultiIsolatePlatform> platform;
static std::unique_ptr<node::ArrayBufferAllocator> allocator;
static node::IsolateData *isolate_data = nullptr;
static thread_local napi_env thread_local_env = nullptr;

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

static Napi::Value Export(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();
	return env.Undefined();
}

// 预加载目录中的所有 .dll，确保依赖链在 llama-addon.node 加载前已解析
// 同时将 Release 目录和 CUDA bin 目录加入 PATH，确保传递依赖（如 cudart64_*.dll）可被找到
static Napi::Value preload_dlls(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();
#ifdef WIN32
	if (info.Length() < 1 || !info[0].IsString()) {
		return env.Undefined();
	}
	std::string dir_utf8 = info[0].As<Napi::String>().Utf8Value();

	auto to_wide = [](const std::string &s) -> std::wstring {
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
		std::wstring w(n, 0);
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
		return w;
	};

	// 把 Release 目录加到 PATH（影响 LOAD_WITH_ALTERED_SEARCH_PATH 的传递依赖搜索）
	std::string dirs_to_add = dir_utf8;

	// 找到 CUDA_PATH 并把 bin 加入 PATH（cudart64_*.dll、cublas64_*.dll 等）
	char cuda_buf[4096] = {};
	if (GetEnvironmentVariableA("CUDA_PATH", cuda_buf, sizeof(cuda_buf)) > 0) {
		dirs_to_add += ";" + std::string(cuda_buf) + "\\bin";
	}
	// 同时尝试常见的版本号变体（CUDA_PATH_V12_0 等）
	for (const char *varname : {"CUDA_PATH_V12_6", "CUDA_PATH_V12_5", "CUDA_PATH_V12_4",
			"CUDA_PATH_V12_3", "CUDA_PATH_V12_2", "CUDA_PATH_V12_1", "CUDA_PATH_V12_0",
			"CUDA_PATH_V11_8", "CUDA_PATH_V11_7"}) {
		char buf[4096] = {};
		if (GetEnvironmentVariableA(varname, buf, sizeof(buf)) > 0) {
			dirs_to_add += ";" + std::string(buf) + "\\bin";
			break;
		}
	}

	// 更新 PATH
	char path_buf[32767] = {};
	GetEnvironmentVariableA("PATH", path_buf, sizeof(path_buf));
	std::string new_path = dirs_to_add + ";" + std::string(path_buf);
	SetEnvironmentVariableA("PATH", new_path.c_str());

	// SetDllDirectory 让后续所有 LoadLibraryExW 都能搜索 Release 目录（包括传递依赖）
	SetDllDirectoryW(to_wide(dir_utf8).c_str());

	// 预加载 Release 目录中的每个 .dll
	std::wstring wdir = to_wide(dir_utf8);
	std::wstring pattern = wdir + L"\\*.dll";
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			std::wstring dll_path = wdir + L"\\" + fd.cFileName;
			HMODULE hm = LoadLibraryExW(dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (!hm) LoadLibraryExW(dll_path.c_str(), nullptr, 0);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
#endif
	return env.Undefined();
}

#ifdef WIN32
// win_delay_load_hook.cc（由 cmake-js 编译进每个 NAPI addon）在 dliStartProcessing 时
// 调用 GetModuleHandleA("node.dll")。若找不到则 fallback 到 GetModuleHandle(NULL)
// 即 GodotEngine.exe，而 GodotEngine.exe 不导出 NAPI 函数 → 崩溃。
// 修复：加载与 libnode.dll 同目录的 node.dll（由构建系统生成的纯转发存根 DLL），
// 使 GetModuleHandleA("node.dll") 命中，GetProcAddress 返回正确的 NAPI 地址。
static void preload_gode_node_exe() {
	HMODULE libnode = GetModuleHandleW(L"libnode.dll");
	if (!libnode) return;
	wchar_t libnode_path[MAX_PATH];
	if (!GetModuleFileNameW(libnode, libnode_path, MAX_PATH)) return;
	wchar_t node_dll_path[MAX_PATH];
	wcscpy_s(node_dll_path, MAX_PATH, libnode_path);
	wchar_t *sep = wcsrchr(node_dll_path, L'\\');
	if (!sep) return;
	wcscpy_s(sep + 1, MAX_PATH - (DWORD)(sep + 1 - node_dll_path), L"node.dll");
	// 加载纯转发存根 node.dll（无 V8 代码，所有导出 forward 到 libnode）
	LoadLibraryW(node_dll_path);
}
#endif

static Napi::Object InitGodeAddon(Napi::Env env, Napi::Object exports) {
#ifdef WIN32
	preload_gode_node_exe();
#endif
	thread_local_env = env;
	gode::register_builtin(env, exports);
	gode::register_classes(env, exports);
	gode::GD::init(env, exports);

	exports.Set("fs_readFile", Napi::Function::New(env, fs_readFile));
	exports.Set("fs_stat", Napi::Function::New(env, fs_stat));
	exports.Set("preload_dlls", Napi::Function::New(env, preload_dlls));

	Napi::Object global = env.Global();
	global.Set("Export", Napi::Function::New(env, Export));
	global.Set("Signal", Napi::Function::New(env, Export)); // @Signal 装饰器，运行时为空操作，由 tree-sitter 静态解析
	global.Set("Tool", Napi::Function::New(env, Export));   // @Tool 装饰器，运行时为空操作，由 tree-sitter 静态解析

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
				"      const base = path.resolve(parentDir, request);"
				"      if (fs.existsSync(base) && fs.statSync(base).isFile()) return base;"
				"      for (const ext of ['.js', '.json', '.node']) {"
				"        if (fs.existsSync(base + ext)) return base + ext;"
				"      }"
				"      for (const idx of ['/index.js', '/index.json']) {"
				"        if (fs.existsSync(base + idx)) return base + idx;"
				"      }"
				"      return base;"
				"    }"
				"    try {"
				"      return originalResolveFilename.call(Module, request, parent, isMain, options);"
				"    } catch(e) {"
				"      const found = findInRes('res://node_modules', request);"
				"      if (found) return found;"
				"      throw e;"
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
				// patch process.dlopen：.node 原生模块必须用真实 OS 路径加载
				// res://xxx 路径传给 LoadLibraryW 会导致地址错误和 NAPI init 崩溃
				// 加载 .node 前将其目录追加到 PATH，让 Windows 能找到同目录的 DLL（如 ggml-cuda.dll）
				"const _originalDlopen = process.dlopen;"
				"process.dlopen = function(mod, filename, flags) {"
				"  let realPath = filename;"
				"  if (typeof filename === 'string') {"
				"    let p = filename;"
				"    if (p.startsWith('file://')) { try { p = require('url').fileURLToPath(p); } catch(_) {} }"  // file:// → OS path
				"    if (p.startsWith('res://')) { p = require('path').join(process.cwd(), p.slice(6)); }"
				"    if (p.startsWith('\\\\\\\\?\\\\')) p = p.slice(4);"  // 剥离 \\?\ 前缀
				"    realPath = p;"
				"  }"
				"  if (typeof realPath === 'string' && realPath.endsWith('.node') && typeof gode.preload_dlls === 'function') {"
				"    try { gode.preload_dlls(require('path').dirname(realPath)); } catch(_) {}"
				"  }"
				"  return _originalDlopen(mod, realPath, flags);"
				"};"
				""
				"if (gode.GDObject && gode.GDObject.prototype) {"
				"  gode.GDObject.prototype.to_signal = function(signal, { timeoutMs, abortSignal } = {}) {"
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
				"        reject(new Error('to_signal: aborted'));"
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
				"          reject(new Error(`to_signal: timeout waiting for '${signal}'`));"
				"        }, timeoutMs);"
				"      }"
				"    });"
				"  };"
				"}"
				// patch child_process.fork：拦截 testBindingBinary 的 fork，用本进程内模拟成功，
				// 避免在 Windows 上 fork 出一个 GodotEngine.exe 子进程来做 addon 兼容性测试。
				";"
				"(function() {"
				"  try {"
				"    const cp = require('child_process');"
				"    const _origFork = cp.fork;"
				"    cp.fork = function(modulePath, args, options) {"
				"      if (typeof modulePath === 'string' && modulePath.includes('testBindingBinary')) {"
				"        const { EventEmitter } = require('events');"
				"        const mock = new EventEmitter();"
				"        mock.pid = 0; mock.exitCode = null; mock.killed = false;"
				"        mock.stdout = null; mock.stderr = null;"
				"        mock.kill = function() {};"
				"        mock.send = function(msg) {"
				"          const self = this;"
				"          if (msg && msg.type === 'start') {"
				"            process.nextTick(() => self.emit('message', { type: 'loaded' }));"
				"          } else if (msg && msg.type === 'test') {"
				"            process.nextTick(() => self.emit('message', { type: 'done' }));"
				"          } else if (msg && msg.type === 'exit') {"
				"            process.nextTick(() => { mock.exitCode = 0; self.emit('exit', 0, null); });"
				"          }"
				"          return true;"
				"        };"
				"        process.nextTick(() => mock.emit('message', { type: 'ready' }));"
				"        return mock;"
				"      }"
				"      return _origFork.apply(cp, arguments);"
				"    };"
				"  } catch(e) {}"
				"})();";

		node::LoadEnvironment(env, boot_script.c_str());

		// 事件循环跑一次，确保 boot_script 执行完毕
		isolate->PerformMicrotaskCheckpoint();
		uv_run(uv_default_loop(), UV_RUN_ONCE);

		// ESM 支持代码单独执行，确保在 boot_script 之后注册
		std::string esm_script =
				"(function() {"
				"const vm = require('vm');"
				"const fs = require('fs');"
				"const path = require('path');"
				""
				"global.__gode_esm_supported = (typeof vm.SourceTextModule !== 'undefined');"
				""
				"global.__gode_esm_cache = new Map();"       // filepath -> namespace (fully evaluated)
				"global.__gode_esm_pending = new Map();"     // filepath -> Promise<namespace>
				"global.__gode_esm_mod_cache = new Map();"   // filepath/specifier -> vm.Module (for linker)
				"global.__gode_cjs_pending = new Map();"     // resolvedPath -> Promise<SyntheticModule> (dedup)
				""
				// __gode_resolve_to_module: 将 specifier 解析为 vm.Module。
				// 关键：对 ESM 文件只创建 SourceTextModule 并返回，不调用 link()。
				// Node.js 的 [kLink] 机制会异步递归地对返回的模块调用同一个 linker，
				// 从而避免同步递归导致的调用栈溢出。
				// 将 res:// 路径转为 file:// URL，供 import.meta.url 使用
				// fileURLToPath() 只认 file:// scheme，所以必须转换
				"const _gode_res_to_file_url = (p) => {"
				"  if (!p || !p.startsWith('res://')) return p;"
				"  const rel = p.slice(6);"  // 去掉 'res://'
				"  const abs = require('path').join(process.cwd(), rel).replace(/\\\\/g, '/');"
				"  return 'file:///' + abs;"
				"};"
				""
				// 在 res://node_modules/ 里手动查找包入口，用于 require.resolve 无法处理 res:// 路径时的兜底
				"function __gode_resolve_in_res(specifier, referrerPath) {"
				"  const parts = specifier.split('/');"
				"  const pkgName = parts[0].startsWith('@') ? parts[0] + '/' + parts[1] : parts[0];"
				"  const subPath = parts.slice(pkgName.split('/').length).join('/');"
				"  let searchDirs = [];"
				"  let d = path.dirname(referrerPath);"
				"  while (true) {"
				"    searchDirs.push(path.join(d, 'node_modules'));"
				"    const p = path.dirname(d); if (p === d) break; d = p;"
				"  }"
				"  for (const base of searchDirs) {"
				"    const pkgDir = path.join(base, pkgName);"
				"    if (!fs.existsSync(pkgDir)) continue;"
				"    if (subPath) {"
				"      const direct = path.join(pkgDir, subPath);"
				"      if (fs.existsSync(direct) && fs.statSync(direct).isFile()) return direct;"
				"      for (const ext of ['.js', '.mjs', '.json']) {"
				"        if (fs.existsSync(direct + ext)) return direct + ext;"
				"      }"
				"      return null;"
				"    }"
				"    const pkgJsonPath = path.join(pkgDir, 'package.json');"
				"    if (!fs.existsSync(pkgJsonPath)) continue;"
				"    let pkg; try { pkg = JSON.parse(fs.readFileSync(pkgJsonPath, 'utf8')); } catch(_) { continue; }"
				"    const exp = pkg.exports;"
				"    if (exp) {"
				"      const dot = exp['.'];"
				"      const entry = typeof dot === 'string' ? dot"
				"        : (dot && (dot.import || dot.module || dot.require || dot.default))"
				"        || (typeof exp === 'string' ? exp : null);"
				"      if (entry) { const r = path.resolve(pkgDir, entry); if (fs.existsSync(r)) return r; }"
				"    }"
				"    const main = pkg.module || pkg.main || 'index.js';"
				"    const r = path.resolve(pkgDir, main);"
				"    if (fs.existsSync(r)) return r;"
				"    for (const ext of ['.js', '.mjs']) {"
				"      if (fs.existsSync(r + ext)) return r + ext;"
				"    }"
				"  }"
				"  return null;"
				"}"
				""
				// 解析 package.json imports 字段中的 #subpath 条目
				"function __gode_resolve_pkg_import(specifier, referrerPath) {"
				"  let dir = path.dirname(referrerPath);"
				"  while (true) {"
				"    const pkgPath = path.join(dir, 'package.json');"
				"    if (fs.existsSync(pkgPath)) {"
				"      try {"
				"        const pkg = JSON.parse(fs.readFileSync(pkgPath, 'utf8'));"
				"        if (pkg.imports) {"
				"          const entry = pkg.imports[specifier];"
				"          if (entry) {"
				"            const target = typeof entry === 'string' ? entry"
				"              : (entry.node || entry.require || entry.default || null);"
				"            if (target) return path.resolve(dir, target);"
				"          }"
				"        }"
				"      } catch(_) {}"
				"    }"
				"    const parent = path.dirname(dir);"
				"    if (parent === dir) break;"
				"    dir = parent;"
				"  }"
				"  return null;"
				"}"
				""
				"global.__gode_resolve_to_module = async function(specifier, referrerPath) {"
				"  let resolvedPath;"
				"  if (specifier.startsWith('#')) {"
				"    const pkgImport = __gode_resolve_pkg_import(specifier, referrerPath);"
				"    if (!pkgImport) throw new Error(`Cannot find module '${specifier}' from '${referrerPath}'`);"
				"    resolvedPath = pkgImport;"
				"  } else if (specifier.startsWith('./') || specifier.startsWith('../')) {"
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
				"    let _cjsMod; try { _cjsMod = require(specifier); } catch(_) {}"
				"    if (_cjsMod !== undefined) {"
				"      if (global.__gode_esm_mod_cache.has(specifier)) return global.__gode_esm_mod_cache.get(specifier);"
				"      const exportNames = Object.keys(_cjsMod);"
				"      const names = exportNames.includes('default') ? exportNames : ['default', ...exportNames];"
				"      const synMod = new vm.SyntheticModule(names, function() {"
				"        this.setExport('default', _cjsMod);"
				"        for (const key of exportNames) { this.setExport(key, _cjsMod[key]); }"
				"      }, { identifier: specifier });"
				"      await synMod.link(() => {});"
				"      await synMod.evaluate();"
				"      global.__gode_esm_mod_cache.set(specifier, synMod);"
				"      return synMod;"
				"    }"
				"    try {"
				"      resolvedPath = require.resolve(specifier, { paths: [path.dirname(referrerPath)] });"
				"    } catch(e) {"
				"      resolvedPath = __gode_resolve_in_res(specifier, referrerPath);"
				"      if (!resolvedPath) {"
				"        console.error('[gode] resolve failed: ' + specifier + ' from ' + referrerPath);"
				"        throw new Error(`Cannot find module '${specifier}' from '${referrerPath}'`);"
				"      }"
				"    }"
				"  }"
				"  if (global.__gode_esm_mod_cache.has(resolvedPath)) {"
				"    return global.__gode_esm_mod_cache.get(resolvedPath);"
				"  }"
				"  let source;"
				"  try {"
				"    source = fs.readFileSync(resolvedPath, 'utf8');"
				"  } catch(e) {"
				"    console.error('[gode] readFileSync failed: ' + resolvedPath + ' => ' + e.message);"
				"    throw e;"
				"  }"
				"  const isESM = resolvedPath.endsWith('.mjs') ||"
				"    (resolvedPath.endsWith('.js') && /^\\s*(import|export)\\s+/m.test(source));"
				"  if (isESM) {"
				"    let mod;"
				"    try {"
				"      mod = new vm.SourceTextModule(source, {"
				"        identifier: resolvedPath,"
				"        initializeImportMeta(meta) { meta.url = _gode_res_to_file_url(resolvedPath); },"
				"        importModuleDynamically: async (spec, ref) => {"
				"          return await global.__gode_resolve_to_module(spec, ref.identifier);"
				"        }"
				"      });"
				"    } catch(e) {"
				"      console.error('[gode] SourceTextModule parse error in ' + resolvedPath + ': ' + e.message);"
				"      throw e;"
				"    }"
				"    global.__gode_esm_mod_cache.set(resolvedPath, mod);"
				"    return mod;"
				"  } else {"
				"    if (global.__gode_cjs_pending.has(resolvedPath)) return global.__gode_cjs_pending.get(resolvedPath);"
				"    const cjsPromise = (async () => {"
				"      let cjsModule;"
				"      try {"
				"        cjsModule = require(resolvedPath);"
				"      } catch(e) {"
				"        console.error('[gode] require failed: ' + resolvedPath + ' => ' + e.message);"
				"        throw e;"
				"      }"
				"      const expNames = Object.keys(cjsModule);"
				"      const allNames = expNames.includes('default') ? expNames : ['default', ...expNames];"
				"      const synMod = new vm.SyntheticModule(allNames,"
				"        function() {"
				"          this.setExport('default', cjsModule);"
				"          for (const key of expNames) { this.setExport(key, cjsModule[key]); }"
				"        }, { identifier: resolvedPath });"
				"      await synMod.link(() => {});"
				"      await synMod.evaluate();"
				"      global.__gode_esm_mod_cache.set(resolvedPath, synMod);"
				"      global.__gode_cjs_pending.delete(resolvedPath);"
				"      return synMod;"
				"    })();"
				"    global.__gode_cjs_pending.set(resolvedPath, cjsPromise);"
				"    return cjsPromise;"
				"  }"
				"};"
				""
				"global.__gode_load_esm = async function(filepath, source) {"
				"  if (!global.__gode_esm_supported) {"
				"    throw new Error('vm.SourceTextModule is not available');"
				"  }"
				"  if (global.__gode_esm_cache.has(filepath)) { return global.__gode_esm_cache.get(filepath); }"
				"  if (global.__gode_esm_pending.has(filepath)) { return global.__gode_esm_pending.get(filepath); }"
				"  const loadPromise = (async () => {"
				"    const module = new vm.SourceTextModule(source, {"
				"      identifier: filepath,"
				"      initializeImportMeta(meta) { meta.url = _gode_res_to_file_url(filepath); },"
				"      importModuleDynamically: async (specifier, referrer) => {"
				"        return await global.__gode_resolve_to_module(specifier, referrer.identifier);"
				"      }"
				"    });"
				"    global.__gode_esm_mod_cache.set(filepath, module);"
				"    try {"
				"      await module.link(async (specifier, referencingModule) => {"
				"        return await global.__gode_resolve_to_module(specifier, referencingModule.identifier);"
				"      });"
				"    } catch(e) {"
				"      console.error('[gode] link error in ' + filepath + ': ' + e.message);"
				"      if (e.stack) console.error(e.stack);"
				"      throw e;"
				"    }"
				"    try {"
				"      await module.evaluate();"
				"    } catch(e) {"
				"      console.error('[gode] evaluate error in ' + filepath + ': ' + e.message);"
				"      if (e.stack) console.error(e.stack);"
				"      throw e;"
				"    }"
				"    const ns = module.namespace;"
				"    global.__gode_esm_cache.set(filepath, ns);"
				"    global.__gode_esm_pending.delete(filepath);"
				"    return ns;"
				"  })();"
				"  global.__gode_esm_pending.set(filepath, loadPromise);"
				"  return loadPromise;"
				"};"
				""
				"global.__gode_compile_esm = async function(code, filename) {"
				"  try {"
				"    const ns = await global.__gode_load_esm(filename, code);"
				"    return ns;"
				"  } catch (e) {"
				"    console.error('[gode] compile_esm FAILED: ' + filename + ' => ' + (e && e.message));"
				"    if (e && e.stack) console.error(e.stack);"
				"    return e;"
				"  }"
				"};"
				"})();";

		// 执行 ESM 支持脚本
		{
			v8::Local<v8::String> esm_source = v8::String::NewFromUtf8(
					isolate, esm_script.c_str(), v8::NewStringType::kNormal)
													   .ToLocalChecked();
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

	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);

	// 检测是否为 ESM
	v8::Local<v8::Value> result;
	if (is_esm_file(filename, code)) {
		result = compile_esm_module(code, filename);
	} else {
		result = compile_cjs_module(code, filename);
	}

	if (result.IsEmpty()) {
		return Napi::Value();
	}

	// 使用thread_local_env来转换v8值为Napi::Value
	if (thread_local_env == nullptr) {
		godot::UtilityFunctions::printerr("[compile_script] Error: thread_local_env is not set");
		return Napi::Value();
	}

	return Napi::Value(thread_local_env, reinterpret_cast<napi_value>(*result));
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
					godot::Dictionary json = static_cast<godot::Dictionary>(godot::JSON::parse_string(content));
					if (json["type"] == "module") {
						return true;
					} else if (json["type"] == "commonjs") {
						return false;
					}
				}
			}
		}
	}

	// 5. 默认为 CommonJS
	return false;
}

bool NodeRuntime::is_esm_file() {
	godot::String gd_pkg_path = godot::String::utf8("res://package.json");
	if (godot::FileAccess::file_exists(gd_pkg_path)) {
		godot::Ref<godot::FileAccess> file = godot::FileAccess::open(gd_pkg_path, godot::FileAccess::READ);
		if (file.is_valid()) {
			godot::String content = file->get_as_text();
			godot::Dictionary json = static_cast<godot::Dictionary>(godot::JSON::parse_string(content));
			if (static_cast<godot::String>(json["type"]) == "module") {
				return true;
			} else if (static_cast<godot::String>(json["type"]) == "commonjs") {
				return false;
			}
		}
	}
	return false;
}

v8::Local<v8::Value> NodeRuntime::compile_esm_module(const std::string &code, const std::string &filename) {
	v8::Local<v8::Context> context = node_context.Get(isolate);
	v8::Context::Scope context_scope(context);
	v8::EscapableHandleScope escapable_scope(isolate);

	v8::Local<v8::String> fn_name = v8::String::NewFromUtf8Literal(isolate, "__gode_compile_esm");
	v8::Local<v8::Value> fn_val;
	if (!context->Global()->Get(context, fn_name).ToLocal(&fn_val) || !fn_val->IsFunction()) {
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
		godot::UtilityFunctions::print("compile_esm_module: fn->Call returned empty");
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Value> promise_val = result.ToLocalChecked();

	// 检查是否是 Promise
	if (!promise_val->IsPromise()) {
		godot::UtilityFunctions::print("compile_esm_module: result is not a Promise");
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Promise> promise = promise_val.As<v8::Promise>();

	// ESM 加载涉及大量异步操作，循环驱动事件循环直到 Promise settled
	while (promise->State() == v8::Promise::kPending) {
		isolate->PerformMicrotaskCheckpoint();
		uv_run(uv_default_loop(), UV_RUN_ONCE);
		if (promise->State() != v8::Promise::kPending) break;
	}

	if (promise->State() == v8::Promise::kRejected) {
		v8::Local<v8::Value> error = promise->Result();
		v8::String::Utf8Value error_str(isolate, error);
		godot::UtilityFunctions::print("compile_esm_module: Promise rejected: ", *error_str);
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
	if (!context->Global()->Get(context, fn_name).ToLocal(&fn_val) || !fn_val->IsFunction()) {
		return v8::Local<v8::Value>();
	}

	v8::Local<v8::Function> fn = fn_val.As<v8::Function>();

	v8::Local<v8::Value> args[] = {
		v8::String::NewFromUtf8(isolate, code.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
		v8::String::NewFromUtf8(isolate, filename.c_str(), v8::NewStringType::kNormal).ToLocalChecked()
	};

	v8::MaybeLocal<v8::Value> result = fn->Call(context, context->Global(), 2, args);

	if (result.IsEmpty()) {
		godot::UtilityFunctions::print("compile_cjs_module: fn->Call returned empty (exception?)");
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

	// 直接是函数，返回
	if (module_exports.IsFunction()) {
		return module_exports.As<Napi::Function>();
	}

	// 尝试获取 default 导出
	if (module_exports.IsObject()) {
		Napi::Object exports_obj = module_exports.As<Napi::Object>();
		if (exports_obj.Has("default")) {
			Napi::Value default_export = exports_obj.Get("default");
			if (default_export.IsFunction()) {
				return default_export.As<Napi::Function>();
			}
		}
	}

	return Napi::Function();
}

godot::Variant NodeRuntime::eval_expression(const std::string &expr) {
	if (!node_initialized || !env) {
		return godot::Variant();
	}

	std::string code = "(function() { return " + expr + "; })()";

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
	return napi_to_godot(napi_result);
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

	platform.reset();
	allocator.reset();

	node_initialized = false;
}

} // namespace gode
