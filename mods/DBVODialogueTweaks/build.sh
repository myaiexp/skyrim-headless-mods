#!/usr/bin/env bash
# Rebuild DBVO's dialoguemenu.swf with the edited DialogueMenu.as (AS2) via ffdec.
#   ./build.sh            import src/ into a copy of stock/ -> build/Interface/dialoguemenu.swf
#   ./build.sh --install  also copy the rebuilt swf over the live game's Interface/dialoguemenu.swf
#
# Only src/__Packages/DialogueMenu.as is authored; ffdec leaves every other class in the
# swf untouched. Reproducible without /tmp and without the live game (stock swf is vendored).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ffdec lives at a stable home path (22 MB Java tool — externalized like ~/.dotnet / wine prefix,
# not git-vendored). Override with FFDEC=... if installed elsewhere.
FFDEC="${FFDEC:-$HOME/.local/share/ffdec/ffdec.jar}"
if [[ ! -f "$FFDEC" ]]; then
	echo "ERROR: ffdec.jar not found at $FFDEC" >&2
	echo "  Install JPEXS Free Flash Decompiler and either place it there or set FFDEC=/path/to/ffdec.jar" >&2
	echo "  (AUR: jpexs-decompiler, or extract the release zip)." >&2
	exit 1
fi

STOCK="$HERE/stock/dialoguemenu.swf"
STOCK_MD5="b1f70c5806ad94359bb0d780a9069d34"
SRC="$HERE/src"
OUT="$HERE/build/Interface/dialoguemenu.swf"

# Guard against vendoring the wrong baseline (e.g. the +900 experiment).
got="$(md5sum "$STOCK" | cut -d' ' -f1)"
if [[ "$got" != "$STOCK_MD5" ]]; then
	echo "ERROR: stock/dialoguemenu.swf md5 $got != expected $STOCK_MD5 (stock DBVO)." >&2
	exit 1
fi

mkdir -p "$(dirname "$OUT")"
cp "$STOCK" "$OUT"
# </dev/null is required: ffdec with no stdin/args opens its GUI; this keeps it headless.
java -jar "$FFDEC" -importScript "$OUT" "$OUT" "$SRC" </dev/null
echo "built:  $OUT"
echo "md5:    $(md5sum "$OUT" | cut -d' ' -f1)  (stock was $STOCK_MD5)"

if [[ "${1:-}" == "--install" ]]; then
	# shellcheck source=../../tools/env.sh
	source "$HERE/../../tools/env.sh"
	DEST="$GAME_DATA/Interface/dialoguemenu.swf"
	echo "live before install: $(md5sum "$DEST" 2>/dev/null | cut -d' ' -f1 || echo missing)"
	cp -v "$OUT" "$DEST"
	echo "installed -> $DEST"
fi
