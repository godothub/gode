extends Node

const TEST_FINISHED_SIGNAL := "test_finished"
const RUN_TEST_METHOD := "run_test"
const PASS_MARKER := "[GodeTest] all tests passed"
const RUNTIME_RESOURCE_SCRIPT_PATH := "res://scripts/tests/runtime_resource_lifetime.ts"
const RUNTIME_NESTED_RESOURCE_PATH := "res://resources/tests/runtime_nested_outer.tres"
const RUNTIME_NESTED_INNER_PATH := "res://resources/tests/runtime_nested_inner.tres"
const RUNTIME_RELOAD_SCRIPT_PATH := "res://scripts/tests/runtime_reload_resource.ts"

var tests: Array[Node] = []
var current_index := -1
var current_test: Node = null


func _ready() -> void:
	for child in get_children():
		if child is Node:
			tests.append(child)

	if tests.is_empty():
		_fail("No Gode smoke tests were found")
		return

	call_deferred("_run_next")


func _run_next() -> void:
	current_index += 1
	if current_index >= tests.size():
		if not _verify_resource_script_lifetime():
			return
		if not _verify_nested_resource_fixture():
			return
		if not _verify_resource_script_reload():
			return
		print(PASS_MARKER)
		get_tree().quit(0)
		return

	current_test = tests[current_index]
	var callback := Callable(self, "_on_test_finished")
	if not current_test.has_signal(TEST_FINISHED_SIGNAL):
		_fail("%s did not expose %s" % [current_test.name, TEST_FINISHED_SIGNAL])
		return
	if current_test.is_connected(TEST_FINISHED_SIGNAL, callback):
		current_test.disconnect(TEST_FINISHED_SIGNAL, callback)
	current_test.connect(TEST_FINISHED_SIGNAL, callback)

	if not current_test.has_method(RUN_TEST_METHOD):
		_fail("%s did not expose %s" % [current_test.name, RUN_TEST_METHOD])
		return
	if current_test.name == "RuntimeIntegrationTest":
		current_test.set("editor_array", [11, "inspector", true])
		if current_test.get("editor_array") != [11, "inspector", true]:
			_fail("RuntimeIntegrationTest export Array did not round-trip through ScriptInstance")
			return
		current_test.set("editor_number_array", [0])
		if current_test.get("editor_number_array") != [0]:
			_fail("RuntimeIntegrationTest typed export Array did not round-trip through ScriptInstance")
			return
		current_test.set("editor_generic_number_array", [1, 2.5])
		if current_test.get("editor_generic_number_array") != [1, 2.5]:
			_fail("RuntimeIntegrationTest generic typed export Array did not round-trip through ScriptInstance")
			return
		current_test.set("editor_record_dictionary", {"from_gdscript": 8})
		if current_test.get("editor_record_dictionary") != {"from_gdscript": 8}:
			_fail("RuntimeIntegrationTest typed string-key Dictionary did not round-trip through ScriptInstance")
			return
		if not current_test.get("editor_interface_array").is_empty():
			_fail("RuntimeIntegrationTest interface array default was not empty")
			return
		var interface_element: Object = ClassDB.instantiate("TypeScriptInterfaceResource")
		current_test.set("editor_interface_array", [interface_element])
		var interface_fields: Array[String] = []
		for property in interface_element.get_property_list():
			interface_fields.append(str(property.name))
		if not interface_fields.has("label") or not interface_fields.has("amount") or not interface_fields.has("enabled") or not interface_fields.has("tags") or not interface_fields.has("metadata"):
			_fail("RuntimeIntegrationTest interface array element did not expose interface fields")
			return
		if interface_element.get("label") != "" or interface_element.get("amount") != 0 or interface_element.get("enabled") != false:
			_fail("RuntimeIntegrationTest interface array scalar fields did not use Godot defaults")
			return
		if not interface_element.get("tags").is_empty() or not interface_element.get("metadata").is_empty():
			_fail("RuntimeIntegrationTest interface array container fields did not use Godot defaults")
			return
		interface_element.set("label", "configured")
		interface_element.set("amount", 9)
		if interface_element.get("label") != "configured" or interface_element.get("amount") != 9:
			_fail("RuntimeIntegrationTest interface array element edits did not round-trip")
			return
		var int_key_dictionary := {}
		int_key_dictionary[7] = "seven"
		current_test.set("editor_int_key_map", int_key_dictionary)
		if current_test.get("editor_int_key_map").get(7) != "seven":
			_fail("RuntimeIntegrationTest typed int-key Dictionary did not round-trip through ScriptInstance")
			return
		current_test.set("static_number_array", [4, 5.5])
		if current_test.get("static_number_array") != [4, 5.5]:
			_fail("RuntimeIntegrationTest static export Array did not round-trip through ScriptInstance")
			return
		if current_test.has_method("staticBridgeAdd"):
			_fail("RuntimeIntegrationTest static method leaked into ScriptInstance method table")
			return
		var script: Object = current_test.get_script()
		if not script.call("has_static_method", "staticBridgeAdd"):
			_fail("RuntimeIntegrationTest script did not expose staticBridgeAdd as a static method")
			return
		if script.call("get_static_method_argument_count", "staticBridgeAdd") != 2:
			_fail("RuntimeIntegrationTest staticBridgeAdd argument count was not reported")
			return
		if script.call("call_static", "staticBridgeAdd", 6, 7) != 13:
			_fail("RuntimeIntegrationTest staticBridgeAdd did not return the expected value")
			return

	current_test.call(RUN_TEST_METHOD)


