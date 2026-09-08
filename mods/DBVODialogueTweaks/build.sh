#!/usr/bin/env bash
# Build DBVODialogueTweaks v5 headlessly into build/, five artifacts:
#   build/Interface/dialoguemenu.swf            (ffdec — reads dbvoPadMs as the post-line-end gap;
#                                                the DLL fires dbvoOnPlayerLineEnded on the real end)
#   build/Scripts/DBVODialogueTweaksMCM.pex     (wine PapyrusCompiler — SkyUI MCM)
#   build/Scripts/DBVOTweaks.pex                (wine PapyrusCompiler — global-native bridge)
#   build/DBVODialogueTweaks.esp                (Mutagen/EspGen — quest + player alias)
#   plugin/build/DBVODialogueTweaks.dll         (clang-cl + xwin — SKSE plugin, tools/skse toolchain)
# …plus one swf per UI-overhaul compatibility variant (variants/README.md):
#   build/variants/<id>/Interface/dialoguemenu.swf
#
#   ./build.sh                    build all five + every variant whose base swf is present
#   ./build.sh --install          also copy them into the live game (Data/ + SKSE/Plugins/) + activate the esp
#   ./build.sh --install <id>     …installing that variant's swf instead of the stock-DBVO one
#
# Only src/__Packages/DialogueMenu.as is authored on the swf side; ffdec leaves every other
# class untouched. The MCM menu is authored in DBVODialogueTweaksMCM.psc (no config.json).
# We compile AGAINST the vendored SkyUI sources but ship none of SkyUI's .pex — only our own.
#
# NB: the Papyrus source lives in src/papyrus/, NOT src/scripts/ — ffdec's -importScript
# treats a "scripts/" subfolder of the import root as the ActionScript source dir, so a
# src/scripts/ would hijack the swf import (it'd find no .as there and silently rebuild stock).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

# --- plugin identity (EspGen + Plugins.txt) ---
ESP="DBVODialogueTweaks.esp"
QUEST_EDID="DBVODialogueTweaksMCMQuest"
MCM_SCRIPT="DBVODialogueTweaksMCM"
FULLNAME="DBVO Dialogue Tweaks"
PLAYER_ALIAS="SKI_PlayerLoadGameAlias"

# FFDEC resolution, the compatibility-variant list, and the built-swf checks all live in
# variants/lib.sh (shared with package.sh and variants/port.sh).
# shellcheck source=variants/lib.sh
source "$HERE/variants/lib.sh"
require_ffdec

STOCK="$HERE/stock/dialoguemenu.swf"
STOCK_MD5="b1f70c5806ad94359bb0d780a9069d34"
SRC="$HERE/src"
BUILD="$HERE/build"
OUT="$BUILD/Interface/dialoguemenu.swf"

# --- SKSE C++ plugin (DLL) ---
NATIVE_SCRIPT="DBVOTweaks"            # global-native bridge .psc compiled alongside the MCM
SKSE_DIR="$REPO_ROOT/tools/skse"      # cross-compile toolchain (cross-env.sh + cmake)
PLUGIN_DIR="$HERE/plugin"
PLUGIN_BUILD="$PLUGIN_DIR/build"
DLL="$PLUGIN_BUILD/DBVODialogueTweaks.dll"

mkdir -p "$BUILD/Interface" "$BUILD/Scripts"

# Import a DialogueMenu.as tree into a base swf, then PROVE the import landed: ffdec's
# -importScript exits 0 whether or not it actually replaced the class, and a silently unpatched
# swf behaves exactly like the mod not being installed — the one build failure a user would
# report as "your mod does nothing".
build_swf() { # <base swf> <src dir> <out swf> <label>
	local base="$1" src="$2" out="$3" label="$4" work as rc=0
	mkdir -p "$(dirname "$out")"
	cp "$base" "$out"
	# </dev/null is required: ffdec with no stdin/args opens its GUI; this keeps it headless.
	java -jar "$FFDEC" -importScript "$out" "$out" "$src" </dev/null
	work="$(mktemp -d)"
	as="$(swf_dialoguemenu_as "$out" "$work/check")" || rc=1
	(( rc )) || check_markers "$as" "$label swf" || rc=1
	(( rc )) || check_offbranch "$as" "$label swf" || rc=1
	rm -rf "$work"
	(( rc == 0 )) || return 1
	echo "   $label: $(md5sum "$out" | cut -d' ' -f1)   (base $(md5sum "$base" | cut -d' ' -f1))"
}

# --- [1/5] swf (ffdec) — the stock-DBVO build, plus one per compatibility variant ---
# Guard against vendoring the wrong baseline (e.g. the +900 experiment).
got="$(md5sum "$STOCK" | cut -d' ' -f1)"
if [[ "$got" != "$STOCK_MD5" ]]; then
	echo "ERROR: stock/dialoguemenu.swf md5 $got != expected $STOCK_MD5 (stock DBVO)." >&2
	exit 1
fi
echo ">> [1/5] swf: import src/ into stock/ -> build/Interface/dialoguemenu.swf"
build_swf "$STOCK" "$SRC" "$OUT" "stock"

