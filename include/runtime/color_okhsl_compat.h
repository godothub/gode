#ifndef GODE_UTILS_COLOR_OKHSL_COMPAT_H
#define GODE_UTILS_COLOR_OKHSL_COMPAT_H

#include <godot_cpp/variant/color.hpp>

namespace gode::color_okhsl_compat {

godot::Color from_ok_hsl(godot::real_t h, godot::real_t s, godot::real_t l, godot::real_t alpha = 1.0);

godot::real_t get_ok_hsl_h(const godot::Color &color);
godot::real_t get_ok_hsl_s(const godot::Color &color);
godot::real_t get_ok_hsl_l(const godot::Color &color);

void set_ok_hsl_h(godot::Color &color, godot::real_t h);
void set_ok_hsl_s(godot::Color &color, godot::real_t s);
void set_ok_hsl_l(godot::Color &color, godot::real_t l);

} // namespace gode::color_okhsl_compat

#endif // GODE_UTILS_COLOR_OKHSL_COMPAT_H
