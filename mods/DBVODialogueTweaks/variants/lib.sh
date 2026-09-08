#!/usr/bin/env bash
# Shared helpers for the UI-overhaul compatibility variants (build.sh, package.sh, port.sh).
#
# A "variant" is our swf deltas re-applied on top of a UI overhaul's OWN DBVO-patched
# dialoguemenu.swf, so a user of that overhaul keeps their menu layout and still gets this mod.
# See variants/README.md for the tree, and for how to add a fifth.
#
# Sourced, never executed. Every function prints to stderr and returns non-zero on failure.

VARIANTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ffdec lives at a stable home path (22 MB Java tool — externalized like ~/.dotnet / the wine
# prefix, not git-vendored). Override with FFDEC=... if installed elsewhere.
FFDEC="${FFDEC:-$HOME/.local/share/ffdec/ffdec.jar}"

require_ffdec() {
	[[ -f "$FFDEC" ]] && return 0
	echo "ERROR: ffdec.jar not found at $FFDEC" >&2
	echo "  Install JPEXS Free Flash Decompiler and either place it there or set FFDEC=/path/to/ffdec.jar" >&2
	echo "  (AUR: jpexs-decompiler, or extract the release zip)." >&2
	return 1
}

# Decompile a swf's ActionScript into <outdir>; echo the path of its DialogueMenu.as.
# </dev/null is required: ffdec with no stdin/args opens its GUI; this keeps it headless.
swf_dialoguemenu_as() {
	local swf="$1" outdir="$2"
	rm -rf "$outdir"
	java -jar "$FFDEC" -export script "$outdir" "$swf" </dev/null >/dev/null 2>&1 || {
		echo "ERROR: ffdec could not decompile $swf" >&2
		return 1
	}
	local as
	as="$(find "$outdir" -name DialogueMenu.as -print -quit)"
	[[ -n "$as" ]] || { echo "ERROR: no DialogueMenu class in $swf" >&2; return 1; }
	printf '%s\n' "$as"
}

# Every marker our deltas leave in DialogueMenu.as, as it survives an ffdec decompile
# round-trip (comments and local-variable NAMES do not survive — none are used here).
#
# This is the build's real correctness gate: ffdec's -importScript reports success whether or
# not it actually replaced the class, so a variant whose script failed to import would ship as
# the plain unpatched base swf and behave exactly like "the mod isn't installed".
DBVO_MARKERS=(
	'var skipArmedAt;'
	'var dbvoPadMs;'
	'static var SKIP_DEBOUNCE_MS'
	'this.trySkipPlayerLine();'
	'skse.SendModEvent("CutNpcDBVOReply","");'
	'this.skipArmedAt = getTimer();'
	'function dbvoOnPlayerLineEnded()'
	'function trySkipPlayerLine()'
)

# Fail unless every delta marker is present in a DialogueMenu.as.
check_markers() {
	local as="$1" label="$2" missing=0 m
	for m in "${DBVO_MARKERS[@]}"; do
		grep -qF "$m" "$as" || { echo "   MISSING in $label: $m" >&2; missing=1; }
	done
	(( missing == 0 )) || { echo "ERROR: $label is not carrying our deltas." >&2; return 1; }
	return 0
}

# Fail unless startTopicClickedTimer's `voicePackID == "off"` branch clears timerBool.
# Stock DBVO does; every third-party patch dropped it, and without it a topic click with the
# voice off leaves timerBool true forever, DoShowDialogueList becomes a no-op and the topic list
# never comes back. port.sh restores it — this is the check that it survived into the build.
# A plain grep can't do this: topicClicked() sets the same field.
check_offbranch() {
	local as="$1" label="$2"
	awk '
		/if\(voicePackID == "off"\)/ { in_off = 1 }
		in_off && /this\.timerBool = false;/ { ok = 1 }
		in_off && /gfx\.io\.GameDelegate\.call\("TopicClicked"/ { in_off = 0 }
		END { exit ok ? 0 : 1 }
	' "$as" && return 0
	echo "ERROR: $label: the voicePackID=='off' branch does not clear timerBool." >&2
	return 1
}

# Variant ids, ordered by each variant.conf's `order=` (the FOMOD / install-page order).
# Derived from the directory listing — adding a variant needs no edit anywhere else.
variant_ids() {
	local d id
	for d in "$VARIANTS_DIR"/*/variant.conf; do
		[[ -f "$d" ]] || continue
		id="$(basename "$(dirname "$d")")"
		printf '%s\t%s\n' "$(variant_get "$id" order)" "$id"
	done | sort -n | cut -f2
}

# One `key=value` field out of a variant.conf (empty if unset).
variant_get() {
	local id="$1" key="$2"
	sed -n "s/^${key}=//p" "$VARIANTS_DIR/$id/variant.conf" | head -1
}

# The base swf a variant patches: present, and the exact file we ported against.
# Ignored by git (third-party UI-mod assets) — variants/README.md says where to get each.
variant_base() {
	local id="$1" swf="$VARIANTS_DIR/$1/base.swf" want got
	if [[ ! -f "$swf" ]]; then
		echo "ERROR: missing base swf: $swf" >&2
		echo "  $(variant_get "$id" base_file)" >&2
		echo "  from: $(variant_get "$id" source)" >&2
		return 1
	fi
	want="$(variant_get "$id" base_md5)"
	got="$(md5sum "$swf" | cut -d' ' -f1)"
	if [[ -n "$want" && "$got" != "$want" ]]; then
		echo "ERROR: $id base.swf md5 $got != $want (not the file the port was made against)." >&2
		echo "  Re-port with variants/port.sh $id if you meant to move to a newer base." >&2
		return 1
	fi
	printf '%s\n' "$swf"
}
