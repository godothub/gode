#!/usr/bin/env bash
set -euo pipefail

typescript_version="6.0.3"
typescript_url="https://github.com/microsoft/TypeScript/releases/download/v6.0.3/typescript-6.0.3.tgz"
typescript_sha256="33cd0ee1beaa8c9e9d15a9da836c62ddea4c34a42d7c2d349dbc80d94165d22a"
output_directory=""
force=0

usage() {
	printf 'Usage: %s [--output-directory DIR] [--url URL] [--sha256 SHA256] [--version VERSION] [--force]\n' "$0"
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--output-directory)
			output_directory="${2:?missing value for --output-directory}"
			shift 2
			;;
		--url)
			typescript_url="${2:?missing value for --url}"
			shift 2
			;;
		--sha256)
			typescript_sha256="${2:?missing value for --sha256}"
			shift 2
			;;
		--version)
			typescript_version="${2:?missing value for --version}"
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

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

if [ -z "$output_directory" ]; then
	output_directory="$repo_root/example/addons/gode/tsc"
fi

if [ "$force" -eq 0 ] && [ -f "$output_directory/package.json" ] && [ -f "$output_directory/lib/typescript.js" ]; then
	if grep -q "\"version\": \"$typescript_version\"" "$output_directory/package.json"; then
		printf 'TypeScript %s already prepared at %s\n' "$typescript_version" "$output_directory"
		exit 0
	fi
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
archive="$tmp_dir/typescript.tgz"

printf 'Downloading TypeScript %s...\n' "$typescript_version"
curl -L --fail --silent --show-error -o "$archive" "$typescript_url"

if command -v sha256sum >/dev/null 2>&1; then
	actual_sha256="$(sha256sum "$archive" | awk '{print $1}')"
else
	actual_sha256="$(shasum -a 256 "$archive" | awk '{print $1}')"
fi

if [ "$actual_sha256" != "$typescript_sha256" ]; then
	printf 'TypeScript archive checksum mismatch.\nexpected: %s\nactual:   %s\n' "$typescript_sha256" "$actual_sha256" >&2
	exit 1
fi

rm -rf "$output_directory"
mkdir -p "$output_directory"
tar -xzf "$archive" -C "$output_directory" --strip-components=1

if [ ! -f "$output_directory/lib/typescript.js" ]; then
	printf 'TypeScript compiler was not extracted correctly: %s\n' "$output_directory/lib/typescript.js" >&2
	exit 1
fi

printf 'Prepared TypeScript %s at %s\n' "$typescript_version" "$output_directory"
