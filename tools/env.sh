# Shared environment for the headless Skyrim modding toolchain.
# Sourced by the build scripts. Override any of these by exporting them first.

# .NET SDK: a local ~/.dotnet (dotnet-install script, no root) if it has a working binary, else
# the system one on PATH. The desktop lost its ~/.dotnet binary and runs only the system SDK, so
# a hard ~/.dotnet default broke every Mutagen step there.
if [[ -z "${DOTNET:-}" ]]; then
	if [[ -x "$HOME/.dotnet/dotnet" ]]; then DOTNET="$HOME/.dotnet/dotnet"
	else DOTNET="$(command -v dotnet || echo "$HOME/.dotnet/dotnet")"; fi
fi
export DOTNET_ROOT="${DOTNET_ROOT:-$(dirname "$(readlink -f "$DOTNET")")}"
# The tools target net8.0; a system SDK is usually newer and ships no 8.0 runtime. Rolling
# forward to the installed major is what lets them run at all there.
export DOTNET_ROLL_FORWARD="${DOTNET_ROLL_FORWARD:-Major}"
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_NOLOGO=1

# Live Skyrim Special Edition install (for --install).
GAME_DATA="${GAME_DATA:-$HOME/.steam/steam/steamapps/common/Skyrim Special Edition/Data}"

# Steam Proton prefix for Skyrim SE (appid 489830) — where Plugins.txt lives.
STEAM_APPID="${STEAM_APPID:-489830}"
PREFIX="${PREFIX:-$HOME/.steam/steam/steamapps/compatdata/$STEAM_APPID/pfx/drive_c/users/steamuser}"
PLUGINS_TXT="${PLUGINS_TXT:-$PREFIX/AppData/Local/Skyrim Special Edition/Plugins.txt}"

# Dedicated wine prefix used only to run the .NET-based PapyrusCompiler.exe.
WINEPREFIX_PAPYRUS="${WINEPREFIX_PAPYRUS:-$HOME/.cache/papyrus-wine}"
