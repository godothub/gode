import os
import json
from core.base_generator import CodeGenerator

# Godot primitive → TypeScript type
PRIMITIVE_MAP = {
    'void':       'void',
    'bool':       'boolean',
    'int':        'number',
    'float':      'number',
    'Nil':        'null',
    'Variant':    'any',
    'Object':     'GodotObject',
    'String':     'string',
    'StringName': 'string',
    'NodePath':   'string',
}

# Builtin classes that map directly to JS primitives — skip class generation
SKIP_BUILTINS = frozenset(['Nil', 'void', 'bool', 'int', 'float'])

# Rename map: Godot name → TS name (avoids conflicts with JS built-ins)
RENAME_MAP = {
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


def godot_type_to_ts(type_str: str) -> str:
    if not type_str:
        return 'void'

    if type_str in PRIMITIVE_MAP:
        return PRIMITIVE_MAP[type_str]

    if type_str.startswith('enum::'):
        inner = type_str[6:]
        if '.' in inner:
            cls, enum = inner.split('.', 1)
            return f'{RENAME_MAP.get(cls, cls)}.{enum}'
        return inner  # global enum name

    if type_str.startswith('bitfield::'):
        inner = type_str[10:]
        if '.' in inner:
            cls, enum = inner.split('.', 1)
            return f'{RENAME_MAP.get(cls, cls)}.{enum}'
        return 'number'

    if type_str.startswith('typedarray::'):
        return 'GDArray'

    if type_str.endswith('*'):
        return godot_type_to_ts(type_str[:-1])

    return RENAME_MAP.get(type_str, type_str)


class DtsGenerator(CodeGenerator):

    def run(self):
        dts_pkg_dir  = os.path.dirname(os.path.abspath(__file__))
        generator_dir = os.path.dirname(dts_pkg_dir)
        project_root  = os.path.dirname(generator_dir)

        api_path   = os.path.join(project_root, 'godot-cpp', 'gdextension', 'extension_api.json')
        output_dir = os.path.join(project_root, 'example', 'addons', 'gode', 'core')
        output_path = os.path.join(output_dir, 'godot.d.ts')

        with open(api_path, 'r', encoding='utf-8') as f:
            api = json.load(f)

        os.makedirs(output_dir, exist_ok=True)

        lines = self._generate(api)

        with open(output_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines))
            f.write('\n')

        print(f'Generated: {output_path}')

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _ind(self, n: int) -> str:
        return '    ' * n

    def _format_params(self, arguments: list) -> str:
        parts = []
        for arg in arguments:
            name = sanitize_name(arg['name'])
            ts_type = godot_type_to_ts(arg['type'])
            parts.append(f'{name}: {ts_type}')
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
            name   = sanitize_name(method['name'])
            ret    = godot_type_to_ts(method.get('return_type', 'void'))
            params = self._format_params(method.get('arguments', []))
            static = 'static ' if method.get('is_static') else ''
            if method.get('is_vararg'):
                params = (params + ', ...args: any[]') if params else '...args: any[]'
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

        name     = cls_data['name']
        inherits = cls_data.get('inherits', '')
        extends  = f' extends {inherits}' if inherits else ' extends GodotObject'
        lines.append(f'{ind}class {name}{extends} {{')

        # Constants
        for const in cls_data.get('constants', []):
            lines.append(f'{ind2}static readonly {const["name"]}: number;')

        # Properties
        for prop in cls_data.get('properties', []):
            ts_type = godot_type_to_ts(prop['type'])
            lines.append(f'{ind2}{prop["name"]}: {ts_type};')

        # Signals (as comments — no runtime type)
        for sig in cls_data.get('signals', []):
            params = self._format_params(sig.get('arguments', []))
            lines.append(f'{ind2}// @signal {sig["name"]}({params})')

        # Methods
        for method in cls_data.get('methods', []):
            mname  = sanitize_name(method['name'])
            ret_v  = method.get('return_value') or {}
            ret    = godot_type_to_ts(ret_v.get('type', 'void'))
            params = self._format_params(method.get('arguments', []))
            static = 'static ' if method.get('is_static') else ''
            if method.get('is_vararg'):
                params = (params + ', ...args: any[]') if params else '...args: any[]'
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

    # ── Top-level ─────────────────────────────────────────────────────────────

    def _generate(self, api: dict) -> list:
        lines = []
        lines.append('// Auto-generated by code_generator/dts — do not edit manually.')
        lines.append('')
        lines.append('declare module "godot" {')
        lines.append('')

        # GodotObject base (every Object without an explicit parent inherits this)
        lines += [
            '    class GodotObject {',
            '        get_instance_id(): number;',
            '        connect(signal: string, callable: (...args: any[]) => void): void;',
            '        disconnect(signal: string, callable: (...args: any[]) => void): void;',
            '        emit_signal(signal: string, ...args: any[]): void;',
            '        toSignal(signal: string, options?: { timeoutMs?: number; abortSignal?: AbortSignal }): Promise<any>;',
            '        to_signal(signal: string, options?: { timeoutMs?: number; abortSignal?: AbortSignal }): Promise<any>;',
            '    }',
            '',
        ]

        # Global enums
        for enum in api.get('global_enums', []):
            lines += self._gen_enum(enum, indent=1)
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

        # GodotNamespace — what `import godot from "godot"` returns
        singletons = {s['name']: s['type'] for s in api.get('singletons', [])}

        lines.append('    interface GodotNamespace {')

        for cls in api.get('builtin_classes', []):
            name = cls['name']
            if name in SKIP_BUILTINS:
                continue
            ts_name = RENAME_MAP.get(name, name)
            lines.append(f'        {ts_name}: typeof {ts_name};')

        for cls in api.get('classes', []):
            name = cls['name']
            lines.append(f'        {name}: typeof {name};')

        for s_name, s_type in singletons.items():
            lines.append(f'        {s_name}: {s_type};')

        lines.append('    }')
        lines.append('')
        lines.append('    const _godot: GodotNamespace;')
        lines.append('    export default _godot;')
        lines.append('}')

        return lines
