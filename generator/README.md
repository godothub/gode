# Gode Code Generator

This directory contains the Python + Jinja2 code generation framework for Gode.

## Structure

- `generator.py`: Main entry point. It owns the explicit generator execution order.
- `base_generator.py`: Shared Jinja2 rendering and write-if-changed support.
- `builtin_classes_generator.py`: Generates built-in Variant bindings.
- `class_generator.py`: Generates Godot class bindings.
- `register_generator.py`: Generates binding registration files.
- `utility_functions_generator.py`: Generates utility-function wrappers.
- `dts_generator.py`: Generates TypeScript declaration files.
- `utils/`: Contains shared generator policy, path, naming, and type-mapping helpers.
- `templates/`: Jinja2 templates for code generation.

## Usage

1. Install dependencies:
   ```bash
   python -m pip install -r generator/requirements.txt
   ```

2. Create a new generator:
   - Create a Python file directly under `generator/`.
   - Define a class inheriting from `generator.base_generator.CodeGenerator`.
   - Implement the `run(self)` method.
   - Use `self.render(template_name, context, output_filename, output_key)` to generate files.
   - Add the generator class to `GENERATOR_CLASSES` in `generator/generator.py`.

3. Run the generator:
   ```bash
   python generator/generator.py
   ```

By default, the generator reads `third/godot-cpp/gdextension/extension_api.json`.
Set `GODOT_EXTENSION_API_JSON` or `GODOT_CPP_DIR` when generating against a
different Godot API checkout.

## Example Generator

```python
from generator.base_generator import CodeGenerator

class MyGenerator(CodeGenerator):
    def run(self):
        context = {"name": "World"}
        self.render("my_template.jinja2", context, "my_output.cpp", "src_dir")
```
