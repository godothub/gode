#include "compiler/typescript_compiler.h"
#include "napi.h"
#include "runtime/gode_event_loop.h"
#include "runtime/node_runtime.h"
#include "script/typescript_language.h"
#include "script/typescript_loader.h"
#include "script/typescript_saver.h"
#include "script/typescript_script.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>

namespace {

godot::Ref<gode::TypeScriptSaver> typescript_saver;
godot::Ref<gode::TypeScriptLoader> typescript_loader;

void initialize_node_module(godot::ModuleInitializationLevel p_level) {
	if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(gode::TypeScriptScript);
	GDREGISTER_CLASS(gode::TypeScriptLanguage);
	GDREGISTER_CLASS(gode::TypeScriptSaver);
	GDREGISTER_CLASS(gode::TypeScriptLoader);
	GDREGISTER_CLASS(gode::GodeEventLoop);
	GDREGISTER_CLASS(gode::GodeTypeScriptCompiler);
	godot::Engine::get_singleton()->register_script_language(gode::TypeScriptLanguage::get_singleton());
	typescript_saver = gode::TypeScriptSaver::get_singleton();
	typescript_loader = gode::TypeScriptLoader::get_singleton();

	godot::ResourceSaver::get_singleton()->add_resource_format_saver(typescript_saver);
	godot::ResourceLoader::get_singleton()->add_resource_format_loader(typescript_loader);

	gode::NodeRuntime::init_once();
}

void uninitialize_node_module(godot::ModuleInitializationLevel p_level) {
	if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	gode::TypeScriptLanguage *typescript_language = gode::TypeScriptLanguage::get_singleton();

	godot::Engine::get_singleton()->unregister_script_language(typescript_language);

	if (typescript_loader.is_valid()) {
		typescript_loader->clear_cache();
	}

	godot::ResourceSaver *resource_saver = godot::ResourceSaver::get_singleton();
	if (resource_saver) {
		if (typescript_saver.is_valid()) {
			resource_saver->remove_resource_format_saver(typescript_saver);
		}
	}

	godot::ResourceLoader *resource_loader = godot::ResourceLoader::get_singleton();
	if (resource_loader) {
		if (typescript_loader.is_valid()) {
			resource_loader->remove_resource_format_loader(typescript_loader);
		}
	}

	typescript_loader.unref();
	typescript_saver.unref();

	memdelete(typescript_language);

	gode::NodeRuntime::shutdown();
}

} // namespace

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
