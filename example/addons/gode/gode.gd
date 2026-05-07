@tool
extends EditorPlugin

func _enable_plugin() -> void:
	if not ProjectSettings.has_setting("autoload/EventLoop"):
		add_autoload_singleton("EventLoop", "res://addons/gode/runtime/event_loop.gd")

func _disable_plugin() -> void:
	if ProjectSettings.has_setting("autoload/EventLoop"):
		remove_autoload_singleton("EventLoop")
