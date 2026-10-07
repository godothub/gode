#!/usr/bin/env python3
"""Build a wasm32 Godot template with the Lite extension's thread/exception ABI."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

VERSION = '5.0.7'


def fix_javascript(source):
    aliases = {alias: '_emscripten_' + base for alias, base in re.findall(
        r'(_emscripten_gl\w+)=wasmExports\["(gl\w+)"\]', source)
        if alias != '_emscripten_' + base}
    late = []
    for alias, function in aliases.items():
        signature = re.search(re.escape(alias) + r'\.sig=', source)
        if signature:
            position = signature.start()
            source = source[:position] + f'var {alias}={function};' + source[position:]
        else:
            late.append(f'{alias}={function};')
    if 'var wasmImports;' not in source:
        raise ValueError('Unsupported Emscripten JavaScript import layout')
    source = source.replace('var wasmImports;', ''.join(late) + 'var wasmImports;', 1)
    # Workers receive shared memory in their load message, after the initial
    # module declarations. Heap accessors must tolerate that initial state.
    return source.replace('if(wasmMemory.buffer!=HEAP8.buffer)',
                          'if(wasmMemory && wasmMemory.buffer!=HEAP8.buffer)', 1)


def prepare_system_library(emscripten, output):
    # Populate the SDK's PIC libc cache, then patch a private copy. Other build
    # jobs and users of the same SDK keep their original libraries.
    dummy = output / 'cache-init.c'
    dummy.write_text('int main(void) { return 0; }\n')
    subprocess.run(['emcc', '-O2', '-pthread', '-fwasm-exceptions', '-sMAIN_MODULE=1',
                    str(dummy), '-o', str(output / 'cache-init.js')], check=True)
    cache = Path(subprocess.check_output(['em-config', 'CACHE'], text=True).strip())
    source = (emscripten / 'system/lib/pthread/proxying.c').read_text()
    before = '''    return false;
  }
  pthread_mutex_lock(&ctx.sync.mutex);
  while (ctx.sync.state == PENDING)'''
    after = '''    return false;
  }
  // dlopen synchronization must wake workers blocked in Atomics.wait.
  _emscripten_thread_notify(target_thread);
  pthread_mutex_lock(&ctx.sync.mutex);
  while (ctx.sync.state == PENDING)'''
    if source.count(before) != 1:
        raise ValueError('Unsupported SDK synchronous proxy implementation')
    patched = output / 'proxying.c'
    patched.write_text(source.replace(before, after, 1))
    includes = [emscripten / path for path in (
        'system/lib/pthread', 'system/lib/libc/musl/src/internal',
        'system/lib/libc/musl/arch/emscripten')]
    target = output / 'proxying.o'
    subprocess.run(['emcc', '-O2', '-fPIC', '-pthread', '-sMAIN_MODULE=1',
                    '-DEMSCRIPTEN_DYNAMIC_LINKING',
                    '-Dhidden=__attribute__((visibility("hidden")))',
                    *['-I' + str(path) for path in includes],
                    '-c', str(patched), '-o', str(target)], check=True)
    library = output / 'libc-mt.a'
    shutil.copy(cache / 'sysroot/lib/wasm32-emscripten/pic/libc-mt.a', library)
    subprocess.run(['emar', 'r', str(library), str(target)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output(['emcc', '--version'], text=True).splitlines()[0]
    if not re.search(r'\b' + re.escape(VERSION) + r'\b', version):
        raise ValueError(f'This template requires Emscripten {VERSION}: {version}')
    emscripten = Path(subprocess.check_output(['em-config', 'EMSCRIPTEN_ROOT'], text=True).strip())
    system_libraries = output / 'system-libraries'
    system_libraries.mkdir(exist_ok=True)
    prepare_system_library(emscripten, system_libraries)
    recipe = source / 'platform/web/SCsub'
    text = recipe.read_text()
    marker = '    # BEGIN GODOT-JS WASM32 TEMPLATE\n'
    end = '    # END GODOT-JS WASM32 TEMPLATE\n'
    if marker in text:
        start = text.index(marker)
        stop = text.index(end, start) + len(end)
        text = text[:start] + text[stop:]
    anchor = '    sys_env.Append(LINKFLAGS=["-s", "EXPORT_ALL=1"])\n'
    if text.count(anchor) != 1:
        raise ValueError('Unsupported Godot dynamic linking template recipe')
    extension = (marker +
                 '    sys_env.Append(CCFLAGS=["-fwasm-exceptions"])\n' +
                 '    sys_env.Append(LINKFLAGS=["-fwasm-exceptions", ' +
                 repr('-L' + str(system_libraries)) + '])\n' + end)
    recipe.write_text(text.replace(anchor, anchor + extension, 1))
    subprocess.run(['scons', 'platform=web', 'arch=wasm32', 'target=template_release',
                    'threads=yes', 'dlink_enabled=yes', 'debug_symbols=no',
                    'use_closure_compiler=no', 'lto=none', f'-j{args.jobs}'],
                   cwd=source, check=True)
    name = 'godot.web.template_release.wasm32.dlink.zip'
    result = output / name
    with zipfile.ZipFile(source / 'bin' / name) as original:
        with zipfile.ZipFile(result, 'w', compression=zipfile.ZIP_DEFLATED) as package:
            for entry in original.infolist():
                data = original.read(entry)
                if entry.filename == 'godot.js':
                    data = fix_javascript(data.decode()).encode()
                package.writestr(entry.filename, data)
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    (output / 'BUILD-METADATA.json').write_text(json.dumps({
        'godot_commit': commit, 'emscripten': version, 'arch': 'wasm32',
        'threads': True, 'dynamic_linking': True, 'wasm_exceptions': True,
        'fixes': ['webgl-import-aliases', 'worker-memory-initialization', 'dlopen-worker-wakeup'],
    }, indent=2) + '\n')
    print(result)


if __name__ == '__main__':
    main()
