#!/usr/bin/env bash
set -euo pipefail

architecture="arm64"
configuration="Debug"
api_level="28"
android_ndk_version="28.1.13356709"
android_ndk_name="android-ndk-r28b"
android_ndk_base_url="https://dl.google.com/android/repository"
generator=""
jobs=0
fresh=0
clean=0
skip_codegen=0
python_executable="${PYTHON3_EXECUTABLE:-}"
ndk_dir_override=""

usage() {
	printf 'Usage: %s [--arch arm64] [--config Debug|Release|RelWithDebInfo|MinSizeRel] [--api-level N] [--generator NAME] [--jobs N] [--fresh] [--clean] [--skip-codegen] [--python PATH] [--ndk-dir PATH]\n' "$0"
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
		--api-level)
			api_level="${2:?missing value for --api-level}"
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
		--python)
			python_executable="${2:?missing value for --python}"
			shift 2
			;;
		--ndk-dir)
			ndk_dir_override="${2:?missing value for --ndk-dir}"
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
	arm64) android_abi="arm64-v8a" ;;
	*) printf 'Unsupported Android architecture: %s\n' "$architecture" >&2; exit 2 ;;
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
build_dir="$build_root/android/$architecture/$config_dir"
addon_root="$repo_root/example/addons/gode"
if [ "${GODE_LITE:-OFF}" = ON ]; then addon_root="$repo_root/third/godot-js/addons/godot-js"; fi
bin_dir="$addon_root/binary/android/$architecture"
expected_library="$bin_dir/libgode_runtime.so"
editor_library="$addon_root/binary/editor/android/$architecture/libgode_editor.so"
libnode_library="$repo_root/libnode/android/$architecture/libnode.a"

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

host_os="$(uname -s)"
case "$host_os" in
	Darwin)
		ndk_archive_name="$android_ndk_name-darwin.zip"
		ndk_sha1="6dec1444aceeadd46cf5e1438c28bcc521fc6668"
		;;
	Linux)
		ndk_archive_name="$android_ndk_name-linux.zip"
		ndk_sha1="f574d3165405bd59ffc5edaadac02689075a729f"
		;;
	*)
		printf 'Unsupported host OS for Android NDK download: %s\n' "$host_os" >&2
		exit 2
		;;
esac

if [ -n "$ndk_dir_override" ]; then
	if [ ! -d "$ndk_dir_override" ]; then
		printf 'Android NDK directory was not found: %s\n' "$ndk_dir_override" >&2
		exit 1
	fi
	ndk_dir="$(cd "$ndk_dir_override" && pwd)"
else
	ndk_dir="$build_root/$android_ndk_name"
fi
ndk_archive="$build_root/$ndk_archive_name"
toolchain_file="$ndk_dir/build/cmake/android.toolchain.cmake"

get_ndk_revision() {
	local properties_file="$1/source.properties"
	if [ ! -f "$properties_file" ]; then
		return 0
	fi

	sed -n 's/^Pkg\.Revision[[:space:]]*=[[:space:]]*//p' "$properties_file" | head -n 1
}

sha1_file() {
	if command -v shasum >/dev/null 2>&1; then
		shasum -a 1 "$1" | awk '{print $1}'
	elif command -v sha1sum >/dev/null 2>&1; then
		sha1sum "$1" | awk '{print $1}'
	else
		printf 'Neither shasum nor sha1sum was found.\n' >&2
		exit 1
	fi
}

