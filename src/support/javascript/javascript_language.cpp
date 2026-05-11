#include "support/javascript/javascript_language.h"
#include "godot_cpp/core/memory.hpp"
#include "support/javascript/javascript.h"
#include "utils/node_runtime.h"
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cctype>
#include <regex>
#include <sstream>

using namespace godot;
using namespace gode;

JavascriptLanguage *JavascriptLanguage::singleton = nullptr;
std::string JavascriptLanguage::last_error;
std::vector<JavascriptLanguage::DebugFrame> JavascriptLanguage::last_stack;

namespace {

PackedStringArray javascript_reserved_words() {
	PackedStringArray arr;
	const char *words[] = {
		"await", "break", "case", "catch", "class", "const", "continue", "debugger",
		"default", "delete", "do", "else", "export", "extends", "finally", "for",
		"function", "if", "import", "in", "instanceof", "let", "new", "return",
		"super", "switch", "this", "throw", "try", "typeof", "var", "void",
		"while", "with", "yield", "async", "static", "get", "set", "from", "of"
	};
	for (const char *word : words) {
		arr.push_back(word);
	}
	return arr;
}

Dictionary make_completion(const String &p_display, const String &p_insert_text, ScriptLanguageExtension::CodeCompletionKind p_kind) {
	Dictionary option;
	option["display"] = p_display;
	option["insert_text"] = p_insert_text;
	option["kind"] = p_kind;
	option["location"] = ScriptLanguageExtension::LOCATION_LOCAL;
	return option;
}

Dictionary make_validation_result(Error p_result = OK) {
	Dictionary d;
	d["result"] = p_result;
	d["valid"] = p_result == OK;
	d["errors"] = Array();
	d["warnings"] = Array();
	d["safe_lines"] = PackedInt32Array();
	return d;
}

void add_validation_error(Dictionary &r_result, const String &p_message, int32_t p_line, int32_t p_column) {
	Array errors = r_result["errors"];
	Dictionary error;
	error["line"] = p_line;
	error["column"] = p_column;
	error["message"] = p_message;
	errors.push_back(error);
	r_result["errors"] = errors;
	r_result["valid"] = false;
	r_result["result"] = ERR_PARSE_ERROR;
}

int32_t find_line_for_regex(const String &p_code, const std::regex &p_regex) {
	std::string code = p_code.utf8().get_data();
	std::smatch match;
	if (!std::regex_search(code, match, p_regex)) {
		return -1;
	}
	int32_t line = 1;
	for (size_t i = 0; i < static_cast<size_t>(match.position()); ++i) {
		if (code[i] == '\n') {
			line++;
		}
	}
	return line;
}

String strip_file_url(const String &p_path) {
	String path = p_path;
	if (path.begins_with("file://")) {
		path = path.substr(7);
	}
	return path;
}

String make_script_function(const String &p_function_name, const PackedStringArray &p_function_args) {
	String code = "\t" + p_function_name + "(";
	for (int i = 0; i < p_function_args.size(); ++i) {
		if (i > 0) {
			code += ", ";
		}
		String arg = p_function_args[i];
		if (arg.contains(":")) {
			arg = arg.get_slice(":", 0).strip_edges();
		}
		if (arg.is_empty()) {
			arg = "arg" + String::num_int64(i);
		}
		code += arg;
	}
	code += ") {\n\t\t\n\t}\n";
	return code;
}

} // namespace

JavascriptLanguage::~JavascriptLanguage() {
	if (singleton == this) {
		// ClassDB::_unregister_engine_singleton(JavascriptLanguage::get_class_static());
		// memdelete(singleton);
		singleton = nullptr;
	}
}

JavascriptLanguage *JavascriptLanguage::get_singleton() {
	if (singleton) {
		return singleton;
	}
	singleton = memnew(JavascriptLanguage);
	// if (likely(singleton)) {
	// 	ClassDB::_register_engine_singleton(JavascriptLanguage::get_class_static(), singleton);
	// }
	return singleton;
}

