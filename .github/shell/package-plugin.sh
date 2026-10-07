#!/usr/bin/env bash
set -euo pipefail

output_directory=""
skip_binary_validation=0
variant=gode

usage() {
	printf 'Usage: %s [--output-directory DIR] [--skip-binary-validation] [--variant gode|godot-js]\n' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--output-directory)
			output_directory="${2:?missing value for --output-directory}"
			shift 2
			;;
		--variant)
			variant="${2:?missing variant}"
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

case "$variant" in gode|godot-js) ;; *) printf 'Unknown variant: %s\n' "$variant" >&2; exit 2 ;; esac
addon_root="$repo_root/example/addons/gode"
staging_root="$repo_root/build/package-staging"
staged_addon_root="$staging_root/$variant"
archive_name="$variant.zip"
archive_path="$output_directory/$archive_name"

required_files=(
	"plugin.cfg"
	"gode.gd"
	"binary/gode.gdextension"
	"binary/gode.gdextension.uid"
	"binary/gode_editor.gdextension.template"
	"config/gode.json"
	"config/tsconfig.json"
	"icons/typescript.svg"
	"runtime/event_loop.gd"
	"runtime/export_plugin.gd"
	"runtime/typescript_compiler.js"
	"types/.gdignore"
	"types/globals.d.ts"
	"types/godot.d.ts"
)
binary_root="$addon_root"
if [ "$variant" = godot-js ]; then binary_root="$repo_root/third/godot-js/addons/godot-js"; fi
required_binaries=(
	"binary/windows/x64/libgode_runtime.dll"
	"binary/windows/x64/node.dll"
	"binary/windows/x64/gode_node.exe"
	"binary/linux/x64/libgode_runtime.so"
	"binary/linux/x64/gode_node"
	"binary/macos/arm64/libgode_runtime.dylib"
	"binary/macos/arm64/gode_node"
	"binary/android/arm64/libgode_runtime.so"
	"binary/ios/arm64/libgode_runtime.dylib"
	"binary/editor/windows/x64/libgode_editor.dll"
	"binary/editor/linux/x64/libgode_editor.so"
	"binary/editor/macos/arm64/libgode_editor.dylib"
)

for file in "${required_files[@]}"; do
	if [ ! -f "$addon_root/$file" ]; then
		printf 'Missing plugin file: %s\n' "$file" >&2
		exit 1
	fi
done

if [ "$variant" = godot-js ]; then
    lite_binaries=()
    for file in "${required_binaries[@]}"; do
        case "$file" in */gode_node|*/gode_node.exe|*/node.dll) ;; *) lite_binaries+=("$file") ;; esac
    done
    required_binaries=("${lite_binaries[@]}"
        "binary/web/wasm32/libgode_runtime.wasm"
        "binary/editor/web/godot.web.template_release.wasm32.dlink.zip")
fi

if [ "$skip_binary_validation" -eq 0 ]; then
	for file in "${required_binaries[@]}"; do
		if [ ! -f "$binary_root/$file" ]; then
			printf 'Missing built plugin binary: %s\n' "$file" >&2
			exit 1
		fi
	done
fi

rm -rf "$staging_root"
mkdir -p "$staged_addon_root" "$output_directory"
cp -R "$addon_root"/. "$staged_addon_root"/
if [ "$variant" = godot-js ]; then
    rm -rf "$staged_addon_root/binary"
    cp -R "$binary_root/binary" "$staged_addon_root/binary"
    python3 "$script_dir/sync-godot-js.py" --output "$staged_addon_root"
    rm -f "$staged_addon_root/gode.gd" "$staged_addon_root/gode.gd.uid"
    find "$staged_addon_root/binary" -type f \( -name gode_node -o -name gode_node.exe -o -name node.dll \) -delete
fi
find "$staged_addon_root" -type f -name '*.import' -delete

"$script_dir/prepare-typescript.sh" --output-directory "$staged_addon_root/tsc"

for file in "tsc/package.json" "tsc/lib/typescript.js"; do
	if [ ! -f "$staged_addon_root/$file" ]; then
		printf 'Missing packaged TypeScript compiler file: %s\n' "$file" >&2
		exit 1
	fi
done

for file in "binary/linux/x64/gode_node" "binary/macos/arm64/gode_node"; do
	if [ -f "$staged_addon_root/$file" ]; then
		chmod +x "$staged_addon_root/$file"
	fi
done

find "$staged_addon_root/binary" -type f \( -name '*.lib' -o -name '*.exp' -o -name '*.pdb' -o -name '*.ilk' \) -delete
find "$staged_addon_root/binary" -type f \( -name 'libgode.dll' -o -name 'libgode.so' -o -name 'libgode.dylib' \) -delete

rm -f "$archive_path"
(
	cd "$staging_root"
	zip -qr "$archive_path" "$variant"
)

printf 'Packaged plugin:\n  %s\n' "$archive_path"
