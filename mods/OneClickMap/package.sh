#!/usr/bin/env bash
# Package the built DLL into a mod-manager-installable archive for Nexus.
#
#   ./package.sh            build the zip from build/ into dist/
#
# Run ./build.sh first (this only packages what's already built — it does not compile).
#
# OneClickMap is a single DLL with no install choices, so this ships a PLAIN ZIP: the archive
# root maps to Data/ on install, so the one file sits at SKSE/Plugins/OneClickMap.dll. No FOMOD —
# there is nothing to choose. (If a branded installer page is ever wanted, wrap this in a FOMOD the
# way mods/DBVODialogueTweaks/package.sh does; for one required file it adds nothing functional.)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- release identity (Version tracks src/main.cpp kVersion) ---
NAME="OneClickMap"
VERSION="1.0.0"

DLL="$HERE/build/OneClickMap.dll"

DIST="$HERE/dist"
STAGE="$DIST/$NAME"
ZIP="$DIST/${NAME} ${VERSION}.zip"

# --- preflight: the DLL must exist (else build.sh hasn't run) ---
if [[ ! -f "$DLL" ]]; then
	echo "ERROR: missing artifact: $DLL" >&2
	echo "  Run ./build.sh first." >&2
	exit 1
fi

# --- clean stage, lay the DLL at its Data-relative path ---
rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE/SKSE/Plugins"
cp "$DLL" "$STAGE/SKSE/Plugins/OneClickMap.dll"

# --- zip (archive root = the Data tree) ---
( cd "$STAGE" && zip -rq "$ZIP" SKSE )

echo ">> packaged: $ZIP"
( cd "$STAGE" && find SKSE -type f | sort | sed 's/^/   /' )
echo ">> size: $(du -h "$ZIP" | cut -f1)"
