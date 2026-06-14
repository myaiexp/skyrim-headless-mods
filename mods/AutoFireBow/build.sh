#!/usr/bin/env bash
# Build AutoFireBow headlessly into build/, four artifacts:
#   build/Scripts/AutoFireBowMCM.pex   (wine PapyrusCompiler — SkyUI MCM, SKI_ConfigBase)
#   build/Scripts/AutoFireBow.pex      (wine PapyrusCompiler — global-native bridge)
#   build/AutoFireBow.esp              (Mutagen/EspGen — quest hosting the MCM + player alias)
#   build/AutoFireBow.dll              (clang-cl + xwin — SKSE plugin, tools/skse toolchain)
#
#   ./build.sh            build all four (Scripts + esp + DLL into build/)
#   ./build.sh --install  also copy them into the live game (Data/ + SKSE/Plugins/) + activate the esp
#
# We compile AGAINST the vendored SkyUI sources but ship none of SkyUI's .pex — only our own two.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

# --- plugin identity (EspGen + Plugins.txt) ---
ESP="AutoFireBow.esp"
QUEST_EDID="AutoFireBowMCMQuest"
MCM_SCRIPT="AutoFireBowMCM"
NATIVE_SCRIPT="AutoFireBow"           # global-native bridge .psc compiled alongside the MCM
FULLNAME="AutoFireBow"
PLAYER_ALIAS="SKI_PlayerLoadGameAlias"

SKSE_DIR="$REPO_ROOT/tools/skse"      # cross-compile toolchain (cross-env.sh + cmake)
BUILD="$HERE/build"
DLL="$BUILD/AutoFireBow.dll"

mkdir -p "$BUILD/Scripts"

# --- [1/3] Papyrus scripts (wine PapyrusCompiler, against vendored SkyUI sources) ---
echo ">> [1/3] compile $MCM_SCRIPT.psc + $NATIVE_SCRIPT.psc -> build/Scripts/"
"$REPO_ROOT/tools/compile-papyrus.sh" "$MCM_SCRIPT"    "$HERE/src/papyrus" "$BUILD/Scripts"
"$REPO_ROOT/tools/compile-papyrus.sh" "$NATIVE_SCRIPT" "$HERE/src/papyrus" "$BUILD/Scripts"

# --- [2/3] esp (Mutagen/EspGen — quest hosting the MCM script + a PlayerRef alias) ---
source "$REPO_ROOT/tools/env.sh"
echo ">> [2/3] generate $ESP (Mutagen / EspGen) — quest + $PLAYER_ALIAS player alias"
"$DOTNET" run --project "$REPO_ROOT/tools/EspGen" -- \
	"$BUILD/$ESP" "$QUEST_EDID" "$MCM_SCRIPT" "$FULLNAME" --player-alias "$PLAYER_ALIAS"

# --- [3/3] SKSE plugin DLL (clang-cl + lld-link + xwin cross-build via tools/skse toolchain) ---
echo ">> [3/3] build AutoFireBow.dll (cross-compile Linux -> Windows)"
# shellcheck source=../../tools/skse/cross-env.sh
source "$SKSE_DIR/cross-env.sh"
cmake -S "$HERE" -B "$BUILD" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$SKSE_DIR/cmake/clang-cl-msvc.cmake"
cmake --build "$BUILD"
echo "built: $DLL"
file "$DLL"

echo ">> artifacts:"
ls -la "$BUILD/$ESP" "$BUILD/Scripts/$MCM_SCRIPT.pex" "$BUILD/Scripts/$NATIVE_SCRIPT.pex" "$DLL"

if [[ "${1:-}" == "--install" ]]; then
	echo ">> installing into live game (Data: $GAME_DATA)"
	mkdir -p "$GAME_DATA/Scripts" "$GAME_DATA/SKSE/Plugins"
	# List each copied file + its pre-install md5 so a later manual revert is possible.
	declare -A DEST=(
		["$BUILD/Scripts/$MCM_SCRIPT.pex"]="$GAME_DATA/Scripts/$MCM_SCRIPT.pex"
		["$BUILD/Scripts/$NATIVE_SCRIPT.pex"]="$GAME_DATA/Scripts/$NATIVE_SCRIPT.pex"
		["$BUILD/$ESP"]="$GAME_DATA/$ESP"
		["$DLL"]="$GAME_DATA/SKSE/Plugins/AutoFireBow.dll"
	)
	for src in "$BUILD/Scripts/$MCM_SCRIPT.pex" "$BUILD/Scripts/$NATIVE_SCRIPT.pex" "$BUILD/$ESP" "$DLL"; do
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
