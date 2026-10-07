#!/usr/bin/env python3
"""Export and execute the packaged Lite probe with an actual export template."""
import argparse
import json
import pathlib
import re
import stat
import zipfile
from run_godot_smoke import non_leak_error_lines, resolve_godot
from run_npm_native_export_smoke import ensure_export_templates
from run_npm_native_smoke import host_platform, run_command

MARKER = '[GodotJsLiteTest] pure JS npm and hash passed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', required=True)
    parser.add_argument('--project', required=True, type=pathlib.Path)
    parser.add_argument('--godot-version', default='4.7-stable')
    parser.add_argument('--install-export-templates', action='store_true')
    parser.add_argument('--template-download-dir', required=True, type=pathlib.Path)
    args = parser.parse_args()
    godot = resolve_godot(args.godot).resolve()
    project = args.project.resolve()
    if not (project / 'scripts/lite_probe.ts').is_file():
        raise FileNotFoundError('Prepare the isolated godot-js smoke project first')
    templates = ensure_export_templates(godot, args)
    platform, _ = host_platform()
    platform_name = {'linux': 'Linux/X11', 'windows': 'Windows Desktop', 'macos': 'macOS'}[platform]
    architecture = 'universal' if platform == 'macos' else 'x86_64'
    template_name = {'linux': 'linux_release.x86_64', 'windows': 'windows_release_x86_64.exe', 'macos': 'macos.zip'}[platform]
    extension = {'linux': '.x86_64', 'windows': '.exe', 'macos': '.zip'}[platform]
    output = project.parent / 'godot-js-export-smoke'
    output.mkdir(exist_ok=True)
    archive = output / ('probe' + extension)
    if archive.exists():
        archive.unlink()
    preset = f'''[preset.0]
name="Lite"
platform={json.dumps(platform_name)}
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
script_export_mode=2
[preset.0.options]
custom_template/release={json.dumps((templates / template_name).as_posix())}
binary_format/architecture={json.dumps(architecture)}
binary_format/embed_pck=true
application/bundle_identifier="org.godothub.godot-js.smoke"
codesign/codesign=0
codesign/enable=false
'''
    config = project / 'project.godot'
    presets = project / 'export_presets.cfg'
    original_config = config.read_bytes()
    original_presets = presets.read_bytes() if presets.exists() else None
    try:
        config.write_text(re.sub(r'run/main_scene=.*', 'run/main_scene="res://scenes/lite_probe.tscn"', original_config.decode()))
        presets.write_text(preset)
        run_command([str(godot), '--headless', '--path', str(project), '--export-release', 'Lite', str(archive)], project, 180)
    finally:
        config.write_bytes(original_config)
        if original_presets is None:
            presets.unlink(missing_ok=True)
        else:
            presets.write_bytes(original_presets)
    binary = archive
    if platform == 'macos':
        with zipfile.ZipFile(archive) as package:
            package.extractall(output)
        binary = next(output.glob('*.app/Contents/MacOS/*'))
    binary.chmod(binary.stat().st_mode | stat.S_IXUSR)
    result = run_command([str(binary), '--headless', '--quit-after', '240'], binary.parent, 90)
    errors = non_leak_error_lines(result)
    if MARKER not in result or errors:
        raise RuntimeError('Lite export smoke failed:\n' + result)
    print(MARKER + ' (export template)')


if __name__ == '__main__':
    main()
