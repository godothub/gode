#include "runtime/node_godot_bridge.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include <cstdint>
#include <string>

#ifdef WIN32
#include <windows.h>
#endif

namespace gode::node_runtime_bridge {

static bool is_godot_path(const std::string &path) {
	return path.find("res://") == 0 || path.find("user://") == 0;
}

static Napi::Value fs_readFile(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsString()) {
		return env.Null();
	}
	std::string path = info[0].As<Napi::String>().Utf8Value();
	if (!is_godot_path(path)) {
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
	if (!is_godot_path(path)) {
		return Napi::Number::New(env, 0);
	}

	godot::String gd_path = godot::String::utf8(path.c_str());
	if (godot::FileAccess::file_exists(gd_path)) {
		return Napi::Number::New(env, 1);
	}
	if (godot::DirAccess::dir_exists_absolute(gd_path)) {
		return Napi::Number::New(env, 2);
	}
	return Napi::Number::New(env, 0);
}

static Napi::Value noop_decorator(const Napi::CallbackInfo &info) {
	return info.Env().Undefined();
}

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

	std::string dirs_to_add = dir_utf8;

	char cuda_buf[4096] = {};
	if (GetEnvironmentVariableA("CUDA_PATH", cuda_buf, sizeof(cuda_buf)) > 0) {
		dirs_to_add += ";" + std::string(cuda_buf) + "\\bin";
	}

	for (const char *varname : { "CUDA_PATH_V12_6", "CUDA_PATH_V12_5", "CUDA_PATH_V12_4",
				 "CUDA_PATH_V12_3", "CUDA_PATH_V12_2", "CUDA_PATH_V12_1", "CUDA_PATH_V12_0",
				 "CUDA_PATH_V11_8", "CUDA_PATH_V11_7" }) {
		char buf[4096] = {};
		if (GetEnvironmentVariableA(varname, buf, sizeof(buf)) > 0) {
			dirs_to_add += ";" + std::string(buf) + "\\bin";
			break;
		}
	}

	char path_buf[32767] = {};
	GetEnvironmentVariableA("PATH", path_buf, sizeof(path_buf));
	std::string new_path = dirs_to_add + ";" + std::string(path_buf);
	SetEnvironmentVariableA("PATH", new_path.c_str());

	SetDllDirectoryW(to_wide(dir_utf8).c_str());

	std::wstring wdir = to_wide(dir_utf8);
	std::wstring pattern = wdir + L"\\*.dll";
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			std::wstring dll_path = wdir + L"\\" + fd.cFileName;
			HMODULE hm = LoadLibraryExW(dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (!hm) {
				LoadLibraryExW(dll_path.c_str(), nullptr, 0);
			}
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
#endif
	return env.Undefined();
}

void preload_node_dll_stub() {
#ifdef WIN32
	HMODULE libnode = GetModuleHandleW(L"libnode.dll");
	if (!libnode) {
		return;
	}
	wchar_t libnode_path[MAX_PATH];
	if (!GetModuleFileNameW(libnode, libnode_path, MAX_PATH)) {
		return;
	}
	wchar_t node_dll_path[MAX_PATH];
	wcscpy_s(node_dll_path, MAX_PATH, libnode_path);
	wchar_t *sep = wcsrchr(node_dll_path, L'\\');
	if (!sep) {
		return;
	}
	wcscpy_s(sep + 1, MAX_PATH - (DWORD)(sep + 1 - node_dll_path), L"node.dll");
	LoadLibraryW(node_dll_path);
#endif
}

void install_exports(Napi::Env env, Napi::Object exports) {
	exports.Set("fs_readFile", Napi::Function::New(env, fs_readFile));
	exports.Set("fs_stat", Napi::Function::New(env, fs_stat));
	exports.Set("preload_dlls", Napi::Function::New(env, preload_dlls));
}

void install_global_decorators(Napi::Env env) {
	Napi::Object global = env.Global();
	global.Set("Export", Napi::Function::New(env, noop_decorator));
	global.Set("Signal", Napi::Function::New(env, noop_decorator));
	global.Set("Tool", Napi::Function::New(env, noop_decorator));
}

} // namespace gode::node_runtime_bridge
