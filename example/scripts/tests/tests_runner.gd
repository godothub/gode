extends Node

const TEST_FINISHED_SIGNAL := "test_finished"
const RUN_TEST_METHOD := "run_test"
const PASS_MARKER := "[GodeTest] all tests passed"
const RUNTIME_NESTED_RESOURCE_PATH := "res://resources/tests/runtime_nested_outer.tres"
const RUNTIME_NESTED_INNER_PATH := "res://resources/tests/runtime_nested_inner.tres"

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
		if not _verify_nested_resource_fixture():
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
