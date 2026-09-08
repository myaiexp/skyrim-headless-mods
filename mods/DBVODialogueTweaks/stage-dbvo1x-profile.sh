#!/usr/bin/env bash
# Stage the REAL DBVO 1.x chain, so the mod can be tested end-to-end instead of with the two
# Papyrus stimuli synthesised (which is all `stage-test-profile.sh` + `replyonlineend.steps` can do).
#
#   ./stage-dbvo1x-profile.sh                     stock-DBVO menu
#   ./stage-dbvo1x-profile.sh --variant nordicui  a compatibility variant's menu
#
#   ~/.cache/skytest-dbvo1x[-<variant>]         DBVO 1.x + voice pack + our mod
#   ~/.cache/skytest-dbvo1x[-<variant>]-nodll   the same, minus our DLL — the A/B control
#
# What this proves that the other stage cannot: DBVO's own Papyrus receiving `PlayDBVOTopic`,
# looking the line up through JContainers, speaking it through ConsoleUtil, and arming our swf via
# `UI.InvokeString(… startTopicClickedTimer …)`. That chain is exactly what SKSE 2.3.1 refused on
# 1.7.104 until ConsoleUtilSSE NG 1.6.1 and JContainers SE 4.3.2 shipped — this is how we find out
# whether the pair revives it (docs/ideas.md, and the hedge in docs/dbvo-page.bbcode).
#
# The two updated DLLs come from their Nexus archives in ~/Downloads (override with CONSOLEUTIL_ZIP=
# / JCONTAINERS_7Z=); everything else is lifted out of the live full profile. NOTHING here writes to
# the live install: the staged DBVO settings are a copy, with the voice pack switched on (the live
# one has "enabled": 0), so the game under test speaks without anyone touching your own config.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
# shellcheck source=/dev/null
source "$REPO/tools/env.sh"
FULL="$(dirname "$GAME_DATA")/.profiles/full"

STYLE="stock"
if [[ "${1:-}" == "--variant" ]]; then STYLE="${2:?--variant needs a variant id (see variants/)}"; fi
SUFFIX=""; [[ "$STYLE" == "stock" ]] || SUFFIX="-$STYLE"
T="$HOME/.cache/skytest-dbvo1x$SUFFIX"
B="$T-nodll"

DL="${DOWNLOADS:-$HOME/Downloads}"
CONSOLEUTIL_ZIP="${CONSOLEUTIL_ZIP:-$(ls -t "$DL"/ConsoleUtilSSE*.zip 2>/dev/null | head -1 || true)}"
JCONTAINERS_7Z="${JCONTAINERS_7Z:-$(ls -t "$DL"/JContainers*.7z 2>/dev/null | head -1 || true)}"
# Newest packaged release by version sort — never a hardcoded number.
DIST="$(ls "$HERE/dist/DBVO Dialogue Tweaks "*.zip 2>/dev/null | sort -V | tail -1 || true)"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

need() { [ -e "$1" ] || { echo "stage: missing $2: $1" >&2; exit 1; }; }
need "$FULL" "full profile (run skytest init --commit)"
need "$DIST" "packaged release (run ./package.sh)"
[ -n "$CONSOLEUTIL_ZIP" ] || { echo "stage: no ConsoleUtilSSE*.zip in $DL — download ConsoleUtilSSE NG 1.6.1+ from Nexus 76649" >&2; exit 1; }
[ -n "$JCONTAINERS_7Z" ]  || { echo "stage: no JContainers*.7z in $DL — download JContainers SE 4.3.2+ from Nexus 16495" >&2; exit 1; }
for f in DBVO.esp "Scripts/DBVO_Script_MCM.pex" DragonbornVoiceOver \
         "KaratVoice - Skyrim.esp" "KaratVoice - Skyrim.bsa" SkyUI_SE.esp SkyUI_SE.bsa; do
	need "$FULL/$f" "DBVO 1.x chain component in the full profile"
done

