#!/usr/bin/env bash
# Build the two Data-shaped stages that `replyonlineend.steps` replays against.
#
#   ~/.cache/skytest-dbvotweaks         the mod as shipped (swf + esp + pex + DLL)
#   ~/.cache/skytest-dbvotweaks-nodll   byte-identical MINUS the DLL — the A/B control
#
#   ./stage-test-profile.sh                       the stock-DBVO menu
#   ./stage-test-profile.sh --variant nordicui    a UI-overhaul compatibility variant, staged as
#                                                 ~/.cache/skytest-dbvotweaks-nordicui{,-nodll}
#
# A variant changes exactly one file — Interface/dialoguemenu.swf — so replaying the same script
# against it tests the ported swf and nothing else (variants/README.md).
#
# They live outside the repo because they carry third-party content (SkyUI, and one Karat
# voice line lifted out of the pack's BSA) that this repo does not redistribute.
#
# The "player's line" is staged as a loose Sound/dbvo/t1.fuz. It has to be a DBVO/-prefixed
# path — that prefix is the mod's own filter, the gate that keeps it off every other sound in
# the game — and it has to be typeable into the console, which rules out the pack's real
# filenames (`heard_any_rumors_lately_.fuz` and friends). Any DBVO line works; a long one is
# chosen so the swf timer can be armed comfortably inside it.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=/dev/null
source "$REPO/tools/env.sh"
FULL="$(dirname "$GAME_DATA")/.profiles/full"
# The newest packaged release, by version sort — never a hardcoded number (1.0.1 was baked in
# here once and would have staged the old build under 1.1.0's name).
DIST="$(ls "$REPO/mods/DBVODialogueTweaks/dist/DBVO Dialogue Tweaks "*.zip 2>/dev/null | sort -V | tail -1 || true)"
STYLE="stock"
if [[ "${1:-}" == "--variant" ]]; then
	STYLE="${2:?--variant needs a variant id (see variants/)}"
fi
SUFFIX=""; [[ "$STYLE" == "stock" ]] || SUFFIX="-$STYLE"
A="$HOME/.cache/skytest-dbvotweaks$SUFFIX"
B="$HOME/.cache/skytest-dbvotweaks$SUFFIX-nodll"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

[ -n "$DIST" ] && [ -f "$DIST" ] || { echo "stage: no packaged release in mods/DBVODialogueTweaks/dist/ (run ./package.sh)" >&2; exit 1; }
echo "stage: using $(basename "$DIST")"
[ -d "$FULL" ]  || { echo "stage: no full profile at $FULL (run skytest init --commit)" >&2; exit 1; }

echo "stage: unpacking the shipped package (menu style: $STYLE)"
unzip -o -q "$DIST" 'core/*' "ui/$STYLE/*" -d "$WORK"
[ -f "$WORK/ui/$STYLE/Interface/dialoguemenu.swf" ] || {
	echo "stage: no menu style '$STYLE' in $(basename "$DIST") — styles:" >&2
	unzip -Z1 "$DIST" 'ui/*/Interface/dialoguemenu.swf' | cut -d/ -f2 | sed 's/^/  /' >&2
	exit 1
}

echo "stage: extracting the LONGEST Karat voice line as Sound/dbvo/t1.fuz"
# Longest, not a named favourite: the replay has to arm the swf's timer while the line is still
# playing, so every millisecond of audio is margin. `--sizes` + `sort -rn` picks it from the
# pack itself, which keeps working if the pack is ever swapped for another.
# Listed to a file, then picked with a single awk pass. `sort -rn | head -1` looks obvious and
# is a trap here: `head` closes the pipe, `sort` takes SIGPIPE, and under `set -o pipefail` that
# fails the assignment and `set -e` kills the script one line after it printed "extracting".
"$DOTNET" run --project "$REPO/tools/BsaExtract" -c Release -- \
  "$FULL/KaratVoice - Skyrim.bsa" --sizes >"$WORK/sizes.txt" 2>/dev/null
LONGEST="$(awk -F'\t' '$1+0>m {m=$1+0; p=$2} END {print p}' "$WORK/sizes.txt")"
[ -n "$LONGEST" ] || { echo "stage: could not list the voice pack BSA" >&2; exit 1; }
echo "  longest line: $LONGEST"
"$DOTNET" run --project "$REPO/tools/BsaExtract" -c Release -- \
  "$FULL/KaratVoice - Skyrim.bsa" "$(basename "$LONGEST" .fuz)" "$WORK/t1.fuz" >/dev/null
[ -s "$WORK/t1.fuz" ] || { echo "stage: voice line not extracted" >&2; exit 1; }

# Deliberately NO SkyUI. It is a requirement of the mod's MCM, not of the reply-timing
# mechanism under test, and in an otherwise-vanilla profile it opens a modal on load ("SKYUI
# ERROR CODE 4 — Your Papyrus INI settings are invalid") that swallows the activation key and
# stalls the script before it reaches a conversation. The MCM's only job here would be to push
# dbvoPadMs into the swf, which the script does itself with a probe ui-set.
for T in "$A" "$B"; do
  rm -rf "$T"; mkdir -p "$T/Sound/dbvo"
  cp -a "$WORK/core/." "$T/"
  cp -a "$WORK/ui/$STYLE/." "$T/"
  cp -a "$WORK/t1.fuz" "$T/Sound/dbvo/t1.fuz"
done
rm -f "$B/SKSE/Plugins/DBVODialogueTweaks.dll"

echo "stage: built"
diff <(cd "$A" && find . -type f | sort) <(cd "$B" && find . -type f | sort) \
  && echo "  WARNING: the two stages are identical — the control is not a control" >&2
echo "  test:    $A"
echo "  control: $B   (differs by exactly SKSE/Plugins/DBVODialogueTweaks.dll)"
