#!/usr/bin/env bash
set -euo pipefail

output_directory=""
skip_binary_validation=0

usage() {
	printf 'Usage: %s [--output-directory DIR] [--skip-binary-validation]\n' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--output-directory)
			output_directory="${2:?missing value for --output-directory}"
			shift 2
			;;
		--skip-binary-validation)
			skip_binary_validation=1
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

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

if [ -z "$output_directory" ]; then
	output_directory="$repo_root/dist"
fi

addon_root="$repo_root/example/addons/gode"
staging_root="$repo_root/build/package-staging"
staged_addon_root="$staging_root/gode"
archive_name="gode.zip"
archive_path="$output_directory/$archive_name"

required_files=(
	"plugin.cfg"
	"gode.gd"
	"binary/gode.gdextension"
	"config/gode.json"
	"config/tsconfig.json"
	"icons/typescript.svg"
	"runtime/event_loop.gd"
	"runtime/export_plugin.gd"
	"runtime/typescript_compiler.js"
	"types/globals.d.ts"
	"types/godot.d.ts"
)
required_binaries=(
	"binary/windows/x64/libgode.dll"
	"binary/linux/x64/libgode.so"
	"binary/macos/arm64/libgode.dylib"
	"binary/android/arm64/libgode.so"
	"binary/ios/arm64/libgode.dylib"
)

for file in "${required_files[@]}"; do
	if [ ! -f "$addon_root/$file" ]; then
		printf 'Missing plugin file: %s\n' "$file" >&2
		exit 1
	fi
done

if [ "$skip_binary_validation" -eq 0 ]; then
	for file in "${required_binaries[@]}"; do
		if [ ! -f "$addon_root/$file" ]; then
			printf 'Missing built plugin binary: %s\n' "$file" >&2
			exit 1
		fi
	done
fi

rm -rf "$staging_root"
mkdir -p "$staged_addon_root" "$output_directory"
cp -R "$addon_root"/. "$staged_addon_root"/
find "$staged_addon_root" -type f -name '*.import' -delete

"$script_dir/prepare-typescript.sh" --output-directory "$staged_addon_root/tsc"

for file in "tsc/package.json" "tsc/lib/typescript.js"; do
	if [ ! -f "$staged_addon_root/$file" ]; then
		printf 'Missing packaged TypeScript compiler file: %s\n' "$file" >&2
		exit 1
	fi
done

find "$staged_addon_root/binary" -type f \( -name '*.lib' -o -name '*.exp' -o -name '*.pdb' -o -name '*.ilk' \) -delete

rm -f "$archive_path"
(
	cd "$staging_root"
	zip -qr "$archive_path" gode
)

printf 'Packaged plugin:\n  %s\n' "$archive_path"
