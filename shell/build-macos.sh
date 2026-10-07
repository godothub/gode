#!/usr/bin/env bash
set -euo pipefail

architecture="arm64"
configuration="Debug"
generator=""
jobs=0
fresh=0
clean=0
skip_codegen=0
deployment_target="10.15"
python_executable="${PYTHON3_EXECUTABLE:-}"

usage() {
	printf 'Usage: %s [--arch x64|arm64] [--config Debug|Release|RelWithDebInfo|MinSizeRel] [--generator NAME] [--jobs N] [--fresh] [--clean] [--skip-codegen] [--deployment-target VERSION] [--python PATH]\n' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--arch)
			architecture="${2:?missing value for --arch}"
			shift 2
			;;
		--config)
			configuration="${2:?missing value for --config}"
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
		--clean)
			clean=1
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
		--python)
			python_executable="${2:?missing value for --python}"
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

case "$configuration" in
	Debug) godotcpp_target="template_debug"; config_dir="debug" ;;
	Release) godotcpp_target="template_release"; config_dir="release" ;;
	RelWithDebInfo) godotcpp_target="template_release"; config_dir="relwithdebinfo" ;;
	MinSizeRel) godotcpp_target="template_release"; config_dir="minsizerel" ;;
	*) printf 'Unsupported build configuration: %s\n' "$configuration" >&2; exit 2 ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
build_root="$repo_root/build"
if [ "${GODE_LITE:-OFF}" = ON ]; then build_root="$build_root/godot-js"; fi
build_dir="$build_root/macos/$architecture/$config_dir"
addon_root="$repo_root/example/addons/gode"
if [ "${GODE_LITE:-OFF}" = ON ]; then addon_root="$repo_root/third/godot-js/addons/godot-js"; fi
bin_dir="$addon_root/binary/macos/$architecture"
runtime_library="$bin_dir/libgode_runtime.dylib"
node_helper="$bin_dir/gode_node"
editor_library="$addon_root/binary/editor/macos/$architecture/libgode_editor.dylib"
libnode_library="$repo_root/libnode/macos/$architecture/libnode.a"

if [ -z "$python_executable" ]; then
	python_executable="$(command -v python3 || command -v python || true)"
fi
if [ -z "$python_executable" ]; then
	printf 'Python was not found. Install Python, set PYTHON3_EXECUTABLE, or pass --python PATH.\n' >&2
	exit 1
fi

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

case "$build_dir" in
	"$build_root"/*) ;;
	*) printf 'Refusing to modify path outside build directory: %s\n' "$build_dir" >&2; exit 1 ;;
esac

if [ "$clean" -eq 1 ]; then
	rm -rf "$build_dir"
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
	-DGODE_LITE="${GODE_LITE:-OFF}" \
	-DGODE_TARGET_ARCH="$architecture" \
	-DGODOTCPP_TARGET="$godotcpp_target"

printf 'Building gode (%s, macos/%s)...\n' "$configuration" "$architecture"
cmake --build "$build_dir" --target gode --config "$configuration" --parallel "$jobs"

for expected_library in "$runtime_library" "$editor_library"; do
	if [ ! -f "$expected_library" ]; then
		printf 'Build finished, but expected Gode binary was not found: %s\n' "$expected_library" >&2
		exit 1
	fi
done

if [ "${GODE_LITE:-OFF}" != ON ] && [ ! -f "$node_helper" ]; then
    printf 'Missing Node helper: %s\n' "$node_helper" >&2; exit 1
fi

printf 'Built Gode binaries:\n  %s\n  %s\n' "$runtime_library" "$editor_library"
