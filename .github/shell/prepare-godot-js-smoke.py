#!/usr/bin/env python3
"""Prepare an isolated project that exercises the packaged Lite addon."""
import argparse
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def prepare(archive, project):
    project = Path(project).resolve()
    if project.exists():
        raise FileExistsError(f'Smoke project already exists: {project}')
    shutil.copytree(ROOT / 'example', project, ignore=shutil.ignore_patterns(
        'addons', '.godot', '.gode', 'node_modules'))
    with zipfile.ZipFile(archive) as package:
        package.extractall(project / 'addons')
    for name in ('project.godot', 'tsconfig.json'):
        path = project / name
        path.write_text(path.read_text().replace('addons/gode/', 'addons/godot-js/'))
    module = project / 'node_modules/pure-js-probe'
    module.mkdir(parents=True)
    (module / 'package.json').write_text('{"name":"pure-js-probe","main":"index.cjs"}')
    (module / 'index.cjs').write_text('module.exports = (a, b) => a + b;\n')
    (project / 'scripts/types/lite_probe.d.ts').write_text('''
declare module 'node:module' {
  export function createRequire(url: string): (id: string) => any;
}
declare module 'pure-js-probe' {
  export default function add(a: number, b: number): number;
}
''')
    (project / 'scripts/lite_probe.ts').write_text('''
import { Node, RenderingServer, OS } from 'godot';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import add from 'pure-js-probe';
import { answer } from './lite_math';
declare const process: any;
const require = createRequire(import.meta.url);
export default class LiteProbe extends Node {
  _ready() {
    assert.equal(add(21, 21), 42);
    assert.equal(answer(), 42);
    assert.equal(process._linkedBinding('godot').sha256_text('abc'),
      'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
    assert.throws(() => require('node:crypto'));
    assert.throws(() => require('node:child_process'));
    assert.equal(typeof Intl, 'undefined');
    const nodeConstants = this as unknown as Node & { NOTIFICATION_READY: number; ProcessMode: typeof Node.ProcessMode };
    assert.equal(nodeConstants.NOTIFICATION_READY, Node.NOTIFICATION_READY);
    assert.equal(Node.ProcessMode[Node.PROCESS_MODE_ALWAYS], 'PROCESS_MODE_ALWAYS');
    assert.equal(nodeConstants.ProcessMode[Node.PROCESS_MODE_ALWAYS], 'PROCESS_MODE_ALWAYS');
    assert.notEqual(nodeConstants.ProcessMode, Node.ProcessMode);
    assert.equal(RenderingServer.ArrayFormat.ARRAY_FLAG_FORMAT_VERSION_2, 34359738368);
    assert.equal(RenderingServer.ArrayFormat[34359738368], 'ARRAY_FLAG_FORMAT_CURRENT_VERSION');
    const sentinel = new Error('binding conversion sentinel');
    assert.throws(() => this.add_child(new Proxy({}, { has() { throw sentinel; } }) as any),
      (error: unknown) => error === sentinel);
    assert.throws(() => this.set_meta('hostile', { get value() { throw sentinel; } }),
      (error: unknown) => error === sentinel);
    assert.equal(this.has_meta('hostile'), false);
    assert.equal(process._linkedBinding('godot').EditorPlugin, undefined);
    if (!OS.has_feature('editor')) {
      assert.equal((globalThis as any).__gode_compile_typescript_project, undefined);
      assert.equal((globalThis as any).__gode_typescript_compiler_boot_error, undefined);
    }
    console.log('[GodotJsLiteTest] pure JS npm and hash passed');
  }
}
''')
    (project / 'scripts/lite_math.ts').write_text('export function answer(): number { return 42; }\n')
    (project / 'scenes/lite_probe.tscn').write_text('''[gd_scene load_steps=3 format=3]
[ext_resource type="Script" path="res://scripts/lite_probe.ts" id="1"]
[ext_resource type="Script" path="res://scripts/lite_probe_exit.gd" id="2"]
[node name="LiteProbe" type="Node"]
script = ExtResource("1")
[node name="Exit" type="Node" parent="."]
script = ExtResource("2")
''')
    (project / 'scripts/lite_probe_exit.gd').write_text('''extends Node
func _ready() -> void:
    await get_tree().process_frame
    get_tree().quit()
''')
    print(project)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--project', required=True, type=Path)
    args = parser.parse_args()
    prepare(args.archive, args.project)
