from .base_generator import CodeGenerator
from .utils.api_data import load_extension_api_json
from .utils.string_utils import to_snake_case

class RegisterGenerator(CodeGenerator):
    def run(self):
        api_data = load_extension_api_json(required_keys=("builtin_classes", "classes"))

        builtins = []
        classes = []

        # Process Builtin Classes
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
        for class_def in api_data['classes']:
            class_name = class_def['name']
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
