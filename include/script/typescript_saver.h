#ifndef GODE_TYPESCRIPT_SAVER_H
#define GODE_TYPESCRIPT_SAVER_H

#include <godot_cpp/classes/resource_format_saver.hpp>

namespace gode {

class TypeScriptSaver : public godot::ResourceFormatSaver {
	GDCLASS(TypeScriptSaver, godot::ResourceFormatSaver);

private:
	TypeScriptSaver() = default;

public:
	~TypeScriptSaver();
	static TypeScriptSaver *get_singleton();

private:
	static TypeScriptSaver *singleton;

protected:
	static void _bind_methods() {}

public:
	godot::Error _save(const godot::Ref<godot::Resource> &p_resource, const godot::String &p_path, uint32_t p_flags) override;
	godot::Error _set_uid(const godot::String &p_path, int64_t p_uid) override;
	bool _recognize(const godot::Ref<godot::Resource> &p_resource) const override;
	godot::PackedStringArray _get_recognized_extensions(const godot::Ref<godot::Resource> &p_resource) const override;
	bool _recognize_path(const godot::Ref<godot::Resource> &p_resource, const godot::String &p_path) const override;
};

} // namespace gode

#endif // GODE_TYPESCRIPT_SAVER_H
