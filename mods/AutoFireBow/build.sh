#!/usr/bin/env bash
# Build AutoFireBow.dll headlessly (Linux -> Windows) with clang-cl + xwin.
#   ./build.sh            configure + build into build/
#   ./build.sh --install  also copy the DLL into the live game's SKSE/Plugins
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
SKSE_DIR="$REPO_ROOT/tools/skse"
BUILD_DIR="$HERE/build"

# shellcheck source=../../tools/skse/cross-env.sh
source "$SKSE_DIR/cross-env.sh"

cmake -S "$HERE" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$SKSE_DIR/cmake/clang-cl-msvc.cmake"

cmake --build "$BUILD_DIR"

DLL="$BUILD_DIR/AutoFireBow.dll"
echo "built: $DLL"
file "$DLL"

if [[ "${1:-}" == "--install" ]]; then
	# shellcheck source=../../tools/env.sh
	source "$REPO_ROOT/tools/env.sh"
	DEST="$GAME_DATA/SKSE/Plugins"
	mkdir -p "$DEST"
	cp -v "$DLL" "$DEST/AutoFireBow.dll"
	echo "installed -> $DEST/AutoFireBow.dll"
fi
