#include "runtime/node_inspector.h"

#include "runtime/node_runtime.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <string>

namespace gode::node_inspector {
namespace {

constexpr const char *PROJECT_GODE_CONFIG_PATH = "res://gode.json";
constexpr const char *DEFAULT_GODE_CONFIG_PATH = "res://addons/gode/config/gode.json";
constexpr const char *DEFAULT_INSPECTOR_HOST = "127.0.0.1";
constexpr int64_t DEFAULT_INSPECTOR_PORT = 9229;
constexpr int64_t DEFAULT_INSPECTOR_MAX_PORT_RETRIES = 20;

struct OpenResult {
	bool ok = false;
	std::string url;
	std::string error;
	int64_t port = DEFAULT_INSPECTOR_PORT;
};

static bool inspector_opened = false;
static bool break_on_next_user_script = false;
static std::string inspector_url;

void warn_debug_config(const godot::String &message) {
	godot::UtilityFunctions::printerr("[Gode Debug] ", message);
}

bool read_bool_field(const godot::Dictionary &dict, const char *key, bool default_value, const char *path) {
	const godot::String key_name(key);
	if (!dict.has(key_name)) {
		return default_value;
	}
	const godot::Variant value = dict.get(key_name, godot::Variant());
	if (value.get_type() != godot::Variant::Type::BOOL) {
		warn_debug_config(godot::String("Ignoring non-boolean config value: ") + path + "." + key);
		return default_value;
	}
	return bool(value);
}

int64_t read_int_field(const godot::Dictionary &dict, const char *key, int64_t default_value, int64_t min_value, int64_t max_value, const char *path) {
	const godot::String key_name(key);
	if (!dict.has(key_name)) {
		return default_value;
	}
	const godot::Variant value = dict.get(key_name, godot::Variant());
	const godot::Variant::Type value_type = value.get_type();
	int64_t number = default_value;
	if (value_type == godot::Variant::Type::INT) {
		number = int64_t(value);
	} else if (value_type == godot::Variant::Type::FLOAT) {
		const double float_value = double(value);
		number = int64_t(float_value);
		if (double(number) != float_value) {
			warn_debug_config(godot::String("Ignoring non-integer config value: ") + path + "." + key);
			return default_value;
		}
	} else {
		warn_debug_config(godot::String("Ignoring non-integer config value: ") + path + "." + key);
		return default_value;
	}
	if (number < min_value || number > max_value) {
		warn_debug_config(godot::String("Ignoring out-of-range config value: ") + path + "." + key);
		return default_value;
	}
	return number;
}

godot::String read_string_field(const godot::Dictionary &dict, const char *key, const godot::String &default_value, const char *path) {
	const godot::String key_name(key);
	if (!dict.has(key_name)) {
		return default_value;
	}
	const godot::Variant value = dict.get(key_name, godot::Variant());
	if (value.get_type() != godot::Variant::Type::STRING && value.get_type() != godot::Variant::Type::STRING_NAME) {
		warn_debug_config(godot::String("Ignoring non-string config value: ") + path + "." + key);
		return default_value;
	}
	godot::String text = godot::String(value).strip_edges();
	if (text.is_empty()) {
		warn_debug_config(godot::String("Ignoring empty config value: ") + path + "." + key);
		return default_value;
	}
	return text;
}

bool is_release_export_runtime() {
	godot::Engine *engine = godot::Engine::get_singleton();
	if (engine && engine->is_editor_hint()) {
		return false;
	}
	godot::OS *os = godot::OS::get_singleton();
	return os && !os->is_debug_build();
}

std::string to_utf8(const godot::String &value) {
	const godot::CharString utf8 = value.utf8();
	return std::string(utf8.get_data());
}

std::string v8_value_to_string(v8::Isolate *isolate, v8::Local<v8::Value> value) {
	v8::String::Utf8Value text(isolate, value);
	return *text ? *text : "";
}

bool get_object_bool(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char *key, bool default_value) {
	v8::Local<v8::Value> value;
	if (!object->Get(context, v8::String::NewFromUtf8(isolate, key, v8::NewStringType::kNormal).ToLocalChecked()).ToLocal(&value)) {
		return default_value;
	}
	return value->IsBoolean() ? value->BooleanValue(isolate) : default_value;
}

int64_t get_object_int(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char *key, int64_t default_value) {
	v8::Local<v8::Value> value;
	if (!object->Get(context, v8::String::NewFromUtf8(isolate, key, v8::NewStringType::kNormal).ToLocalChecked()).ToLocal(&value)) {
		return default_value;
	}
	if (!value->IsNumber()) {
		return default_value;
	}
	return value->IntegerValue(context).FromMaybe(default_value);
}

std::string get_object_string(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char *key) {
	v8::Local<v8::Value> value;
	if (!object->Get(context, v8::String::NewFromUtf8(isolate, key, v8::NewStringType::kNormal).ToLocalChecked()).ToLocal(&value)) {
		return std::string();
	}
	return value->IsString() ? v8_value_to_string(isolate, value) : std::string();
}

OpenResult open_node_inspector(const Config &config) {
	OpenResult open_result;
	open_result.port = config.port;

	v8::Local<v8::Context> context = NodeRuntime::node_context.Get(NodeRuntime::isolate);
	v8::Context::Scope context_scope(context);
	v8::TryCatch try_catch(NodeRuntime::isolate);

	v8::Local<v8::Value> fn_value;
	if (!context->Global()->Get(context, v8::String::NewFromUtf8Literal(NodeRuntime::isolate, "__gode_open_inspector")).ToLocal(&fn_value) || !fn_value->IsFunction()) {
		open_result.error = "__gode_open_inspector is not registered.";
		return open_result;
	}

	v8::Local<v8::Function> fn = fn_value.As<v8::Function>();
	const std::string host = to_utf8(config.host);
	v8::Local<v8::Value> args[] = {
		v8::Integer::New(NodeRuntime::isolate, static_cast<int32_t>(config.port)),
		v8::String::NewFromUtf8(NodeRuntime::isolate, host.c_str(), v8::NewStringType::kNormal).ToLocalChecked(),
		v8::Boolean::New(NodeRuntime::isolate, config.wait_for_debugger),
		v8::Boolean::New(NodeRuntime::isolate, config.auto_increment_port),
		v8::Integer::New(NodeRuntime::isolate, static_cast<int32_t>(config.max_port_retries))
	};

	v8::Local<v8::Value> result;
	if (!fn->Call(context, context->Global(), 5, args).ToLocal(&result) || try_catch.HasCaught()) {
		open_result.error = try_catch.HasCaught() ? v8_value_to_string(NodeRuntime::isolate, try_catch.Exception()) : "Node inspector bootstrap failed.";
		return open_result;
	}

	if (!result->IsObject()) {
		open_result.error = "Node inspector bootstrap returned an invalid result.";
		return open_result;
	}

	v8::Local<v8::Object> object = result.As<v8::Object>();
	open_result.ok = get_object_bool(NodeRuntime::isolate, context, object, "ok", false);
	open_result.url = get_object_string(NodeRuntime::isolate, context, object, "url");
	open_result.error = get_object_string(NodeRuntime::isolate, context, object, "error");
	open_result.port = get_object_int(NodeRuntime::isolate, context, object, "port", config.port);
	return open_result;
}

std::string wait_for_node_debugger() {
	v8::Local<v8::Context> context = NodeRuntime::node_context.Get(NodeRuntime::isolate);
	v8::Context::Scope context_scope(context);
	v8::TryCatch try_catch(NodeRuntime::isolate);

	v8::Local<v8::Value> fn_value;
	if (!context->Global()->Get(context, v8::String::NewFromUtf8Literal(NodeRuntime::isolate, "__gode_wait_for_debugger")).ToLocal(&fn_value) || !fn_value->IsFunction()) {
		return "__gode_wait_for_debugger is not registered.";
	}

	v8::Local<v8::Function> fn = fn_value.As<v8::Function>();
	v8::Local<v8::Value> result;
	if (!fn->Call(context, context->Global(), 0, nullptr).ToLocal(&result) || try_catch.HasCaught()) {
		return try_catch.HasCaught() ? v8_value_to_string(NodeRuntime::isolate, try_catch.Exception()) : "Node inspector wait failed.";
	}
	if (!result->IsObject()) {
		return "Node inspector wait returned an invalid result.";
	}

	v8::Local<v8::Object> object = result.As<v8::Object>();
	if (!get_object_bool(NodeRuntime::isolate, context, object, "ok", false)) {
		return get_object_string(NodeRuntime::isolate, context, object, "error");
	}
	return std::string();
}

std::string devtools_url_for_ws_url(const std::string &url) {
	const std::string ws_prefix = "ws://";
	if (url.rfind(ws_prefix, 0) == 0) {
		return "devtools://devtools/bundled/inspector.html?v8only=true&ws=" + url.substr(ws_prefix.size());
	}
	return std::string();
}

void print_attach_info(const Config &config, const OpenResult &open_result) {
	godot::UtilityFunctions::print("[Gode Debug] Node inspector listening: ", open_result.url.c_str());
	const std::string devtools_url = devtools_url_for_ws_url(open_result.url);
	if (!devtools_url.empty()) {
		godot::UtilityFunctions::print("[Gode Debug] Chrome DevTools: ", devtools_url.c_str());
	}
	const std::string host = to_utf8(config.host);
	godot::UtilityFunctions::print("[Gode Debug] VS Code attach: ", host.c_str(), ":", open_result.port);
}

bool is_user_compiled_typescript_module(const std::string &filename) {
	return filename.rfind("user://.gode/typescript/", 0) == 0 ||
			filename.rfind("res://.gode/build/typescript/", 0) == 0;
}

} // namespace

Config load_config() {
	Config config;
	godot::String config_path = PROJECT_GODE_CONFIG_PATH;
	if (!godot::FileAccess::file_exists(config_path)) {
		config_path = DEFAULT_GODE_CONFIG_PATH;
	}
	if (!godot::FileAccess::file_exists(config_path)) {
		return config;
	}

	const godot::String content = godot::FileAccess::get_file_as_string(config_path);
	if (godot::FileAccess::get_open_error() != godot::Error::OK) {
		warn_debug_config(godot::String("Could not read config: ") + config_path);
		return config;
	}

	const godot::Variant parsed = godot::JSON::parse_string(content);
	if (parsed.get_type() != godot::Variant::Type::DICTIONARY) {
		warn_debug_config(godot::String("Gode config must be a JSON object: ") + config_path);
		return config;
	}

	const godot::Dictionary root = parsed;
	const godot::Variant debug_value = root.get("debug", godot::Dictionary());
	if (debug_value.get_type() != godot::Variant::Type::DICTIONARY) {
		warn_debug_config(godot::String("Gode config field debug must be an object: ") + config_path);
		return config;
	}

	const godot::Dictionary debug_config = debug_value;
	const godot::Variant inspector_value = debug_config.get("inspector", godot::Dictionary());
	if (inspector_value.get_type() != godot::Variant::Type::DICTIONARY) {
		warn_debug_config(godot::String("Gode config field debug.inspector must be an object: ") + config_path);
		return config;
	}

	const godot::Dictionary inspector_config = inspector_value;
	config.enabled = read_bool_field(inspector_config, "enabled", config.enabled, "debug.inspector");
	config.host = read_string_field(inspector_config, "host", config.host, "debug.inspector");
	config.port = read_int_field(inspector_config, "port", config.port, 0, 65535, "debug.inspector");
	config.wait_for_debugger = read_bool_field(inspector_config, "waitForDebugger", config.wait_for_debugger, "debug.inspector");
	config.break_on_start = read_bool_field(inspector_config, "breakOnStart", config.break_on_start, "debug.inspector");
	config.source_maps = read_bool_field(inspector_config, "sourceMaps", config.source_maps, "debug.inspector");
	config.log_url = read_bool_field(inspector_config, "logUrl", config.log_url, "debug.inspector");
	config.auto_increment_port = read_bool_field(inspector_config, "autoIncrementPort", config.auto_increment_port, "debug.inspector");
	config.max_port_retries = read_int_field(inspector_config, "maxPortRetries", config.max_port_retries, 1, 100, "debug.inspector");
	config.allow_in_release = read_bool_field(inspector_config, "allowInRelease", config.allow_in_release, "debug.inspector");

	if (config.enabled && is_release_export_runtime() && !config.allow_in_release) {
		warn_debug_config("Node inspector is disabled in release exports. Set debug.inspector.allowInRelease to true to override.");
		config.enabled = false;
	}

	if (config.enabled && config.host != "127.0.0.1" && config.host != "localhost" && config.host != "::1") {
		warn_debug_config("Node inspector is not bound to a loopback host. Anyone who can connect can execute JavaScript in this process.");
	}

	return config;
}

void open_if_enabled(const Config &config) {
	if (!config.enabled) {
		return;
	}

	OpenResult open_result = open_node_inspector(config);
	if (!open_result.ok) {
		godot::UtilityFunctions::printerr("[Gode Debug] Failed to open Node inspector: ", open_result.error.c_str());
		return;
	}

	inspector_opened = true;
	inspector_url = open_result.url;
	break_on_next_user_script = config.break_on_start;

	if (config.log_url || config.wait_for_debugger) {
		print_attach_info(config, open_result);
	}

	if (config.wait_for_debugger) {
		godot::UtilityFunctions::print("[Gode Debug] Waiting for debugger to attach...");
		const std::string wait_error = wait_for_node_debugger();
		if (!wait_error.empty()) {
			godot::UtilityFunctions::printerr("[Gode Debug] Failed while waiting for debugger: ", wait_error.c_str());
		}
	}
}

void maybe_break_on_user_script(const std::string &filename) {
	if (!break_on_next_user_script || !is_user_compiled_typescript_module(filename)) {
		return;
	}
	break_on_next_user_script = false;

	v8::HandleScope handle_scope(NodeRuntime::isolate);
	v8::Local<v8::Context> context = NodeRuntime::node_context.Get(NodeRuntime::isolate);
	v8::Context::Scope context_scope(context);
	v8::TryCatch try_catch(NodeRuntime::isolate);

	v8::Local<v8::String> source = v8::String::NewFromUtf8Literal(NodeRuntime::isolate, "debugger;");
	v8::Local<v8::String> name = v8::String::NewFromUtf8Literal(NodeRuntime::isolate, "<gode-break-on-start>");
	v8::ScriptOrigin origin(name);
	v8::Local<v8::Script> script;
	if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
		godot::UtilityFunctions::printerr("[Gode Debug] Failed to compile break-on-start script.");
		return;
	}

