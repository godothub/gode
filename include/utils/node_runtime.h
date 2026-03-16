#ifndef NODE_RUNTIME_H
#define NODE_RUNTIME_H

#include "node.h"

#include <napi.h>

#include <string>


namespace gode {

class JsEnvManager {
public:
	static void init(Napi::Env env);
	static Napi::Env get_env();
};

class NodeRuntime {
public:
	static v8::Isolate *isolate;
	static node::Environment *env;
	static v8::Global<v8::Context> node_context;

	static void init_once();
	static void run_script(const std::string &code);
	static Napi::Value compile_script(const std::string &code, const std::string &filename);
	static Napi::Function get_default_class(Napi::Value module_exports);
    static void spin_loop();
	static void shutdown();

private:
	static bool is_esm_file(const std::string &filename, const std::string &code);
	static Napi::Value compile_esm_module(const std::string &code, const std::string &filename);
	static Napi::Value compile_cjs_module(const std::string &code, const std::string &filename);
};

} // namespace gode

#endif // NODE_RUNTIME_H