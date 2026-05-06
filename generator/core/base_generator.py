import os
from jinja2 import Environment, FileSystemLoader

class CodeGenerator:
    def __init__(self, template_dir, output_config):
        self.template_dir = template_dir
        
        # Backward compatibility or dict support
        if isinstance(output_config, str):
            self.output_dirs = {'default': output_config}
        elif isinstance(output_config, dict):
            self.output_dirs = output_config
        else:
            raise ValueError("output_config must be a string (path) or a dictionary of paths")
            
        # Create all output directories
        for path in self.output_dirs.values():
            if not os.path.exists(path):
                os.makedirs(path)

        self.env = Environment(
            loader=FileSystemLoader(self.template_dir),
            trim_blocks=True,
            lstrip_blocks=True,
            keep_trailing_newline=True
        )

    def render(self, template_name, context, output_filename, output_key='default'):
        template = self.env.get_template(template_name)
        rendered_content = template.render(context)
        
        if output_key not in self.output_dirs:
             # Fallback to default if available, or error
             if 'default' in self.output_dirs:
                 output_dir = self.output_dirs['default']
             else:
                 # If user didn't specify key and no default, try 'include_dir' as sensible default for headers
                 if output_filename.endswith('.h') or output_filename.endswith('.hpp'):
                     output_dir = self.output_dirs.get('include_dir')
                 else:
                     output_dir = self.output_dirs.get('src_dir')
                 
                 if not output_dir:
                     raise ValueError(f"Output key '{output_key}' not found in configuration and no suitable default found.")
        else:
            output_dir = self.output_dirs[output_key]

        output_path = os.path.join(output_dir, output_filename)
        output_parent_dir = os.path.dirname(output_path)
        if output_parent_dir and not os.path.exists(output_parent_dir):
            os.makedirs(output_parent_dir)
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(rendered_content)
        
        print(f"Generated: {output_path}")

    def run(self):
        """Override this method to implement the generation logic."""
        raise NotImplementedError("Subclasses must implement run()")
