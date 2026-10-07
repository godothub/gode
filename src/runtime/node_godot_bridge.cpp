#include "runtime/node_godot_bridge.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cstdlib>
#include <cstdint>
#include <limits.h>
#include <string>

#ifdef WIN32
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef WIN32
static std::wstring get_module_file_name(HMODULE module) {
	std::wstring path(MAX_PATH, L'\0');
	for (;;) {
		DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0) {
			return {};
		}
		if (length < path.size()) {
			path.resize(length);
			return path;
		}
		path.resize(path.size() * 2);
	}
}

static std::string wide_to_utf8(const std::wstring &value) {
	if (value.empty()) {
		return {};
	}
	int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (size <= 0) {
		return {};
	}
	std::string out(static_cast<size_t>(size - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), size, nullptr, nullptr);
	return out;
}
#elif defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>

static std::string get_module_file_name(void *symbol) {
	Dl_info info = {};
	if (dladdr(symbol, &info) == 0 || !info.dli_fname) {
		return {};
	}
	std::string path = info.dli_fname;
	char resolved[PATH_MAX] = {};
	if (realpath(path.c_str(), resolved) != nullptr) {
		return resolved;
	}
	return path;
}

static void promote_current_module_symbols(void *symbol) {
	Dl_info info = {};
	if (dladdr(symbol, &info) == 0 || !info.dli_fname) {
		return;
	}
#ifdef RTLD_NOLOAD
	void *existing_handle = dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
	if (existing_handle) {
		return;
	}
#endif
	(void)dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL);
}

static bool native_file_exists(const std::string &path) {
	return access(path.c_str(), F_OK) == 0;
}

static void make_native_executable(const std::string &path) {
	(void)chmod(path.c_str(), 0755);
}
#endif

