import os

from .base_generator import CodeGenerator
from .utils.api_data import load_extension_api_json
from .utils.binding_policy import (
    builtin_operator_method_name,
    method_conflicts_with_builtin_member,
    variant_operator_enum_name,
)
from .utils.builtin_compat import builtin_member_compat, builtin_method_compat
from .utils.string_utils import sanitize_method_name, to_snake_case
from .utils.type_mappings import (
    GENERATED_BUILTIN_TYPES,
    PACKED_ARRAY_TYPES,
    constant_cpp_value,
    default_arg_napi_expr,
    get_cpp_type,
    js_class_name as get_js_class_name,
)

def napi_match_expr(type_name, index):
    value = f"info[{index}]"
    if type_name == 'int':
        return f"({value}.IsNumber() || {value}.IsBigInt())"
    if type_name == 'float':
        return f"{value}.IsNumber()"
    if type_name == 'bool':
        return f"{value}.IsBoolean()"
    if type_name in ('String', 'StringName', 'NodePath'):
        return (
            f"{value}.IsString() || "
            f"({value}.IsObject() && {value}.As<Napi::Object>().InstanceOf({type_name}Binding::constructor.Value()))"
        )
    if type_name == 'Basis':
        return (
            f"{value}.IsObject() && "
            f"({value}.As<Napi::Object>().InstanceOf(BasisBinding::constructor.Value()) || "
            f"{value}.As<Napi::Object>().InstanceOf(QuaternionBinding::constructor.Value()))"
        )
    if type_name == 'Variant':
        return 'true'
    if type_name == 'Array':
        return (
            f"{value}.IsArray() || "
            f"({value}.IsObject() && {value}.As<Napi::Object>().InstanceOf(ArrayBinding::constructor.Value()))"
        )
    if type_name in PACKED_ARRAY_TYPES:
        return (
            f"{value}.IsArray() || "
            f"({value}.IsObject() && {value}.As<Napi::Object>().InstanceOf({type_name}Binding::constructor.Value()))"
        )
    if type_name.startswith('typedarray::'):
        return f"{value}.IsArray() || {value}.IsObject()"
    if type_name in GENERATED_BUILTIN_TYPES:
        return f"{value}.IsObject() && {value}.As<Napi::Object>().InstanceOf({type_name}Binding::constructor.Value())"
    return f"{value}.IsObject()"


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
    'Basis': ['rows'],
    'Transform2D': ['columns'], # Transform2D exposes columns[3]
    'Transform3D': ['basis', 'origin'],
    'Projection': ['columns'],
}

