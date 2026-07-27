@tool
extends EditorPlugin

var export_plugin: EditorExportPlugin

func _enable_plugin() -> void:
	if not ProjectSettings.has_setting("autoload/EventLoop"):
		add_autoload_singleton("EventLoop", "res://addons/gode/runtime/event_loop.gd")
	if export_plugin == null:
		export_plugin = preload("res://addons/gode/runtime/export_plugin.gd").new()
		add_export_plugin(export_plugin)

func _disable_plugin() -> void:
	if export_plugin != null:
		remove_export_plugin(export_plugin)
		export_plugin = null
	if ProjectSettings.has_setting("autoload/EventLoop"):
		remove_autoload_singleton("EventLoop")
