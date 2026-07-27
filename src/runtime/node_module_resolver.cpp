#include "runtime/node_module_resolver.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace gode::node_module_resolver {

static bool is_path_separator(char c) {
	return c == '/' || c == '\\';
}

static std::string parent_directory(const std::string &path) {
	if (path.empty() || path == "/" || path == "res://" || path == "user://") {
		return std::string();
	}

	std::string dir = path;
	while (dir.size() > 1 && is_path_separator(dir.back())) {
		if (dir == "res://" || dir == "user://") {
			return std::string();
		}
		dir.pop_back();
	}

	if (dir.size() == 3 && dir[1] == ':' && is_path_separator(dir[2])) {
		return std::string();
	}

	size_t slash = dir.find_last_of("/\\");
	if (slash == std::string::npos) {
		return std::string();
	}

	if (dir.rfind("res://", 0) == 0) {
		return slash < 6 ? std::string("res://") : dir.substr(0, slash);
	}
	if (dir.rfind("user://", 0) == 0) {
		return slash < 7 ? std::string("user://") : dir.substr(0, slash);
	}
	if (slash == 0) {
		return std::string("/");
	}
	if (slash == 2 && dir[1] == ':') {
		return dir.substr(0, 3);
	}

	return dir.substr(0, slash);
}

static int read_package_module_type(const std::string &filename) {
	std::string dir = filename;
	size_t last_slash = dir.find_last_of("/\\");
	if (last_slash == std::string::npos) {
		return 0;
	}
	dir = dir.substr(0, last_slash);

	while (!dir.empty()) {
		const std::string pkg_path = dir + "/package.json";
		const godot::String gd_pkg_path = godot::String::utf8(pkg_path.c_str());
		if (godot::FileAccess::file_exists(gd_pkg_path)) {
			godot::Ref<godot::FileAccess> file = godot::FileAccess::open(gd_pkg_path, godot::FileAccess::READ);
			if (file.is_valid()) {
				const godot::String content = file->get_as_text();
				const godot::Variant parsed = godot::JSON::parse_string(content);
				if (parsed.get_type() == godot::Variant::Type::DICTIONARY) {
					const godot::Dictionary json = parsed;
					const godot::String type = json.get("type", godot::String());
					if (type == "module") {
						return 1;
					}
					if (type == "commonjs") {
						return -1;
					}
				}
			}
		}

		std::string parent = parent_directory(dir);
		if (parent.empty() || parent == dir) {
			break;
		}
		dir = parent;
	}

	return 0;
}

bool is_esm_file(const std::string &filename, const std::string &code) {
	if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".mjs") {
		return true;
	}
	if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".cjs") {
		return false;
	}

	if (filename.size() >= 3 && filename.substr(filename.size() - 3) == ".js") {
		const int package_type = read_package_module_type(filename);
		if (package_type > 0) {
			return true;
		}
		if (package_type < 0) {
			return false;
		}
	}

	if (code.find("module.exports") != std::string::npos ||
			code.find("exports.") != std::string::npos ||
			code.find("require(") != std::string::npos) {
		return false;
	}

	if (code.find("import ") != std::string::npos ||
			code.find("export ") != std::string::npos ||
			code.find("import{") != std::string::npos ||
			code.find("export{") != std::string::npos ||
			code.find("export default") != std::string::npos) {
		return true;
	}

	return false;
}

} // namespace gode::node_module_resolver
