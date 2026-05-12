import os
import json
from core.base_generator import CodeGenerator
from utils.api_path import find_extension_api_json

# Godot primitive → TypeScript type
PRIMITIVE_MAP = {
    'void':       'void',
    'bool':       'boolean',
    'int':        'number',
    'float':      'number',
    'Nil':        'null',
    'Object':     'GodotObject',
    'String':     'string',
    'StringName': 'string',
    'NodePath':   'string',
}

# JS-facing collection types used for return values.
JS_ARRAY_TYPE = 'VariantArgument[]'
JS_OBJECT_TYPE = '{ [key: string]: VariantArgument }'

# Variant.Type id -> Godot type name, used by typedarray encoded forms like "typedarray::27/0:"
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

# Builtin classes that map directly to JS primitives — skip class generation
SKIP_BUILTINS = frozenset(['Nil', 'void', 'bool', 'int', 'float'])

# Global enums to skip — already represented in the hand-crafted Variant class
SKIP_GLOBAL_ENUMS = frozenset(['Variant.Type', 'Variant.Operator'])

# Rename map: Godot name → TS name (avoids conflicts with JS built-ins)
RENAME_MAP = {
    'Object':     'GodotObject',
    'String':     'GDString',
    'Dictionary': 'GDDictionary',
    'Array':      'GDArray',
}

# Method/param names that are reserved in TypeScript/JS
TS_RESERVED = frozenset([
    'constructor', 'delete', 'class', 'new', 'return', 'typeof',
    'void', 'function', 'var', 'let', 'const', 'if', 'else',
    'for', 'while', 'break', 'continue', 'switch', 'case',
    'default', 'import', 'export', 'from', 'extends', 'super',
    'this', 'static', 'get', 'set', 'in', 'of', 'instanceof',
    'throw', 'try', 'catch', 'finally', 'async', 'await',
    'yield', 'debugger', 'with', 'enum',
])


def sanitize_name(name: str) -> str:
    name = name.replace('-', '_')
    if name in TS_RESERVED:
        return name + '_gd'
    return name


def parse_typedarray_element_type(type_str: str) -> str:
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
        if explicit:
            payload = explicit
        else:
            payload = meta

    if '/' in payload:
        maybe_variant_id = payload.split('/', 1)[0]
        payload = VARIANT_TYPE_ID_MAP.get(maybe_variant_id, payload)

    payload = payload.strip().lstrip('-')
    return payload or 'Variant'


def parse_typeddictionary_types(type_str: str) -> tuple[str, str]:
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
        t = payload.lstrip('-') or 'Variant'
        return t, 'Variant'

    key_type, value_type = payload.split(';', 1)
    key_type = key_type.strip().lstrip('-') or 'Variant'
    value_type = value_type.strip().lstrip('-') or 'Variant'
    return key_type, value_type


