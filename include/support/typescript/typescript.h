#ifndef GODOT_GODE_TYPESCRIPT_H
#define GODOT_GODE_TYPESCRIPT_H

#include "support/javascript/javascript.h"

namespace gode {

class Typescript : public Javascript {
	GDCLASS(Typescript, Javascript)

protected:
	static void _bind_methods() {}

public:
	bool compile() const override;
	Napi::Function get_default_class() const override;
	godot::ScriptLanguage *_get_language() const override;
};

} // namespace gode

#endif // GODOT_GODE_TYPESCRIPT_H
