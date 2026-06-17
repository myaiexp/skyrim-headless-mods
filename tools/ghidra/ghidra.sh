#!/usr/bin/env bash
# ghidra.sh — reproducible headless Ghidra workflow for the Skyrim RE tier.
#
# The SKSE C++ tier hooks engine functions by Address-Library ID. When a behaviour
# lives in a *non-virtual* function body with no Address-Library seam (e.g. the
# stop-point cast inside FlameProjectile/BeamProjectile::UpdateImpl — see
# docs/plans/ghost-allies-design.md §2b), the only way to find a hookable point is to
# disassemble SkyrimSE.exe. This wraps that, headlessly and repeatably, so any session
# can reproduce a finding instead of re-deriving the toolchain. Full rationale +
# gotchas: docs/ghidra.md.
#
# Workflow: analyse the binary ONCE into a Ghidra project, then run cheap read-only
# PyGhidra query scripts against it as many times as you like (no re-analysis).
#
# Usage (run `ghidra.sh` with no args for this):
#   ghidra.sh setup              install check + create the PyGhidra venv (idempotent)
#   ghidra.sh unpack             Steamless-decrypt SkyrimSE.exe (SteamStub DRM) — REQUIRED FIRST;
#                                the on-disk .text is encrypted, raw analysis is garbage
#   ghidra.sh analyze [binary]   import + auto-analyse a binary into the project
#                                (default: the unpacked exe; ~tens of min, run detached)
#   ghidra.sh query <script.py> [args...]
#                                run a PyGhidra query (read-only) over the analysed project
#   ghidra.sh gui                open the analysed project in the Ghidra GUI
#   ghidra.sh status             show install / venv / project / binary state
#
# Everything is overridable by exporting before the call:
#   GHIDRA_INSTALL_DIR (/opt/ghidra)  GHIDRA_HEADLESS_MAXMEM (8G — 2G GC-thrashes!)
#   GHIDRA_BINARY (derived from GAME_DATA)  GHIDRA_PROJECT (SkyrimSE)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"  # tools/ghidra
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# shellcheck source=/dev/null
source "$REPO_ROOT/tools/env.sh"  # GAME_DATA, etc.

GHIDRA_INSTALL_DIR="${GHIDRA_INSTALL_DIR:-/opt/ghidra}"
# 8G, NOT Arch's 2G default: a 37 MB PE GC-thrashes a 2G heap (~7x slower). See docs/ghidra.md.
GHIDRA_HEADLESS_MAXMEM="${GHIDRA_HEADLESS_MAXMEM:-8G}"
GHIDRA_PROJECT="${GHIDRA_PROJECT:-SkyrimSE}"

PROJ_DIR="$SCRIPT_DIR/projects"
VENV="$SCRIPT_DIR/.venv"
PY="$VENV/bin/python"
SCRIPTS_DIR="$SCRIPT_DIR/scripts"
OUT_DIR="$SCRIPT_DIR/out"
HEADLESS="$GHIDRA_INSTALL_DIR/support/analyzeHeadless"

# SkyrimSE.exe sits next to Data/ but is SteamStub-DRM encrypted (.text is garbage on disk) —
# `unpack` runs Steamless to produce the decrypted exe we actually analyse. See docs/ghidra.md.
STEAM_EXE="${STEAM_EXE:-${GAME_DATA%/Data}/SkyrimSE.exe}"
STEAMLESS_DIR="$SCRIPT_DIR/.steamless"
UNPACKED="$STEAMLESS_DIR/SkyrimSE.exe.unpacked.exe"
# Analysis/queries default to the UNPACKED exe; only fall back to the (encrypted) original if
# unpacking hasn't run yet — so a stale-binary mistake is loud, not silent garbage.
GHIDRA_BINARY="${GHIDRA_BINARY:-$( [[ -f "$UNPACKED" ]] && echo "$UNPACKED" || echo "$STEAM_EXE" )}"

die() { echo "ghidra.sh: $*" >&2; exit 1; }

