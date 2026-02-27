#ifndef NODE_RUNTIME_H
#define NODE_RUNTIME_H


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
	static void init_once();
	static void run_script(const std::string &code);
	static Napi::Value compile_script(const std::string &code, const std::string &filename);
	static Napi::Function get_default_class(Napi::Value module_exports);
	static void shutdown();
};

} // namespace gode

#endif // NODE_RUNTIME_H