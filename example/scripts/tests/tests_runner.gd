extends Node

const TEST_FINISHED_SIGNAL := "test_finished"
const RUN_TEST_METHOD := "run_test"
const PASS_MARKER := "[GodeTest] all tests passed"

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
		var custom_resource := load("res://scripts/tests/runtime_array_resource.tres")
		if custom_resource == null:
			_fail("RuntimeIntegrationTest custom Resource fixture did not load")
			return
		current_test.set("editor_custom_resource_array", [custom_resource])
		var custom_resource_array: Array = current_test.get("editor_custom_resource_array")
		if custom_resource_array.size() != 1 or custom_resource_array[0] != custom_resource:
			_fail("RuntimeIntegrationTest custom Resource Array did not round-trip through ScriptInstance")
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
