import json
import os
from core.base_generator import CodeGenerator

class UtilityFunctionsGenerator(CodeGenerator):
    def run(self):
        root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        api_path = os.path.join(root_dir, 'godot-cpp', 'gdextension', 'extension_api.json')
        
        try:
            with open(api_path, 'r', encoding='utf-8') as f:
                api_data = json.load(f)
        except FileNotFoundError:
            print(f"Error: extension_api.json not found at {api_path}")
            return

        utility_funcs = []
        if 'utility_functions' in api_data:
            for func in api_data['utility_functions']:
                # Filter logic if needed, currently taking all or specific ones as per manual example
                # The manual example only had a few functions: print, print_rich, printerr, printt, prints, printraw, print_verbose, push_error, push_warning, max, min, str
                # We should probably generate all of them or a specific subset. 
                # Let's generate all that are vararg for now as the template supports vararg primarily.
                
                name = func['name']
                is_vararg = func.get('is_vararg', False)
                return_type = func.get('return_type')
                
                # Check for return type void explicitly to determine has_return_value
                has_ret = True
                if return_type == 'void' or return_type is None:
                    has_ret = False
                
                # We want to support ALL utility functions, not just vararg ones.
                # The template might need adjustment if non-vararg functions are handled differently.
                # But looking at utility_functions.cpp.jinja2, it has an {% else %} block for non-vararg which currently returns Undefined.
                # So we need to implement non-vararg support in the template first, or just register them all and fix the template.
                
                # For now, let's register ALL functions.
                cpp_name = name
                if name == 'typeof':
                    cpp_name = 'type_of'
                
                if name == 'is_instance_valid':
                    continue

                # Determine correct return type
                return_type_cpp = "godot::Variant"
                if return_type:
                    # Map simple types if needed, but godot:: namespace prefix is usually enough for classes
                    # Basic types might need mapping if they differ from API names
                    if return_type == 'String':
                        return_type_cpp = 'godot::String'
                    elif return_type == 'bool':
                        return_type_cpp = 'bool'
                    elif return_type == 'int':
                        return_type_cpp = 'int64_t'
                    elif return_type == 'float':
                        return_type_cpp = 'double'
                    elif return_type in ['Variant', 'Object']: # Object return is usually Variant in utility funcs?
                         return_type_cpp = f"godot::{return_type}"
                    else:
                        # For other Godot types
                        return_type_cpp = f"godot::{return_type}"

                utility_funcs.append({
                    'name': name,
                    'cpp_name': cpp_name,
                    'is_vararg': is_vararg,
                    'has_return_value': has_ret,
                    'return_type': return_type_cpp,
                    'hash': func['hash']
                })
        
        context = {
            'utility_functions': utility_funcs
        }

        # Generate Header
        self.render('utility_functions.h.jinja2', context, 'utility_functions/utility_functions.h', 'include_dir')
        
        # Generate Source
        self.render('utility_functions.cpp.jinja2', context, 'utility_functions/utility_functions.cpp', 'src_dir')
        
        # Generate Vararg Header
        self.render('utility_functions_vararg.h.jinja2', context, 'utility_functions/utility_functions_vararg_method.h', 'include_dir')
