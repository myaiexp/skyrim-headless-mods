#!/bin/bash
# Capture the current headless gamescope frame.
#
# gamescope can't be screenshotted with X11 grabbers (the game renders via
# Vulkan/DXVK, so X11 sees a black buffer). Instead gamescope dumps its own
# composited frame to /tmp/gamescope_<timestamp>.avif on SIGUSR2. We convert
# that to PNG (ImageMagick) so it can be viewed/diffed.
#
# Usage:
#   ./shot.sh [out.png] [cropWxH+X+Y] [scaleWxH]
#     out.png    output path (default /tmp/sky-shot.png)
#     cropWxH+X+Y optional ImageMagick crop geometry (coords in WxH = output res)
#     scaleWxH   optional rescale (e.g. to magnify a crop)
set -euo pipefail

OUT="${1:-/tmp/sky-shot.png}"
CROP="${2:-}"
SCALE="${3:-}"

GS="$(pgrep -f 'gamescope --backend headless' | head -1 || true)"
[ -z "$GS" ] && { echo "no headless gamescope running" >&2; exit 1; }

kill -USR2 "$GS"
sleep 1.5
AVIF="$(ls -t /tmp/gamescope_*.avif 2>/dev/null | head -1 || true)"
[ -z "$AVIF" ] && { echo "no gamescope screenshot appeared in /tmp" >&2; exit 1; }

args=("$AVIF")
[ -n "$CROP" ]  && args+=(-crop "$CROP" +repage)
[ -n "$SCALE" ] && args+=(-scale "$SCALE")
args+=("$OUT")
magick "${args[@]}"
echo "$OUT"
