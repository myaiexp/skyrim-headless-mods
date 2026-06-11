#!/usr/bin/env bash
# xwin splats the Windows SDK import libs under the exact casings found in MS's
# manifest (e.g. advapi32.lib, AdvAPI32.Lib, ADVAPI32.lib) plus an UPPERCASE
# variant. CommonLibSSE-NG (and other MSVC projects) reference them in PascalCase
# with a lowercase extension — Advapi32.lib, Dbghelp.lib, Ole32.lib, Version.lib.
# On case-sensitive Linux, lld-link needs an exact match, so add the missing
# "capitalize-first-letter + .lib" symlink for every lib. Idempotent; stamped.
set -euo pipefail

XWIN_SDK="${XWIN_SDK:-$HOME/.local/xwin-sdk}"
STAMP="$XWIN_SDK/.case-symlinks-done"
[ -f "$STAMP" ] && exit 0

shopt -s nullglob
for dir in \
	"$XWIN_SDK/sdk/lib/um/x86_64" \
	"$XWIN_SDK/sdk/lib/ucrt/x86_64" \
	"$XWIN_SDK/crt/lib/x86_64"; do
	[ -d "$dir" ] || continue
	for f in "$dir"/*.lib; do
		base="$(basename "$f")"
		stem="${base%.lib}"
		cap="$(printf '%s' "${stem:0:1}" | tr '[:lower:]' '[:upper:]')${stem:1}.lib"
		[ -e "$dir/$cap" ] || ln -s "$base" "$dir/$cap"
	done
done

touch "$STAMP"
echo "setup-sdk-symlinks: created PascalCase .lib symlinks in $XWIN_SDK"
