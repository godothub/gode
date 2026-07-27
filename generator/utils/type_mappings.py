GODOT_BUILTIN_TYPES = {
    'String', 'StringName', 'NodePath', 'Variant',
    'Vector2', 'Vector2i', 'Rect2', 'Rect2i',
    'Vector3', 'Vector3i', 'Transform2D',
    'Vector4', 'Vector4i', 'Plane',
    'Quaternion', 'AABB', 'Basis', 'Transform3D',
    'Projection', 'Color', 'Callable', 'Signal',
    'Dictionary', 'Array',
    'PackedByteArray', 'PackedInt32Array', 'PackedInt64Array',
    'PackedFloat32Array', 'PackedFloat64Array', 'PackedStringArray',
    'PackedVector2Array', 'PackedVector3Array', 'PackedColorArray',
    'PackedVector4Array', 'RID',
}

GENERATED_BUILTIN_TYPES = GODOT_BUILTIN_TYPES - {'Variant'}

PACKED_ARRAY_ELEMENT_TYPES = {
    'PackedByteArray': 'int',
    'PackedInt32Array': 'int',
    'PackedInt64Array': 'int',
    'PackedFloat32Array': 'float',
    'PackedFloat64Array': 'float',
    'PackedStringArray': 'String',
    'PackedVector2Array': 'Vector2',
    'PackedVector3Array': 'Vector3',
    'PackedColorArray': 'Color',
    'PackedVector4Array': 'Vector4',
}

PACKED_ARRAY_TYPES = frozenset(PACKED_ARRAY_ELEMENT_TYPES)

JS_CLASS_RENAME_MAP = {
    'Object': 'GodotObject',
    'String': 'GDString',
    'Dictionary': 'GDDictionary',
    'Array': 'GDArray',
}


def js_class_name(godot_name):
    return JS_CLASS_RENAME_MAP.get(godot_name, godot_name)


VARIANT_TYPE_ID_MAP = {
    '0': 'Nil',
    '1': 'bool',
    '2': 'int',
    '3': 'float',
    '4': 'String',
    '5': 'Vector2',
    '6': 'Vector2i',
    '7': 'Rect2',
    '8': 'Rect2i',
    '9': 'Vector3',
    '10': 'Vector3i',
    '11': 'Transform2D',
    '12': 'Vector4',
    '13': 'Vector4i',
    '14': 'Plane',
    '15': 'Quaternion',
    '16': 'AABB',
    '17': 'Basis',
    '18': 'Transform3D',
    '19': 'Projection',
    '20': 'Color',
    '21': 'StringName',
    '22': 'NodePath',
    '23': 'RID',
    '24': 'Object',
    '25': 'Callable',
    '26': 'Signal',
    '27': 'Dictionary',
    '28': 'Array',
    '29': 'PackedByteArray',
    '30': 'PackedInt32Array',
    '31': 'PackedInt64Array',
    '32': 'PackedFloat32Array',
    '33': 'PackedFloat64Array',
    '34': 'PackedStringArray',
    '35': 'PackedVector2Array',
    '36': 'PackedVector3Array',
    '37': 'PackedColorArray',
    '38': 'PackedVector4Array',
}

INTEGER_META_TYPES = {
    'int8': 'int8_t',
    'int16': 'int16_t',
    'int32': 'int32_t',
    'int64': 'int64_t',
    'uint8': 'uint8_t',
    'uint16': 'uint16_t',
    'uint32': 'uint32_t',
    'uint64': 'uint64_t',
    'char16': 'char16_t',
    'char32': 'char32_t',
}


def parse_typedarray_element_type(type_str):
    """
    Parse typedarray forms from extension_api.json:
      - typedarray::Node
      - typedarray::int
      - typedarray::24/17:CompositorEffect
      - typedarray::27/0:
    Returns a Godot type token for the element.
    """
    payload = type_str[len('typedarray::'):]
    if not payload:
        return 'Variant'

    if ':' in payload:
        meta, explicit = payload.split(':', 1)
        payload = explicit if explicit else meta

    if '/' in payload:
        maybe_variant_id = payload.split('/', 1)[0]
        payload = VARIANT_TYPE_ID_MAP.get(maybe_variant_id, payload)

    payload = payload.strip().lstrip('-')
    return payload or 'Variant'


def parse_typeddictionary_types(type_str):
    """
    Parse typeddictionary forms from extension_api.json:
      - typeddictionary::int;String
      - typeddictionary::Color;Color
    Returns a pair of Godot type tokens: (key_type, value_type).
    """
    payload = type_str[len('typeddictionary::'):].strip()
    if not payload:
        return 'Variant', 'Variant'

    if ';' not in payload:
        type_name = payload.lstrip('-') or 'Variant'
        return type_name, 'Variant'

    key_type, value_type = payload.split(';', 1)
    key_type = key_type.strip().lstrip('-') or 'Variant'
    value_type = value_type.strip().lstrip('-') or 'Variant'
    return key_type, value_type


def typed_collection_element_types(type_name):
    if type_name.startswith('typedarray::'):
        return [parse_typedarray_element_type(type_name)]
    if type_name.startswith('typeddictionary::'):
        return list(parse_typeddictionary_types(type_name))
    return []


