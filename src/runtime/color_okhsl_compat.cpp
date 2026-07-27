#include "runtime/color_okhsl_compat.h"

#include "runtime/ok_color.h"

#include <algorithm>
#include <cmath>

namespace gode::color_okhsl_compat {

static godot::real_t clamp01(godot::real_t value) {
	return std::clamp<godot::real_t>(value, 0.0, 1.0);
}

static ok_color::HSL to_ok_hsl(const godot::Color &color) {
	ok_color::RGB rgb;
	rgb.r = color.r;
	rgb.g = color.g;
	rgb.b = color.b;
	return ok_color::srgb_to_okhsl(rgb);
}

static godot::real_t normalized_channel(godot::real_t value) {
	if (std::isnan(static_cast<double>(value))) {
		return 0.0;
	}
	return clamp01(value);
}

static void set_ok_hsl(godot::Color &color, godot::real_t h, godot::real_t s, godot::real_t l, godot::real_t alpha) {
	ok_color::HSL hsl;
	hsl.h = static_cast<float>(h);
	hsl.s = static_cast<float>(s);
	hsl.l = static_cast<float>(l);

	const ok_color::RGB rgb = ok_color::okhsl_to_srgb(hsl);
	const godot::Color converted(rgb.r, rgb.g, rgb.b, static_cast<float>(alpha));
	color = converted.clamp();
}

godot::Color from_ok_hsl(godot::real_t h, godot::real_t s, godot::real_t l, godot::real_t alpha) {
	godot::Color color;
	set_ok_hsl(color, h, s, l, alpha);
	return color;
}

godot::real_t get_ok_hsl_h(const godot::Color &color) {
	return normalized_channel(to_ok_hsl(color).h);
}

godot::real_t get_ok_hsl_s(const godot::Color &color) {
	return normalized_channel(to_ok_hsl(color).s);
}

godot::real_t get_ok_hsl_l(const godot::Color &color) {
	return normalized_channel(to_ok_hsl(color).l);
}

void set_ok_hsl_h(godot::Color &color, godot::real_t h) {
	set_ok_hsl(color, h, get_ok_hsl_s(color), get_ok_hsl_l(color), color.a);
}

void set_ok_hsl_s(godot::Color &color, godot::real_t s) {
	set_ok_hsl(color, get_ok_hsl_h(color), s, get_ok_hsl_l(color), color.a);
}

void set_ok_hsl_l(godot::Color &color, godot::real_t l) {
	set_ok_hsl(color, get_ok_hsl_h(color), get_ok_hsl_s(color), l, color.a);
}

} // namespace gode::color_okhsl_compat
