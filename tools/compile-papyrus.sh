#!/usr/bin/env bash
# Compile one Papyrus script to .pex using the in-repo compiler + source trees, via wine.
# Fully self-contained: needs only `wine` on the host (the compiler + vanilla/SKSE sources
# live in this repo).
#
# usage: compile-papyrus.sh <ScriptName> <src-dir-containing-the-psc> <out-dir>
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO_ROOT/tools/env.sh"

SCRIPT="$1"; SRC_DIR="$2"; OUT_DIR="$3"
mkdir -p "$OUT_DIR"

COMPILER_DIR="$REPO_ROOT/tools/papyrus-compiler"
SRCROOT="$REPO_ROOT/tools/papyrus-sources"

# wine maps the unix root to drive Z:, with backslash separators.
winpath() { printf 'Z:%s' "$(printf '%s' "$1" | sed 's|/|\\|g')"; }

export WINEPREFIX="$WINEPREFIX_PAPYRUS" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree=b"
[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1

# Import order matters: the mod's own src first, then SKSE (its versions win for SKSE
# functions), then the vanilla base tree (fills the rest of the type graph), then the
# folder holding TESV_Papyrus_Flags.flg.
cd "$COMPILER_DIR"
wine PapyrusCompiler.exe "$SCRIPT" \
  -f=TESV_Papyrus_Flags.flg \
  -i="$(winpath "$SRC_DIR");$(winpath "$SRCROOT/skse");$(winpath "$SRCROOT/vanilla");$(winpath "$SRCROOT")" \
  -o="$(winpath "$OUT_DIR")" 2>&1 | grep -viE 'pci id|libEGL|gmisc-win32|assertion' || true

[ -f "$OUT_DIR/$SCRIPT.pex" ] || { echo "ERROR: $SCRIPT.pex was not produced" >&2; exit 1; }
