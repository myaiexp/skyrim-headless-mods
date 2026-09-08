#!/usr/bin/env bash
# Port this mod's swf deltas onto a UI overhaul's DBVO-patched dialoguemenu.swf.
#
#   ./variants/port.sh              re-port every variant
#   ./variants/port.sh untarnished  re-port one
#   ./variants/port.sh --verify     check the committed ports carry every delta (no re-merge)
#
# Writes variants/<id>/src/__Packages/DialogueMenu.as — a GENERATED file that is committed, so
# the build never depends on this script and a port is reviewable as a diff.
#
# How, and why this shape: our deltas are a handful of hunks against stock DBVO's script, and
# every third-party patch is the SAME DBVO hunks applied to a different UI mod's DialogueMenu
# class. So the port is a three-way merge — base = stock DBVO's decompiled script, ours = src/,
# theirs = the variant's decompiled script — and `git merge-file` does it. The deltas therefore
# live in exactly one place (src/__Packages/DialogueMenu.as); this script never restates them,
# which is what keeps a variant from silently missing a feature the stock swf grew later.
#
# A conflict is expected work, not a bug: resolve it in the .conflict file this script leaves,
# save it over the generated .as, and re-run with --verify.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MOD="$(cd "$HERE/.." && pwd)"
# shellcheck source=lib.sh
source "$HERE/lib.sh"

STOCK="$MOD/stock/dialoguemenu.swf"
OURS="$MOD/src/__Packages/DialogueMenu.as"

verify_only=0
if [[ "${1:-}" == "--verify" ]]; then verify_only=1; shift; fi

ids=("$@")
(( ${#ids[@]} )) || mapfile -t ids < <(variant_ids)

if (( verify_only )); then
	failed=0
	for id in "${ids[@]}"; do
		as="$HERE/$id/src/__Packages/DialogueMenu.as"
		[[ -f "$as" ]] || { echo "ERROR: $id has no port at $as" >&2; failed=1; continue; }
		if check_markers "$as" "$id port" && check_offbranch "$as" "$id port"; then
			echo ">> $id: ok ($(wc -l <"$as") lines)"
		else
			failed=1
		fi
	done
	exit "$failed"
fi

require_ffdec
[[ -f "$STOCK" ]] || { echo "ERROR: no stock swf at $STOCK" >&2; exit 1; }
check_markers "$OURS" "src/__Packages/DialogueMenu.as"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

echo ">> decompiling the stock DBVO baseline (the merge base)"
BASE_AS="$(swf_dialoguemenu_as "$STOCK" "$WORK/stock")"

failed=0
for id in "${ids[@]}"; do
	[[ -f "$HERE/$id/variant.conf" ]] || { echo "ERROR: no such variant: $id" >&2; failed=1; continue; }
	echo ">> $id — $(variant_get "$id" name)"
	base_swf="$(variant_base "$id")" || { failed=1; continue; }
	theirs="$(swf_dialoguemenu_as "$base_swf" "$WORK/$id")" || { failed=1; continue; }
	merged="$WORK/$id.merged.as"

	# `git merge-file <current> <base> <other>`: replay base->other (stock DBVO -> ours) onto
	# current (their patched script). -p prints instead of editing in place; a non-zero rc is
	# the conflict count.
	conflicts=0
	git merge-file -p \
		-L "$id (their DBVO patch)" -L "stock DBVO" -L "ours (src/)" \
		"$theirs" "$BASE_AS" "$OURS" >"$merged" || conflicts=$?

	# The fixup runs BEFORE the conflict bail on purpose: it touches a region no conflict has
	# ever landed in, and running it first means the .conflict file a human resolves is already
	# fixed up — so the resolution is only ever about the real conflict.
	# Fixup, not a delta: every third-party patch dropped stock DBVO's `timerBool = false` from
	# the `voicePackID == "off"` branch (the no-voice path). Left out, timerBool stays true after
	# a topic click with the voice off, DoShowDialogueList then does nothing, and the topic list
	# never comes back. It is missing identically in all four bases, so restoring it belongs here
	# rather than in four hand-edits that the next re-port would drop.
	rc=0
	awk '
		/if\(voicePackID == "off"\)/ { in_off = 1; have = 0 }
		in_off && /this\.timerBool = false;/ { have = 1 }
		in_off && /gfx\.io\.GameDelegate\.call\("TopicClicked"/ {
			if (!have) { print "         this.timerBool = false;"; touched = 1 }
			in_off = 0
		}
		{ print }
		END { exit touched ? 9 : 0 }
	' "$merged" >"$merged.fixed" || rc=$?
	case "$rc" in
		0) ;;
		9) echo "   fixup: restored timerBool=false in the voicePackID=='off' branch" ;;
		*) echo "   ERROR: fixup pass failed (rc=$rc)" >&2; failed=1; continue ;;
	esac
	# ffdec exports CRLF; the fixup line above is inserted LF. Normalize so a generated file is
	# never mixed (and so its diff is line-ending noise free).
	awk '{ sub(/\r?$/, "\r"); print }' "$merged.fixed" >"$merged" && rm -f "$merged.fixed"

	if (( conflicts != 0 )); then
		out="$HERE/$id/src/__Packages/DialogueMenu.as.conflict"
		mkdir -p "$(dirname "$out")"
		cp "$merged" "$out"
		echo "   $conflicts conflict(s) — resolve $(realpath --relative-to="$MOD" "$out"), save it as DialogueMenu.as, then ./variants/port.sh --verify $id" >&2
		failed=1
		continue
	fi

	check_markers "$merged" "$id port" || { failed=1; continue; }
	check_offbranch "$merged" "$id port" || { failed=1; continue; }

	dst="$HERE/$id/src/__Packages/DialogueMenu.as"
	mkdir -p "$(dirname "$dst")"
	cp "$merged" "$dst"
	rm -f "$dst.conflict"
	echo "   wrote $(realpath --relative-to="$MOD" "$dst") ($(wc -l <"$dst") lines)"
done

(( failed == 0 )) || { echo ">> some variants did not port cleanly." >&2; exit 1; }
echo ">> ported: ${ids[*]}"
