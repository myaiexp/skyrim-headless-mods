#!/usr/bin/env bash
# Compile one Papyrus script to .pex using the in-repo compiler + source trees, via wine.
# Needs `wine` on the host plus the locally-populated compiler + vanilla/SKSE source trees
# (both git-ignored third-party IP — see tools/papyrus-{compiler,sources}/README.md).
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

# Import order matters: the mod's own src first, then SkyUI (its MCM base classes,
# compile-time only — runtime .pex comes from the user's SkyUI), then SKSE (its versions
# win for SKSE functions), then the vanilla base tree (fills the rest of the type graph),
# then the folder holding TESV_Papyrus_Flags.flg.
# Delete any prior .pex first so the existence check below is a true freshness gate.
# PapyrusCompiler.exe exits 0 even when compilation fails, and its output is piped through
# grep (whose status we discard) — so without this, a leftover .pex from an earlier build
# would let a failed compile pass silently and ship the stale binary.
rm -f "$OUT_DIR/$SCRIPT.pex"
cd "$COMPILER_DIR"
wine PapyrusCompiler.exe "$SCRIPT" \
  -f=TESV_Papyrus_Flags.flg \
  -i="$(winpath "$SRC_DIR");$(winpath "$SRCROOT/skyui");$(winpath "$SRCROOT/skse");$(winpath "$SRCROOT/vanilla");$(winpath "$SRCROOT")" \
  -o="$(winpath "$OUT_DIR")" 2>&1 | grep -viE 'pci id|libEGL|gmisc-win32|assertion' || true

[ -f "$OUT_DIR/$SCRIPT.pex" ] || { echo "ERROR: $SCRIPT.pex was not produced — compile failed (see output above)" >&2; exit 1; }