download_ndk_archive() {
	local partial_archive="$ndk_archive.part"

	if [ -f "$ndk_archive" ]; then
		actual_sha1="$(sha1_file "$ndk_archive")"
		if [ "$actual_sha1" = "$ndk_sha1" ]; then
			printf 'Using existing Android NDK archive:\n  %s\n' "$ndk_archive"
			return
		fi

		printf 'Existing Android NDK archive is incomplete or invalid; resuming download.\n'
		mv "$ndk_archive" "$partial_archive"
	fi

	if [ -f "$partial_archive" ]; then
		actual_sha1="$(sha1_file "$partial_archive")"
		if [ "$actual_sha1" = "$ndk_sha1" ]; then
			printf 'Using completed Android NDK partial archive:\n  %s\n' "$partial_archive"
			mv "$partial_archive" "$ndk_archive"
			return
		fi
	fi

	if ! command -v curl >/dev/null 2>&1; then
		printf 'curl was not found. Cannot download Android NDK %s.\n' "$android_ndk_version" >&2
		exit 1
	fi

	printf 'Downloading Android NDK %s...\n' "$android_ndk_version"
	printf '  %s/%s\n' "$android_ndk_base_url" "$ndk_archive_name"
	curl \
		-L \
		--fail \
		--retry 3 \
		--retry-all-errors \
		--retry-delay 5 \
		--continue-at - \
		--progress-bar \
		"$android_ndk_base_url/$ndk_archive_name" \
		-o "$partial_archive"

	actual_sha1="$(sha1_file "$partial_archive")"
	if [ "$actual_sha1" != "$ndk_sha1" ]; then
		printf 'Android NDK archive SHA1 mismatch.\nExpected: %s\nActual:   %s\nArchive:  %s\n' "$ndk_sha1" "$actual_sha1" "$partial_archive" >&2
		exit 1
	fi

	mv "$partial_archive" "$ndk_archive"
}

if [ ! -f "$toolchain_file" ] && [ -z "$ndk_dir_override" ]; then
	mkdir -p "$build_root"
	download_ndk_archive

	if [ -d "$ndk_dir" ]; then
		existing_revision="$(get_ndk_revision "$ndk_dir")"
		printf 'Existing Android NDK directory has revision "%s", expected "%s":\n  %s\n' "$existing_revision" "$android_ndk_version" "$ndk_dir" >&2
		exit 1
	fi

	if ! command -v unzip >/dev/null 2>&1; then
		printf 'unzip was not found. Cannot extract Android NDK archive: %s\n' "$ndk_archive" >&2
		exit 1
	fi

	printf 'Extracting Android NDK to build directory...\n'
	unzip -q "$ndk_archive" -d "$build_root"
fi

if [ ! -f "$toolchain_file" ]; then
	if [ -n "$ndk_dir_override" ]; then
		printf 'Android NDK toolchain file was not found in --ndk-dir.\nExpected toolchain: %s\n' "$toolchain_file" >&2
	else
		printf 'Android NDK %s was not found after extraction.\nExpected toolchain: %s\n' "$android_ndk_version" "$toolchain_file" >&2
	fi
	exit 1
fi

installed_revision="$(get_ndk_revision "$ndk_dir")"
if [ "$installed_revision" != "$android_ndk_version" ]; then
	printf 'Android NDK revision mismatch.\nExpected: %s\nActual:   %s\nDirectory: %s\n' "$android_ndk_version" "$installed_revision" "$ndk_dir" >&2
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

printf 'Configuring gode (%s, android/%s, API %s) with %s...\n' "$configuration" "$architecture" "$api_level" "$generator"
printf 'Using Android NDK:\n  %s\n' "$ndk_dir"
cmake \
	-S "$repo_root" \
	-B "$build_dir" \
	-G "$generator" \
	-DCMAKE_BUILD_TYPE="$configuration" \
	-DCMAKE_SUPPRESS_REGENERATION=ON \
	-DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
	-DANDROID_ABI="$android_abi" \
	-DANDROID_PLATFORM="android-$api_level" \
	-DANDROID_STL=c++_shared \
	-DPython3_EXECUTABLE="$python_executable" \
	-DGODE_RUN_CODEGEN="$codegen" \
	-DGODE_LITE="${GODE_LITE:-OFF}" \
	-DGODE_TARGET_ARCH="$architecture" \
	-DGODOTCPP_TARGET="$godotcpp_target"

printf 'Building gode (%s, android/%s)...\n' "$configuration" "$architecture"
cmake --build "$build_dir" --target gode --config "$configuration" --parallel "$jobs"

for library in "$expected_library" "$editor_library"; do
	if [ ! -f "$library" ]; then
		printf 'Build finished, but expected GDExtension library was not found: %s\n' "$library" >&2
		exit 1
	fi
done

printf 'Built GDExtension library:\n  %s\n' "$expected_library"