echo "stage: menu style   $STYLE"
echo "stage: release      $(basename "$DIST")"
echo "stage: ConsoleUtil  $(basename "$CONSOLEUTIL_ZIP")"
echo "stage: JContainers  $(basename "$JCONTAINERS_7Z")"

rm -rf "$T"; mkdir -p "$T/SKSE/Plugins" "$T/Scripts"

# --- our mod (core + the chosen menu style) ---
unzip -o -q "$DIST" 'core/*' "ui/$STYLE/*" -d "$WORK"
[ -f "$WORK/ui/$STYLE/Interface/dialoguemenu.swf" ] || { echo "stage: no menu style '$STYLE' in $(basename "$DIST")" >&2; exit 1; }
cp -aL "$WORK/core/." "$T/"
cp -aL "$WORK/ui/$STYLE/." "$T/"

# --- DBVO 1.1.1 itself + the Karat voice pack + SkyUI (DBVO's MCM extends SKI_ConfigBase, so
#     without SkyUI's scripts the DBVO quest script cannot even be instantiated) ---
cp -aL "$FULL/DBVO.esp" "$T/"
cp -aL "$FULL/Scripts/DBVO_Script_MCM.pex" "$T/Scripts/"
cp -aL "$FULL/DragonbornVoiceOver" "$T/"
cp -aL "$FULL/KaratVoice - Skyrim.esp" "$FULL/KaratVoice - Skyrim.bsa" "$T/"
cp -aL "$FULL/SkyUI_SE.esp" "$FULL/SkyUI_SE.bsa" "$T/"

# --- the two dependencies SKSE refused on 1.7.104, in their updated builds ---
unzip -o -q "$CONSOLEUTIL_ZIP" -d "$WORK/cu"
cp -aL "$WORK/cu/SKSE/Plugins/ConsoleUtilSSE.dll" "$T/SKSE/Plugins/"
cp -aL "$WORK/cu/Scripts/ConsoleUtil.pex" "$T/Scripts/"
7z x -o"$WORK/jc" -y "$JCONTAINERS_7Z" >/dev/null
cp -aL "$WORK/jc/Data/SKSE/Plugins/JContainers64.dll" "$T/SKSE/Plugins/"
cp -aL "$WORK/jc/Data/scripts/"*.pex "$T/Scripts/"
# JCData is not optional: without SKSE/Plugins/JCData/Domains/ JContainers throws a boost
# filesystem error out of "Registering functions" and the game dies during boot, with nothing in
# skse64.log to say why (it loaded "correctly" — the throw comes later). Cost one dead session.
cp -aL "$WORK/jc/Data/SKSE/Plugins/JCData" "$T/SKSE/Plugins/"
# `.force-install` is an empty marker some mod managers strip; the Domains dir must exist, so make
# sure the copy really carries it.
[ -d "$T/SKSE/Plugins/JCData/Domains" ] || mkdir -p "$T/SKSE/Plugins/JCData/Domains"

# --- switch the voice pack ON in the STAGED settings (the live copy says "enabled": 0 and is
#     never touched). Without this DBVO reports pack "off" and the swf fires the reply itself,
#     which looks exactly like the failure this test is trying to measure. ---
jq '.enabled = 1' "$FULL/DragonbornVoiceOver/settings/selected_voice_pack.json" \
	> "$T/DragonbornVoiceOver/settings/selected_voice_pack.json"

# --- the A/B control: identical, minus our DLL ---
rm -rf "$B"; cp -a "$T" "$B"
rm -f "$B/SKSE/Plugins/DBVODialogueTweaks.dll"

echo "stage: built"
echo "  voice pack: $(jq -r '"\(.name) (\(.id)), enabled=\(.enabled)"' "$T/DragonbornVoiceOver/settings/selected_voice_pack.json")"
echo "  plugins:    $(cd "$T" && ls SKSE/Plugins/*.dll | xargs -n1 basename | tr '\n' ' ')"
echo "  test:       $T"
echo "  control:    $B   (differs by exactly SKSE/Plugins/DBVODialogueTweaks.dll)"
