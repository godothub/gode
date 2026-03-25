@tool
extends EditorPlugin

var _init_button: Button
var _compile_button: Button

func _enable_plugin() -> void:
	pass

func _disable_plugin() -> void:
	pass

func _enter_tree() -> void:
	add_autoload_singleton("EventLoop", "res://addons/gode/script/event_loop.gd")

	_init_button = Button.new()
	_init_button.text = "Init TS Project"
	_init_button.tooltip_text = "Initialize TypeScript / gode project (tsconfig, package.json, npm install)"
	_init_button.pressed.connect(_on_init_pressed)
	add_control_to_container(EditorPlugin.CONTAINER_TOOLBAR, _init_button)

	_compile_button = Button.new()
	_compile_button.text = "Compile TS"
	_compile_button.tooltip_text = "Compile TypeScript via tsc"
	_compile_button.pressed.connect(_on_compile_pressed)
	add_control_to_container(EditorPlugin.CONTAINER_TOOLBAR, _compile_button)


func _exit_tree() -> void:
	remove_autoload_singleton("EventLoop")

	if _init_button:
		remove_control_from_container(EditorPlugin.CONTAINER_TOOLBAR, _init_button)
		_init_button.queue_free()
		_init_button = null

	if _compile_button:
		remove_control_from_container(EditorPlugin.CONTAINER_TOOLBAR, _compile_button)
		_compile_button.queue_free()
		_compile_button = null


func _on_init_pressed() -> void:
	var project_dir := ProjectSettings.globalize_path("res://")
	var plugin_path := (get_script() as GDScript).resource_path
	var addon_dir   := ProjectSettings.globalize_path(plugin_path.get_base_dir())

	var is_windows  := OS.get_name() == "Windows"
	var script_name := "init.bat" if is_windows else "init.sh"
	var init_script := addon_dir + "/" + script_name

	if not FileAccess.file_exists(init_script):
		OS.alert("%s not found in:\n%s" % [script_name, addon_dir], "gode: Init Failed")
		return

	var output    := []
	var exit_code: int

	if is_windows:
		exit_code = OS.execute("cmd.exe", ["/c", init_script, project_dir], output, true)
	else:
		exit_code = OS.execute("bash", [init_script, project_dir], output, true)

	var log_text := "\n".join(output)

	if exit_code == 0:
		OS.alert("Project initialized!\n\n" + log_text, "gode: Done")
	else:
		OS.alert("Init failed (exit %d):\n\n%s" % [exit_code, log_text], "gode: Failed")


func _on_compile_pressed() -> void:
	var project_dir := ProjectSettings.globalize_path("res://")
	var tsconfig := project_dir.path_join("tsconfig.json")
	var is_windows := OS.get_name() == "Windows"
	var tsc_bin := project_dir.path_join("node_modules/.bin/tsc")
	var output := []
	var exit_code: int

	if not FileAccess.file_exists(tsconfig):
		OS.alert("tsconfig.json not found.\nPlease run \"Init TS Project\" first.", "gode: Compile Failed")
		return

	if is_windows:
		var tsc_cmd := tsc_bin + ".cmd"
		if FileAccess.file_exists(tsc_cmd):
			exit_code = OS.execute("cmd.exe", ["/c", "cd /d \"" + project_dir + "\" && \"" + tsc_cmd + "\" --project \"" + tsconfig + "\""], output, true)
		else:
			exit_code = OS.execute("cmd.exe", ["/c", "cd /d \"" + project_dir + "\" && npx tsc --project \"" + tsconfig + "\""], output, true)
	else:
		if FileAccess.file_exists(tsc_bin):
			exit_code = OS.execute("bash", ["-lc", "cd \"" + project_dir + "\" && \"" + tsc_bin + "\" --project \"" + tsconfig + "\""], output, true)
		else:
			exit_code = OS.execute("bash", ["-lc", "cd \"" + project_dir + "\" && npx tsc --project \"" + tsconfig + "\""], output, true)

	var log_text := "\n".join(output)
	if exit_code == 0:
		OS.alert("TypeScript compiled successfully.\n\n" + log_text, "gode: Compile Done")
	else:
		OS.alert("TypeScript compile failed (exit %d):\n\n%s" % [exit_code, log_text], "gode: Compile Failed")
