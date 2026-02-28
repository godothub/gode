import json
import os
import sys

# Ensure parent directory is in sys.path to import core
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.base_generator import CodeGenerator
from utils.string_utils import to_snake_case, sanitize_method_name

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
        builtins = ['String', 'StringName', 'NodePath', 'Variant', 'Vector2', 'Vector2i', 'Vector3', 'Vector3i', 'Vector4', 'Vector4i', 'Color', 'Rect2', 'Rect2i', 'Transform2D', 'Plane', 'Quaternion', 'AABB', 'Basis', 'Transform3D', 'Projection', 'Callable', 'Signal', 'Dictionary', 'Array', 'PackedByteArray', 'PackedInt32Array', 'PackedInt64Array', 'PackedFloat32Array', 'PackedFloat64Array', 'PackedStringArray', 'PackedVector2Array', 'PackedVector3Array', 'PackedColorArray']
        
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
        root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        api_path = os.path.join(root_dir, 'godot-cpp', 'gdextension', 'extension_api.json')
        
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
            
            context = {
                'js_class_name': js_class_name,
                'godot_class_name': godot_class_name,
                'godot_include_name': godot_include_name,
                'class_name': class_name,
                'snake_name': snake_name,
                'godot_header_dir': 'core' if class_name == 'Object' else 'classes',
                'generated_subdir': 'classes',
                'inherits': class_def.get('inherits'),
                'parent_class_name': class_def.get('inherits'),
                'parent_snake_name': to_snake_case(class_def.get('inherits')) if class_def.get('inherits') else None,
                'parent_include': f"classes/{to_snake_case(class_def.get('inherits'))}_binding.gen.h" if class_def.get('inherits') else None,
                'methods': methods,
                'vararg_methods': vararg_methods,
                'dependencies': sorted(list(dependencies)),
                'enums': class_def.get('enums', []),
                'constants': class_def.get('constants', []),
                'properties': class_def.get('properties', []),
                'signals': class_def.get('signals', [])
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