	v8::Local<v8::Value> ignored_result;
	if (!script->Run(context).ToLocal(&ignored_result) && try_catch.HasCaught()) {
		godot::UtilityFunctions::printerr("[Gode Debug] Failed to run break-on-start script.");
		return;
	}

	godot::UtilityFunctions::print("[Gode Debug] Break before first TypeScript script: ", filename.c_str());
}

void close_if_open() {
	if (!inspector_opened || !NodeRuntime::is_running()) {
		return;
	}

	v8::Local<v8::Context> context = NodeRuntime::node_context.Get(NodeRuntime::isolate);
	v8::Context::Scope context_scope(context);
	v8::TryCatch try_catch(NodeRuntime::isolate);

	v8::Local<v8::Value> fn_value;
	if (!context->Global()->Get(context, v8::String::NewFromUtf8Literal(NodeRuntime::isolate, "__gode_close_inspector")).ToLocal(&fn_value) || !fn_value->IsFunction()) {
		return;
	}

	v8::Local<v8::Function> fn = fn_value.As<v8::Function>();
	v8::Local<v8::Value> ignored_result;
	if (!fn->Call(context, context->Global(), 0, nullptr).ToLocal(&ignored_result) && try_catch.HasCaught()) {
		godot::UtilityFunctions::printerr("[Gode Debug] Failed to close Node inspector cleanly.");
	}
	inspector_opened = false;
	break_on_next_user_script = false;
	inspector_url.clear();
}

} // namespace gode::node_inspector
