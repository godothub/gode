from typing import Optional, Sequence, Set


METHOD_BIND_OUT_ARGUMENTS = {
    ("ResourceLoader", "load_threaded_get_status"): (1,),
    ("OS", "execute"): (2,),
    ("EditorExportPlatform", "ssh_run_on_remote"): (4,),
    ("VideoStreamPlayback", "mix_audio"): (1,),
}

VARIANT_OPERATOR_ENUM_NAMES = {
    '==': 'OP_EQUAL',
    '!=': 'OP_NOT_EQUAL',
    '<': 'OP_LESS',
    '<=': 'OP_LESS_EQUAL',
    '>': 'OP_GREATER',
    '>=': 'OP_GREATER_EQUAL',
    '+': 'OP_ADD',
    '-': 'OP_SUBTRACT',
    'unary-': 'OP_NEGATE',
    'unary+': 'OP_POSITIVE',
    '*': 'OP_MULTIPLY',
    '/': 'OP_DIVIDE',
    '%': 'OP_MODULE',
    '**': 'OP_POWER',
    '<<': 'OP_SHIFT_LEFT',
    '>>': 'OP_SHIFT_RIGHT',
    '&': 'OP_BIT_AND',
    '|': 'OP_BIT_OR',
    '^': 'OP_BIT_XOR',
    '~': 'OP_BIT_NEGATE',
    'and': 'OP_AND',
    'or': 'OP_OR',
    'xor': 'OP_XOR',
    'not': 'OP_NOT',
    'in': 'OP_IN',
}

BUILTIN_OPERATOR_METHOD_NAMES = {
    '==': 'equal',
    '!=': 'not_equal',
    '<': 'less',
    '<=': 'less_equal',
    '>': 'greater',
    '>=': 'greater_equal',
    '+': 'add',
    '-': 'subtract',
    'unary-': 'negate',
    'unary+': 'positive',
    '*': 'multiply',
    '/': 'divide',
    '%': 'module',
    '**': 'power',
    '<<': 'bit_shift_left',
    '>>': 'bit_shift_right',
    '&': 'bit_and',
    '|': 'bit_or',
    '^': 'bit_xor',
    '~': 'bit_not',
}

SKIPPED_BUILTIN_OPERATORS = frozenset({'and', 'or', 'xor', 'not', 'in'})


def _clean_pointer_type(type_name: str) -> str:
    clean_type = type_name.replace('*', '').strip()
    if clean_type.startswith('const '):
        clean_type = clean_type[6:].strip()
    return clean_type


def is_safe_pointer_argument(type_name: str, object_class_names: Optional[Set[str]] = None) -> bool:
    if '*' not in type_name:
        return True
    if object_class_names is None:
        return False
    return _clean_pointer_type(type_name) in object_class_names


def skipped_method_reason(method: dict, object_class_names: Optional[Set[str]] = None) -> Optional[str]:
    return_value = method.get('return_value')
    return_type = method.get('return_type')
    if return_value:
        return_type = return_value.get('type') if isinstance(return_value, dict) else return_value
    if return_type and '*' in return_type:
        return f"raw pointer return type {return_type}"

    for arg in method.get('arguments', []):
        arg_type = arg.get('type', '')
        if not is_safe_pointer_argument(arg_type, object_class_names):
            return f"raw pointer argument {arg.get('name', '<unnamed>')}: {arg_type}"

    return None


def is_method_bindable(method: dict, object_class_names: Optional[Set[str]] = None) -> bool:
    return skipped_method_reason(method, object_class_names) is None


def resolve_property_accessor(name: str, method_names: Set[str]) -> Optional[str]:
    if not name:
        return None
    if name in method_names:
        return name
    if name.startswith('_') and name[1:] in method_names:
        return name[1:]
    return None


def method_conflicts_with_builtin_member(method_name: str, member_names: Set[str]) -> bool:
    return (
        method_name.startswith('get_') and method_name[4:] in member_names
    ) or (
        method_name.startswith('set_') and method_name[4:] in member_names
    )


def builtin_operator_method_name(operator_symbol: str) -> Optional[str]:
    if operator_symbol in SKIPPED_BUILTIN_OPERATORS:
        return None
    return BUILTIN_OPERATOR_METHOD_NAMES.get(operator_symbol)


def variant_operator_enum_name(operator_symbol: str) -> Optional[str]:
    return VARIANT_OPERATOR_ENUM_NAMES.get(operator_symbol)


def method_bind_out_argument_indices(class_name: str, method: dict) -> Sequence[int]:
    return METHOD_BIND_OUT_ARGUMENTS.get((class_name, method.get("name", "")), ())
