extends Node

signal test_finished(success: bool, message: String)

const EMPTY_SCRIPT := "res://scripts/tests/runtime_empty_control.ts"
const DERIVED_SCRIPT := "res://scripts/tests/runtime_script_dispatch.ts"
const ARROW_SCRIPT := "res://scripts/tests/runtime_script_dispatch_arrow.ts"
const GETTER_SCRIPT := "res://scripts/tests/runtime_script_dispatch_getter.ts"

var controls: Array[Control] = []
var unhandled_clicks := 0
var button_clicks := 0
var button_enters := 0


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		unhandled_clicks += 1


func run_test() -> void:
	var plain := _add_control(NinePatchRect.new(), "PlainControl")
	var empty := _add_control(NinePatchRect.new(), "EmptyControl", EMPTY_SCRIPT)
	var button := _add_control(TextureButton.new(), "EmptyButton", EMPTY_SCRIPT)
	button.connect("button_up", func(): button_clicks += 1)
	button.mouse_entered.connect(func(): button_enters += 1)
	var derived := _add_control(NinePatchRect.new(), "DerivedControl", DERIVED_SCRIPT)
	var arrow := _add_control(NinePatchRect.new(), "ArrowControl", ARROW_SCRIPT)
	var removed := _add_control(NinePatchRect.new(), "RemovedScript", EMPTY_SCRIPT)
	removed.set_script(null)
	var getter := _add_control(Control.new(), "GetterControl", GETTER_SCRIPT)
	if not _check(empty.get_tooltip() == "native tooltip", "Empty script suppressed the native tooltip"):
		return
	if not _check(not empty.has_method("_has_point"), "Empty script exposed a native virtual stub"):
		return
	if not _check(not derived.has_method("constructor"), "Class constructor was exposed as an instance method"):
		return
	if not _check(empty.call("get_name") == empty.name, "Native methods did not fall back to Godot"):
		return
	if not _check(derived.has_method("inheritedAdd") and derived.call("inheritedAdd", 2, 3) == 5, "Inherited script method lookup failed"):
		return
	if not _check(derived.get_method_argument_count("inheritedAdd") == 2, "Inherited method argument count was lost"):
		return
	if not _check(derived.has_method("ownMultiply") and derived.call("ownMultiply", 2, 3) == 6, "Instance-field method lookup failed"):
		return
	if not _check(derived.get_method_argument_count("ownMultiply") == 2, "Instance-field method argument count was lost"):
		return
	for method in ["declaredDefault", "inheritedDefault", "inheritedAllDefaults", "inheritedArrowDefault", "sameFileDefault", "sameFileArrowDefault", "ownDefault", "ownAllDefaults", "ownExpression", "ownExplicitThis", "constructorDefault", "constructorAllDefaults", "constructorAlias", "constructorInterface", "nullableCallback", "getterDefault", "getterAlias"]:
		if not _check(derived.get_method_argument_count(method) == 2, "Declared parameter count was lost for %s" % method):
			return
		if not _check(derived.call(method, 3, 4) == 7 and derived.call(method, 3) == 5, "Default parameter call failed for %s" % method):
			return
	for method in ["inheritedAllDefaults", "ownAllDefaults", "constructorAllDefaults"]:
		if not _check(derived.call(method) == 3, "All-default parameter call failed for %s" % method):
			return
	derived.call("clearNullable")
	if not _check(not derived.has_method("nullableCallback") and derived.get_method_argument_count("nullableCallback") == 0, "Static metadata exposed a non-function field"):
		return
	if not _check(derived.get_method_argument_count("ownSingle") == 1 and derived.call("ownSingle", 3) == 4, "Single-parameter arrow function metadata was lost"):
		return
	if not _check(derived.get_method_argument_count("dynamicOverride") == 1 and derived.call("dynamicOverride", 3) == 4, "Dynamic override reused its base declaration's arity"):
		return
	if not _check(derived.has_method("callback") and derived.call("callback", 7) == "DerivedControl:7", "Method accessor used a prototype as its receiver"):
		return
	if not _check(not derived.has_method("shadowedMethod"), "A non-function field exposed a shadowed base method"):
		return
	if not _check(str(derived) == "dispatch:DerivedControl" and str(arrow) == "arrow:ArrowControl", "Inherited or instance-field toString failed"):
		return
	for control in [derived, arrow, getter]:
		control.notification(9001)
		var leaf := "leaf" if control == derived else "arrow"
		if control == getter:
			leaf = "getter"
		var expected := "base:%s,%s:%s" % [control.name, leaf, control.name]
		if not _check(control.call("takeNotifications") == expected, "Notification order or receiver was incorrect"):
			return
		control.notification(9001, true)
		expected = "%s:%s,base:%s" % [leaf, control.name, control.name]
		if not _check(control.call("takeNotifications") == expected, "Reversed notification order was incorrect"):
			return
	await get_tree().process_frame
	await get_tree().process_frame
	for control in [derived, arrow]:
		if not _check(control.is_processing() and control.call("getTicks") > 0, "Inherited or instance-field _process was not enabled"):
			return
	if DisplayServer.get_name() != "headless":
		for control in [plain, empty, button, removed]:
			if not _check_hit(control, Vector2(50, 30), true):
				return
		for local_x in [50.0, 150.0]:
			if not _check_hit(derived, Vector2(local_x, 30), local_x < 100):
				return
			if not _check_hit(arrow, Vector2(local_x, 30), local_x >= 100):
				return
		if not _check(button_clicks == 1 and button_enters >= 1, "Scripted TextureButton did not emit mouse/button signals"):
			return
		for control in [derived, arrow]:
			if not _check(control.call("getGuiInputs") >= 2, "Inherited _gui_input did not receive mouse events"):
				return
		print("[GodeTest] GUI mouse picking passed")
	_cleanup()
	await get_tree().process_frame
	test_finished.emit(true, "")


func _add_control(control: Control, node_name: String, script_path: String = "") -> Control:
	control.name = node_name
	control.position = Vector2(20, 20 + controls.size() * 90)
	control.size = Vector2(200, 60)
	control.mouse_filter = Control.MOUSE_FILTER_STOP
	control.tooltip_text = "native tooltip"
	if not script_path.is_empty():
		control.set_script(load(script_path))
	add_child(control)
	controls.append(control)
	return control


func _check_hit(control: Control, local_point: Vector2, expected: bool) -> bool:
	var point := control.get_global_transform_with_canvas() * local_point
	var motion := InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	get_viewport().push_input(motion)
	if not _check((get_viewport().gui_get_hovered_control() == control) == expected, "GUI mouse picking failed for %s" % control.name):
		return false
	var before := unhandled_clicks
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.position = point
		click.global_position = point
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		get_viewport().push_input(click)
	return _check(unhandled_clicks - before == (0 if expected else 2), "GUI click handling failed for %s" % control.name)


func _check(condition: bool, message: String) -> bool:
	if not condition:
		_cleanup()
		test_finished.emit(false, message)
	return condition


func _cleanup() -> void:
	for control in controls:
		control.queue_free()
	controls.clear()