def godot_type_to_ts(type_str: str, is_input: bool = False) -> str:
    if not type_str:
        return 'void'

    # Comma-separated multi-type → TypeScript union
    # Strip leading '-' (Godot exclusion syntax, e.g. "-AnimatedTexture")
    if ',' in type_str:
        return ' | '.join(godot_type_to_ts(t.strip().lstrip('-'), is_input) for t in type_str.split(','))

    if type_str == 'Variant':
        return 'VariantArgument'  # Treat Variant as any type since we removed the binding

    # Handle Variant.Type enum specially
    if type_str == 'Variant.Type':
        return 'VariantType'

    # Handle Variant.Operator enum specially
    if type_str == 'Variant.Operator':
        return 'VariantOperator'

    if type_str == 'Callable' and is_input:
        return 'Callable | Function'

    if is_input and type_str in ('String', 'GDString', 'StringName'):
        return 'GDString | StringName | string'

    if type_str in PRIMITIVE_MAP:
        return PRIMITIVE_MAP[type_str]

    if type_str.startswith('enum::'):
        inner = type_str[6:]
        if '.' in inner:
            cls, enum = inner.split('.', 1)
            # Handle special cases for Variant enums
            if cls == 'Variant' and enum == 'Type':
                return 'VariantType'
            if cls == 'Variant' and enum == 'Operator':
                return 'VariantOperator'
            return f'{RENAME_MAP.get(cls, cls)}.{enum}'
        return inner  # global enum name

    if type_str.startswith('bitfield::'):
        inner = type_str[10:]
        if '.' in inner:
            cls, enum = inner.split('.', 1)
            return f'{RENAME_MAP.get(cls, cls)}.{enum}'
        return 'number'

    if type_str.startswith('typedarray::'):
        element_type = parse_typedarray_element_type(type_str)
        # typedarray is always represented as a JS generic array on the TypeScript side.
        element_ts = godot_type_to_ts(element_type, is_input=False)
        typed_array = f'Array<{element_ts}>'
        return f'GDArray | {typed_array}' if is_input else typed_array

    if type_str.startswith('typeddictionary::'):
        key_type, value_type = parse_typeddictionary_types(type_str)
        key_ts = godot_type_to_ts(key_type, is_input=False)
        value_ts = godot_type_to_ts(value_type, is_input=False)

        # JS object literals can only model PropertyKey keys precisely.
        if key_ts in ('string', 'number', 'symbol'):
            typed_container = f'Record<{key_ts}, {value_ts}>'
        else:
            typed_container = f'Map<{godot_type_to_ts(key_type, is_input=True)}, {value_ts}>'

        return f'GDDictionary | {typed_container}' if is_input else typed_container

    if type_str.endswith('*'):
        return godot_type_to_ts(type_str[:-1], is_input)

    if is_input and type_str in ('Dictionary', 'GDDictionary'):
        return f'GDDictionary | {JS_OBJECT_TYPE}'

    if is_input and type_str in ('Array', 'GDArray'):
        return f'GDArray | {JS_ARRAY_TYPE}'

    if not is_input and type_str == 'Array':
        return JS_ARRAY_TYPE

    if not is_input and type_str in ('Dictionary', 'GDDictionary'):
        return JS_OBJECT_TYPE

    return RENAME_MAP.get(type_str, type_str)


