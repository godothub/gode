#ifndef NODE_INSPECTOR_H
#define NODE_INSPECTOR_H

#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <string>

namespace gode::node_inspector {

struct Config {
	bool enabled = false;
	godot::String host = "127.0.0.1";
	int64_t port = 9229;
	bool wait_for_debugger = false;
	bool break_on_start = false;
	bool source_maps = true;
	bool log_url = true;
	bool auto_increment_port = true;
	int64_t max_port_retries = 20;
	bool allow_in_release = false;
};

Config load_config();
void open_if_enabled(const Config &config);
void maybe_break_on_user_script(const std::string &filename);
void close_if_open();

} // namespace gode::node_inspector

#endif // NODE_INSPECTOR_H
