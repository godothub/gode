#!/usr/bin/env python3
"""Generate the standalone addon from the canonical Gode implementation."""
import argparse
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'example/addons/gode'


def sync(output):
    output = Path(output).resolve()
    tracked = subprocess.check_output(
        ['git', '-C', str(ROOT), 'ls-files', '-z', 'example/addons/gode'],
    ).decode().split('\0')
    for name in filter(None, tracked):
        source = ROOT / name
        relative = source.relative_to(SOURCE)
        if relative.name.endswith('.import'):
            continue
        if relative.name in ('gode.gd', 'gode.gd.uid'):
            relative = relative.with_name(relative.name.replace('gode.gd', 'godot-js.gd', 1))
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.suffix in ('.gd', '.js', '.json', '.cfg', '.template', '.gdextension'):
            text = source.read_text().replace('res://addons/gode/', 'res://addons/godot-js/')
            text = text.replace('addons/gode/', 'addons/godot-js/')
            text = text.replace('addons/godot-js/gode.gd', 'addons/godot-js/godot-js.gd')
            if source.name == 'gode.gdextension':
                text = text.partition('[dependencies]')[0]
                text += '\nweb.debug.wasm32 = "res://addons/godot-js/binary/web/wasm32/libgode_runtime.wasm"\n'
                text += 'web.release.wasm32 = "res://addons/godot-js/binary/web/wasm32/libgode_runtime.wasm"\n'
            if source.name == 'export_plugin.gd':
                text = text.replace('const LITE_RUNTIME := false', 'const LITE_RUNTIME := true')
            if source.name == 'plugin.cfg':
                text = text.replace('name="gode"', 'name="godot-js"')
                text = text.replace('script="gode.gd"', 'script="godot-js.gd"')
                text = text.replace('Godot with TypeScript and Node.js', 'Godot JavaScript and TypeScript (Lite runtime)')
            destination.write_text(text)
        else:
            shutil.copy2(source, destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'third/godot-js/addons/godot-js')
    sync(parser.parse_args().output)