void JavascriptLanguage::report_exception(const String &p_error, const String &p_stack) {
	last_error = p_error.utf8().get_data();
	last_stack.clear();

	std::string stack = p_stack.utf8().get_data();
	std::regex frame_regex(R"(^\s*at\s+(?:(.*?)\s+\()?(.+?):(\d+):(\d+)\)?\s*$)");
	std::regex bare_frame_regex(R"(^\s*at\s+(.+?):(\d+):(\d+)\s*$)");
	std::stringstream stream(stack);
	std::string line;
	while (std::getline(stream, line)) {
		std::smatch match;
		DebugFrame frame;
		if (std::regex_match(line, match, frame_regex)) {
			frame.function = match[1].matched && match[1].length() > 0 ? match[1].str() : std::string("<anonymous>");
			frame.source = strip_file_url(String(match[2].str().c_str())).utf8().get_data();
			frame.line = String(match[3].str().c_str()).to_int();
			frame.column = String(match[4].str().c_str()).to_int();
			last_stack.push_back(frame);
		} else if (std::regex_match(line, match, bare_frame_regex)) {
			frame.function = "<anonymous>";
			frame.source = strip_file_url(String(match[1].str().c_str())).utf8().get_data();
			frame.line = String(match[2].str().c_str()).to_int();
			frame.column = String(match[3].str().c_str()).to_int();
			last_stack.push_back(frame);
		}
	}

	if (last_stack.empty()) {
		DebugFrame frame;
		frame.function = "<javascript>";
		last_stack.push_back(frame);
	}
}

void JavascriptLanguage::clear_exception() {
	last_error.clear();
	last_stack.clear();
}

void JavascriptLanguage::_bind_methods() {
}

String JavascriptLanguage::_get_name() const {
	return String("JavaScript");
}

void JavascriptLanguage::_init() {
}

String JavascriptLanguage::_get_type() const {
	return String("JavaScript");
}

String JavascriptLanguage::_get_extension() const {
	return String("js");
}

void JavascriptLanguage::_finish() {
}
PackedStringArray JavascriptLanguage::_get_reserved_words() const {
	return javascript_reserved_words();
}

bool JavascriptLanguage::_is_control_flow_keyword(const String &p_keyword) const {
	return p_keyword == "break" || p_keyword == "case" || p_keyword == "catch" || p_keyword == "continue" ||
			p_keyword == "default" || p_keyword == "do" || p_keyword == "else" || p_keyword == "finally" ||
			p_keyword == "for" || p_keyword == "if" || p_keyword == "return" || p_keyword == "switch" ||
			p_keyword == "throw" || p_keyword == "try" || p_keyword == "while";
}

PackedStringArray JavascriptLanguage::_get_comment_delimiters() const {
	PackedStringArray delimiters;
	delimiters.push_back("//");
	return delimiters;
}

PackedStringArray JavascriptLanguage::_get_doc_comment_delimiters() const {
	PackedStringArray delimiters;
	delimiters.push_back("/** */");
	return delimiters;
}

PackedStringArray JavascriptLanguage::_get_string_delimiters() const {
	PackedStringArray delimiters;
	delimiters.push_back("\"\"");
	delimiters.push_back("''");
	return delimiters;
}

Ref<Script> JavascriptLanguage::_make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	Ref<Javascript> script;
	script.instantiate();

	String class_name = p_class_name;
	if (class_name.is_empty()) {
		class_name = String("NewScript");
	}

	String base_name = p_base_class_name;

	String code;
	code += String("export default class ") + class_name;
	if (!base_name.is_empty()) {
		code += String(" extends ") + base_name;
	}
	code += String(" {\n");
	code += String("\tconstructor() {\n");
	code += String("\t\tsuper();\n");
	code += String("\t}\n\n");
	code += String("\t_ready() {\n");
	code += String("\t}\n");
	code += String("}\n");

	script->_set_source_code(code);
	return script;
}

