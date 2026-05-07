import json
import os
import sys

# Ensure parent directory is in sys.path to import core
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.base_generator import CodeGenerator
from utils.api_path import find_extension_api_json
from utils.string_utils import to_snake_case, sanitize_method_name

BUILTIN_TYPES = {
    'String', 'StringName', 'NodePath', 'Variant', 'Vector2', 'Vector2i', 'Vector3', 'Vector3i',
    'Vector4', 'Vector4i', 'Color', 'Rect2', 'Rect2i', 'Transform2D', 'Plane', 'Quaternion',
    'AABB', 'Basis', 'Transform3D', 'Projection', 'Callable', 'Signal', 'Dictionary', 'Array',
    'PackedByteArray', 'PackedInt32Array', 'PackedInt64Array', 'PackedFloat32Array',
    'PackedFloat64Array', 'PackedStringArray', 'PackedVector2Array', 'PackedVector3Array',
    'PackedColorArray', 'PackedVector4Array', 'RID',
}


def default_arg_napi_expr(arg, env_expr='info.Env()'):
    value = arg.get('default_value')
    if value is None:
        return f"{env_expr}.Undefined()"

    arg_type = arg.get('type')
    if value == 'null':
        return f"{env_expr}.Null()"
    if arg_type == 'bool' or value in ('true', 'false'):
        return f"Napi::Boolean::New({env_expr}, {value})"
    if arg_type in ('int', 'float') or isinstance(value, (int, float)):
        return f"Napi::Number::New({env_expr}, static_cast<double>({value}))"
    if isinstance(value, str) and arg_type and arg_type.startswith('typedarray::'):
        return f"gode::godot_to_napi({env_expr}, godot::Array())"
    if isinstance(value, str) and arg_type in BUILTIN_TYPES:
        if value == '[]':
            return f"gode::godot_to_napi({env_expr}, godot::{arg_type}())"
        if value == '{}':
            return f"gode::godot_to_napi({env_expr}, godot::{arg_type}())"
        cpp_value = value
        if value.startswith(f"{arg_type}("):
            cpp_value = value.replace(f"{arg_type}(", f"godot::{arg_type}(", 1).replace("inf", "INFINITY")
        elif arg_type == 'RID' and value == 'RID()':
            cpp_value = 'godot::RID()'
        elif arg_type in ('String', 'StringName', 'NodePath') and value.startswith('"'):
            cpp_value = f"godot::{arg_type}({value})"
        return f"gode::godot_to_napi({env_expr}, {cpp_value})"
    if isinstance(value, str) and value.lstrip('-').isdigit():
        return f"Napi::Number::New({env_expr}, static_cast<double>({value}))"
    return f"{env_expr}.Undefined()"


def resolve_property_accessor(name, method_names):
    if not name:
        return None
    if name in method_names:
        return name
    if name.startswith('_') and name[1:] in method_names:
        return name[1:]
    return None


