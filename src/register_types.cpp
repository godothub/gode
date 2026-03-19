#include "register_types.h"
#include "napi.h"
#include "support/javascript/javascript.h"
#include "support/javascript/javascript_language.h"
#include "support/javascript/javascript_loader.h"
#include "support/javascript/javascript_saver.h"
#include "support/typescript/typescript.h"
#include "support/typescript/typescript_language.h"
#include "support/typescript/typescript_loader.h"
#include "support/typescript/typescript_saver.h"
#include "tests/test_runner.h"
#include "utils/gode_event_loop.h"
#include "utils/node_runtime.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

static gode::GodeEventLoop *event_loop_singleton = nullptr;

void initialize_node_module(godot::ModuleInitializationLevel p_level) {
	if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(gode::Javascript);
	GDREGISTER_CLASS(gode::JavascriptLanguage);
	GDREGISTER_CLASS(gode::JavascriptSaver);
	GDREGISTER_CLASS(gode::JavascriptLoader);
	GDREGISTER_CLASS(gode::Typescript);
	GDREGISTER_CLASS(gode::TypescriptLanguage);
	GDREGISTER_CLASS(gode::TypescriptSaver);
	GDREGISTER_CLASS(gode::TypescriptLoader);
	GDREGISTER_CLASS(gode::GodeEventLoop);
	godot::Engine::get_singleton()->register_script_language(gode::JavascriptLanguage::get_singleton());
	godot::Engine::get_singleton()->register_script_language(gode::TypescriptLanguage::get_singleton());
	godot::ResourceSaver::get_singleton()->add_resource_format_saver(gode::JavascriptSaver::get_singleton());
	godot::ResourceSaver::get_singleton()->add_resource_format_saver(gode::TypescriptSaver::get_singleton());
	godot::ResourceLoader::get_singleton()->add_resource_format_loader(gode::JavascriptLoader::get_singleton());
	godot::ResourceLoader::get_singleton()->add_resource_format_loader(gode::TypescriptLoader::get_singleton());

	gode::NodeRuntime::init_once();

	// Run unit tests
	// gode::TestRunner::run_tests();
}

void uninitialize_node_module(godot::ModuleInitializationLevel p_level) {
	if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	godot::Engine::get_singleton()->unregister_script_language(gode::JavascriptLanguage::get_singleton());
	godot::Engine::get_singleton()->unregister_script_language(gode::TypescriptLanguage::get_singleton());
	godot::ResourceSaver::get_singleton()->remove_resource_format_saver(gode::JavascriptSaver::get_singleton());
	godot::ResourceSaver::get_singleton()->remove_resource_format_saver(gode::TypescriptSaver::get_singleton());
	godot::ResourceLoader::get_singleton()->remove_resource_format_loader(gode::JavascriptLoader::get_singleton());
	godot::ResourceLoader::get_singleton()->remove_resource_format_loader(gode::TypescriptLoader::get_singleton());

	// 注销并删除事件循环单例
	if (event_loop_singleton) {
		godot::Engine::get_singleton()->unregister_singleton("GodeEventLoop");
		memdelete(event_loop_singleton);
		event_loop_singleton = nullptr;
	}

	gode::NodeRuntime::shutdown();
}

extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT node_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_node_module);
	init_obj.register_terminator(uninitialize_node_module);
	init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