TypedArray<Dictionary> JavascriptLanguage::_get_built_in_templates(const StringName &p_object) const {
	TypedArray<Dictionary> arr;
	Dictionary basic;
	String base = String(p_object);
	if (base.is_empty()) {
		base = "Node";
	}
	basic["id"] = "default";
	basic["name"] = "Default";
	basic["description"] = "Default JavaScript script";
	basic["content"] = String("export default class NewScript extends ") + base + " {\n\t_ready() {\n\t}\n}\n";
	arr.push_back(basic);
	return arr;
}

bool JavascriptLanguage::_is_using_templates() {
	return true;
}

Dictionary JavascriptLanguage::_validate(const String &p_script, const String &p_path, bool p_validate_functions, bool p_validate_errors, bool p_validate_warnings, bool p_validate_safe_lines) const {
	Dictionary result = make_validation_result();

	if (p_validate_errors) {
		int32_t balance = 0;
		int32_t line = 1;
		int32_t column = 0;
		for (int64_t i = 0; i < p_script.length(); ++i) {
			char32_t c = p_script[i];
			column++;
			if (c == '\n') {
				line++;
				column = 0;
				continue;
			}
			if (c == '{') {
				balance++;
			} else if (c == '}') {
				balance--;
				if (balance < 0) {
					add_validation_error(result, "Unexpected closing brace.", line, column);
					break;
				}
			}
		}
		if ((bool)result["valid"] && balance != 0) {
			add_validation_error(result, "Unbalanced braces in script.", line, column);
		}
	}

	if (p_validate_functions) {
		Array warnings = result["warnings"];
		if (p_script.find("export default class") < 0 && p_script.find("module.exports") < 0) {
			Dictionary warning;
			warning["line"] = 1;
			warning["column"] = 1;
			warning["message"] = "Gode scripts should export a default class or assign module.exports.";
			warnings.push_back(warning);
			result["warnings"] = warnings;
		}
	}

	return result;
}

String JavascriptLanguage::_validate_path(const String &p_path) const {
	if (p_path.is_empty()) {
		return String();
	}
	String extension = p_path.get_extension().to_lower();
	if (extension != _get_extension()) {
		return String("Expected a .") + _get_extension() + " script.";
	}
	return String();
}

Object *JavascriptLanguage::_create_script() const {
	return memnew(Javascript);
}

bool JavascriptLanguage::_has_named_classes() const {
	return true;
}

bool JavascriptLanguage::_supports_builtin_mode() const {
	return false;
}

bool JavascriptLanguage::_supports_documentation() const {
	return false;
}

bool JavascriptLanguage::_can_inherit_from_file() const {
	return false;
}

int32_t JavascriptLanguage::_find_function(const String &p_function, const String &p_code) const {
	std::string name = p_function.utf8().get_data();
	std::string escaped;
	for (char c : name) {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
			escaped.push_back(c);
		}
	}
	if (escaped.empty()) {
		return -1;
	}
	std::regex method_regex("(?:async\\s+)?(?:(?:function\\s+" + escaped + "\\s*\\()|(?:" + escaped + "\\s*\\())");
	return find_line_for_regex(p_code, method_regex);
}

String JavascriptLanguage::_make_function(const String &p_class_name, const String &p_function_name, const PackedStringArray &p_function_args) const {
	return make_script_function(p_function_name, p_function_args);
}

bool JavascriptLanguage::_can_make_function() const {
	return true;
}

Error JavascriptLanguage::_open_in_external_editor(const Ref<Script> &p_script, int32_t p_line, int32_t p_column) {
	return Error::ERR_UNAVAILABLE;
}

bool JavascriptLanguage::_overrides_external_editor() {
	return false;
}

