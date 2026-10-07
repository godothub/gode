#!/usr/bin/env python3
"""Export a packaged Lite project and execute its JS/npm probe in Chromium."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
from threading import Thread

from run_godot_smoke import resolve_godot
from run_npm_native_smoke import run_command

MARKER = '[GodotJsLiteTest] pure JS npm and hash passed'


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

    def log_message(self, *args):
        pass


def browser_test(directory):
    from playwright.sync_api import sync_playwright
    server = ThreadingHTTPServer(('127.0.0.1', 0), partial(Handler, directory=str(directory)))
    Thread(target=server.serve_forever, daemon=True).start()
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(headless=True)
            page = browser.new_page()
            errors = []
            page.on('console', lambda message: print(message.type, message.text, flush=True))
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.goto(f'http://127.0.0.1:{server.server_port}/index.html')
            page.wait_for_function('globalThis.godotJsSmokePassed === true', timeout=90000)
            page.wait_for_timeout(2000)
            assert not errors, errors
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
    print(MARKER + ' (Web export)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot')
    parser.add_argument('--project', type=Path)
    parser.add_argument('--template', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--browser-only', type=Path)
    args = parser.parse_args()
    if args.browser_only:
        browser_test(args.browser_only.resolve())
        return
    if not args.godot or args.project is None or args.output is None:
        parser.error('--godot, --project and --output are required')
    project = args.project.resolve()
    godot = resolve_godot(args.godot).resolve()
    template = args.template or project / 'addons/godot-js/binary/editor/web/godot.web.template_release.wasm32.dlink.zip'
    template = template.resolve()
    if not template.is_file():
        raise FileNotFoundError(template)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    preset = f'''[preset.0]
name="Web"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
script_export_mode=2
[preset.0.options]
custom_template/release={json.dumps(template.as_posix())}
variant/extensions_support=true
variant/thread_support=true
threads/emscripten_pool_size=32
threads/godot_pool_size=4
html/ensure_cross_origin_isolation_headers=true
'''
    config = project / 'project.godot'
    presets = project / 'export_presets.cfg'
    exit_script = project / 'scripts/lite_probe_exit.gd'
    originals = {path: path.read_bytes() if path.exists() else None
                 for path in (config, presets, exit_script)}
    try:
        text = originals[config].decode()
        text = re.sub(r'run/main_scene=.*', 'run/main_scene="res://scenes/lite_probe.tscn"', text)
        # Godot Web uses Compatibility rendering. The imported native project
        # may have selected Forward+ before preparing this isolated fixture.
        text = re.sub(r'^renderer/rendering_method(?:\.mobile)?=.*\n?', '', text, flags=re.M)
        if '[rendering]' not in text:
            text += '\n[rendering]\n'
        text = text.replace('[rendering]', '[rendering]\nrenderer/rendering_method="gl_compatibility"\nrenderer/rendering_method.mobile="gl_compatibility"', 1)
        config.write_text(text)
        presets.write_text(preset)
        exit_script.write_text('''extends Node
func _ready() -> void:
    await get_tree().create_timer(2).timeout
    print("[GodotJsLiteTest] Web frames completed")
''')
        run_command([str(godot), '--headless', '--path', str(project),
                     '--export-release', 'Web', str(output / 'index.html')], project, 240)
    finally:
        for path, original in originals.items():
            if original is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(original)
    html = output / 'index.html'
    observer = '''<script>
const godotJsOriginalLog = console.log.bind(console);
console.log = (...args) => {
 godotJsOriginalLog(...args);
 if (args.join(' ').includes(''' + json.dumps(MARKER) + ''')) globalThis.godotJsSmokePassed = true;
};
</script>'''
    html.write_text(html.read_text().replace('<head>', '<head>\n' + observer, 1))
    process = subprocess.Popen([sys.executable, __file__, '--browser-only', str(output)],
                               start_new_session=True)
    try:
        status = process.wait(timeout=120)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        raise RuntimeError('Web export browser did not finish within 120 seconds')
    if status:
        raise RuntimeError(f'Web export browser exited with status {status}')


if __name__ == '__main__':
    main()