namespace gode::node_runtime_bridge {

static bool is_godot_path(const std::string &path) {
	return path.find("res://") == 0 || path.find("user://") == 0;
}

static bool is_res_path(const std::string &path) {
	return path.find("res://") == 0;
}

static bool is_user_path(const std::string &path) {
	return path.find("user://") == 0;
}

static bool path_has_parent_segment(const godot::String &path) {
	godot::PackedStringArray segments = path.replace("\\", "/").split("/", false);
	for (int64_t i = 0; i < segments.size(); i++) {
		if (segments[i] == "..") {
			return true;
		}
	}
	return false;
}

static godot::String normalized_godot_path(const std::string &path) {
	return godot::String::utf8(path.c_str()).replace("\\", "/").simplify_path();
}

static godot::String raw_godot_path(const std::string &path) {
	return godot::String::utf8(path.c_str()).replace("\\", "/");
}

static std::string to_utf8(const godot::String &value) {
	return value.utf8().get_data();
}

static godot::String globalize_godot_path(const godot::String &path) {
	godot::ProjectSettings *project_settings = godot::ProjectSettings::get_singleton();
	if (!project_settings) {
		return {};
	}
	return project_settings->globalize_path(path);
}

static godot::Error make_dir_recursive_godot(const godot::String &path) {
	godot::Error error = godot::DirAccess::make_dir_recursive_absolute(path);
	if (error == godot::OK) {
		return error;
	}

	godot::String globalized = globalize_godot_path(path);
	if (globalized.is_empty() || globalized == path) {
		return error;
	}
	return godot::DirAccess::make_dir_recursive_absolute(globalized);
}

static godot::Error remove_godot_path(const godot::String &path) {
	godot::Error error = godot::DirAccess::remove_absolute(path);
	if (error == godot::OK) {
		return error;
	}

	godot::String globalized = globalize_godot_path(path);
	if (globalized.is_empty() || globalized == path) {
		return error;
	}
	return godot::DirAccess::remove_absolute(globalized);
}

static bool remove_godot_path_recursive(const godot::String &path) {
	if (path.is_empty()) {
		return false;
	}
	if (godot::FileAccess::file_exists(path)) {
		return remove_godot_path(path) == godot::OK;
	}
	if (!godot::DirAccess::dir_exists_absolute(path)) {
		return true;
	}

	godot::PackedStringArray files = godot::DirAccess::get_files_at(path);
	for (int64_t i = 0; i < files.size(); i++) {
		godot::String file_path = path.path_join(files[i]);
		if (!remove_godot_path_recursive(file_path)) {
			return false;
		}
	}

	godot::PackedStringArray directories = godot::DirAccess::get_directories_at(path);
	for (int64_t i = 0; i < directories.size(); i++) {
		godot::String child_path = path.path_join(directories[i]);
		if (!remove_godot_path_recursive(child_path)) {
			return false;
		}
	}

	return remove_godot_path(path) == godot::OK;
}

static bool copy_godot_file(const godot::String &source_path, const godot::String &target_path) {
	godot::Error dir_error = make_dir_recursive_godot(target_path.get_base_dir());
	if (dir_error != godot::OK) {
		return false;
	}

	godot::Ref<godot::FileAccess> source = godot::FileAccess::open(source_path, godot::FileAccess::READ);
	if (source.is_null()) {
		return false;
	}
	godot::Ref<godot::FileAccess> target = godot::FileAccess::open(target_path, godot::FileAccess::WRITE);
	if (target.is_null()) {
		return false;
	}

	constexpr uint64_t chunk_size = 1024 * 1024;
	uint64_t remaining = source->get_length();
	while (remaining > 0) {
		uint64_t requested = remaining < chunk_size ? remaining : chunk_size;
		godot::PackedByteArray buffer = source->get_buffer(requested);
		if (buffer.size() <= 0) {
			return false;
		}
		target->store_buffer(buffer);
		remaining -= static_cast<uint64_t>(buffer.size());
	}
	source->close();
	target->close();
	return true;
}

static bool copy_godot_path_recursive(const godot::String &source_path, const godot::String &target_path) {
	if (godot::FileAccess::file_exists(source_path)) {
		return copy_godot_file(source_path, target_path);
	}
	if (!godot::DirAccess::dir_exists_absolute(source_path)) {
		return false;
	}
	if (make_dir_recursive_godot(target_path) != godot::OK) {
		return false;
	}

	godot::PackedStringArray files = godot::DirAccess::get_files_at(source_path);
	for (int64_t i = 0; i < files.size(); i++) {
		if (!copy_godot_path_recursive(source_path.path_join(files[i]), target_path.path_join(files[i]))) {
			return false;
		}
	}

	godot::PackedStringArray directories = godot::DirAccess::get_directories_at(source_path);
	for (int64_t i = 0; i < directories.size(); i++) {
		if (!copy_godot_path_recursive(source_path.path_join(directories[i]), target_path.path_join(directories[i]))) {
			return false;
		}
	}
	return true;
}

static bool is_safe_materialize_target(const std::string &path, const godot::String &normalized) {
	return is_user_path(path) && !path_has_parent_segment(raw_godot_path(path)) &&
			!path_has_parent_segment(normalized) && normalized.begins_with("user://.gode/");
}

static std::string get_current_module_directory() {
#ifdef WIN32
	HMODULE current_module = nullptr;
	if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&prepare_native_addon_host),
			&current_module)) {
		return {};
	}
	std::wstring module_path = get_module_file_name(current_module);
	size_t sep = module_path.find_last_of(L"\\/");
	if (sep == std::wstring::npos) {
		return {};
	}
	return wide_to_utf8(module_path.substr(0, sep));
#elif defined(__APPLE__) || defined(__linux__)
	std::string module_path = get_module_file_name(reinterpret_cast<void *>(&prepare_native_addon_host));
	size_t sep = module_path.find_last_of("/\\");
	if (sep == std::string::npos) {
		return {};
	}
	return module_path.substr(0, sep);
#else
	return {};
#endif
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

static Napi::Value globalize_path(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();
	if (info.Length() < 1 || !info[0].IsString()) {
		return env.Null();
	}
	std::string path = info[0].As<Napi::String>().Utf8Value();
	if (!is_godot_path(path)) {
		return env.Null();
	}
	godot::String normalized = normalized_godot_path(path);
	if (path_has_parent_segment(raw_godot_path(path)) || path_has_parent_segment(normalized)) {
		return env.Null();
	}

	godot::String globalized = globalize_godot_path(normalized);
	if (globalized.is_empty()) {
		return env.Null();
	}
	return Napi::String::New(env, to_utf8(globalized));
}