cmd_setup() {
  if ! command -v ghidra-analyzeHeadless >/dev/null 2>&1 && [[ ! -x "$HEADLESS" ]]; then
    cat >&2 <<EOF
Ghidra is not installed. Install it (needs sudo, so run it yourself):
    sudo pacman -S --noconfirm ghidra
Then re-run: ghidra.sh setup
EOF
    exit 1
  fi
  echo "ghidra: $(pacman -Q ghidra 2>/dev/null || echo "found at $GHIDRA_INSTALL_DIR")"

  if [[ ! -x "$PY" ]]; then
    echo "creating PyGhidra venv (python 3.12; jpype1 has no wheel for newer)…"
    command -v uv >/dev/null 2>&1 || die "uv not found (needed to build the venv)"
    uv venv --python 3.12 "$VENV"
    uv pip install --python "$PY" pyghidra
  fi
  GHIDRA_INSTALL_DIR="$GHIDRA_INSTALL_DIR" "$PY" -c \
    "import jpype, pyghidra; print('venv ok: jpype', jpype.__version__, '| pyghidra', pyghidra.__version__)"
}

cmd_unpack() {
  # SkyrimSE.exe is wrapped in SteamStub DRM (SteamStub Variant 3.1 x64): its .text is
  # encrypted on disk, so static disassembly is pure garbage until unpacked. Steamless
  # decrypts it. We run it on a COPY under wine — the live game exe is never touched.
  [[ -f "$STEAM_EXE" ]] || die "Steam exe not found: $STEAM_EXE (set STEAM_EXE)"
  command -v wine >/dev/null 2>&1 || die "wine not found (Steamless is .NET)"
  local cli="$STEAMLESS_DIR/steamless/Steamless.CLI.exe"
  if [[ ! -f "$cli" ]]; then
    echo "fetching Steamless (gh release)…"
    command -v gh >/dev/null 2>&1 || die "Steamless missing and no gh CLI to fetch it"
    mkdir -p "$STEAMLESS_DIR"
    gh release download v3.1.0.5 -R atom0s/Steamless -D "$STEAMLESS_DIR" --clobber \
      || die "Steamless download failed"
    (cd "$STEAMLESS_DIR" && for z in *.zip; do unzip -o -q "$z" -d steamless; done)
    cp "$STEAMLESS_DIR/steamless/Plugins/Steamless.API.dll" "$STEAMLESS_DIR/steamless/"  # JIT needs it adjacent
  fi
  cp -f "$STEAM_EXE" "$STEAMLESS_DIR/SkyrimSE.exe"
  echo "unpacking SteamStub via Steamless under wine…"
  ( cd "$STEAMLESS_DIR/steamless" \
    && WINEPREFIX="${WINEPREFIX_PAPYRUS:-$HOME/.cache/papyrus-wine}" WINEDEBUG=-all \
       wine Steamless.CLI.exe ../SkyrimSE.exe ) 2>&1 | rg -i 'packed with|decrypt|Saved As|Successfully|error' | tail -8
  [[ -f "$UNPACKED" ]] || die "unpack produced no output ($UNPACKED)"
  echo "unpacked -> $UNPACKED ($(stat -c%s "$UNPACKED") bytes). analyse/query now use it."
}

cmd_analyze() {
  local binary="${1:-$GHIDRA_BINARY}"
  [[ -f "$binary" ]] || die "binary not found: $binary (set GHIDRA_BINARY)"
  [[ -x "$PY" ]] || die "venv missing — run: ghidra.sh setup"
  mkdir -p "$PROJ_DIR"
  echo "analysing $binary -> project $PROJ_DIR/$GHIDRA_PROJECT (heap $GHIDRA_HEADLESS_MAXMEM)"
  echo "this is the long, one-time step — fine to run detached. progress: $SCRIPT_DIR/analyze.log"
  # PreLean.java (Java preScript — stock headless has no Python) trims the heavy passes
  # we don't need; RTTI + basic disassembly stay on. See docs/ghidra.md.
  GHIDRA_HEADLESS_MAXMEM="$GHIDRA_HEADLESS_MAXMEM" \
    "$HEADLESS" "$PROJ_DIR" "$GHIDRA_PROJECT" \
    -import "$binary" \
    -overwrite \
    -max-cpu 12 \
    -preScript PreLean.java \
    -scriptPath "$SCRIPTS_DIR" \
    -log "$SCRIPT_DIR/analyze.log" \
    -scriptlog "$SCRIPT_DIR/analyze-script.log"
  echo "analysis done."
}

