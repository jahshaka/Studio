#!/usr/bin/env bash
# vr.eye_grade — the app, a session, AND THE RUNTIME'S OWN PICTURE (lane
# EYE-GRADE-1; SPECS/VR_SPEC.md §6).
#
# Usage: run_vr_eye_grade.sh <app-binary> <monado-manifest> <script.js> <judge> <outdir>
#
# WHY THIS RUNNER IS NOT run_vr_app.sh. Every other VR suite starts Monado with
# XRT_COMPOSITOR_NULL=1 — no compositor, no picture, nothing to look at — which
# is right when the subject is the session's own arithmetic. The subject HERE is
# the picture the runtime displays, i.e. what the wearer's eye receives after
# the runtime has read our bytes, so the compositor must really composite:
# XRT_COMPOSITOR_FORCE_XCB=1 makes it open an ordinary X window, and that window
# is readable with xwd.
#
# XRT_COMPOSITOR_XCB_DISPLAY IS LOAD-BEARING and cost an hour to find: the XCB
# target does NOT fall back to $DISPLAY, so without it the compositor opens its
# window on :0 (or nowhere) and this runner waits for a window that never
# arrives on the rig's display. It is set to the same display everything else
# here uses.
#
# THE HANDSHAKE. A script cannot signal a shell, but it CAN write a file: the
# script takes a 32x32 screenshot named `ready-<arm>.png` and then renders 600
# frames of the same picture. This loop polls for those markers and captures the
# compositor window while the picture is held. A marker whose capture never
# happened is a missing file, and the reader fails on it rather than passing
# quietly.
#
# SKIPS (77) where the stack is not installed — monado-service, the manifest,
# xwd/xwininfo (x11-apps), ImageMagick's convert — exactly like the other VR
# suites: a box with no VR stack is a supported box.
set -u

APP="${1:?app binary}"
MANIFEST="${2:?monado manifest}"
SCRIPT="${3:?script}"
JUDGE="${4:?judge binary}"
OUTDIR="${5:?output directory}"

command -v monado-service >/dev/null 2>&1 || { echo "SKIP: monado-service is not installed"; exit 77; }
command -v xwd           >/dev/null 2>&1 || { echo "SKIP: xwd is not installed (x11-apps)"; exit 77; }
command -v xwininfo      >/dev/null 2>&1 || { echo "SKIP: xwininfo is not installed (x11-utils)"; exit 77; }
command -v convert       >/dev/null 2>&1 || { echo "SKIP: ImageMagick's convert is not installed"; exit 77; }
[ -r "$MANIFEST" ] || { echo "SKIP: no Monado manifest at $MANIFEST"; exit 77; }
[ -x "$APP" ]      || { echo "SKIP: the app binary was not built ($APP)"; exit 77; }
[ -x "$JUDGE" ]    || { echo "SKIP: the reader was not built ($JUDGE)"; exit 77; }
[ -r "$SCRIPT" ]   || { echo "SKIP: no script at $SCRIPT"; exit 77; }
[ -n "${DISPLAY:-}" ] || { echo "SKIP: a Vulkan engine cannot boot with no display"; exit 77; }

rm -rf "$OUTDIR"
mkdir -p "$OUTDIR" || exit 1

XDG_DIR="$(mktemp -d /tmp/jahvr.XXXXXX)" || exit 1
chmod 700 "$XDG_DIR"

MON_PID=""
APP_PID=""
cleanup() {
    # ONLY THE PIDS THIS SCRIPT SPAWNED, by the value recorded at spawn (the
    # display-number law's sibling rule for processes).
    [ -n "$APP_PID" ] && kill "$APP_PID" 2>/dev/null
    [ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
    [ -n "$MON_PID" ] && { sleep 1; kill -9 "$MON_PID" 2>/dev/null; }
    rm -rf "$XDG_DIR"
}
trap cleanup EXIT

XDG_RUNTIME_DIR="$XDG_DIR" SIMULATED_ENABLE=1 SIMULATED_LEFT=simple SIMULATED_RIGHT=simple \
    XRT_COMPOSITOR_FORCE_XCB=1 XRT_COMPOSITOR_XCB_DISPLAY="$DISPLAY" XRT_NO_STDIN=1 \
    monado-service > "$XDG_DIR/monado.log" 2>&1 &
MON_PID=$!

for _ in $(seq 1 100); do
    [ -S "$XDG_DIR/monado_comp_ipc" ] && break
    kill -0 "$MON_PID" 2>/dev/null || { echo "monado-service died:"; cat "$XDG_DIR/monado.log"; exit 1; }
    sleep 0.2
done
[ -S "$XDG_DIR/monado_comp_ipc" ] || { echo "monado-service never opened its socket:"; tail -20 "$XDG_DIR/monado.log"; exit 1; }

XDG_RUNTIME_DIR="$XDG_DIR" XR_RUNTIME_JSON="$MANIFEST" \
    "$APP" --vr --script "$SCRIPT" &
APP_PID=$!

# THE COMPOSITOR'S WINDOW, BY NAME. Monado's XCB target names it "Monado" and
# nothing else on this display is called that; the id is re-read every time
# because the window only exists while a session does.
monado_window() {
    xwininfo -root -children 2>/dev/null | awk '/"Monado"/ { print $1; exit }'
}

capture() {   # capture <arm>
    local arm="$1" win
    win="$(monado_window)"
    [ -n "$win" ] || { echo "eye_grade: no Monado window to capture for arm $arm"; return 1; }
    xwd -id "$win" -silent > "$OUTDIR/runtime-$arm.xwd" 2>/dev/null || return 1
    convert "$OUTDIR/runtime-$arm.xwd" "$OUTDIR/runtime-$arm.png" 2>/dev/null || return 1
    rm -f "$OUTDIR/runtime-$arm.xwd"
    echo "eye_grade: captured the runtime's window for arm $arm (window $win)"
}

# The poll. Ends when the app ends; a marker is captured once, the first time it
# is seen (the script holds its picture for 600 frames after writing it).
declare -A DONE=()
while kill -0 "$APP_PID" 2>/dev/null; do
    for marker in "$OUTDIR"/ready-*.png; do
        [ -e "$marker" ] || continue
        arm="${marker##*/ready-}"; arm="${arm%.png}"
        [ -n "${DONE[$arm]:-}" ] && continue
        DONE[$arm]=1
        capture "$arm"
    done
    sleep 0.2
done
wait "$APP_PID"; rc=$?
APP_PID=""
if [ "$rc" != 0 ]; then
    echo "eye_grade: the app run failed (rc=$rc)"
    exit "$rc"
fi

"$JUDGE" "$OUTDIR"
