import json
import os
import sys

# Ensure parent directory is in sys.path to import core
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.base_generator import CodeGenerator
from utils.string_utils import to_snake_case

class RegisterGenerator(CodeGenerator):
    def run(self):
        root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        api_path = os.path.join(root_dir, 'godot-cpp', 'gdextension', 'extension_api.json')
        
        try:
            with open(api_path, 'r', encoding='utf-8') as f:
                api_data = json.load(f)
        except FileNotFoundError:
            print(f"Error: extension_api.json not found at {api_path}")
            return

        builtins = []
        classes = []

        # Process Builtin Classes
        if 'builtin_classes' in api_data:
            for builtin_class in api_data['builtin_classes']:
                class_name = builtin_class['name']
                # Skip POD types and void/Nil
                if class_name in ['bool', 'int', 'float', 'void', 'Nil']:
                    continue
                
                snake_name = to_snake_case(class_name)
                
                builtins.append({
                    'class_name': class_name,
                    'snake_name': snake_name,
                    'include': f"builtin/{snake_name}_binding.gen.h"
                })

        # Process Classes
        if 'classes' in api_data:
            ignored_classes = {'WebRTCDataChannelExtension', 'AudioStreamPlaybackResampled'}
            for class_def in api_data['classes']:
                class_name = class_def['name']
                if class_name in ignored_classes:
                    continue
                snake_name = to_snake_case(class_name)
                
                classes.append({
                    'class_name': class_name,
                    'snake_name': snake_name,
                    'include': f"classes/{snake_name}_binding.gen.h"
                })

        context = {
            'builtins': builtins,
            'classes': [] # We don't want to register classes globally anymore
        }

        # Generate Header
        self.render('register_builtin.h.jinja2', context, 'register_builtin.gen.h', 'include_dir')
        
        # Generate Source
        self.render('register_builtin.cpp.jinja2', context, 'register_builtin.gen.cpp', 'src_dir')

        # Generate per-class registration files for classes
        for item in classes:
            context = {
                'class_name': item['class_name'],
                'snake_name': item['snake_name'],
                'include': item['include']
            }
            pass

        # Process Singletons
        singletons = []
        for s in api_data.get('singletons', []):
            singleton_name = s['name']
            singleton_type = s['type']
            binding_type = singleton_type
            if singleton_name == 'ClassDB':
                singleton_type = 'ClassDBSingleton'
                binding_type = 'ClassDB'
            singletons.append({
                'name': singleton_name,
                'type': singleton_type,
                'binding_type': binding_type
            })

        context = {
            'classes': classes,
            'singletons': singletons
        }
        self.render('register_classes.h.jinja2', context, 'register_classes.gen.h', 'include_dir')
        self.render('register_classes.cpp.jinja2', context, 'register_classes.gen.cpp', 'src_dir')
