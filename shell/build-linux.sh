#!/usr/bin/env bash
set -euo pipefail

architecture="x64"
generator=""
jobs=0
fresh=0
skip_codegen=0

usage() {
	printf 'Usage: %s [--arch x64] [--generator NAME] [--jobs N] [--fresh] [--skip-codegen]\n' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--arch)
			architecture="${2:?missing value for --arch}"
			shift 2
			;;
		--generator)
			generator="${2:?missing value for --generator}"
			shift 2
			;;
		--jobs)
			jobs="${2:?missing value for --jobs}"
			shift 2
			;;
		--fresh)
			fresh=1
			shift
			;;
		--skip-codegen)
			skip_codegen=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			printf 'Unknown argument: %s\n' "$1" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [ "$architecture" != "x64" ]; then
	printf 'Unsupported Linux architecture: %s\n' "$architecture" >&2
	exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
build_root="$repo_root/build"
build_dir="$build_root/linux/$architecture"
bin_dir="$repo_root/example/addons/gode/binary/linux/$architecture"
expected_library="$bin_dir/libgode.so"
libnode_library="$repo_root/libnode/linux/$architecture/libnode.a"
configuration="Release"
python_executable="${PYTHON3_EXECUTABLE:-$(command -v python3)}"

if [ ! -f "$libnode_library" ]; then
	printf 'Missing libnode static library: %s\n' "$libnode_library" >&2
	exit 1
fi

if [ "$jobs" -le 0 ]; then
	if command -v nproc >/dev/null 2>&1; then
		cpu_count="$(nproc)"
	elif command -v getconf >/dev/null 2>&1; then
		cpu_count="$(getconf _NPROCESSORS_ONLN)"
	else
		cpu_count="2"
	fi
	jobs=$(( cpu_count > 2 ? cpu_count - 1 : 1 ))
fi

if [ -z "$generator" ]; then
	if command -v ninja >/dev/null 2>&1; then
		generator="Ninja"
	else
		generator="Unix Makefiles"
	fi
fi

mkdir -p "$build_dir"
if [ "$fresh" -eq 1 ]; then
	rm -f "$build_dir/CMakeCache.txt"
	rm -rf "$build_dir/CMakeFiles"
fi

codegen="ON"
if [ "$skip_codegen" -eq 1 ]; then
	codegen="OFF"
fi

printf 'Configuring gode (%s, linux/%s) with %s...\n' "$configuration" "$architecture" "$generator"
cmake \
	-S "$repo_root" \
	-B "$build_dir" \
	-G "$generator" \
	-DCMAKE_BUILD_TYPE="$configuration" \
	-DPython3_EXECUTABLE="$python_executable" \
	-DGODE_RUN_CODEGEN="$codegen" \
	-DGODE_TARGET_ARCH="$architecture"

printf 'Building gode (%s, linux/%s)...\n' "$configuration" "$architecture"
cmake --build "$build_dir" --target gode --parallel "$jobs"

if [ ! -f "$expected_library" ]; then
	printf 'Build finished, but expected GDExtension library was not found: %s\n' "$expected_library" >&2
	exit 1
fi

printf 'Built GDExtension library:\n  %s\n' "$expected_library"