class DtsGenerator(CodeGenerator):

    def run(self):
        dts_pkg_dir  = os.path.dirname(os.path.abspath(__file__))
        generator_dir = os.path.dirname(dts_pkg_dir)
        project_root  = os.path.dirname(generator_dir)

        api_path   = find_extension_api_json()
        output_dir = os.path.join(project_root, 'example', 'addons', 'gode', 'types')
        godot_output_path = os.path.join(output_dir, 'godot.d.ts')
        globals_output_path = os.path.join(output_dir, 'globals.d.ts')

        with open(api_path, 'r', encoding='utf-8') as f:
            api = json.load(f)

        os.makedirs(output_dir, exist_ok=True)

        godot_lines = self._generate(api)
        globals_lines = self._generate_globals(api)

        with open(godot_output_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(godot_lines))
            f.write('\n')

        with open(globals_output_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(globals_lines))
            f.write('\n')

        print(f'Generated: {godot_output_path}')
        print(f'Generated: {globals_output_path}')

    def _has_raw_pointer(self, method: dict) -> bool:
        """Return True if any argument or the return value contains a raw pointer type."""
        for arg in method.get('arguments', []):
            if '*' in arg.get('type', ''):
                return True
        ret = method.get('return_value') or method.get('return_type')
        if ret:
            t = ret if isinstance(ret, str) else ret.get('type', '')
            if '*' in t:
                return True
        return False

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _ind(self, n: int) -> str:
        return '    ' * n

    def _format_params(self, arguments: list) -> str:
        parts = []
        optional_flags = [False] * len(arguments)

        # In extension_api.json, default arguments are usually trailing.
        # Mark only the trailing default arguments as optional in TypeScript.
        optional_tail = True
        for i in range(len(arguments) - 1, -1, -1):
            has_default = 'default_value' in arguments[i]
            if optional_tail and has_default:
                optional_flags[i] = True
            else:
                optional_tail = False

        for arg, is_optional in zip(arguments, optional_flags):
            name = sanitize_name(arg['name'])
            ts_type = godot_type_to_ts(arg['type'], is_input=True)
            opt = '?' if is_optional else ''
            parts.append(f'{name}{opt}: {ts_type}')
        return ', '.join(parts)

    # ── Enum ──────────────────────────────────────────────────────────────────

    def _gen_enum(self, enum_data: dict, indent: int) -> list:
        ind  = self._ind(indent)
        ind2 = self._ind(indent + 1)
        lines = [f'{ind}const enum {enum_data["name"]} {{']
        for val in enum_data.get('values', []):
            lines.append(f'{ind2}{val["name"]} = {val["value"]},')
        lines.append(f'{ind}}}')
        return lines

    # ── Builtin class ─────────────────────────────────────────────────────────

    def _gen_builtin(self, cls_data: dict, ts_name: str, indent: int) -> list:
        ind  = self._ind(indent)
        ind2 = self._ind(indent + 1)
        lines = [f'{ind}class {ts_name} {{']

        # Constructors
        for ctor in cls_data.get('constructors', []):
            args = ctor.get('arguments', [])
            params = self._format_params(args) if args else ''
            lines.append(f'{ind2}constructor({params});')

        # Members (instance properties)
        for member in cls_data.get('members', []):
            ts_type = godot_type_to_ts(member['type'])
            lines.append(f'{ind2}{member["name"]}: {ts_type};')

        # Constants
        for const in cls_data.get('constants', []):
            ts_type = godot_type_to_ts(const['type'])
            lines.append(f'{ind2}static readonly {const["name"]}: {ts_type};')

        # Methods
        for method in cls_data.get('methods', []):
            if self._has_raw_pointer(method):
                continue
            name   = sanitize_name(method['name'])
            ret    = godot_type_to_ts(method.get('return_type', 'void'))
            params = self._format_params(method.get('arguments', []))
            static = 'static ' if method.get('is_static') else ''
            if method.get('is_vararg'):
                params = (params + ', ...args: VariantArgument[]') if params else '...args: VariantArgument[]'
            lines.append(f'{ind2}{static}{name}({params}): {ret};')

        # Index signature
        idx_type = cls_data.get('indexing_return_type')
        if idx_type:
            lines.append(f'{ind2}[index: number]: {godot_type_to_ts(idx_type)};')

        lines.append(f'{ind}}}')

        # Declaration merging: namespace for nested enums
        enums = cls_data.get('enums', [])
        if enums:
            lines.append(f'{ind}namespace {ts_name} {{')
            for enum in enums:
                lines += self._gen_enum(enum, indent + 1)
            lines.append(f'{ind}}}')

        return lines

    # ── Object-derived class ──────────────────────────────────────────────────

    def _gen_class(self, cls_data: dict, indent: int) -> list:
        ind  = self._ind(indent)
        ind2 = self._ind(indent + 1)
        lines = []

        name     = godot_type_to_ts(cls_data['name'])
        inherits = cls_data.get('inherits', '')
        extends  = f' extends {godot_type_to_ts(inherits)}' if inherits else ' extends _GodotObject'
        lines.append(f'{ind}class {name}{extends} {{')

        # Constants
        for const in cls_data.get('constants', []):
            lines.append(f'{ind2}static readonly {const["name"]}: number;')

        # Properties
        # Track which method names the methods loop will declare (to avoid duplicates)
        declared_methods: set = set()
        for method in cls_data.get('methods', []):
            if not self._has_raw_pointer(method):
                declared_methods.add(method['name'])
        # Keep getter/setter method type overrides aligned with property declared types.
        property_method_overrides: dict = {}

        for prop in cls_data.get('properties', []):
            if '/' in prop['name']:  # skip grouped sub-properties
                continue
            ts_type = godot_type_to_ts(prop['type'])
            ts_type_input = godot_type_to_ts(prop['type'], is_input=True)
            getter  = prop.get('getter', '')
            setter  = prop.get('setter', '')
            lines.append(f'{ind2}get {prop["name"]}(): {ts_type};')
            if setter:
                lines.append(f'{ind2}set {prop["name"]}(value: {ts_type_input});')
                property_method_overrides[setter] = {'first_arg_type': ts_type_input}
            # Emit getter/setter as explicit methods when not already in the methods section
            if getter and getter not in declared_methods:
                lines.append(f'{ind2}{sanitize_name(getter)}(): {ts_type};')
            if getter:
                property_method_overrides[getter] = {'return_type': ts_type}
            if setter and setter not in declared_methods:
                lines.append(f'{ind2}{sanitize_name(setter)}(value: {ts_type_input}): void;')

        # Signals (as comments — no runtime type)
        for sig in cls_data.get('signals', []):
            params = self._format_params(sig.get('arguments', []))
            lines.append(f'{ind2}{sig["name"]}: Signal')

        # Methods
        for method in cls_data.get('methods', []):
            if self._has_raw_pointer(method):
                continue
            mname  = sanitize_name(method['name'])
            ret_v  = method.get('return_value') or {}
            ret    = godot_type_to_ts(ret_v.get('type', 'void'))
            params = self._format_params(method.get('arguments', []))
            override = property_method_overrides.get(method['name'])
            if override:
                if 'return_type' in override:
                    ret = override['return_type']
                if 'first_arg_type' in override:
                    args = method.get('arguments', [])
                    if args:
                        first_name = sanitize_name(args[0]['name'])
                        first_param = f'{first_name}: {override["first_arg_type"]}'
                        if len(args) > 1:
                            rest_params = self._format_params(args[1:])
                            params = f'{first_param}, {rest_params}' if rest_params else first_param
                        else:
                            params = first_param
            static = 'static ' if method.get('is_static') else ''
            if method.get('is_vararg'):
                params = (params + ', ...args: VariantArgument[]') if params else '...args: VariantArgument[]'
            lines.append(f'{ind2}{static}{mname}({params}): {ret};')

        lines.append(f'{ind}}}')

        # Namespace for nested enums
        enums = cls_data.get('enums', [])
        if enums:
            lines.append(f'{ind}namespace {name} {{')
            for enum in enums:
                lines += self._gen_enum(enum, indent + 1)
            lines.append(f'{ind}}}')

        return lines

    def _gen_utility_functions(self, api: dict, indent: int) -> list:
        ind = self._ind(indent)
        lines = []
        for func in api.get('utility_functions', []):
            name = sanitize_name(func['name'])
            ret = godot_type_to_ts(func.get('return_type', 'void'))
            params = self._format_params(func.get('arguments', []))
            if func.get('is_vararg'):
                params = (params + ', ...args: VariantArgument[]') if params else '...args: VariantArgument[]'
            lines.append(f'{ind}{ind}{name}({params}): {ret};')
        return lines

    def _gen_variant_type_enum(self) -> list:
        """Generate Variant.Type enum definition"""
        lines = []
        lines.append('    const enum VariantType {')
        lines.append('        TYPE_NIL = 0,')
        lines.append('        TYPE_BOOL = 1,')
        lines.append('        TYPE_INT = 2,')
        lines.append('        TYPE_FLOAT = 3,')
        lines.append('        TYPE_STRING = 4,')
        lines.append('        TYPE_VECTOR2 = 5,')
        lines.append('        TYPE_VECTOR2I = 6,')
        lines.append('        TYPE_RECT2 = 7,')
        lines.append('        TYPE_RECT2I = 8,')
        lines.append('        TYPE_VECTOR3 = 9,')
        lines.append('        TYPE_VECTOR3I = 10,')
        lines.append('        TYPE_TRANSFORM2D = 11,')
        lines.append('        TYPE_VECTOR4 = 12,')
        lines.append('        TYPE_VECTOR4I = 13,')
        lines.append('        TYPE_PLANE = 14,')
        lines.append('        TYPE_QUATERNION = 15,')
        lines.append('        TYPE_AABB = 16,')
        lines.append('        TYPE_BASIS = 17,')
        lines.append('        TYPE_TRANSFORM3D = 18,')
        lines.append('        TYPE_PROJECTION = 19,')
        lines.append('        TYPE_COLOR = 20,')
        lines.append('        TYPE_STRING_NAME = 21,')
        lines.append('        TYPE_NODE_PATH = 22,')
        lines.append('        TYPE_RID = 23,')
        lines.append('        TYPE_OBJECT = 24,')
        lines.append('        TYPE_CALLABLE = 25,')
        lines.append('        TYPE_SIGNAL = 26,')
        lines.append('        TYPE_DICTIONARY = 27,')
        lines.append('        TYPE_ARRAY = 28,')
        lines.append('        TYPE_PACKED_BYTE_ARRAY = 29,')
        lines.append('        TYPE_PACKED_INT32_ARRAY = 30,')
        lines.append('        TYPE_PACKED_INT64_ARRAY = 31,')
        lines.append('        TYPE_PACKED_FLOAT32_ARRAY = 32,')
        lines.append('        TYPE_PACKED_FLOAT64_ARRAY = 33,')
        lines.append('        TYPE_PACKED_STRING_ARRAY = 34,')
        lines.append('        TYPE_PACKED_VECTOR2_ARRAY = 35,')
        lines.append('        TYPE_PACKED_VECTOR3_ARRAY = 36,')
        lines.append('        TYPE_PACKED_COLOR_ARRAY = 37,')
        lines.append('        TYPE_PACKED_VECTOR4_ARRAY = 38,')
        lines.append('        TYPE_MAX = 39,')
        lines.append('    }')
        lines.append('')
        lines.append('    export const VariantType: typeof VariantType;')
        return lines

    def _gen_variant_operator_enum(self) -> list:
        """Generate Variant.Operator enum definition"""
        lines = []
        lines.append('    const enum VariantOperator {')
        lines.append('        OP_EQUAL = 0,')
        lines.append('        OP_NOT_EQUAL = 1,')
        lines.append('        OP_LESS = 2,')
        lines.append('        OP_LESS_EQUAL = 3,')
        lines.append('        OP_GREATER = 4,')
        lines.append('        OP_GREATER_EQUAL = 5,')
        lines.append('        OP_ADD = 6,')
        lines.append('        OP_SUBTRACT = 7,')
        lines.append('        OP_MULTIPLY = 8,')
        lines.append('        OP_DIVIDE = 9,')
        lines.append('        OP_NEGATE = 10,')
        lines.append('        OP_POSITIVE = 11,')
        lines.append('        OP_MODULE = 12,')
        lines.append('        OP_POWER = 13,')
        lines.append('        OP_SHIFT_LEFT = 14,')
        lines.append('        OP_SHIFT_RIGHT = 15,')
        lines.append('        OP_BIT_AND = 16,')
        lines.append('        OP_BIT_OR = 17,')
        lines.append('        OP_BIT_XOR = 18,')
        lines.append('        OP_BIT_NEGATE = 19,')
        lines.append('        OP_AND = 20,')
        lines.append('        OP_OR = 21,')
        lines.append('        OP_XOR = 22,')
        lines.append('        OP_NOT = 23,')
        lines.append('        OP_IN = 24,')
        lines.append('        OP_MAX = 25,')
        lines.append('    }')
        lines.append('')
        lines.append('    export const VariantOperator: typeof VariantOperator;')
        return lines

    def _collect_global_symbols(self, api: dict) -> list:
        symbols = []

        for cls in api.get('builtin_classes', []):
            name = cls['name']
            if name in SKIP_BUILTINS:
                continue
            symbols.append(RENAME_MAP.get(name, name))

        for singleton in api.get('singletons', []):
            symbols.append(singleton['name'])

        symbols.append('GD')

        # Preserve order while removing duplicates.
        return list(dict.fromkeys(symbols))

    def _generate_globals(self, api: dict) -> list:
        symbols = self._collect_global_symbols(api)
        symbols.append('VariantArgument')

        import_items = ', '.join(f'{name} as Godot{name}' for name in symbols)

        lines = []
        lines.append('// Auto-generated by generator/dts — do not edit manually.')
        lines.append('')
        lines.append(f'import {{ {import_items} }} from "godot";')
        lines.append('')
        lines.append('declare global {')
        for name in symbols:
            if name == 'Signal':
                # Signal 支持泛型类型注解：fieldName!: Signal<() => void>，由 tree-sitter 静态解析参数
                lines.append(f'  type {name}<T extends (...args: any[]) => void = (...args: any[]) => void> = Godot{name};')
                lines.append(f'  const {name}: typeof Godot{name};')
            else:
                lines.append(f'  type {name} = Godot{name};')
                lines.append(f'  const {name}: typeof Godot{name};')

        lines.append('  interface ExportOptions {')
        lines.append('    hint?: number;')
        lines.append('    hintString?: string;')
        lines.append('  }')
        lines.append('')
        lines.append('  function Export(hint: number, hintString?: string): any;')
        lines.append('  function Export(options?: ExportOptions): any;')
        lines.append('')
        lines.append('  function Tool(target: Function): void;')
        lines.append('')
        lines.append('  interface ExportEntry {')
        lines.append('    type: string;')
        lines.append('    hint?: number;')
        lines.append('    hint_string?: string;')
        lines.append('  }')
        lines.append('  type ExportMap = Record<string, ExportEntry>;')

        lines.append('}')
        lines.append('')
        lines.append('export {};')
        return lines

    # ── Top-level ─────────────────────────────────────────────────────────────

    def _generate(self, api: dict) -> list:
        # Collect all builtin classes that are generated
        builtin_types = []
        for cls in api.get('builtin_classes', []):
            name = cls['name']
            if name in SKIP_BUILTINS:
                continue
            builtin_types.append(RENAME_MAP.get(name, name))
        
        variant_arg_types = ['null', 'undefined', 'boolean', 'number', 'string', 'Function', 'Object'] + builtin_types
        variant_arg_types.extend([JS_OBJECT_TYPE, JS_ARRAY_TYPE])
        variant_arg_types = list(dict.fromkeys(variant_arg_types))
        variant_arg_str = ' | '.join(variant_arg_types)

        lines = []
        lines.append('// Auto-generated by generator/dts — do not edit manually.')
        lines.append('')
        lines.append('declare module "godot" {')
        lines.append('')
        lines.append(f'    type VariantArgument = {variant_arg_str};')
        lines.append('')

        # GodotObject base (every Object without an explicit parent inherits this)
        lines += [
            '    class _GodotObject {',
            '        get_instance_id(): number;',
            '        connect(signal: string, callable: (...args: VariantArgument[]) => void): void;',
            '        disconnect(signal: string, callable: (...args: VariantArgument[]) => void): void;',
            '        emit_signal(signal: string, ...args: VariantArgument[]): void;',
            '        to_signal(signal: string, options?: { timeoutMs?: number; abortSignal?: AbortSignal }): Promise<VariantArgument>;',
            '    }',
            '',
        ]

        # Global enums
        for enum in api.get('global_enums', []):
            if enum['name'] in SKIP_GLOBAL_ENUMS:
                continue
            lines += self._gen_enum(enum, indent=1)
            lines.append('')

        # Variant.Type enum (manually added since we removed VariantBinding)
        lines += self._gen_variant_type_enum()
        lines.append('')

        # Variant.Operator enum (manually added since we removed VariantBinding)
        lines += self._gen_variant_operator_enum()
        lines.append('')

        # Builtin classes (Vector2, Color, …)
        for cls in api.get('builtin_classes', []):
            name = cls['name']
            if name in SKIP_BUILTINS:
                continue
            ts_name = RENAME_MAP.get(name, name)
            lines += self._gen_builtin(cls, ts_name, indent=1)
            lines.append('')

        # Object-derived classes (Node, Sprite2D, …)
        for cls in api.get('classes', []):
            lines += self._gen_class(cls, indent=1)
            lines.append('')

        lines.append('    class GD {')
        # Utility functions (sin, cos, print, ...)
        lines += self._gen_utility_functions(api, indent=1)
        lines.append('    }')

        # GodotNamespace — what `import godot from "godot"` returns
        singletons = {s['name']: s['type'] for s in api.get('singletons', [])}

        for cls in api.get('builtin_classes', []):
            name = cls['name']
            if name in SKIP_BUILTINS:
                continue
            ts_name = RENAME_MAP.get(name, name)
            lines.append(f'    export const {ts_name}: typeof {ts_name};')
            lines.append(f'    export type {ts_name} = {ts_name};')

        for cls in api.get('classes', []):
            name = cls['name']
            ts_name = RENAME_MAP.get(name, name)
            lines.append(f'    export const {ts_name}: typeof {ts_name};')
            lines.append(f'    export type {ts_name} = {ts_name};')

        for s_name, s_type in singletons.items():
            lines.append(f'    export const {s_name}: {s_type};')
        lines.append('    export const GD: GD;')
        lines.append('')
        

        lines.append('    interface GodotNamespace {')
        
        for cls in api.get('builtin_classes', []):
            name = cls['name']
            if name in SKIP_BUILTINS:
                continue
            ts_name = RENAME_MAP.get(name, name)
            lines.append(f'        {ts_name}: typeof {ts_name};')

        for cls in api.get('classes', []):
            name = cls['name']
            ts_name = RENAME_MAP.get(name, name)
            lines.append(f'        {ts_name}: typeof {ts_name};')

        for s_name, s_type in singletons.items():
            lines.append(f'        {s_name}: {s_type};')
        lines.append('        GD: GD;')

        lines.append('    }')
        lines.append('')
        lines.append('    const _godot: GodotNamespace;')
        lines.append('    export default _godot;')
        lines.append('}')

        return lines