# Special mapping for members that are fields but need index access or name mapping
FIELD_MAPPING = {
    'Basis': {
        'x': {'get': 'get_column(0)', 'set': 'set_column(0, {value})'},
        'y': {'get': 'get_column(1)', 'set': 'set_column(1, {value})'},
        'z': {'get': 'get_column(2)', 'set': 'set_column(2, {value})'},
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
    def run(self):
        api_data = load_extension_api_json(required_keys=("builtin_classes",))
            
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
            
            js_class_name = get_js_class_name(class_name)
            
            # Prepare context for the template
            methods = builtin_class.get('methods', [])
            vararg_methods = []
            dependencies = set()
            
            # Filter ignored methods
            ignored_methods = set()
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
                if not method_conflicts_with_builtin_member(method_name, member_names):
                    filtered_methods.append(method)
            
            methods = filtered_methods
            
            for method in methods:
                method['name_cpp'] = sanitize_method_name(method['name'])
                if class_name in ['AABB', 'Plane'] and method['name'] in ['intersects_segment', 'intersects_ray', 'intersect_3']:
                    method['name_cpp'] = method['name'] + '_bind'

                compat = builtin_method_compat(class_name, method['name'])
                if compat:
                    method['custom_function'] = compat['function']
                    dependencies.add(compat['include'])
                
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
                    
                    ret_cpp = get_cpp_type(ret_type, ret_meta, refcounted_classes, False)
                    
                    args_cpp = []
                    for arg in method.get('arguments', []):
                        args_cpp.append(get_cpp_type(arg['type'], arg.get('meta', ''), refcounted_classes, True))
                    
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
                        
                        method['return_type_cpp'] = get_cpp_type(ret_type, ret_meta, refcounted_classes, False)
                    else:
                        method['has_return_value'] = False
                    vararg_methods.append(method)

                method['default_args'] = []
                method['has_default_args'] = False
                for arg in method.get('arguments', []):
                    if 'default_value' in arg:
                        method['has_default_args'] = True
                    method['default_args'].append(default_arg_napi_expr(arg))
            
            # Process members
            members = []
            if 'members' in builtin_class:
                for member in builtin_class['members']:
                    member_name = member['name']
                    
                    # Determine if it's a direct field or property access
                    is_field = False
                    mapped_name = member_name
                    compat = builtin_member_compat(class_name, member_name)
                    if compat:
                        dependencies.add(compat['include'])
                    
                    if class_name in DIRECT_FIELDS:
                        if member_name in DIRECT_FIELDS[class_name]:
                            is_field = True
                    
                    get_expr = f"instance.{mapped_name}"
                    set_expr = f"instance.{mapped_name} = {{value}}"
                    if class_name in FIELD_MAPPING:
                         if member_name in FIELD_MAPPING[class_name]:
                             is_field = True
                             mapped_name = FIELD_MAPPING[class_name][member_name]
                             if isinstance(mapped_name, dict):
                                 get_expr = f"instance.{mapped_name['get']}"
                                 set_expr = f"instance.{mapped_name['set']}"
                                 mapped_name = member_name
                             else:
                                 get_expr = f"instance.{mapped_name}"
                                 set_expr = f"instance.{mapped_name} = {{value}}"
                    
                    member_data = {
                        'name': member_name,
                        'mapped_name': mapped_name,
                        'get_expr': get_expr,
                        'set_expr': set_expr,
                        'type': member['type'],
                        'type_cpp': get_cpp_type(member['type'], '', refcounted_classes, False),
                        'getter': f"get_{member_name}",
                        'setter': f"set_{member_name}",
                        'is_field': is_field,
                        'custom_getter': compat['getter'] if compat else None,
                        'custom_setter': compat['setter'] if compat else None,
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
                             'type_cpp': get_cpp_type(arg['type'], '', refcounted_classes, True),
                             'match_expr': napi_match_expr(arg['type'], len(args)),
                         })
                    constructors.append({
                        'index': c['index'],
                        'arguments': args
                    })

            # Process operators
            operators = []
            operator_groups = {}
            for constructor in constructors:
                for arg in constructor['arguments']:
                    arg_type = arg['type']
                    if arg_type in GENERATED_BUILTIN_TYPES and arg_type != class_name:
                        dependencies.add(f"builtin/{to_snake_case(arg_type)}_binding.gen.h")
                    if arg_type == 'Basis' and class_name != 'Quaternion':
                        dependencies.add("builtin/quaternion_binding.gen.h")
            
            if 'operators' in builtin_class:
                for op in builtin_class['operators']:
                    op_symbol = op['name']
                    
                    # Determine arguments
                    right_type = op.get('right_type')
                    is_unary = not bool(right_type)

                    op_name = builtin_operator_method_name(op_symbol)
                    variant_op = variant_operator_enum_name(op_symbol)
                    if not op_name or not variant_op:
                        continue

                    args = []
                    if right_type:
                        args.append({
                            'name': 'right',
                            'type': right_type,
                            'type_cpp': get_cpp_type(right_type, '', refcounted_classes, True),
                            'match_expr': napi_match_expr(right_type, 0),
                        })
                        
                        if right_type in GENERATED_BUILTIN_TYPES and right_type != class_name:
                             dependencies.add(f"builtin/{to_snake_case(right_type)}_binding.gen.h")
                        elif right_type == 'Object':
                             dependencies.add(f"classes/object_binding.gen.h")
                        
                    
                    return_type = op['return_type']
                    
                    # C++ operator is just the symbol
                    cpp_op = op_symbol
                    if op_symbol == 'unary-': cpp_op = '-'
                    if op_symbol == 'unary+': cpp_op = '+'
                    if op_symbol == 'not': cpp_op = '!';
                    if op_symbol == 'and': cpp_op = '&&';
                    if op_symbol == 'or': cpp_op = '||';
                    if op_symbol == 'xor': cpp_op = '^'; # ??
                    
                    op_data = {
                        'name': op_name, # JS method name (e.g. 'add')
                        'cpp_op': cpp_op, # C++ symbol (e.g. '+')
                        'return_type': return_type,
                        'return_type_cpp': get_cpp_type(return_type, '', refcounted_classes, False),
                        'arguments': args,
                        'is_unary': is_unary,
                        'variant_op': variant_op,
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
                    'overloads': overloads,
                    'has_unary': any(overload['is_unary'] for overload in overloads),
                    'has_binary': any(not overload['is_unary'] for overload in overloads),
                })

            constants = []
            for constant in builtin_class.get('constants', []):
                constants.append({
                    **constant,
                    'cpp_value': constant_cpp_value(constant),
                })

            context = {
                'js_class_name': js_class_name,
                'class_name': class_name,
                'methods': methods,
                'vararg_methods': vararg_methods,
                'members': members,
                'constants': constants,
                'enums': builtin_class.get('enums', []),
                'operators': grouped_operators, # Changed structure!
                'constructors': constructors,
                'has_destructor': builtin_class.get('has_destructor', False),
                'indexing_return_type': builtin_class.get('indexing_return_type'),
                'is_iterable_array': class_name == 'Array' or (class_name.startswith('Packed') and class_name.endswith('Array')),
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
