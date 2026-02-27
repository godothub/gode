import json
import os
import re
from core.base_generator import CodeGenerator

class ClassGenerator(CodeGenerator):
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
            
        for class_def in api_data['classes']:
            class_name = class_def['name']
            
            print(f"Generating bindings for class: {class_name}")
            
            s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', class_name)
            snake_name = re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()
            
            context = {
                'class_name': class_name,
                'snake_name': snake_name,
                'generated_subdir': 'classes',
                'inherits': class_def.get('inherits'),
                'methods': class_def.get('methods', []),
                'enums': class_def.get('enums', []),
                'constants': class_def.get('constants', []),
                'properties': class_def.get('properties', []),
                'signals': class_def.get('signals', [])
            }
            
            header_rel_path = os.path.join('classes', f"{snake_name}_binding.gen.h")
            context['header_include'] = header_rel_path.replace('\\', '/')
            self.render('class_binding.h.jinja2', context, header_rel_path, 'include_dir')
            
            source_rel_path = os.path.join('classes', f"{snake_name}_binding.gen.cpp")
            self.render('class_binding.cpp.jinja2', context, source_rel_path, 'src_dir')
