#!/usr/bin/env bash
# Build DBVODialogueTweaks v3 headlessly into build/, five artifacts:
#   build/Interface/dialoguemenu.swf            (ffdec — reads dbvoMsPerWord/dbvoPadMs)
#   build/Scripts/DBVODialogueTweaksMCM.pex     (wine PapyrusCompiler — SkyUI MCM)
#   build/Scripts/DBVOTweaks.pex                (wine PapyrusCompiler — global-native bridge)
#   build/DBVODialogueTweaks.esp                (Mutagen/EspGen — quest + player alias)
#   plugin/build/DBVODialogueTweaks.dll         (clang-cl + xwin — SKSE plugin, tools/skse toolchain)
#
#   ./build.sh            build all five (Data artifacts into build/, DLL into plugin/build/)
#   ./build.sh --install  also copy them into the live game (Data/ + SKSE/Plugins/) + activate the esp
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
BUILD="$HERE/build"
OUT="$BUILD/Interface/dialoguemenu.swf"

# --- SKSE C++ plugin (DLL) ---
NATIVE_SCRIPT="DBVOTweaks"            # global-native bridge .psc compiled alongside the MCM
SKSE_DIR="$REPO_ROOT/tools/skse"      # cross-compile toolchain (cross-env.sh + cmake)
PLUGIN_DIR="$HERE/plugin"
PLUGIN_BUILD="$PLUGIN_DIR/build"
DLL="$PLUGIN_BUILD/DBVODialogueTweaks.dll"

mkdir -p "$BUILD/Interface" "$BUILD/Scripts"

# --- [1/4] swf (ffdec) ---
# Guard against vendoring the wrong baseline (e.g. the +900 experiment).
got="$(md5sum "$STOCK" | cut -d' ' -f1)"
if [[ "$got" != "$STOCK_MD5" ]]; then
	echo "ERROR: stock/dialoguemenu.swf md5 $got != expected $STOCK_MD5 (stock DBVO)." >&2
	exit 1
fi
echo ">> [1/4] swf: import src/ into stock/ -> build/Interface/dialoguemenu.swf"
cp "$STOCK" "$OUT"
# </dev/null is required: ffdec with no stdin/args opens its GUI; this keeps it headless.
java -jar "$FFDEC" -importScript "$OUT" "$OUT" "$SRC" </dev/null
echo "   md5: $(md5sum "$OUT" | cut -d' ' -f1)  (stock was $STOCK_MD5)"

# --- [2/4] Papyrus scripts (wine PapyrusCompiler, against vendored SkyUI sources) ---
echo ">> [2/4] compile $MCM_SCRIPT.psc + $NATIVE_SCRIPT.psc -> build/Scripts/"
"$REPO_ROOT/tools/compile-papyrus.sh" "$MCM_SCRIPT" "$SRC/papyrus" "$BUILD/Scripts"
"$REPO_ROOT/tools/compile-papyrus.sh" "$NATIVE_SCRIPT" "$SRC/papyrus" "$BUILD/Scripts"

# --- [3/4] esp (Mutagen/EspGen — quest hosting the MCM script + a PlayerRef alias) ---
source "$REPO_ROOT/tools/env.sh"
echo ">> [3/4] generate $ESP (Mutagen / EspGen) — quest + $PLAYER_ALIAS player alias"
"$DOTNET" run --project "$REPO_ROOT/tools/EspGen" -- \
	"$BUILD/$ESP" "$QUEST_EDID" "$MCM_SCRIPT" "$FULLNAME" --player-alias "$PLAYER_ALIAS"

# --- [4/4] SKSE plugin DLL (clang-cl + lld-link + xwin cross-build via tools/skse toolchain) ---
echo ">> [4/4] build DBVODialogueTweaks.dll (cross-compile Linux -> Windows)"
# shellcheck source=../../tools/skse/cross-env.sh
source "$SKSE_DIR/cross-env.sh"
cmake -S "$PLUGIN_DIR" -B "$PLUGIN_BUILD" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$SKSE_DIR/cmake/clang-cl-msvc.cmake"
cmake --build "$PLUGIN_BUILD"
file "$DLL"

echo ">> artifacts:"
ls -la "$BUILD/$ESP" "$OUT" "$BUILD/Scripts/$MCM_SCRIPT.pex" "$BUILD/Scripts/$NATIVE_SCRIPT.pex" "$DLL"

if [[ "${1:-}" == "--install" ]]; then
	echo ">> installing into live game (Data: $GAME_DATA)"
	mkdir -p "$GAME_DATA/Interface" "$GAME_DATA/Scripts" "$GAME_DATA/SKSE/Plugins"
	# List each copied file + its pre-install md5 so a later manual revert is possible.
	declare -A DEST=(
		["$OUT"]="$GAME_DATA/Interface/dialoguemenu.swf"
		["$BUILD/Scripts/$MCM_SCRIPT.pex"]="$GAME_DATA/Scripts/$MCM_SCRIPT.pex"
		["$BUILD/Scripts/$NATIVE_SCRIPT.pex"]="$GAME_DATA/Scripts/$NATIVE_SCRIPT.pex"
		["$BUILD/$ESP"]="$GAME_DATA/$ESP"
		["$DLL"]="$GAME_DATA/SKSE/Plugins/DBVODialogueTweaks.dll"
	)
	for src in "$OUT" "$BUILD/Scripts/$MCM_SCRIPT.pex" "$BUILD/Scripts/$NATIVE_SCRIPT.pex" "$BUILD/$ESP" "$DLL"; do
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
