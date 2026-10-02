import os
import json

from .base_generator import CodeGenerator
from .utils.api_data import load_extension_api_json
from .utils.binding_policy import (
    GLOBAL_ENUM_ALIASES,
    builtin_operator_method_name,
    global_enum_export_name,
    method_conflicts_with_builtin_member,
    resolve_property_accessor,
    sanitize_ts_identifier,
    skipped_method_reason,
)
from .utils.type_mappings import (
    JS_CLASS_RENAME_MAP,
    PACKED_ARRAY_ELEMENT_TYPES,
    PACKED_ARRAY_TYPED_INPUTS,
    parse_typedarray_element_type,
    parse_typeddictionary_types,
)

# Godot primitive → TypeScript type
PRIMITIVE_MAP = {
    'void':       'void',
    'bool':       'boolean',
    'float':      'number',
    'Nil':        'null',
    'Object':     'GodotObject',
    'String':     'string',
    'StringName': 'string',
}

# JS-facing collection types used for return values.
JS_ARRAY_TYPE = 'VariantArgument[]'
JS_OBJECT_TYPE = '{ [key: string]: VariantArgument }'
JS_MAP_TYPE = 'Map<VariantArgument, VariantArgument>'
JS_DICTIONARY_TYPE = f'{JS_OBJECT_TYPE} | {JS_MAP_TYPE}'

# Builtin classes that map directly to JS primitives — skip class generation
SKIP_BUILTINS = frozenset(['Nil', 'void', 'bool', 'int', 'float'])

# Rename map: Godot name → JS/TS API name (avoids conflicts with JS built-ins)
RENAME_MAP = JS_CLASS_RENAME_MAP

UTILITY_RETURN_TYPE_OVERRIDES = {
    'instance_from_id': 'GodotObject | null',
    'typeof': 'VariantType',
}

UTILITY_ARGUMENT_TYPE_OVERRIDES = {
    'error_string': {'error': 'Error'},
    'type_convert': {'type': 'VariantType'},
    'type_string': {'type': 'VariantType'},
}


def sanitize_name(name: str) -> str:
    return sanitize_ts_identifier(name)


def member_name(name: str) -> str:
    name = name.replace('-', '_')
    if sanitize_ts_identifier(name) != name:
        return json.dumps(name)
    return name


def singleton_enum_type_ref(owner_name: str, enum_name: str) -> str:
    owner_ts_name = sanitize_name(RENAME_MAP.get(owner_name, owner_name))
    enum_ts_name = sanitize_name(enum_name)
    return f'import("godot").{owner_ts_name}.{enum_ts_name}'


