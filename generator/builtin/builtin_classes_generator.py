import json
import os
import sys

# Ensure parent directory is in sys.path to import core
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.base_generator import CodeGenerator
from utils.api_path import find_extension_api_json
from utils.string_utils import to_snake_case, sanitize_method_name

# Map of classes and their direct fields (vs properties accessed via methods)
DIRECT_FIELDS = {
    'Vector2': ['x', 'y'],
    'Vector2i': ['x', 'y'],
    'Vector3': ['x', 'y', 'z'],
    'Vector3i': ['x', 'y', 'z'],
    'Vector4': ['x', 'y', 'z', 'w'],
    'Vector4i': ['x', 'y', 'z', 'w'],
    'Rect2': ['position', 'size'],
    'Rect2i': ['position', 'size'],
    'Quaternion': ['x', 'y', 'z', 'w'],
    'Color': ['r', 'g', 'b', 'a'],
    'Plane': ['normal', 'd', 'x', 'y', 'z'],
    'AABB': ['position', 'size'],
    'Basis': ['rows'], # Basis usually exposes rows[3]
    'Transform2D': ['columns'], # Transform2D exposes columns[3]
    'Transform3D': ['basis', 'origin'],
    'Projection': ['columns'],
}

# Special mapping for members that are fields but need index access or name mapping
FIELD_MAPPING = {
    'Basis': {
        'x': 'rows[0]',
        'y': 'rows[1]',
        'z': 'rows[2]',
    },
    'Transform2D': {
        'x': 'columns[0]',
        'y': 'columns[1]',
        'origin': 'columns[2]',
    },
    'Projection': {
        'x': 'columns[0]',
        'y': 'columns[1]',
        'z': 'columns[2]',
        'w': 'columns[3]',
    },
    'Plane': {
        'x': 'normal.x',
        'y': 'normal.y',
        'z': 'normal.z',
    }
}

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
        builtins = [
            'String', 'StringName', 'NodePath', 'Variant', 
            'Vector2', 'Vector2i', 'Rect2', 'Rect2i', 'Vector3', 'Vector3i', 'Transform2D',
            'Vector4', 'Vector4i', 'Plane', 'Quaternion', 'AABB', 'Basis', 'Transform3D', 
            'Projection', 'Color', 'Callable', 'Signal', 'Dictionary', 'Array', 
            'PackedByteArray', 'PackedInt32Array', 'PackedInt64Array', 'PackedFloat32Array', 
            'PackedFloat64Array', 'PackedStringArray', 'PackedVector2Array', 'PackedVector3Array', 
            'PackedColorArray', 'PackedVector4Array', 'RID'
        ]
        
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
        # Script is at d:\Godot\gode\generator\builtin\builtin_classes_generator.py
        # Root is d:\Godot\gode
        # Json is at d:\Godot\gode\godot-cpp\gdextension\extension_api.json
        
        api_path = find_extension_api_json()
        
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
            
            # Collect member names to avoid redefinition
            member_names = set()
            if 'members' in builtin_class:
                for member in builtin_class['members']:
                     member_names.add(member['name'])

            problematic_methods = {'rotated'}
            
            # Filter out methods that conflict with member getters/setters
            filtered_methods = []
            for method in methods:
                method_name = method['name']
                is_conflict = False
                
                # Check for get_member / set_member pattern
                if method_name.startswith('get_') and method_name[4:] in member_names:
                    is_conflict = True
                elif method_name.startswith('set_') and method_name[4:] in member_names:
                    is_conflict = True
                
                if not is_conflict:
                    filtered_methods.append(method)
            
            methods = filtered_methods
            
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
            
            # Process members
            members = []
            if 'members' in builtin_class:
                for member in builtin_class['members']:
                    member_name = member['name']

                    # Skip problematic members in Color class
                    if class_name == 'Color' and member_name in ['ok_hsl_h', 'ok_hsl_s', 'ok_hsl_l']:
                        continue
                    
                    # Determine if it's a direct field or property access
                    is_field = False
                    mapped_name = member_name
                    
                    if class_name in DIRECT_FIELDS:
                        if member_name in DIRECT_FIELDS[class_name]:
                            is_field = True
                    
                    if class_name in FIELD_MAPPING:
                         if member_name in FIELD_MAPPING[class_name]:
                             is_field = True
                             mapped_name = FIELD_MAPPING[class_name][member_name]
                    
                    member_data = {
                        'name': member_name,
                        'mapped_name': mapped_name,
                        'type': member['type'],
                        'type_cpp': self.get_cpp_type(member['type'], '', refcounted_classes, False),
                        'getter': f"get_{member_name}",
                        'setter': f"set_{member_name}",
                        'is_field': is_field
                    }
                    members.append(member_data)

            # Process constructors
            constructors = []
            if 'constructors' in builtin_class:
                for c in builtin_class['constructors']:
                    args = []
                    for arg in c.get('arguments', []):
                         args.append({
                             'name': arg['name'],
                             'type': arg['type'],
                             'type_cpp': self.get_cpp_type(arg['type'], '', refcounted_classes, True)
                         })
                    constructors.append({
                        'index': c['index'],
                        'arguments': args
                    })

            # Process operators
            operators = []
            operator_groups = {}
            dependencies = set()
            
            # Map operator symbols to readable names for JS methods
            op_symbol_to_name = {
                '==': 'equal',
                '!=': 'not_equal',
                '<': 'less',
                '<=': 'less_equal',
                '>': 'greater',
                '>=': 'greater_equal',
                '+': 'add',
                '-': 'subtract',
                '*': 'multiply',
                '/': 'divide',
                '%': 'module',
                '**': 'power',
                '~': 'bit_not',
                '&': 'bit_and',
                '|': 'bit_or',
                '^': 'bit_xor',
                '<<': 'bit_shift_left',
                '>>': 'bit_shift_right',
                'in': 'in'
            }

            if 'operators' in builtin_class:
                for op in builtin_class['operators']:
                    op_symbol = op['name']
                    
                    # Determine arguments
                    right_type = op.get('right_type')
                    is_unary = not bool(right_type)
                    
                    # Handle unary operators specifically
                    if is_unary:
                        if op_symbol == '-':
                            op_name = 'negate'
                        elif op_symbol == '+':
                            op_name = 'positive'
                        elif op_symbol == 'not':
                            op_name = 'not'
                        elif op_symbol in op_symbol_to_name:
                            op_name = op_symbol_to_name[op_symbol]
                        else:
                            # print(f"Warning: Unknown unary operator {op_symbol}")
                            op_name = 'operator_' + sanitize_method_name(op_symbol)
                    else:
                        if op_symbol in op_symbol_to_name:
                            op_name = op_symbol_to_name[op_symbol]
                        else:
                            # print(f"Warning: Unknown binary operator {op_symbol}")
                            op_name = 'operator_' + sanitize_method_name(op_symbol)
                    
                    # Skip 'in' operator for now if needed, or map it
                    if op_symbol == 'in':
                        continue

                    args = []
                    if right_type:
                        # Skip if right_type is different from left type (class_name)
                        # UNLESS it is a primitive type (float, int, bool, String) or Variant
                        primitive_types = ['float', 'int', 'bool', 'String', 'StringName', 'Object']
                        if right_type != class_name and right_type not in primitive_types:
                             continue

                        args.append({
                            'name': 'right',
                            'type': right_type,
                            'type_cpp': self.get_cpp_type(right_type, '', refcounted_classes, True)
                        })
                        
                        # Add dependency for right type
                        # We need a mapping from type name to header file
                        # This is usually 'builtin/{snake_case}_binding.gen.h'
                        if right_type not in ['float', 'int', 'bool', 'String', 'StringName', 'Object']:
                             dependencies.add(f"builtin/{to_snake_case(right_type)}_binding.gen.h")
                        elif right_type == 'Object':
                             dependencies.add(f"classes/object_binding.gen.h")
                        
                    
                    return_type = op['return_type']
                    
                    # C++ operator is just the symbol
                    cpp_op = op_symbol
                    if op_symbol == 'not': cpp_op = '!';
                    if op_symbol == 'and': cpp_op = '&&';
                    if op_symbol == 'or': cpp_op = '||';
                    if op_symbol == 'xor': cpp_op = '^'; # ??
                    
                    op_data = {
                        'name': op_name, # JS method name (e.g. 'add')
                        'cpp_op': cpp_op, # C++ symbol (e.g. '+')
                        'return_type': return_type,
                        'return_type_cpp': self.get_cpp_type(return_type, '', refcounted_classes, False),
                        'arguments': args,
                        'is_unary': is_unary
                    }
                    
                    if op_name not in operator_groups:
                        operator_groups[op_name] = []
                    operator_groups[op_name].append(op_data)

            # Flatten operators for context, but keep groups accessible if needed
            # Actually, we should pass groups to template
            operators = [] 
            # We construct a list of unique operator names for the header
            # But for implementation, we need the group logic.
            
            # Let's restructure 'operators' to be a list of groups
            # Each item has 'name' and 'overloads' list
            grouped_operators = []
            for name, overloads in operator_groups.items():
                grouped_operators.append({
                    'name': name,
                    'overloads': overloads
                })

            context = {
                'js_class_name': js_class_name,
                'class_name': class_name,
                'methods': methods,
                'vararg_methods': vararg_methods,
                'members': members,
                'constants': builtin_class.get('constants', []),
                'operators': grouped_operators, # Changed structure!
                'constructors': constructors,
                'has_destructor': builtin_class.get('has_destructor', False),
                'indexing_return_type': builtin_class.get('indexing_return_type'),
                'is_packed_array': class_name.startswith('Packed') and class_name.endswith('Array'),
                'is_keyed': builtin_class.get('is_keyed', False),
                'vararg_methods': vararg_methods,
                'dependencies': sorted(list(dependencies)),
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
