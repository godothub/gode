#ifndef GODOT_GODE_TYPESCRIPT_LANGUAGE_H
#define GODOT_GODE_TYPESCRIPT_LANGUAGE_H

#include "support/javascript/javascript_language.h"

namespace gode {

class TypescriptLanguage : public JavascriptLanguage {
	GDCLASS(TypescriptLanguage, JavascriptLanguage);

public:
	~TypescriptLanguage();
	static TypescriptLanguage *get_singleton();

private:
	static TypescriptLanguage *singleton;

protected:
	static void _bind_methods() {}

public:
	godot::String _get_name() const override;
	godot::String _get_type() const override;
	godot::String _get_extension() const override;
	godot::PackedStringArray _get_recognized_extensions() const override;
	godot::Object *_create_script() const override;
	godot::Dictionary _get_global_class_name(const godot::String &p_path) const override;
};

} // namespace gode

#endif // GODOT_GODE_TYPESCRIPT_LANGUAGE_H