class ClassGenerator(CodeGenerator):
    def get_cpp_type(self, type_name, meta, refcounted_classes, is_arg=False):
        if type_name == 'void': return 'void'
        
        # Primitives
        if type_name == 'int':
            if 'int64' in meta: return 'int64_t'
            if 'uint64' in meta: return 'uint64_t'
            if 'int32' in meta: return 'int32_t'
            if 'uint32' in meta: return 'uint32_t'
            if 'int16' in meta: return 'int16_t'
            if 'uint16' in meta: return 'uint16_t'
            if 'int8' in meta: return 'int8_t'
            if 'uint8' in meta: return 'uint8_t'
            return 'int64_t'
        
        if type_name == 'float':
            return 'godot::real_t'
            
        if type_name == 'bool': return 'bool'
        
        # Enums
        if type_name.startswith('enum::'):
            return f"godot::{type_name.replace('enum::', '').replace('.', '::')}"
        if type_name.startswith('bitfield::'):
            return f"godot::BitField<godot::{type_name.replace('bitfield::', '').replace('.', '::')}>"
            
        # Builtins
        builtins = ['String', 'StringName', 'NodePath', 'Variant', 'Vector2', 'Vector2i', 'Vector3', 'Vector3i', 'Vector4', 'Vector4i', 'Color', 'Rect2', 'Rect2i', 'Transform2D', 'Plane', 'Quaternion', 'AABB', 'Basis', 'Transform3D', 'Projection', 'Callable', 'Signal', 'Dictionary', 'Array', 'PackedByteArray', 'PackedInt32Array', 'PackedInt64Array', 'PackedFloat32Array', 'PackedFloat64Array', 'PackedStringArray', 'PackedVector2Array', 'PackedVector3Array', 'PackedColorArray', 'PackedVector4Array', 'RID']
        
        if type_name in builtins:
            if is_arg:
                return f"const godot::{type_name} &"
            else:
                return f"godot::{type_name}"
                
        # Objects
        if type_name in refcounted_classes:
            if is_arg:
                return f"const godot::Ref<godot::{type_name}> &"
            else:
                return f"godot::Ref<godot::{type_name}>"
        
        # Other Classes (pointers)
        return f"godot::{type_name} *"

    def run(self):
        api_path = find_extension_api_json()
        
        try:
            with open(api_path, 'r', encoding='utf-8') as f:
                api_data = json.load(f)
        except FileNotFoundError:
            print(f"Error: extension_api.json not found at {api_path}")
            return

        if 'classes' not in api_data:
            print("Error: 'classes' key not found in api_data")
            return
            
        # Build type map for dependency resolution
        type_map = {}
        ignored_types = {'int', 'float', 'bool', 'void', 'Nil'}
        
        for bc in api_data.get('builtin_classes', []):
            name = bc['name']
            if name in ignored_types:
                continue
            snake = to_snake_case(name)
            type_map[name] = f"godot_cpp/variant/{snake}.hpp"
            
        for c in api_data.get('classes', []):
            name = c['name']
            snake = to_snake_case(name)
            if name == 'Object':
                type_map[name] = "godot_cpp/core/object.hpp"
            elif name == 'ClassDB':
                type_map[name] = "godot_cpp/classes/class_db_singleton.hpp"
            else:
                type_map[name] = f"godot_cpp/classes/{snake}.hpp"

        # Collect refcounted classes
        refcounted_classes = set()
        all_class_names = set()
        for c in api_data.get('classes', []):
            all_class_names.add(c['name'])
            if c.get('is_refcounted'):
                refcounted_classes.add(c['name'])
                
        # Collect singletons
        singletons = set()
        for s in api_data.get('singletons', []):
            singletons.add(s['name'])

        for class_def in api_data['classes']:
            class_name = class_def['name']
            
            # Skip ignored classes
            # ignored_classes = {'WebRTCDataChannelExtension', 'AudioStreamPlaybackResampled'}
            # if class_name in ignored_classes:
            #     continue
            
            print(f"Generating bindings for class: {class_name}")
            
            snake_name = to_snake_case(class_name)
            godot_include_name = 'class_db_singleton' if class_name == 'ClassDB' else snake_name
            godot_class_name = 'ClassDBSingleton' if class_name == 'ClassDB' else class_name
            
            # Rename classes that collide with JS builtins
            js_class_renames = {
                'Object': 'GDObject',
            }
            js_class_name = js_class_renames.get(class_name, class_name)
            
            raw_methods = class_def.get('methods', [])
            methods = []
            
            # Filter methods with unsafe pointer arguments
            for m in raw_methods:
                is_safe = True
                for arg in m.get('arguments', []):
                    arg_type = arg['type']
                    if '*' in arg_type:
                        # Check if it's a safe pointer (Godot Object subclass)
                        clean_type = arg_type.replace('*', '').strip()
                        if clean_type.startswith('const '):
                            clean_type = clean_type[6:].strip()
                        
                        if clean_type not in all_class_names:
                            is_safe = False
                            break
                if is_safe:
                    methods.append(m)
            
            vararg_methods = []
            
            # Detect overloads
            method_counts = {}
            for m in methods:
                name = m['name']
                method_counts[name] = method_counts.get(name, 0) + 1
            
            problematic_methods = {'get_buffer', 'store_buffer'}
            
            # Collect dependencies
            dependencies = set()
            def process_type(type_name):
                if type_name.startswith('enum::'):
                    type_name = type_name.split('::')[1]
                    if '.' in type_name: type_name = type_name.split('.')[0]
                elif type_name.startswith('bitfield::'):
                    type_name = type_name.split('::')[1]
                    if '.' in type_name: type_name = type_name.split('.')[0]
                elif type_name.startswith('typedarray::'):
                    type_name = type_name.split('::')[1]
                
                if type_name in type_map and type_name != class_name:
                    dependencies.add(type_map[type_name])

            for method in methods:
                method['name_cpp'] = sanitize_method_name(method['name'])
                if class_name == 'Node' and method['name'] == 'get_node':
                     method['name_cpp'] = 'get_node_internal'
                
                # Extract return type from return_value if present
                if 'return_value' in method:
                    method['return_type'] = method['return_value']['type']
                    process_type(method['return_value']['type'])
                
                for arg in method.get('arguments', []):
                    process_type(arg['type'])

                method['default_args'] = []
                method['has_default_args'] = False
                for arg in method.get('arguments', []):
                    if 'default_value' in arg:
                        method['has_default_args'] = True
                    method['default_args'].append(default_arg_napi_expr(arg))
                
                # Handle overloads
                is_overloaded = method_counts[method['name']] > 1 or method['name'] in problematic_methods
                
                if is_overloaded:
                    # Generate signature
                    ret_type = method.get('return_type', 'void')
                    ret_meta = ''
                    if 'return_value' in method and 'meta' in method['return_value']:
                        ret_meta = method['return_value']['meta']
                    
                    ret_cpp = self.get_cpp_type(ret_type, ret_meta, refcounted_classes, False)
                    
                    args_cpp = []
                    for arg in method.get('arguments', []):
                        args_cpp.append(self.get_cpp_type(arg['type'], arg.get('meta', ''), refcounted_classes, True))
                    
                    const_qualifier = " const" if method.get('is_const', False) else ""
                    
                    method['cast_signature'] = f"static_cast<{ret_cpp} (godot::{godot_class_name}::*)({', '.join(args_cpp)}){const_qualifier}>"

                # Check for vararg
                if method.get('is_vararg', False):
                    # Check return type
                    if 'return_type' in method and method['return_type'] != 'void':
                        method['has_return_value'] = True
                        ret_type = method['return_type']
                        ret_meta = ''
                        if 'return_value' in method and 'meta' in method['return_value']:
                            ret_meta = method['return_value']['meta']
                        
                        method['return_type_cpp'] = self.get_cpp_type(ret_type, ret_meta, refcounted_classes, False)
                    else:
                        method['has_return_value'] = False
                    vararg_methods.append(method)
            
            # Determine if class inherits from Node or RefCounted
            is_node = False
            is_ref_counted = False
            current_class = class_name
            while current_class:
                if current_class == 'Node':
                    is_node = True
                    break
                if current_class == 'RefCounted':
                    is_ref_counted = True
                    break
                # Find parent class
                parent = None
                for c in api_data['classes']:
                    if c['name'] == current_class:
                        parent = c.get('inherits')
                        break
                current_class = parent

            # Process Properties
            properties = []
            method_names = {m['name'] for m in methods}
            for prop in class_def.get('properties', []):
                 prop_name = prop['name']
                 prop_type = prop['type']
                 
                 # Only handle non-grouped properties for now
                 if '/' in prop_name:
                     continue
                     
                 getter = prop.get('getter')
                 setter = prop.get('setter')

                 resolved_getter = resolve_property_accessor(getter, method_names)
                 resolved_setter = resolve_property_accessor(setter, method_names)

                 if resolved_getter:
                     properties.append({
                         'name': prop_name,
                         'getter': sanitize_method_name(resolved_getter),
                         'setter': sanitize_method_name(resolved_setter) if resolved_setter else None
                     })

            context = {
                'js_class_name': js_class_name,
                'godot_class_name': godot_class_name,
                'godot_include_name': godot_include_name,
                'class_name': class_name,
                'is_node': is_node,
                'is_ref_counted': is_ref_counted,
                'snake_name': snake_name,
                'godot_header_dir': 'core' if class_name == 'Object' else 'classes',
                'generated_subdir': 'classes',
                'inherits': class_def.get('inherits'),
                'parent_class_name': class_def.get('inherits'),
                'parent_snake_name': to_snake_case(class_def.get('inherits')) if class_def.get('inherits') else None,
                'parent_include': f"classes/{to_snake_case(class_def.get('inherits'))}_binding.gen.h" if class_def.get('inherits') else None,
                'is_instantiable': class_def.get('is_instantiable', False),
                'methods': methods,
                'vararg_methods': vararg_methods,
                'dependencies': sorted(list(dependencies)),
                'enums': class_def.get('enums', []),
                'constants': class_def.get('constants', []),
                'properties': properties,
                'signals': class_def.get('signals', []),
                'is_singleton': class_name in singletons
            }
            
            header_rel_path = os.path.join('classes', f"{snake_name}_binding.gen.h")
            context['header_include'] = header_rel_path.replace('\\', '/')
            self.render('class_binding.h.jinja2', context, header_rel_path, 'include_dir')
            
            # Render Vararg Header if needed
            if vararg_methods:
                vararg_header_rel_path = os.path.join('classes', f"{snake_name}_vararg_method.h")
                context['vararg_header_include'] = vararg_header_rel_path.replace('\\', '/')
                vararg_header_file = vararg_header_rel_path
                self.render('class_vararg_method.h.jinja2', context, vararg_header_file, 'include_dir')
            
            source_rel_path = os.path.join('classes', f"{snake_name}_binding.gen.cpp")
            self.render('class_binding.cpp.jinja2', context, source_rel_path, 'src_dir')
