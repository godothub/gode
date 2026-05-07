#!/usr/bin/env bash
set -euo pipefail

url=""
force=0

usage() {
	printf 'Usage: %s --url URL [--force]\n' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--url)
			url="${2:?missing value for --url}"
			shift 2
			;;
		--force)
			force=1
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

if [ -z "$url" ]; then
	printf 'Missing required argument: --url\n' >&2
	usage >&2
	exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
libnode_dir="$repo_root/libnode"
build_deps_dir="$repo_root/build/deps"
archive_path="$build_deps_dir/libnode.zip"
extract_dir="$build_deps_dir/libnode-extract"

required_files=(
	"include/node.h"
	"windows/x64/libnode.lib"
	"linux/x64/libnode.a"
	"macos/arm64/libnode.a"
	"android/arm64/libnode.a"
	"ios/arm64/libnode.a"
)

test_libnode_layout() {
	local dir="$1"
	local file
	for file in "${required_files[@]}"; do
		if [ ! -f "$dir/$file" ]; then
			return 1
		fi
	done
}

if [ "$force" -eq 0 ] && test_libnode_layout "$libnode_dir"; then
	printf 'libnode is ready:\n  %s\n' "$libnode_dir"
	exit 0
fi

mkdir -p "$build_deps_dir"

printf 'Downloading libnode...\n'
curl --fail --location --show-error "$url" --output "$archive_path"

rm -rf "$extract_dir"
mkdir -p "$extract_dir"

printf 'Extracting libnode...\n'
python - "$archive_path" "$extract_dir" <<'PY'
import sys
import zipfile

archive_path, extract_dir = sys.argv[1:3]
with zipfile.ZipFile(archive_path) as archive:
    archive.extractall(extract_dir)
PY

source_dir=""
for candidate in "$extract_dir/libnode" "$extract_dir"; do
	if test_libnode_layout "$candidate"; then
		source_dir="$candidate"
		break
	fi
done

if [ -z "$source_dir" ]; then
	printf 'The downloaded archive does not contain the expected libnode layout.\n' >&2
	exit 1
fi

if [ -e "$libnode_dir" ]; then
	if [ "$force" -eq 0 ]; then
		printf 'Existing libnode directory is incomplete. Pass --force to replace it: %s\n' "$libnode_dir" >&2
		exit 1
	fi
	rm -rf "$libnode_dir"
fi

mkdir -p "$libnode_dir"
cp -R "$source_dir"/. "$libnode_dir"/

if ! test_libnode_layout "$libnode_dir"; then
	printf 'libnode was extracted, but required files are still missing.\n' >&2
	exit 1
fi

printf 'libnode is ready:\n  %s\n' "$libnode_dir"
