# Gode Code Generator

This directory contains the Python+Jinja2 code generation framework for Gode.

## Structure

- `core/`: Contains the base generator class and utilities.
- `templates/`: Jinja2 templates for code generation.
- `builtin/`, `class/`, `utility/`: Packages containing specific generator implementations.
- `generator.py`: Main entry point to run all generators.

## Usage

1. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```

2. Create a new generator:
   - Create a python file in `builtin/`, `class/`, or `utility/`.
   - Define a class inheriting from `code_generator.core.base_generator.CodeGenerator`.
   - Implement the `run(self)` method.
   - Use `self.render(template_name, context, output_filename)` to generate files.

3. Run the generator:
   ```bash
   python generator.py
   ```

## Example Generator

```python
from ..core.base_generator import CodeGenerator

class MyGenerator(CodeGenerator):
    def run(self):
        context = {"name": "World"}
        self.render("my_template.jinja2", context, "my_output.cpp")
```
