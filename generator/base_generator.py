import os
from jinja2 import Environment, FileSystemLoader


class CodeGenerator:
    def __init__(self, template_dir, output_dirs):
        self.template_dir = template_dir

        if not isinstance(output_dirs, dict):
            raise ValueError("output_dirs must be a dictionary of output paths")
        self.output_dirs = output_dirs

        for path in self.output_dirs.values():
            if not os.path.exists(path):
                os.makedirs(path)

        self.env = Environment(
            loader=FileSystemLoader(self.template_dir),
            trim_blocks=True,
            lstrip_blocks=True,
            keep_trailing_newline=True,
        )

    def write_file_if_changed(self, output_path, content):
        output_parent_dir = os.path.dirname(output_path)
        if output_parent_dir and not os.path.exists(output_parent_dir):
            os.makedirs(output_parent_dir)

        if os.path.exists(output_path):
            with open(output_path, 'r', encoding='utf-8') as f:
                if f.read() == content:
                    print(f"Unchanged: {output_path}")
                    return False

        tmp_path = output_path + '.tmp'
        with open(tmp_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content)
        os.replace(tmp_path, output_path)
        print(f"Generated: {output_path}")
        return True

    def render(self, template_name, context, output_filename, output_key='default'):
        template = self.env.get_template(template_name)
        rendered_content = template.render(context)

        if output_key not in self.output_dirs:
            if 'default' in self.output_dirs:
                output_dir = self.output_dirs['default']
            else:
                if output_filename.endswith(('.h', '.hpp')):
                    output_dir = self.output_dirs.get('include_dir')
                else:
                    output_dir = self.output_dirs.get('src_dir')

                if not output_dir:
                    raise ValueError(f"Output key '{output_key}' not found in configuration and no suitable default found.")
        else:
            output_dir = self.output_dirs[output_key]

        output_path = os.path.join(output_dir, output_filename)
        self.write_file_if_changed(output_path, rendered_content)

    def run(self):
        """Override this method to implement the generation logic."""
        raise NotImplementedError("Subclasses must implement run()")
