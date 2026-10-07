#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_LIBRARY_DIR="${GODE_LIBNODE_DIR:-${ROOT_DIR}/libnode}"
WEB_BUILD_DIR="${GODE_BUILD_DIR:-${ROOT_DIR}/build/godot-js-web}"
WEB_JOBS="${JOBS:-4}"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --libnode-dir) WEB_LIBRARY_DIR="$2"; shift 2 ;;
        --build-dir) WEB_BUILD_DIR="$2"; shift 2 ;;
        --jobs) WEB_JOBS="$2"; shift 2 ;;
        *) printf 'Unknown Web build option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
WEB_PYTHON="${PYTHON:-${ROOT_DIR}/.venv/bin/python}"
if [ ! -x "$WEB_PYTHON" ]; then WEB_PYTHON="$(command -v python3)"; fi
"$WEB_PYTHON" "$ROOT_DIR/generator/generator.py"
"$WEB_PYTHON" "$ROOT_DIR/.github/shell/sync-godot-js.py"
emcmake cmake -S "$ROOT_DIR" -B "$WEB_BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DGODE_RUN_CODEGEN=OFF -DPython3_EXECUTABLE="$WEB_PYTHON" -DGODE_LITE=ON -DGODE_BUILD_EDITOR_EXTENSION=OFF \
    -DGODE_UNITY_GENERATED_BINDINGS=ON -DGODE_UNITY_GENERATED_BATCH_SIZE=8 \
    -DGODE_LIBNODE_DIR="$WEB_LIBRARY_DIR"
cmake --build "$WEB_BUILD_DIR" -j"$WEB_JOBS"