def godot_type_to_ts(type_str: str, is_input: bool = False, singleton_class_names: frozenset = frozenset(), meta: str = '') -> str:
    if not type_str:
        return 'void'

    # Comma-separated multi-type → TypeScript union
    # Strip leading '-' (Godot exclusion syntax, e.g. "-AnimatedTexture")
    if ',' in type_str:
        return ' | '.join(
            godot_type_to_ts(t.strip().lstrip('-'), is_input, singleton_class_names, meta)
            for t in type_str.split(',')
        )

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

    if type_str == 'int':
        if is_input or meta in ('', 'int64', 'uint64'):
            return 'number | bigint'
        return 'number'

    if is_input and type_str in ('String', 'GDString', 'StringName'):
        return 'GDString | StringName | string'

    if type_str == 'NodePath':
        return 'NodePath | string' if is_input else 'NodePath'

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
            if cls in singleton_class_names:
                return singleton_enum_type_ref(cls, enum)
            return f'{RENAME_MAP.get(cls, cls)}.{enum}'
        return inner  # global enum name

    if type_str.startswith('bitfield::'):
        inner = type_str[10:]
        if '.' in inner:
            cls, enum = inner.split('.', 1)
            if cls in singleton_class_names:
                return singleton_enum_type_ref(cls, enum)
            return f'{RENAME_MAP.get(cls, cls)}.{enum}'
        return 'number'

    if type_str.startswith('typedarray::'):
        element_type = parse_typedarray_element_type(type_str)
        # typedarray is always represented as a JS generic array on the TypeScript side.
        element_ts = godot_type_to_ts(element_type, is_input=is_input, singleton_class_names=singleton_class_names)
        typed_array = f'Array<{element_ts}>'
        return f'GDArray<{element_ts}> | {typed_array}' if is_input else typed_array

    if type_str.startswith('typeddictionary::'):
        key_type, value_type = parse_typeddictionary_types(type_str)
        key_ts = godot_type_to_ts(key_type, is_input=False, singleton_class_names=singleton_class_names)
        key_input_ts = godot_type_to_ts(key_type, is_input=True, singleton_class_names=singleton_class_names)
        value_ts = godot_type_to_ts(value_type, is_input=is_input, singleton_class_names=singleton_class_names)

        # JS object literals preserve Godot dictionary keys only for string-like keys.
        if key_ts == 'string':
            typed_container = f'Record<{key_ts}, {value_ts}>'
        else:
            typed_container = f'Map<{key_input_ts if is_input else key_ts}, {value_ts}>'

        if is_input and key_ts == 'string':
            return f'GDDictionary<{key_input_ts}, {value_ts}> | {typed_container} | Map<{key_input_ts}, {value_ts}>'
        return f'GDDictionary<{key_input_ts}, {value_ts}> | {typed_container}' if is_input else typed_container

    if type_str.endswith('*'):
        return godot_type_to_ts(type_str[:-1], is_input, singleton_class_names, meta)

    if is_input and type_str in ('Dictionary', 'GDDictionary'):
        return f'GDDictionary | {JS_DICTIONARY_TYPE}'

    if is_input and type_str in ('Array', 'GDArray'):
        return f'GDArray | {JS_ARRAY_TYPE}'

    if is_input and type_str in PACKED_ARRAY_ELEMENT_TYPES:
        element_type = PACKED_ARRAY_ELEMENT_TYPES[type_str]
        element_ts = godot_type_to_ts(element_type, is_input=True, singleton_class_names=singleton_class_names)
        typed_input = f' | {PACKED_ARRAY_TYPED_INPUTS[type_str]}' if type_str in PACKED_ARRAY_TYPED_INPUTS else ''
        return f'{type_str} | Array<{element_ts}>{typed_input}'

    if not is_input and type_str == 'Array':
        return JS_ARRAY_TYPE

    if not is_input and type_str in ('Dictionary', 'GDDictionary'):
        return JS_DICTIONARY_TYPE

    return RENAME_MAP.get(type_str, type_str)


