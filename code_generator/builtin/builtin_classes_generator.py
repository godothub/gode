import json
import os
import sys

# Ensure parent directory is in sys.path to import core
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.base_generator import CodeGenerator
from utils.string_utils import to_snake_case, sanitize_method_name

class BuiltinClassGenerator(CodeGenerator):
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
        # Path to extension_api.json
        # We need to find where extension_api.json is relative to this script
        # Script is at d:\Godot\gode\code_generator\builtin\builtin_classes_generator.py
        # Root is d:\Godot\gode
        # Json is at d:\Godot\gode\godot-cpp\gdextension\extension_api.json
        
        root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        api_path = os.path.join(root_dir, 'godot-cpp', 'gdextension', 'extension_api.json')
        
        try:
            with open(api_path, 'r', encoding='utf-8') as f:
                api_data = json.load(f)
        except FileNotFoundError:
            print(f"Error: extension_api.json not found at {api_path}")
            return

        # Iterate over all builtin classes
        if 'builtin_classes' not in api_data:
            print("Error: 'builtin_classes' key not found in api_data")
            return
            
        # Collect refcounted classes
        refcounted_classes = set()
        if 'classes' in api_data:
            for c in api_data['classes']:
                if c.get('is_refcounted'):
                    refcounted_classes.add(c['name'])

        for builtin_class in api_data['builtin_classes']:
            class_name = builtin_class['name']
            
            # Skip some types if needed (e.g., Nil, void)
            if class_name == 'Nil' or class_name == 'void':
                continue
            
            # Skip POD types (int, float, bool)
            if class_name in ['bool', 'int', 'float']:
                continue
                
            print(f"Generating bindings for builtin class: {class_name}")
            
            # Rename classes that collide with JS builtins
            js_class_renames = {
                'String': 'GDString',
                'Dictionary': 'GDDictionary',
                'Array': 'GDArray'
            }
            js_class_name = js_class_renames.get(class_name, class_name)
            
            # Prepare context for the template
            methods = builtin_class.get('methods', [])
            vararg_methods = []
            
            # Filter ignored methods
            ignored_methods = {'from_ok_hsl'}
            methods = [m for m in methods if m['name'] not in ignored_methods]
            
            # Detect overloads
            method_counts = {}
            for m in methods:
                name = m['name']
                method_counts[name] = method_counts.get(name, 0) + 1
            
            problematic_methods = {'rotated'}
            
            for method in methods:
                method['name_cpp'] = sanitize_method_name(method['name'])
                if class_name in ['AABB', 'Plane'] and method['name'] in ['intersects_segment', 'intersects_ray', 'intersect_3']:
                    method['name_cpp'] = method['name'] + '_bind'
                
                # Extract return type from return_value if present
                if 'return_value' in method:
                    method['return_type'] = method['return_value']['type']
                
                # Handle overloads
                is_overloaded = method_counts[method['name']] > 1 or method['name'] in problematic_methods
                
                if method['name'] == 'rotated' and class_name == 'Basis':
                    method['cast_signature'] = "static_cast<godot::Basis (godot::Basis::*)(const godot::Vector3 &, godot::EulerOrder) const>"
                elif is_overloaded:
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
                    
                    method['cast_signature'] = f"static_cast<{ret_cpp} (godot::{class_name}::*)({', '.join(args_cpp)}){const_qualifier}>"

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
                'class_name': class_name,
                'methods': methods,
                'vararg_methods': vararg_methods,
                'members': builtin_class.get('members', []),
                'constants': builtin_class.get('constants', []),
                'operators': builtin_class.get('operators', []),
                'constructors': builtin_class.get('constructors', []),
                'has_destructor': builtin_class.get('has_destructor', False),
                'indexing_return_type': builtin_class.get('indexing_return_type'),
                'is_keyed': builtin_class.get('is_keyed', False),
                'vararg_methods': vararg_methods,
            }
            
            # Generate the files
            # Naming convention: snake_case_binding.gen.h and .cpp
            snake_name = to_snake_case(class_name)
            
            # Add snake_name to context for use in templates
            context['snake_name'] = snake_name
            context['generated_subdir'] = 'builtin'
            
            # Render Header
            header_rel_path = os.path.join('builtin', f"{snake_name}_binding.gen.h")
            context['header_include'] = header_rel_path.replace('\\', '/')
            header_file = header_rel_path
            self.render('builtin_binding.h.jinja2', context, header_file, 'include_dir')
            
            # Render Vararg Header if needed
            if vararg_methods:
                vararg_header_rel_path = os.path.join('builtin', f"{snake_name}_vararg_method.h")
                context['vararg_header_include'] = vararg_header_rel_path.replace('\\', '/')
                vararg_header_file = vararg_header_rel_path
                self.render('builtin_vararg_method.h.jinja2', context, vararg_header_file, 'include_dir')
            
            # Render Source
            source_rel_path = os.path.join('builtin', f"{snake_name}_binding.gen.cpp")
            source_file = source_rel_path
            self.render('builtin_binding.cpp.jinja2', context, source_file, 'src_dir')
