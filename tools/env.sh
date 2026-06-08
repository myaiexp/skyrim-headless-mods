# Shared environment for the headless Skyrim modding toolchain.
# Sourced by the build scripts. Override any of these by exporting them first.

# Local .NET SDK (installed to ~/.dotnet via the official dotnet-install script, no root).
DOTNET="${DOTNET:-$HOME/.dotnet/dotnet}"
export DOTNET_ROOT="${DOTNET_ROOT:-$HOME/.dotnet}"
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_NOLOGO=1

# Live Skyrim Special Edition install (for --install).
GAME_DATA="${GAME_DATA:-$HOME/.steam/steam/steamapps/common/Skyrim Special Edition/Data}"

# Steam Proton prefix for Skyrim SE (appid 489830) — where Plugins.txt lives.
STEAM_APPID="${STEAM_APPID:-489830}"
PREFIX="$HOME/.steam/steam/steamapps/compatdata/$STEAM_APPID/pfx/drive_c/users/steamuser"
PLUGINS_TXT="$PREFIX/AppData/Local/Skyrim Special Edition/Plugins.txt"

# Dedicated wine prefix used only to run the .NET-based PapyrusCompiler.exe.
WINEPREFIX_PAPYRUS="${WINEPREFIX_PAPYRUS:-$HOME/.cache/papyrus-wine}"
