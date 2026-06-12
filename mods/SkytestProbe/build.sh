#!/usr/bin/env bash
# Build SkytestProbe.dll headlessly (Linux -> Windows) with clang-cl + xwin.
#   ./build.sh            configure + build into build/
#                         (skytest reads SkytestProbe.dll straight from build/)
#   ./build.sh --install  also copy the DLL into the live game's SKSE/Plugins
#                         (full-profile manual install, for DBVO-style cases)
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

DLL="$BUILD_DIR/SkytestProbe.dll"
echo "built: $DLL"
file "$DLL"

for arg in "$@"; do
	case "$arg" in
	--install)
		# shellcheck source=../../tools/env.sh
		source "$REPO_ROOT/tools/env.sh"
		DEST="$GAME_DATA/SKSE/Plugins"
		mkdir -p "$DEST"
		cp -v "$DLL" "$DEST/SkytestProbe.dll"
		cp -v "$HERE/SkytestProbe.ini" "$DEST/SkytestProbe.ini"
		echo "installed -> $DEST/SkytestProbe.dll (+ ini)"
		;;
	esac
done
