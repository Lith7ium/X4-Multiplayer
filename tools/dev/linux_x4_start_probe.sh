#!/usr/bin/env bash
# One bounded start attempt of the Linux X4 build on a private Xvfb display with Mesa
# lavapipe (software Vulkan) and null audio. Dedicated-server gate H0 preflight.
#
# It never edits the game directory: X4 runs with an isolated HOME (fresh profile
# 45841 only), inside a private mount namespace with its own /tmp (so the singleton
# lock and X11 socket are task-private). It records stdout, the X11 window tree and a
# screenshot, and reports whether X4 stopped at its modal "Startup error" dialog.
#
# Needs root (unshare/mount; WSL root is fine), Xvfb, mesa-vulkan-drivers, x11-utils,
# x11-apps (xwd). Usage:
#   GAME=/path/to/X4-linux-install [SECS=120] [SHOT=25] [CPUS=0-7] tools/dev/linux_x4_start_probe.sh [extra X4 args]
# Outputs go to $OUT (default .local/linux-start-probe/<timestamp> in this checkout).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
GAME=${GAME:?set GAME to the Linux X4 installation directory}
SECS=${SECS:-120}
SHOT=${SHOT:-25}
CPUS=${CPUS:-0-7}
ICD=${ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
OUT=${OUT:-$ROOT/.local/linux-start-probe/$(date +%Y%m%d-%H%M%S)}
PINNED=113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20

[ "$(id -u)" = 0 ] || { echo "needs root for a private mount namespace" >&2; exit 2; }
[ -x "$GAME/X4" ] || [ -f "$GAME/X4" ] || { echo "no X4 in $GAME" >&2; exit 2; }
[ -f "$ICD" ] || { echo "Vulkan ICD $ICD missing (install mesa-vulkan-drivers)" >&2; exit 2; }
for tool in Xvfb xwininfo xwd unshare taskset; do
    command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 2; }
done
actual=$(sha256sum "$GAME/X4" | cut -d' ' -f1)
[ "$actual" = "$PINNED" ] || { echo "X4 differs from pinned 9.00 build 23660954 ($actual)" >&2; exit 2; }
mkdir -p "$OUT/home"
echo "out=$OUT"

export OUT GAME SECS SHOT CPUS ICD
unshare --mount --propagation private bash -s -- "$@" <<'INNER'
set -u
mount -t tmpfs -o mode=1777 tmpfs /tmp
mkdir -p /tmp/.X11-unix && chmod 1777 /tmp/.X11-unix
Xvfb :91 -screen 0 1280x720x24 -nolisten tcp -noreset >"$OUT/xvfb.log" 2>&1 &
xvfb=$!
for _ in $(seq 100); do [ -e /tmp/.X11-unix/X91 ] && break; sleep 0.1; done
[ -e /tmp/.X11-unix/X91 ] || { echo "Xvfb did not start" >&2; kill $xvfb; exit 3; }
export HOME="$OUT/home" DISPLAY=:91 VK_ICD_FILENAMES="$ICD" VK_DRIVER_FILES="$ICD"
export SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=null LD_LIBRARY_PATH="$GAME/lib"
( sleep "$SHOT"
  xwininfo -root -tree >"$OUT/windows.txt" 2>&1
  xwd -root -silent >"$OUT/screen.xwd" 2>/dev/null ) &
cd "$GAME"
start=$(date +%s)
timeout -s KILL "$SECS" taskset -c "$CPUS" nice -n 10 ./X4 -personalfolderid 45841 -skipintro \
    -debug all -logfile x4-debug.txt -nocputhrottle -noworkshopsync "$@" </dev/null >"$OUT/x4-stdout.log" 2>&1
code=$?
echo "x4_exit=$code seconds=$(( $(date +%s) - start ))" | tee "$OUT/result.txt"
kill $xvfb 2>/dev/null
INNER

if grep -q '"X4 - Startup error"' "$OUT/windows.txt" 2>/dev/null; then
    echo "RESULT: X4 stopped at its modal startup error dialog (see $OUT/screen.xwd)." | tee -a "$OUT/result.txt"
    echo "Steam builds show 'Steam client is required' here when no logged-in Steam client runs." | tee -a "$OUT/result.txt"
fi
grep -m3 -E 'steam.sh|Steam' "$OUT/x4-stdout.log" 2>/dev/null || true
log=$(find "$OUT/home" -name x4-debug.txt 2>/dev/null | head -1)
[ -n "$log" ] && { echo "debug log: $log"; tail -5 "$log"; } || echo "no X4 debug log was created"