func _on_test_finished(success: bool, message: String = "") -> void:
	var test_name := "<unknown>"
	if current_test != null:
		test_name = str(current_test.name)
	if current_test != null:
		var callback := Callable(self, "_on_test_finished")
		if current_test.is_connected(TEST_FINISHED_SIGNAL, callback):
			current_test.disconnect(TEST_FINISHED_SIGNAL, callback)

	if success:
		print("[GodeTest] %s passed" % test_name)
		call_deferred("_run_next")
		return

	_fail("%s failed: %s" % [test_name, message])


func _fail(message: String) -> void:
	push_error("[GodeTest] " + message)
	get_tree().quit(1)


func _verify_nested_resource_fixture() -> bool:
	var nested_container := ResourceLoader.load(RUNTIME_NESTED_RESOURCE_PATH, "", ResourceLoader.CACHE_MODE_IGNORE_DEEP)
	if not nested_container is Resource:
		_fail("Nested Resource fixture did not load")
		return false

	var nested_resource = nested_container.get("nested")
	if not nested_resource is Resource:
		_fail("Resource export did not retain its nested Resource dependency")
		return false

	if nested_resource.resource_path != RUNTIME_NESTED_INNER_PATH:
		_fail("Nested Resource fixture loaded wrong dependency: %s" % nested_resource.resource_path)
		return false

	nested_container.set("nested", null)
	nested_resource.set_script(null)
	nested_container.set_script(null)
	return true


func _verify_resource_script_lifetime() -> bool:
	var resource_script = ResourceLoader.load(RUNTIME_RESOURCE_SCRIPT_PATH)
	var weak_script = weakref(resource_script)
	var resource := Resource.new()
	for iteration in range(2):
		resource.set_script(resource_script)
		if resource.get_script() != resource_script:
			_fail("Engine-created Resource did not keep its script")
			return false
		var child_resource = resource.get("child")
		if not child_resource is Resource or child_resource.get_script() == null:
			_fail("Resource created in a field initializer did not receive its script")
			return false
		if child_resource.get("label") != "field child":
			_fail("Resource field initializer did not preserve its exported state")
			return false
		resource.set_script(null)
	resource_script = null
	if weak_script.get_ref() != null:
		_fail("Detached Resource retained an orphan TypeScript script instance")
		return false
	return true


func _verify_resource_script_reload() -> bool:
	var resource_script = ResourceLoader.load(RUNTIME_RELOAD_SCRIPT_PATH)
	var resources: Array[Resource] = []
	var children: Array[Resource] = []
	for index in range(15):
		var resource := Resource.new()
		resource.set_script(resource_script)
		resource.set("label", index + 1)
		resources.append(resource)
		var child = resource.get("child")
		if not child is Resource or child.get_script() != resource_script:
			_fail("Reload fixture did not adopt its same-class child Resource")
			return false
		children.append(child)
	var constructed: Array = resource_script.call("call_static", "takeConstructed")
	var initial_count: int = resource_script.call("call_static", "constructionCount")
	if initial_count != 30:
		_fail("Reload fixture did not construct exactly 15 parents and 15 children")
		return false
	if resource_script.reload(true) != OK:
		_fail("Resource script reload with state preservation failed")
		return false
	constructed.append_array(resource_script.call("call_static", "takeConstructed"))
	if resource_script.call("call_static", "constructionCount") != initial_count * 3:
		_fail("Reload included same-class Resources created during its constructors")
		return false
	for index in range(resources.size()):
		if resources[index].get("label") != index + 1 or resources[index].get("child") != children[index]:
			_fail("Resource reload did not preserve exported state and child identity")
			return false
	_detach_reload_resources(constructed)
	resources.clear()
	children.clear()

	resource_script.call("call_static", "disableChildren")
	for index in range(15):
		var resource := Resource.new()
		resource.set_script(resource_script)
		resource.set("label", index + 1)
		resources.append(resource)
	constructed = resource_script.call("call_static", "takeConstructed")
	initial_count = resource_script.call("call_static", "constructionCount")
	if resource_script.reload(false) != OK:
		_fail("Resource script reload without state preservation failed")
		return false
	constructed.append_array(resource_script.call("call_static", "takeConstructed"))
	if resource_script.call("call_static", "constructionCount") != initial_count + resources.size():
		_fail("Resource reload did not reconstruct each existing instance exactly once")
		return false
	for resource in resources:
		if resource.get("label") != 0:
			_fail("Resource reload(false) preserved an exported value instead of its default")
			return false

	# The first constructor removes the scripts from all other pending owners.
	# Reload must resolve each owner's current instance before accessing it.
	resource_script.call("call_static", "detachOnNextConstruction", resources)
	initial_count = resource_script.call("call_static", "constructionCount")
	if resource_script.reload(true) != OK:
		_fail("Resource script reload with removed pending instances failed")
		return false
	constructed.append_array(resource_script.call("call_static", "takeConstructed"))
	if resource_script.call("call_static", "constructionCount") != initial_count + 1:
		_fail("Resource reload accessed instances detached by an earlier constructor")
		return false
	var attached_count := 0
	for resource in resources:
		if resource.get_script() == resource_script:
			attached_count += 1
	if attached_count != 1:
		_fail("Reload fixture did not detach all other pending instances")
		return false
	_detach_reload_resources(constructed)
	return true


func _detach_reload_resources(resources: Array) -> void:
	for resource in resources:
		if resource.get_script() != null:
			resource.set("child", null)
	for resource in resources:
		if resource.get_script() != null:
			resource.set_script(null)
	resources.clear()