cmd_query() {
  local script="${1:-}"
  [[ -n "$script" ]] || die "usage: ghidra.sh query <script.py> [args...]"
  shift || true
  # accept a bare name (resolved under scripts/) or an explicit path
  [[ -f "$script" ]] || script="$SCRIPTS_DIR/$script"
  [[ -f "$script" ]] || die "query script not found: $script"
  [[ -x "$PY" ]] || die "venv missing — run: ghidra.sh setup"
  [[ -d "$PROJ_DIR/$GHIDRA_PROJECT.rep" ]] || die "no analysed project — run: ghidra.sh analyze"
  mkdir -p "$OUT_DIR"
  # Query scripts read these so they carry no hardcoded paths.
  GHIDRA_INSTALL_DIR="$GHIDRA_INSTALL_DIR" \
  GHIDRA_PROJ_LOC="$PROJ_DIR" GHIDRA_PROJ_NAME="$GHIDRA_PROJECT" GHIDRA_OUT="$OUT_DIR" \
    "$PY" "$script" "$@"
}

cmd_gui() {
  command -v ghidra >/dev/null 2>&1 || die "ghidra GUI launcher not on PATH"
  echo "opening $PROJ_DIR/$GHIDRA_PROJECT.gpr — leave this running; Ctrl-C to close"
  ghidra "$PROJ_DIR/$GHIDRA_PROJECT.gpr"
}

cmd_status() {
  echo "install : $(command -v ghidra-analyzeHeadless >/dev/null 2>&1 && echo "$(pacman -Q ghidra 2>/dev/null)" || echo "MISSING — ghidra.sh setup")"
  echo "venv    : $([[ -x "$PY" ]] && echo ready || echo "MISSING — ghidra.sh setup")"
  echo "unpacked: $([[ -f "$UNPACKED" ]] && echo "ready ($UNPACKED)" || echo "MISSING — ghidra.sh unpack (SteamStub-encrypted otherwise)")"
  echo "binary  : $GHIDRA_BINARY $([[ -f "$GHIDRA_BINARY" ]] && echo "($(stat -c%s "$GHIDRA_BINARY") bytes)" || echo "(NOT FOUND)")"
  [[ "$GHIDRA_BINARY" == "$STEAM_EXE" ]] && echo "          ^ WARNING: this is the ENCRYPTED Steam exe — run ghidra.sh unpack first"
  if [[ -d "$PROJ_DIR/$GHIDRA_PROJECT.rep" ]]; then
    echo "project : $PROJ_DIR/$GHIDRA_PROJECT ($(du -sh "$PROJ_DIR/$GHIDRA_PROJECT.rep" 2>/dev/null | cut -f1) analysed)"
  else
    echo "project : not analysed — ghidra.sh analyze"
  fi
  echo "heap    : $GHIDRA_HEADLESS_MAXMEM"
  echo "scripts : $(ls "$SCRIPTS_DIR" 2>/dev/null | tr '\n' ' ')"
}

case "${1:-}" in
  setup)   shift; cmd_setup "$@" ;;
  unpack)  shift; cmd_unpack "$@" ;;
  analyze|analyse) shift; cmd_analyze "$@" ;;
  query)   shift; cmd_query "$@" ;;
  gui)     shift; cmd_gui "$@" ;;
  status)  shift; cmd_status "$@" ;;
  ""|help|-h|--help) sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d; s/^# \{0,1\}//' ;;
  *) die "unknown verb '${1}' — run: ghidra.sh help" ;;
esac
