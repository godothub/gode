#ifndef GODE_UTILS_NODE_MODULE_RESOLVER_H
#define GODE_UTILS_NODE_MODULE_RESOLVER_H

#include <string>

namespace gode::node_module_resolver {

bool is_esm_file(const std::string &filename, const std::string &code);

} // namespace gode::node_module_resolver

#endif // GODE_UTILS_NODE_MODULE_RESOLVER_H
