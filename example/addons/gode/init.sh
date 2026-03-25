#!/usr/bin/env bash
# init.sh — Initialize a Godot project with gode TypeScript support
# Usage: bash init.sh [project_dir]
#   project_dir defaults to current directory

set -e
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${1:-$(pwd)}"

echo "Initializing gode project in: $PROJECT_DIR"

ADDON_SRC="$SCRIPT_DIR"
ADDON_DST="$PROJECT_DIR/addons/gode"

if [ "$(realpath "$ADDON_SRC")" = "$(realpath "$ADDON_DST" 2>/dev/null || echo "")" ]; then
  echo "Already running inside target project, skipping addon copy"
else
  if [ -f "$ADDON_SRC/bin/Release/libgode.dll" ] || [ -f "$ADDON_SRC/bin/Release/libgode.so" ]; then
    BUILD_CONFIG="Release"
  elif [ -f "$ADDON_SRC/bin/Debug/libgode.dll" ] || [ -f "$ADDON_SRC/bin/Debug/libgode.so" ]; then
    BUILD_CONFIG="Debug"
  else
    echo "ERROR: No built binaries found in $ADDON_SRC/bin/."
    exit 1
  fi

  mkdir -p "$ADDON_DST/bin/$BUILD_CONFIG" "$ADDON_DST/core" "$ADDON_DST/script"

  cp "$ADDON_SRC/bin/$BUILD_CONFIG/"*  "$ADDON_DST/bin/$BUILD_CONFIG/"
  cp "$ADDON_SRC/plugin.cfg"           "$ADDON_DST/"
  cp "$ADDON_SRC/gode.gd"             "$ADDON_DST/"
  [ -f "$ADDON_SRC/gode.gd.uid" ] && cp "$ADDON_SRC/gode.gd.uid" "$ADDON_DST/"
  cp "$ADDON_SRC/init.sh"             "$ADDON_DST/"

  cp "$ADDON_SRC/script/"*.gd "$ADDON_DST/script/"
  for uid in "$ADDON_SRC/script/"*.gd.uid; do
    [ -f "$uid" ] && cp "$uid" "$ADDON_DST/script/"
  done

  cp "$ADDON_SRC/core/"*.d.ts "$ADDON_DST/core/" 2>/dev/null || true

  echo "Copied addons/gode ($BUILD_CONFIG)"
fi

TSCONFIG="$PROJECT_DIR/tsconfig.json"
if [ -f "$TSCONFIG" ]; then
  echo "tsconfig.json already exists, skipping"
else
  cat > "$TSCONFIG" << 'EOF'
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "strict": false,
    "noCheck": false,
    "outDir": "dist",
    "rootDir": ".",
    "esModuleInterop": true,
    "skipLibCheck": true,
    "jsx": "react"
  },
  "include": ["**/*.ts", "**/*.tsx"],
  "exclude": ["node_modules", "dist"]
}
EOF
  echo "Created tsconfig.json"
fi

PKGJSON="$PROJECT_DIR/package.json"
if [ -f "$PKGJSON" ]; then
  echo "package.json already exists, skipping"
else
  PKG_NAME="$(basename "$PROJECT_DIR")"
  cat > "$PKGJSON" << EOF
{
  "name": "$PKG_NAME",
  "version": "1.0.0",
  "type": "module",
  "scripts": {
    "build": "tsc && tsc-alias",
    "watch": "tsc --watch & tsc-alias --watch"
  },
  "dependencies": {
    "typescript": "^5.9.3"
  }
}
EOF
  echo "Created package.json"
fi

echo "Installing npm dependencies..."
cd "$PROJECT_DIR"
npm install
echo "npm install done"

GITIGNORE="$PROJECT_DIR/.gitignore"
if ! grep -q "node_modules" "$GITIGNORE" 2>/dev/null; then
  printf "\nnode_modules/\ndist/\n" >> "$GITIGNORE"
  echo "Updated .gitignore"
fi

echo ""
echo "Done! Project initialized."
echo "  Build scripts: npm run build / npm run watch"
echo "  Enable the gode plugin in: Project > Project Settings > Plugins"
