@tool
extends EditorPlugin

const EDITOR_ICON_THEME_TYPE := "EditorIcons"
const LANGUAGE_ICONS := {
	"JavaScript": "res://addons/gode/icons/javascript.svg",
	"TypeScript": "res://addons/gode/icons/typescript.svg",
}

var _previous_icons := {}
var _icon_registration_pending := false
var _icons_registered := false

func _enter_tree() -> void:
	_queue_register_language_icons()

func _exit_tree() -> void:
	_icon_registration_pending = false

func _enable_plugin() -> void:
	_queue_register_language_icons()
	if not ProjectSettings.has_setting("autoload/EventLoop"):
		add_autoload_singleton("EventLoop", "res://addons/gode/runtime/event_loop.gd")

func _disable_plugin() -> void:
	_restore_language_icons()
	if ProjectSettings.has_setting("autoload/EventLoop"):
		remove_autoload_singleton("EventLoop")

func _queue_register_language_icons() -> void:
	if _icon_registration_pending or _icons_registered:
		return

	_icon_registration_pending = true
	_register_language_icons.call_deferred()

func _register_language_icons() -> void:
	_icon_registration_pending = false

	var theme := get_editor_interface().get_editor_theme()
	if not theme:
		return

	for icon_name in LANGUAGE_ICONS:
		var icon_path: String = LANGUAGE_ICONS[icon_name]
		var icon := load(icon_path)
		if not icon:
			push_warning("gode: failed to load editor icon: " + icon_path)
			continue

		if not _previous_icons.has(icon_name):
			_previous_icons[icon_name] = theme.get_icon(icon_name, EDITOR_ICON_THEME_TYPE) if theme.has_icon(icon_name, EDITOR_ICON_THEME_TYPE) else null

		theme.set_icon(icon_name, EDITOR_ICON_THEME_TYPE, icon)
	_icons_registered = true

func _restore_language_icons() -> void:
	if _previous_icons.is_empty():
		return

	var theme := get_editor_interface().get_editor_theme()
	if not theme:
		return

	for icon_name in _previous_icons:
		var previous_icon = _previous_icons[icon_name]
		if previous_icon:
			theme.set_icon(icon_name, EDITOR_ICON_THEME_TYPE, previous_icon)
		else:
			theme.clear_icon(icon_name, EDITOR_ICON_THEME_TYPE)
	_previous_icons.clear()
	_icons_registered = false