class DtsGenerator(CodeGenerator):

    def run(self):
        generator_dir = os.path.dirname(os.path.abspath(__file__))
        project_root  = os.path.dirname(generator_dir)

        output_dir = os.path.join(project_root, 'example', 'addons', 'gode', 'types')
        godot_ignore_path = os.path.join(output_dir, '.gdignore')
        godot_output_path = os.path.join(output_dir, 'godot.d.ts')
        globals_output_path = os.path.join(output_dir, 'globals.d.ts')

        api = load_extension_api_json(required_keys=("builtin_classes", "classes", "global_enums", "utility_functions"))

        os.makedirs(output_dir, exist_ok=True)

        self.write_file_if_changed(
            godot_ignore_path,
            '# TypeScript declarations are compiler inputs, not Godot resources.\n.d.ts\n',
        )
        godot_lines = self._generate(api)
        globals_lines = self._generate_globals(api)

        self.write_file_if_changed(godot_output_path, '\n'.join(godot_lines) + '\n')
        self.write_file_if_changed(globals_output_path, '\n'.join(globals_lines) + '\n')

    def _has_raw_pointer(self, method: dict) -> bool:
        return skipped_method_reason(method, getattr(self, '_object_class_names', set())) is not None

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _ind(self, n: int) -> str:
        return '    ' * n

    @staticmethod
    def _append_unique_line(lines: list, seen: set, line: str) -> None:
        if line in seen:
            return
        seen.add(line)
        lines.append(line)

    def _extend_unique_lines(self, lines: list, seen: set, new_lines: list) -> None:
        for line in new_lines:
            self._append_unique_line(lines, seen, line)

    def _type_to_ts(self, type_str: str, is_input: bool = False) -> str:
        return godot_type_to_ts(type_str, is_input, getattr(self, '_singleton_class_names', frozenset()))

    def _type_to_ts_with_meta(self, type_str: str, is_input: bool = False, meta: str = '') -> str:
        return godot_type_to_ts(type_str, is_input, getattr(self, '_singleton_class_names', frozenset()), meta)

    def _builtin_type_parameters(self, ts_name: str) -> str:
        if ts_name == 'GDArray':
            return '<T extends VariantArgument = VariantArgument>'
        if ts_name == 'GDDictionary':
            return '<K extends VariantArgument = VariantArgument, V extends VariantArgument = VariantArgument>'
        if ts_name == 'Signal':
            return '<T extends (...args: any[]) => void = (...args: VariantArgument[]) => void>'
        return ''

    def _array_like_type(self) -> str:
        return 'GDArray<T> | T[]'

    def _dictionary_like_type(self) -> str:
        return 'GDDictionary<K, V> | { [key: string]: V } | Map<K, V>'

    def _builtin_constructor_param_overrides(self, ts_name: str, arguments: list) -> dict:
        if ts_name == 'GDArray':
            overrides = {}
            for arg in arguments:
                if arg['type'] == 'Array':
                    overrides[arg['name']] = self._array_like_type()
            return overrides
        if ts_name == 'GDDictionary':
            overrides = {}
            for arg in arguments:
                if arg['type'] == 'Dictionary':
                    overrides[arg['name']] = self._dictionary_like_type()
            return overrides
        if ts_name == 'Signal':
            return {
                arg['name']: 'Signal<T>'
                for arg in arguments
                if arg['type'] == 'Signal'
            }
        return {}

    def _builtin_method_param_overrides(self, ts_name: str, method_name: str, arguments: list) -> dict:
        if ts_name == 'GDArray':
            overrides = {}
            for arg in arguments:
                if arg['type'] == 'Array':
                    overrides[arg['name']] = self._array_like_type()
                elif arg['type'] == 'Variant' and arg['name'] in {
                    'value', 'what',
                }:
                    overrides[arg['name']] = 'T'
            return overrides
        if ts_name == 'GDDictionary':
            overrides = {}
            for arg in arguments:
                if arg['type'] == 'Dictionary':
                    overrides[arg['name']] = self._dictionary_like_type()
                elif arg['name'] == 'keys':
                    overrides[arg['name']] = 'GDArray<K> | K[]'
                elif arg['name'] == 'key':
                    overrides[arg['name']] = 'K'
                elif arg['name'] == 'value':
                    overrides[arg['name']] = 'V'
                elif arg['name'] == 'default':
                    overrides[arg['name']] = 'V'
            return overrides
        if ts_name == 'Signal' and method_name in {'connect', 'disconnect', 'is_connected'}:
            return {
                arg['name']: 'Callable | T'
                for arg in arguments
                if arg['name'] == 'callable'
            }
        return {}

    def _builtin_method_return_override(self, ts_name: str, method_name: str) -> str:
        if ts_name == 'GDArray':
            if method_name in {'get', 'front', 'back', 'pick_random', 'pop_back', 'pop_front', 'pop_at', 'max', 'min'}:
                return 'T'
            if method_name in {'duplicate', 'duplicate_deep', 'slice', 'filter'}:
                return 'T[]'
        if ts_name == 'GDDictionary':
            if method_name in {'merged', 'duplicate', 'duplicate_deep'}:
                return '{ [key: string]: V } | Map<K, V>'
            if method_name == 'find_key':
                return 'K'
            if method_name == 'keys':
                return 'K[]'
            if method_name == 'values':
                return 'V[]'
            if method_name in {'get', 'get_or_add'}:
                return 'V'
        return ''

    def _enum_type_ref(self, owner_name: str, enum_name: str) -> str:
        enum_name = sanitize_name(enum_name)
        if owner_name in getattr(self, '_singleton_ts_names', frozenset()):
            return singleton_enum_type_ref(owner_name, enum_name)
        return f'{owner_name}.{enum_name}'

    def _extends_type_to_ts(self, type_str: str) -> str:
        ts_type = self._type_to_ts(type_str)
        if type_str in getattr(self, '_singleton_class_names', frozenset()):
            return f'__GodotSingletonBases.{ts_type}'
        return ts_type

    def _format_params(self, arguments: list, type_overrides: dict = None) -> str:
        type_overrides = type_overrides or {}
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
            ts_type = type_overrides.get(arg['name'])
            if not ts_type:
                ts_type = self._type_to_ts_with_meta(arg['type'], is_input=True, meta=arg.get('meta', ''))
            opt = '?' if is_optional else ''
            parts.append(f'{name}{opt}: {ts_type}')
        return ', '.join(parts)

    # ── Enum ──────────────────────────────────────────────────────────────────

    def _gen_enum(self, enum_data: dict, indent: int, export: bool = False, name: str = None) -> list:
        ind  = self._ind(indent)
        ind2 = self._ind(indent + 1)
        prefix = ''
        if export:
            prefix += 'export '
        enum_name = sanitize_name(name or enum_data['name'])
        lines = [f'{ind}{prefix}enum {enum_name} {{']
        for val in enum_data.get('values', []):
            lines.append(f'{ind2}{val["name"]} = {val["value"]},')
        lines.append(f'{ind}}}')
        return lines

    def _gen_singleton_enum_namespaces(self, owner_name: str, enums: list, indent: int) -> list:
        if not enums:
            return []

        ind = self._ind(indent)
        ind2 = self._ind(indent + 1)
        lines = [f'{ind}export namespace {owner_name} {{']
        for enum in enums:
            enum_name = sanitize_name(enum['name'])
            values = list(dict.fromkeys(str(value["value"]) for value in enum.get('values', [])))
            value_type = ' | '.join(values) if values else 'never'
            lines.append(f'{ind2}export type {enum_name} = {value_type};')
        lines.append(f'{ind}}}')
        return lines

    def _gen_enum_object_property(self, enum_data: dict, indent: int, enum_type: str) -> list:
        ind = self._ind(indent)
        ind2 = self._ind(indent + 1)
        enum_name = sanitize_name(enum_data['name'])
        lines = [f'{ind}readonly {enum_name}: {{']
        reverse_values = {}
        for val in enum_data.get('values', []):
            value_name = member_name(val['name'])
            lines.append(f'{ind2}readonly {value_name}: {enum_type};')
            reverse_values[val['value']] = val['name']
        for value, value_name in reverse_values.items():
            lines.append(f'{ind2}readonly [{value}]: {json.dumps(value_name)};')
        lines.append(f'{ind}}};')
        return lines

    def _gen_enum_static_constants(self, enums: list, indent: int, owner_name: str, static: bool = True, seen: set = None) -> list:
        """Godot exposes class enum values directly on constructors at runtime.

        The nested namespace enum keeps enum types available as `Viewport.MSAA`,
        while these static constants type expressions such as
        `Viewport.MSAA_DISABLED` and `Window.MODE_FULLSCREEN`.
        """
        ind = self._ind(indent)
        lines = []
        seen = seen if seen is not None else set()
        modifier = 'static readonly' if static else 'readonly'
        for enum in enums:
            enum_name = sanitize_name(enum['name'])
            enum_type = self._enum_type_ref(owner_name, enum_name)
            if not static and enum_name not in seen:
                seen.add(enum_name)
                lines.extend(self._gen_enum_object_property(enum, indent, enum_type))
            for val in enum.get('values', []):
                name = sanitize_name(val['name'])
                if name in seen:
                    continue
                seen.add(name)
                lines.append(f'{ind}{modifier} {name}: {enum_type};')
        return lines

    # ── Builtin class ─────────────────────────────────────────────────────────

    def _gen_builtin(self, cls_data: dict, ts_name: str, indent: int) -> list:
        ind  = self._ind(indent)
        ind2 = self._ind(indent + 1)
        lines = [f'{ind}export class {ts_name}{self._builtin_type_parameters(ts_name)} {{']
        body_seen = set()

        # Constructors
        for ctor in cls_data.get('constructors', []):
            args = ctor.get('arguments', [])
            param_overrides = self._builtin_constructor_param_overrides(ts_name, args)
            params = self._format_params(args, param_overrides) if args else ''
            self._append_unique_line(lines, body_seen, f'{ind2}constructor({params});')

        # Members (instance properties)
        for member in cls_data.get('members', []):
            ts_type = self._type_to_ts_with_meta(member['type'], meta=member.get('meta', ''))
            self._append_unique_line(lines, body_seen, f'{ind2}{member["name"]}: {ts_type};')

        # Constants
        for const in cls_data.get('constants', []):
            ts_type = self._type_to_ts_with_meta(const['type'], meta=const.get('meta', ''))
            self._append_unique_line(lines, body_seen, f'{ind2}static readonly {const["name"]}: {ts_type};')

        lines.extend(self._gen_enum_static_constants(cls_data.get('enums', []), indent + 1, ts_name, static=True, seen=body_seen))
        member_names = {member['name'] for member in cls_data.get('members', [])}

        # Methods
        for method in cls_data.get('methods', []):
            if self._has_raw_pointer(method):
                continue
            if method_conflicts_with_builtin_member(method['name'], member_names):
                continue
            name   = member_name(method['name'])
            ret    = self._builtin_method_return_override(ts_name, method['name'])
            if not ret:
                ret = self._type_to_ts_with_meta(method.get('return_type', 'void'), meta=(method.get('return_value') or {}).get('meta', ''))
            args = method.get('arguments', [])
            params = self._format_params(args, self._builtin_method_param_overrides(ts_name, method['name'], args))
            static = 'static ' if method.get('is_static') else ''
            if method.get('is_vararg'):
                vararg = '...args: Parameters<T>' if ts_name == 'Signal' and method['name'] == 'emit' else '...args: VariantArgument[]'
                params = (params + ', ' + vararg) if params else vararg
            self._append_unique_line(lines, body_seen, f'{ind2}{static}{name}({params}): {ret};')

        operator_types = {}
        for operator in cls_data.get('operators', []):
            op_name = builtin_operator_method_name(operator['name'])
            right_type = operator.get('right_type')
            if op_name and right_type:
                operator_types.setdefault(op_name, set()).add(right_type)

        for operator in cls_data.get('operators', []):
            op_name = builtin_operator_method_name(operator['name'])
            if not op_name:
                continue
            right_type = operator.get('right_type')
            if right_type == 'int' and 'float' in operator_types.get(op_name, set()):
                right_ts_type = 'bigint'
            else:
                right_ts_type = self._type_to_ts(right_type, is_input=True) if right_type else ''
            params = f'right: {right_ts_type}' if right_type else ''
            ret = self._type_to_ts_with_meta(operator.get('return_type', 'void'), meta=operator.get('return_meta', ''))
            self._append_unique_line(lines, body_seen, f'{ind2}{member_name(op_name)}({params}): {ret};')

        lines.append(f'{ind}}}')

        # Declaration merging: namespace for nested enums
        enums = cls_data.get('enums', [])
        if enums:
            lines.append(f'{ind}export namespace {ts_name} {{')
            for enum in enums:
                lines += self._gen_enum(enum, indent + 1, export=True)
            lines.append(f'{ind}}}')

        return lines

    # ── Object-derived class ──────────────────────────────────────────────────

    def _gen_class(self, cls_data: dict, indent: int, is_singleton: bool = False) -> list:
        ind = self._ind(indent)
        class_indent = indent + 1 if is_singleton else indent
        body_indent = class_indent + 1
        class_ind = self._ind(class_indent)
        body_ind = self._ind(body_indent)
        lines = []

        name = self._type_to_ts(cls_data['name'])
        inherits = cls_data.get('inherits', '')
        extends = f' extends {self._extends_type_to_ts(inherits)}' if inherits else ' extends _GodotObject'

        if is_singleton and cls_data.get('enums', []):
            lines += self._gen_singleton_enum_namespaces(name, cls_data.get('enums', []), indent)
            lines.append('')

        if is_singleton:
            lines.append(f'{ind}namespace __GodotSingletonBases {{')
            lines.append(f'{class_ind}export abstract class {name}{extends} {{')
        else:
            class_modifier = 'abstract ' if not cls_data.get('is_instantiable', False) else ''
            lines.append(f'{ind}export {class_modifier}class {name}{extends} {{')
        body_seen = set()

        # Constants
        for const in cls_data.get('constants', []):
            modifier = 'readonly' if is_singleton else 'static readonly'
            self._append_unique_line(lines, body_seen, f'{body_ind}{modifier} {const["name"]}: number;')

        lines.extend(self._gen_enum_static_constants(cls_data.get('enums', []), body_indent, name, static=not is_singleton, seen=body_seen))

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
            ts_type = self._type_to_ts_with_meta(prop['type'], meta=prop.get('meta', ''))
            ts_type_input = self._type_to_ts_with_meta(prop['type'], is_input=True, meta=prop.get('meta', ''))
            getter = prop.get('getter', '')
            setter = prop.get('setter', '')
            resolved_getter = resolve_property_accessor(getter, declared_methods)
            resolved_setter = resolve_property_accessor(setter, declared_methods)
            if not resolved_getter:
                continue

            self._append_unique_line(lines, body_seen, f'{body_ind}get {prop["name"]}(): {ts_type};')
            if resolved_setter:
                self._append_unique_line(lines, body_seen, f'{body_ind}set {prop["name"]}(value: {ts_type_input});')
                property_method_overrides[resolved_setter] = {'first_arg_type': ts_type_input}
            # Emit getter/setter as explicit methods when not already in the methods section
            if resolved_getter not in declared_methods:
                self._append_unique_line(lines, body_seen, f'{body_ind}{sanitize_name(resolved_getter)}(): {ts_type};')
            property_method_overrides[resolved_getter] = {'return_type': ts_type}
            if resolved_setter and resolved_setter not in declared_methods:
                self._append_unique_line(lines, body_seen, f'{body_ind}{sanitize_name(resolved_setter)}(value: {ts_type_input}): void;')

        # Signals carry their extension_api argument list through Signal<T>.
        for sig in cls_data.get('signals', []):
            params = self._format_params(sig.get('arguments', []))
            self._append_unique_line(
                lines,
                body_seen,
                f'{body_ind}{sig["name"]}: Signal<({params}) => void>;',
            )

        # Methods
        for method in cls_data.get('methods', []):
            if self._has_raw_pointer(method):
                continue
            mname = member_name(method['name'])
            ret_v = method.get('return_value') or {}
            ret = self._type_to_ts_with_meta(ret_v.get('type', 'void'), meta=ret_v.get('meta', ''))
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
            static = 'static ' if method.get('is_static') and not is_singleton else ''
            if method.get('is_vararg'):
                params = (params + ', ...args: VariantArgument[]') if params else '...args: VariantArgument[]'
            self._append_unique_line(lines, body_seen, f'{body_ind}{static}{mname}({params}): {ret};')

        lines.append(f'{class_ind}}}')

        if is_singleton:
            lines.append(f'{ind}}}')
            lines.append(f'{ind}export type {name} = __GodotSingletonBases.{name};')

        # Namespace for nested enums
        enums = cls_data.get('enums', [])
        if enums and not is_singleton:
            lines.append(f'{ind}export namespace {name} {{')
            for enum in enums:
                lines += self._gen_enum(enum, indent + 1, export=True)
            lines.append(f'{ind}}}')

        return lines

    def _gen_utility_functions(self, api: dict, indent: int) -> list:
        ind = self._ind(indent)
        lines = []
        seen = set()
        for func in api.get('utility_functions', []):
            name = member_name(func['name'])
            ret_v = func.get('return_value') or {}
            ret = UTILITY_RETURN_TYPE_OVERRIDES.get(func['name'])
            if not ret:
                ret = self._type_to_ts_with_meta(ret_v.get('type', func.get('return_type', 'void')), meta=ret_v.get('meta', ''))
            params = self._format_params(
                func.get('arguments', []),
                UTILITY_ARGUMENT_TYPE_OVERRIDES.get(func['name'], {}),
            )
            if func.get('is_vararg'):
                params = (params + ', ...args: VariantArgument[]') if params else '...args: VariantArgument[]'
            self._append_unique_line(lines, seen, f'{ind}{ind}{name}({params}): {ret};')
        return lines

    def _gen_global_enums(self, api: dict) -> list:
        global_enums = {enum['name']: enum for enum in api.get('global_enums', [])}
        missing = sorted(name for name in GLOBAL_ENUM_ALIASES if name not in global_enums)
        if missing:
            raise KeyError(f"extension_api.json missing Variant enum(s): {', '.join(missing)}")

        lines = []
        for enum in api.get('global_enums', []):
            export_name = global_enum_export_name(enum['name'])
            if not export_name:
                continue
            lines += self._gen_enum(enum, indent=1, export=True, name=export_name)
            lines.append('')
        return lines

    def _collect_global_singleton_symbols(self, api: dict) -> dict:
        singletons = {}

        for singleton in api.get('singletons', []):
            name = singleton['name']
            singletons[name] = RENAME_MAP.get(singleton['type'], singleton['type'])

        return singletons

    def _generate_globals(self, api: dict) -> list:
        lines = []
        lines.append('// Auto-generated by generator/dts_generator.py; do not edit manually.')
        lines.append('')
        lines.append('declare global {')
        lines.append('  interface ExportOptions {')
        lines.append('    hint?: number;')
        lines.append('    hint_string?: string;')
        lines.append('  }')
        lines.append('')
        lines.append('  function Export(hint: number, hint_string?: string): any;')
        lines.append('  function Export(options?: ExportOptions): any;')
        lines.append('')
        lines.append('  function Signal(...args: any[]): any;')
        lines.append('  function Tool(target: object): void;')
        lines.append('  function Tool(): any;')
        lines.append('  function GlobalClass(target: object): void;')
        lines.append('  function GlobalClass(): any;')
        lines.append('')
        lines.append('  interface ExportEntry {')
        lines.append('    type: string;')
        lines.append('    hint?: number;')
        lines.append('    hint_string?: string;')
        lines.append('    default?: import("godot").VariantArgument;')
        lines.append('  }')
        lines.append('  type ExportMap = Record<string, ExportEntry>;')
        lines.append('')
        lines.append('  type RpcMode = "authority" | "any_peer" | "disabled" | number;')
        lines.append('  type RpcTransferMode = "reliable" | "unreliable" | "unreliable_ordered" | number;')
        lines.append('  interface RpcEntry {')
        lines.append('    rpc_mode?: RpcMode;')
        lines.append('    transfer_mode?: RpcTransferMode;')
        lines.append('    call_local?: boolean;')
        lines.append('    channel?: number;')
        lines.append('  }')
        lines.append('  type RpcConfig = Record<string, RpcEntry>;')
        lines.append('')
        lines.append('  type int = number | bigint;')
        lines.append('  type float = number;')

        lines.append('}')
        lines.append('')
        lines.append('export {};')
        return lines

    # Top-level generation.

    def _generate(self, api: dict) -> list:
        self._object_class_names = {cls['name'] for cls in api.get('classes', [])}
        self._singleton_class_names = frozenset(
            name
            for singleton in api.get('singletons', [])
            for name in (singleton['name'], singleton['type'])
        )
        self._singleton_ts_names = frozenset(RENAME_MAP.get(name, name) for name in self._singleton_class_names)

        # Collect all builtin classes that are generated
        builtin_types = []
        for cls in api.get('builtin_classes', []):
            name = cls['name']
            if name in SKIP_BUILTINS:
                continue
            builtin_types.append(RENAME_MAP.get(name, name))
        
        variant_arg_types = ['null', 'undefined', 'boolean', 'number', 'bigint', 'string', 'Function', 'Object'] + builtin_types
        variant_arg_types.extend([JS_OBJECT_TYPE, JS_MAP_TYPE, JS_ARRAY_TYPE])
        variant_arg_types = list(dict.fromkeys(variant_arg_types))
        variant_arg_str = ' | '.join(variant_arg_types)

        lines = []
        lines.append('// Auto-generated by generator/dts_generator.py; do not edit manually.')
        lines.append('')
        lines.append('declare module "godot" {')
        lines.append('')
        lines.append(f'    export type VariantArgument = {variant_arg_str};')
        lines.append('')

        # GodotObject base (every Object without an explicit parent inherits this)
        lines += [
            '    class _GodotObject {',
            '        get_instance_id(): number | bigint;',
            '        connect(signal: string, callable: (...args: VariantArgument[]) => void): void;',
            '        disconnect(signal: string, callable: (...args: VariantArgument[]) => void): void;',
            '        emit_signal(signal: string, ...args: VariantArgument[]): void;',
            '        to_signal(signal: string, options?: { timeoutMs?: number; abortSignal?: AbortSignal }): Promise<VariantArgument>;',
            '    }',
            '',
        ]

        lines += self._gen_global_enums(api)

        # Builtin classes (Vector2, Color, …)
        for cls in api.get('builtin_classes', []):
            name = cls['name']
            if name in SKIP_BUILTINS:
                continue
            ts_name = RENAME_MAP.get(name, name)
            lines += self._gen_builtin(cls, ts_name, indent=1)
            lines.append('')

        # Object-derived classes (Node, Sprite2D, …)
        singletons = self._collect_global_singleton_symbols(api)
        for cls in api.get('classes', []):
            lines += self._gen_class(cls, indent=1, is_singleton=cls['name'] in singletons)
            lines.append('')

        lines.append('    export interface GD {')
        # Utility functions (sin, cos, print, ...)
        lines += self._gen_utility_functions(api, indent=1)
        lines.append('    }')

        for s_name, s_type in singletons.items():
            lines.append(f'    export const {s_name}: {s_type};')
        lines.append('    export const GD: GD;')
        lines.append('}')

        return lines
