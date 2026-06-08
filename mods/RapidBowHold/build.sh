#!/usr/bin/env bash
# Build RapidBowHold headlessly: generate the host .esp (Mutagen) + compile the .pex (wine).
#   ./build.sh            build into ./build/
#   ./build.sh --install  build, then copy into the live Skyrim Data folder + activate plugin
#
# NOTE: after --install you must FULLY restart Skyrim (the Papyrus VM caches .pex for the
# whole session), then in the console: stopquest RapidBowHoldQuest / startquest RapidBowHoldQuest
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MOD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$REPO_ROOT/tools/env.sh"

ESP="RapidBowHoldQuest.esp"
QUEST_EDID="RapidBowHoldQuest"
SCRIPT="RapidBowHoldScript"
FULLNAME="RapidBowHold"
BUILD="$MOD_DIR/build"
mkdir -p "$BUILD/Scripts"

echo ">> [1/2] generate $ESP (Mutagen / EspGen)"
"$DOTNET" run --project "$REPO_ROOT/tools/EspGen" -- "$BUILD/$ESP" "$QUEST_EDID" "$SCRIPT" "$FULLNAME"

echo ">> [2/2] compile $SCRIPT.psc -> $SCRIPT.pex (wine PapyrusCompiler)"
"$REPO_ROOT/tools/compile-papyrus.sh" "$SCRIPT" "$MOD_DIR/src" "$BUILD/Scripts"

echo ">> artifacts:"
ls -la "$BUILD/$ESP" "$BUILD/Scripts/$SCRIPT.pex"

if [[ "${1:-}" == "--install" ]]; then
  echo ">> installing into live game"
  cp "$BUILD/$ESP" "$GAME_DATA/$ESP"
  cp "$BUILD/Scripts/$SCRIPT.pex" "$GAME_DATA/Scripts/$SCRIPT.pex"
  if grep -q "^\*$ESP$" "$PLUGINS_TXT"; then
    :
  elif grep -q "^$ESP$" "$PLUGINS_TXT"; then
    sed -i "s|^$ESP$|*$ESP|" "$PLUGINS_TXT"
  else
    printf '*%s\n' "$ESP" >> "$PLUGINS_TXT"
  fi
  echo ">> installed + activated. FULLY restart Skyrim, then console: stopquest $QUEST_EDID / startquest $QUEST_EDID"
fi
