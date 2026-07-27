import os

from .base_generator import CodeGenerator
from .utils.api_data import load_extension_api_json
from .utils.binding_policy import method_bind_out_argument_indices, resolve_property_accessor, skipped_method_reason
from .utils.string_utils import sanitize_method_name, to_snake_case
from .utils.type_mappings import (
    GODOT_BUILTIN_TYPES,
    default_arg_napi_expr,
    get_cpp_type,
    js_class_name as get_js_class_name,
    typed_collection_element_types,
)


class ClassGenerator(CodeGenerator):
    def run(self):
        api_data = load_extension_api_json(required_keys=("classes",))
            
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
            
            js_class_name = get_js_class_name(class_name)
            
            raw_methods = class_def.get('methods', [])
            methods = []
            skipped_methods = []
            
            # Filter methods that cannot be represented safely in JavaScript.
            for m in raw_methods:
                skipped_reason = skipped_method_reason(m, all_class_names)
                if skipped_reason:
                    skipped_methods.append((m['name'], skipped_reason))
                    continue
                methods.append(m)
            if skipped_methods:
                print(f"  Skipped {len(skipped_methods)} unsafe methods for {class_name}")
            
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
                if type_name.startswith('typedarray::'):
                    dependencies.add("godot_cpp/variant/typed_array.hpp")
                    for element_type in typed_collection_element_types(type_name):
                        process_type(element_type)
                    return
                if type_name.startswith('typeddictionary::'):
                    dependencies.add("godot_cpp/variant/typed_dictionary.hpp")
                    for element_type in typed_collection_element_types(type_name):
                        process_type(element_type)
                    return
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
                out_argument_indices = tuple(method_bind_out_argument_indices(class_name, method))
                method['out_argument_indices'] = out_argument_indices
                method['has_out_arguments'] = bool(out_argument_indices)
                method['out_argument_indices_cpp'] = ', '.join(str(index) for index in out_argument_indices)
                method['argument_count'] = len(method.get('arguments', []))
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
                    
                    ret_cpp = get_cpp_type(ret_type, ret_meta, refcounted_classes, False)
                    
                    args_cpp = []
                    for arg in method.get('arguments', []):
                        args_cpp.append(get_cpp_type(arg['type'], arg.get('meta', ''), refcounted_classes, True))
                    
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
                        
                        method['return_type_cpp'] = get_cpp_type(ret_type, ret_meta, refcounted_classes, False)
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
            live_property_getters = {}
            method_names = {m['name'] for m in methods}
            for prop in class_def.get('properties', []):
                 prop_name = prop['name']
                 prop_type = prop['type']
                 process_type(prop_type)
                 
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
                         'type': prop_type,
                         'getter': sanitize_method_name(resolved_getter),
                         'setter': sanitize_method_name(resolved_setter) if resolved_setter else None
                     })
                     if resolved_setter and prop_type in GODOT_BUILTIN_TYPES:
                         live_property_getters[sanitize_method_name(resolved_getter)] = prop_name

            for method in methods:
                method['live_property_name'] = live_property_getters.get(method['name_cpp'])

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