static Napi::Value materialize_path(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();
	if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
		return env.Null();
	}

	std::string source = info[0].As<Napi::String>().Utf8Value();
	std::string target = info[1].As<Napi::String>().Utf8Value();
	godot::String source_path = normalized_godot_path(source);
	godot::String target_path = normalized_godot_path(target);
	if (!is_res_path(source) || path_has_parent_segment(raw_godot_path(source)) ||
			path_has_parent_segment(source_path) || !is_safe_materialize_target(target, target_path)) {
		return env.Null();
	}
	if (!godot::FileAccess::file_exists(source_path) && !godot::DirAccess::dir_exists_absolute(source_path)) {
		return env.Null();
	}

	if (!remove_godot_path_recursive(target_path)) {
		return env.Null();
	}
	if (!copy_godot_path_recursive(source_path, target_path)) {
		return env.Null();
	}

	godot::String globalized = globalize_godot_path(target_path);
	if (globalized.is_empty()) {
		return env.Null();
	}
	return Napi::String::New(env, to_utf8(globalized));
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

static Napi::Value native_probe_executable(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();
#if defined(WIN32) || defined(__APPLE__) || defined(__linux__)
	std::string dir = get_current_module_directory();
	if (dir.empty()) {
		return env.Null();
	}
#ifdef WIN32
	return Napi::String::New(env, dir + "\\gode_node.exe");
#elif defined(__APPLE__)
	std::string framework_helper = dir + "/gode_node";
	if (native_file_exists(framework_helper)) {
		make_native_executable(framework_helper);
		return Napi::String::New(env, framework_helper);
	}
	std::string plugin_framework_helper = dir + "/../PlugIns/gode_node.framework/gode_node";
	if (native_file_exists(plugin_framework_helper)) {
		make_native_executable(plugin_framework_helper);
		return Napi::String::New(env, plugin_framework_helper);
	}
	return env.Null();
#else
	std::string helper = dir + "/gode_node";
	if (native_file_exists(helper)) {
		make_native_executable(helper);
		return Napi::String::New(env, helper);
	}
	return env.Null();
#endif
#else
	return env.Null();
#endif
}

void prepare_native_addon_host() {
#ifdef WIN32
	HMODULE current_module = nullptr;
	if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&prepare_native_addon_host),
			&current_module)) {
		return;
	}
	std::wstring node_dll_path = get_module_file_name(current_module);
	if (node_dll_path.empty()) {
		return;
	}
	size_t sep = node_dll_path.find_last_of(L"\\/");
	if (sep == std::wstring::npos) {
		return;
	}
	node_dll_path.resize(sep + 1);
	node_dll_path += L"node.dll";
	LoadLibraryW(node_dll_path.c_str());
#elif defined(__APPLE__) || defined(__linux__)
	promote_current_module_symbols(reinterpret_cast<void *>(&prepare_native_addon_host));
#endif
}

static Napi::Value sha256_text(const Napi::CallbackInfo &info) {
	const std::string text = info[0].ToString().Utf8Value();
	const godot::String hash = godot::String::utf8(text.data(), text.size()).sha256_text();
	return Napi::String::New(info.Env(), hash.utf8().get_data());
}

void install_exports(Napi::Env env, Napi::Object exports) {
	exports.Set("sha256_text", Napi::Function::New(env, sha256_text));
	exports.Set("fs_readFile", Napi::Function::New(env, fs_readFile));
	exports.Set("fs_stat", Napi::Function::New(env, fs_stat));
	exports.Set("globalize_path", Napi::Function::New(env, globalize_path));
	exports.Set("materialize_path", Napi::Function::New(env, materialize_path));
	exports.Set("preload_dlls", Napi::Function::New(env, preload_dlls));
	exports.Set("native_probe_executable", Napi::Function::New(env, native_probe_executable));
}

void install_global_decorators(Napi::Env env) {
	Napi::Object global = env.Global();
	global.Set("Export", Napi::Function::New(env, noop_decorator));
	global.Set("Signal", Napi::Function::New(env, noop_decorator));
	global.Set("Tool", Napi::Function::New(env, noop_decorator));
	global.Set("GlobalClass", Napi::Function::New(env, noop_decorator));
}

} // namespace gode::node_runtime_bridge
