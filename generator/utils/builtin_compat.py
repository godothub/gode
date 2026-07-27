COLOR_OKHSL_COMPAT_INCLUDE = "runtime/color_okhsl_compat.h"

BUILTIN_METHOD_COMPAT = {
    ("Color", "from_ok_hsl"): {
        "function": "gode::color_okhsl_compat::from_ok_hsl",
        "include": COLOR_OKHSL_COMPAT_INCLUDE,
    },
}

BUILTIN_MEMBER_COMPAT = {
    ("Color", "ok_hsl_h"): {
        "getter": "gode::color_okhsl_compat::get_ok_hsl_h",
        "setter": "gode::color_okhsl_compat::set_ok_hsl_h",
        "include": COLOR_OKHSL_COMPAT_INCLUDE,
    },
    ("Color", "ok_hsl_s"): {
        "getter": "gode::color_okhsl_compat::get_ok_hsl_s",
        "setter": "gode::color_okhsl_compat::set_ok_hsl_s",
        "include": COLOR_OKHSL_COMPAT_INCLUDE,
    },
    ("Color", "ok_hsl_l"): {
        "getter": "gode::color_okhsl_compat::get_ok_hsl_l",
        "setter": "gode::color_okhsl_compat::set_ok_hsl_l",
        "include": COLOR_OKHSL_COMPAT_INCLUDE,
    },
}


def builtin_method_compat(class_name, method_name):
    return BUILTIN_METHOD_COMPAT.get((class_name, method_name))


def builtin_member_compat(class_name, member_name):
    return BUILTIN_MEMBER_COMPAT.get((class_name, member_name))