ScriptLanguage::ScriptNameCasing JavascriptLanguage::_preferred_file_name_casing() const {
	return ScriptLanguage::ScriptNameCasing::SCRIPT_NAME_CASING_PASCAL_CASE;
}

Dictionary JavascriptLanguage::_complete_code(const String &p_code, const String &p_path, Object *p_owner) const {
	Dictionary d;
	Array options;
	options.push_back(make_completion("_ready", "_ready() {\n\t\n}", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("_process", "_process(delta) {\n\t\n}", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("_physics_process", "_physics_process(delta) {\n\t\n}", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("_input", "_input(event) {\n\t\n}", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("console.log", "console.log()", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("preload", "preload(\"res://\")", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("load", "load(\"res://\")", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("Export", "Export", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("Signal", "Signal", ScriptLanguageExtension::CODE_COMPLETION_KIND_SIGNAL));
	options.push_back(make_completion("Rpc", "Rpc", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("GlobalClass", "GlobalClass", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	options.push_back(make_completion("Tool", "Tool", ScriptLanguageExtension::CODE_COMPLETION_KIND_FUNCTION));
	d["result"] = OK;
	d["options"] = options;
	d["force"] = false;
	d["call_hint"] = String();
	return d;
}

Dictionary JavascriptLanguage::_lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner) const {
	Dictionary d;
	int32_t line = _find_function(p_symbol, p_code);
	if (line >= 0) {
		d["result"] = OK;
		d["type"] = ScriptLanguageExtension::LOOKUP_RESULT_SCRIPT_LOCATION;
		d["location"] = p_path;
		d["line"] = line;
		d["class"] = String();
		d["class_name"] = String();
		return d;
	}
	d["result"] = ERR_DOES_NOT_EXIST;
	d["type"] = ScriptLanguageExtension::LOOKUP_RESULT_SCRIPT_LOCATION;
	return d;
}

String JavascriptLanguage::_auto_indent_code(const String &p_code, int32_t p_from_line, int32_t p_to_line) const {
	return p_code;
}

void JavascriptLanguage::_add_global_constant(const StringName &p_name, const Variant &p_value) {
}

void JavascriptLanguage::_add_named_global_constant(const StringName &p_name, const Variant &p_value) {
}

void JavascriptLanguage::_remove_named_global_constant(const StringName &p_name) {
}

void JavascriptLanguage::_thread_enter() {
}

void JavascriptLanguage::_thread_exit() {
}

String JavascriptLanguage::_debug_get_error() const {
	return String(last_error.c_str());
}

int32_t JavascriptLanguage::_debug_get_stack_level_count() const {
	return static_cast<int32_t>(last_stack.size());
}

int32_t JavascriptLanguage::_debug_get_stack_level_line(int32_t p_level) const {
	if (p_level < 0 || p_level >= static_cast<int32_t>(last_stack.size())) {
		return -1;
	}
	return last_stack[p_level].line;
}

String JavascriptLanguage::_debug_get_stack_level_function(int32_t p_level) const {
	if (p_level < 0 || p_level >= static_cast<int32_t>(last_stack.size())) {
		return String();
	}
	return String(last_stack[p_level].function.c_str());
}

String JavascriptLanguage::_debug_get_stack_level_source(int32_t p_level) const {
	if (p_level < 0 || p_level >= static_cast<int32_t>(last_stack.size())) {
		return String();
	}
	return String(last_stack[p_level].source.c_str());
}

Dictionary JavascriptLanguage::_debug_get_stack_level_locals(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) {
	Dictionary d;
	return d;
}

Dictionary JavascriptLanguage::_debug_get_stack_level_members(int32_t p_level, int32_t p_max_subitems, int32_t p_max_depth) {
	Dictionary d;
	return d;
}

void *JavascriptLanguage::_debug_get_stack_level_instance(int32_t p_level) {
	return nullptr;
}

Dictionary JavascriptLanguage::_debug_get_globals(int32_t p_max_subitems, int32_t p_max_depth) {
	Dictionary d;
	d["console"] = "Godot console bridge";
	d["godot"] = "Godot module";
	return d;
}

String JavascriptLanguage::_debug_parse_stack_level_expression(int32_t p_level, const String &p_expression, int32_t p_max_subitems, int32_t p_max_depth) {
	Variant value = NodeRuntime::eval_expression(p_expression.utf8().get_data());
	return Variant(value).stringify();
}

TypedArray<Dictionary> JavascriptLanguage::_debug_get_current_stack_info() {
	TypedArray<Dictionary> arr;
	for (const DebugFrame &frame : last_stack) {
		Dictionary d;
		d["file"] = String(frame.source.c_str());
		d["line"] = frame.line;
		d["function"] = String(frame.function.c_str());
		arr.push_back(d);
	}
	return arr;
}

void JavascriptLanguage::_reload_all_scripts() {
	clear_exception();
}

void JavascriptLanguage::_reload_scripts(const Array &p_scripts, bool p_soft_reload) {
	for (int64_t i = 0; i < p_scripts.size(); ++i) {
		Ref<Javascript> script = p_scripts[i];
		if (script.is_valid()) {
			script->_reload(p_soft_reload);
		}
	}
}

void JavascriptLanguage::_reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) {
	Ref<Javascript> script = p_script;
	if (script.is_valid()) {
		script->_reload(p_soft_reload);
	}
}

PackedStringArray JavascriptLanguage::_get_recognized_extensions() const {
	PackedStringArray arr;
	arr.push_back(String("js"));
	return arr;
}

TypedArray<Dictionary> JavascriptLanguage::_get_public_functions() const {
	TypedArray<Dictionary> arr;
	const char *names[] = { "preload", "load" };
	for (const char *name : names) {
		Dictionary d;
		d["name"] = name;
		d["args"] = Array();
		d["return"] = Dictionary();
		arr.push_back(d);
	}
	return arr;
}

Dictionary JavascriptLanguage::_get_public_constants() const {
	Dictionary d;
	return d;
}

TypedArray<Dictionary> JavascriptLanguage::_get_public_annotations() const {
	TypedArray<Dictionary> arr;
	const char *annotations[] = {
		"Export", "ExportCategory", "ExportGroup", "ExportSubgroup", "ExportRange",
		"ExportEnum", "ExportFlags", "ExportFile", "ExportDir", "ExportMultiline",
		"ExportColorNoAlpha", "ExportNodePath", "ExportResource", "Signal", "Rpc",
		"Tool", "GlobalClass"
	};
	for (const char *name : annotations) {
		Dictionary d;
		d["name"] = name;
		d["args"] = Array();
		d["description"] = String("Gode TypeScript decorator: @") + name;
		arr.push_back(d);
	}
	return arr;
}

void JavascriptLanguage::_profiling_start() {
}

void JavascriptLanguage::_profiling_stop() {
}

void JavascriptLanguage::_profiling_set_save_native_calls(bool p_enable) {
}

int32_t JavascriptLanguage::_profiling_get_accumulated_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) {
	return 0;
}

int32_t JavascriptLanguage::_profiling_get_frame_data(ScriptLanguageExtensionProfilingInfo *p_info_array, int32_t p_info_max) {
	return 0;
}

void JavascriptLanguage::_frame() {
}

bool JavascriptLanguage::_handles_global_class_type(const String &p_type) const {
	return p_type == _get_type() || p_type == String("Script");
}

Dictionary JavascriptLanguage::_get_global_class_name(const String &p_path) const {
	Dictionary d;
	Ref<Javascript> script = ResourceLoader::get_singleton()->load(p_path, "", ResourceLoader::CACHE_MODE_REUSE);
	if (script.is_null()) {
		return d;
	}
	StringName name = script->_get_global_name();
	if (name == StringName()) {
		return d;
	}
	d["name"] = name;
	d["base_type"] = script->get_base_class_name();
	return d;
}
