#ifndef GODE_VARIANTWRAPPED_H
#define GODE_VARIANTWRAPPED_H

#include <napi.h>
#include <godot_cpp/variant/variant.hpp>

class VariantWrapped : public Napi::ObjectWrap<VariantWrapped> {
	godot::Variant var;
};

#endif //GODE_VARIANTWRAPPED_H