def _cpp_type_argument(type_name, refcounted_classes):
    if type_name == 'Nil':
        return 'godot::Variant'
    if type_name == 'int':
        return 'int64_t'
    if type_name == 'float':
        return 'godot::real_t'
    if type_name == 'bool':
        return 'bool'
    if type_name == 'Variant':
        return 'godot::Variant'
    if type_name in GODOT_BUILTIN_TYPES:
        return f"godot::{type_name}"
    if type_name in refcounted_classes:
        return f"godot::Ref<godot::{type_name}>"
    return f"godot::{type_name}"


def get_cpp_type(type_name, meta='', refcounted_classes=None, is_arg=False):
    refcounted_classes = refcounted_classes or set()

    if type_name == 'void':
        return 'void'

    if type_name.startswith('typedarray::'):
        element_type = parse_typedarray_element_type(type_name)
        cpp_type = f"godot::TypedArray<{_cpp_type_argument(element_type, refcounted_classes)}>"
        return f"const {cpp_type} &" if is_arg else cpp_type

    if type_name.startswith('typeddictionary::'):
        key_type, value_type = parse_typeddictionary_types(type_name)
        cpp_type = (
            "godot::TypedDictionary<"
            f"{_cpp_type_argument(key_type, refcounted_classes)}, "
            f"{_cpp_type_argument(value_type, refcounted_classes)}>"
        )
        return f"const {cpp_type} &" if is_arg else cpp_type

    if type_name == 'int':
        return INTEGER_META_TYPES.get(meta, 'int64_t')

    if type_name == 'float':
        return 'godot::real_t'
    if type_name == 'bool':
        return 'bool'

    if type_name.startswith('enum::'):
        return f"godot::{type_name.replace('enum::', '').replace('.', '::')}"
    if type_name.startswith('bitfield::'):
        return f"godot::BitField<godot::{type_name.replace('bitfield::', '').replace('.', '::')}>"

    if type_name in GODOT_BUILTIN_TYPES:
        return f"const godot::{type_name} &" if is_arg else f"godot::{type_name}"

    if type_name in refcounted_classes:
        return f"const godot::Ref<godot::{type_name}> &" if is_arg else f"godot::Ref<godot::{type_name}>"

    return f"godot::{type_name} *"


def default_arg_napi_expr(arg, env_expr='info.Env()'):
    value = arg.get('default_value')
    if value is None:
        return f"{env_expr}.Undefined()"

    arg_type = arg.get('type')
    arg_meta = arg.get('meta', '')
    if value in ('null', 'nullptr'):
        return f"{env_expr}.Null()"
    if arg_type == 'bool' or value in ('true', 'false'):
        return f"Napi::Boolean::New({env_expr}, {value})"
    if arg_type == 'int':
        if arg_meta.startswith('uint') or arg_meta in ('char16', 'char32'):
            return f"gode::godot_uint_to_napi({env_expr}, static_cast<uint64_t>({value}))"
        return f"gode::godot_int_to_napi({env_expr}, static_cast<int64_t>({value}))"
    if arg_type == 'float' or isinstance(value, float):
        return f"Napi::Number::New({env_expr}, static_cast<double>({value}))"
    if isinstance(value, int):
        return f"gode::godot_int_to_napi({env_expr}, static_cast<int64_t>({value}))"
    if isinstance(value, str) and arg_type and arg_type.startswith('typedarray::'):
        return f"gode::godot_to_napi({env_expr}, godot::Array())"
    if isinstance(value, str) and arg_type in GODOT_BUILTIN_TYPES:
        if value in ('[]', '{}'):
            return f"gode::godot_to_napi({env_expr}, godot::{arg_type}())"
        cpp_value = value
        if value.startswith(f"{arg_type}("):
            cpp_value = value.replace(f"{arg_type}(", f"godot::{arg_type}(", 1).replace("inf", "INFINITY")
        elif arg_type == 'RID' and value == 'RID()':
            cpp_value = 'godot::RID()'
        elif arg_type == 'StringName' and value.startswith('&"'):
            cpp_value = f"godot::StringName({value[1:]})"
        elif arg_type == 'NodePath' and value.startswith('^"'):
            cpp_value = f"godot::NodePath({value[1:]})"
        elif arg_type in ('String', 'StringName', 'NodePath') and value.startswith('"'):
            cpp_value = f"godot::{arg_type}({value})"
        return f"gode::godot_to_napi({env_expr}, {cpp_value})"
    if isinstance(value, str) and value.lstrip('-').isdigit():
        return f"gode::godot_int_to_napi({env_expr}, static_cast<int64_t>({value}))"
    if isinstance(value, str) and value.startswith('"'):
        return f"Napi::String::New({env_expr}, {value})"
    return f"{env_expr}.Undefined()"


def constant_cpp_value(constant):
    value = constant.get('value')
    type_name = constant.get('type')
    if isinstance(value, str) and type_name and value.startswith(f"{type_name}("):
        return value.replace(f"{type_name}(", f"godot::{type_name}(", 1).replace("inf", "INFINITY")
    return value
