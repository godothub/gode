#ifndef NODE_RUNTIME_H
#define NODE_RUNTIME_H

#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "node.h"

#include <napi.h>

#include <string>

namespace gode {

class NodeRuntime {
public:
	static v8::Isolate *isolate;
	static node::Environment *env;
	static v8::Global<v8::Context> node_context;
	static thread_local napi_env thread_local_env;

	static void init_once();
	static bool is_running();
	static void run_script(const std::string &code);
	static Napi::Value compile_script(const std::string &code, const std::string &filename);
	static godot::Dictionary compile_typescript_project(const godot::Array &files);
	static Napi::Function get_default_class(Napi::Value module_exports);
	static godot::Variant eval_expression(const std::string &expr);
	static void spin_loop();
	static void shutdown();

private:
	static v8::Local<v8::Value> compile_esm_module(const std::string &code, const std::string &filename);
	static v8::Local<v8::Value> compile_cjs_module(const std::string &code, const std::string &filename);
};

} // namespace gode

#endif // NODE_RUNTIME_H