# Variants whose base swf is absent are SKIPPED, not fatal: the bases are third-party UI-mod
# assets and are git-ignored, so a fresh clone legitimately has none. package.sh is the strict
# one — a release must carry every declared variant.
for id in $(variant_ids); do
	if [[ -f "$VARIANTS_DIR/$id/base.swf" ]]; then
		# Present but wrong (an md5 that isn't what the port was made against) IS fatal.
		base="$(variant_base "$id")"
		build_swf "$base" "$VARIANTS_DIR/$id/src" "$BUILD/variants/$id/Interface/dialoguemenu.swf" "$id"
	else
		echo "   $id: SKIPPED — no variants/$id/base.swf (see variants/README.md)"
	fi
done

# --- [2/5] + [3/5] Papyrus scripts (wine PapyrusCompiler, against vendored SkyUI sources) ---
echo ">> [2/5] compile $MCM_SCRIPT.psc (SkyUI MCM) -> build/Scripts/"
"$REPO_ROOT/tools/compile-papyrus.sh" "$MCM_SCRIPT" "$SRC/papyrus" "$BUILD/Scripts"
echo ">> [3/5] compile $NATIVE_SCRIPT.psc (global-native bridge) -> build/Scripts/"
"$REPO_ROOT/tools/compile-papyrus.sh" "$NATIVE_SCRIPT" "$SRC/papyrus" "$BUILD/Scripts"

# --- [4/5] esp (Mutagen/EspGen — quest hosting the MCM script + a PlayerRef alias) ---
source "$REPO_ROOT/tools/env.sh"
echo ">> [4/5] generate $ESP (Mutagen / EspGen) — quest + $PLAYER_ALIAS player alias"
"$DOTNET" run --project "$REPO_ROOT/tools/EspGen" -- \
	"$BUILD/$ESP" "$QUEST_EDID" "$MCM_SCRIPT" "$FULLNAME" --player-alias "$PLAYER_ALIAS" --esl

# --- [5/5] SKSE plugin DLL (clang-cl + lld-link + xwin cross-build via tools/skse toolchain) ---
echo ">> [5/5] build DBVODialogueTweaks.dll (cross-compile Linux -> Windows)"
# shellcheck source=../../tools/skse/cross-env.sh
source "$SKSE_DIR/cross-env.sh"
cmake -S "$PLUGIN_DIR" -B "$PLUGIN_BUILD" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$SKSE_DIR/cmake/clang-cl-msvc.cmake"
cmake --build "$PLUGIN_BUILD"
file "$DLL"

echo ">> artifacts:"
ls -la "$BUILD/$ESP" "$OUT" "$BUILD/Scripts/$MCM_SCRIPT.pex" "$BUILD/Scripts/$NATIVE_SCRIPT.pex" "$DLL"
[[ -d "$BUILD/variants" ]] && ls -la "$BUILD"/variants/*/Interface/dialoguemenu.swf

if [[ "${1:-}" == "--install" ]]; then
	# `--install <variant>` swaps ONLY the swf: the esp, both .pex and the DLL are identical
	# across variants (every base keeps Bethesda's _root.DialogueMenu_mc path, which is all the
	# DLL and the MCM address).
	SWF_IN="$OUT"
	if [[ -n "${2:-}" ]]; then
		SWF_IN="$BUILD/variants/$2/Interface/dialoguemenu.swf"
		[[ -f "$SWF_IN" ]] || { echo "ERROR: no built swf for variant '$2' ($SWF_IN)" >&2; exit 1; }
		echo ">> installing the '$2' variant swf ($(variant_get "$2" name))"
	fi
	echo ">> installing into live game (Data: $GAME_DATA)"
	mkdir -p "$GAME_DATA/Interface" "$GAME_DATA/Scripts" "$GAME_DATA/SKSE/Plugins"
	# List each copied file + its pre-install md5 so a later manual revert is possible.
	declare -A DEST=(
		["$SWF_IN"]="$GAME_DATA/Interface/dialoguemenu.swf"
		["$BUILD/Scripts/$MCM_SCRIPT.pex"]="$GAME_DATA/Scripts/$MCM_SCRIPT.pex"
		["$BUILD/Scripts/$NATIVE_SCRIPT.pex"]="$GAME_DATA/Scripts/$NATIVE_SCRIPT.pex"
		["$BUILD/$ESP"]="$GAME_DATA/$ESP"
		["$DLL"]="$GAME_DATA/SKSE/Plugins/DBVODialogueTweaks.dll"
	)
	for src in "$SWF_IN" "$BUILD/Scripts/$MCM_SCRIPT.pex" "$BUILD/Scripts/$NATIVE_SCRIPT.pex" "$BUILD/$ESP" "$DLL"; do
		dst="${DEST[$src]}"
		echo "   live before: $(md5sum "$dst" 2>/dev/null | cut -d' ' -f1 || echo missing)  $dst"
		cp -v "$src" "$dst"
	done
	# Activate the esp (leading '*' = enabled in Plugins.txt).
	if grep -q "^\*$ESP$" "$PLUGINS_TXT"; then
		:
	elif grep -q "^$ESP$" "$PLUGINS_TXT"; then
		sed -i "s|^$ESP$|*$ESP|" "$PLUGINS_TXT"
	else
		printf '*%s\n' "$ESP" >> "$PLUGINS_TXT"
	fi
	echo ">> installed + activated. FULLY restart Skyrim (Papyrus VM caches .pex per session)."
fi
