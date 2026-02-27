import json
import os
from core.base_generator import CodeGenerator

class BuiltinClassGenerator(CodeGenerator):
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
            
        for builtin_class in api_data['builtin_classes']:
            class_name = builtin_class['name']
            
            # Skip some types if needed (e.g., Nil, void)
            if class_name == 'Nil' or class_name == 'void':
                continue
            
            # Skip POD types (int, float, bool)
            if class_name in ['bool', 'int', 'float']:
                continue
                
            print(f"Generating bindings for builtin class: {class_name}")
            
            # Prepare context for the template
            context = {
                'class_name': class_name,
                'methods': builtin_class.get('methods', []),
                'members': builtin_class.get('members', []),
                'constants': builtin_class.get('constants', []),
                'operators': builtin_class.get('operators', []),
                'constructors': builtin_class.get('constructors', []),
                'has_destructor': builtin_class.get('has_destructor', False),
                'indexing_return_type': builtin_class.get('indexing_return_type'),
                'is_keyed': builtin_class.get('is_keyed', False)
            }
            
            # Generate the files
            # Naming convention: snake_case_binding.gen.h and .cpp
            import re
            s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', class_name)
            snake_name = re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()
            
            # Add snake_name to context for use in templates
            context['snake_name'] = snake_name
            context['generated_subdir'] = 'builtin'
            
            # Render Header
            header_rel_path = os.path.join('builtin', f"{snake_name}_binding.gen.h")
            context['header_include'] = header_rel_path.replace('\\', '/')
            header_file = header_rel_path
            self.render('builtin_binding.h.jinja2', context, header_file, 'include_dir')
            
            # Render Source
            source_rel_path = os.path.join('builtin', f"{snake_name}_binding.gen.cpp")
            source_file = source_rel_path
            self.render('builtin_binding.cpp.jinja2', context, source_file, 'src_dir')
