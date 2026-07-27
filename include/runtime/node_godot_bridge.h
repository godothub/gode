#ifndef GODE_UTILS_NODE_GODOT_BRIDGE_H
#define GODE_UTILS_NODE_GODOT_BRIDGE_H

#include <napi.h>

namespace gode::node_runtime_bridge {

void preload_node_dll_stub();
void install_exports(Napi::Env env, Napi::Object exports);
void install_global_decorators(Napi::Env env);

} // namespace gode::node_runtime_bridge

#endif // GODE_UTILS_NODE_GODOT_BRIDGE_H
