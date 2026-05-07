#!/usr/bin/env bash
set -euo pipefail

architecture="arm64"
generator=""
jobs=0
fresh=0
skip_codegen=0
deployment_target="10.15"

usage() {
	printf 'Usage: %s [--arch x64|arm64] [--generator NAME] [--jobs N] [--fresh] [--skip-codegen] [--deployment-target VERSION]\n' "$0"
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
		--deployment-target)
			deployment_target="${2:?missing value for --deployment-target}"
			shift 2
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

case "$architecture" in
	x64) osx_arch="x86_64" ;;
	arm64) osx_arch="arm64" ;;
	*) printf 'Unsupported macOS architecture: %s\n' "$architecture" >&2; exit 2 ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
build_root="$repo_root/build"
build_dir="$build_root/macos/$architecture"
bin_dir="$repo_root/example/addons/gode/binary/macos/$architecture"
expected_library="$bin_dir/libgode.dylib"
libnode_library="$repo_root/libnode/macos/$architecture/libnode.a"
configuration="Release"
python_executable="${PYTHON3_EXECUTABLE:-$(command -v python3)}"

if [ ! -f "$libnode_library" ]; then
	printf 'Missing libnode static library: %s\n' "$libnode_library" >&2
	exit 1
fi

if [ "$jobs" -le 0 ]; then
	if command -v sysctl >/dev/null 2>&1; then
		cpu_count="$(sysctl -n hw.ncpu 2>/dev/null || true)"
		if [ -z "$cpu_count" ]; then
			cpu_count="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')"
		fi
	else
		cpu_count="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')"
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

printf 'Configuring gode (%s, macos/%s) with %s...\n' "$configuration" "$architecture" "$generator"
cmake \
	-S "$repo_root" \
	-B "$build_dir" \
	-G "$generator" \
	-DCMAKE_BUILD_TYPE="$configuration" \
	-DCMAKE_OSX_ARCHITECTURES="$osx_arch" \
	-DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
	-DPython3_EXECUTABLE="$python_executable" \
	-DGODE_RUN_CODEGEN="$codegen" \
	-DGODE_TARGET_ARCH="$architecture"

printf 'Building gode (%s, macos/%s)...\n' "$configuration" "$architecture"
cmake --build "$build_dir" --target gode --parallel "$jobs"

if [ ! -f "$expected_library" ]; then
	printf 'Build finished, but expected GDExtension library was not found: %s\n' "$expected_library" >&2
	exit 1
fi

printf 'Built GDExtension library:\n  %s\n' "$expected_library"
