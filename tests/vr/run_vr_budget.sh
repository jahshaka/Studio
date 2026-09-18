#!/usr/bin/env bash
# vr.frame_budget — the APP in a session, then the READER over what it recorded
# (lane V1-RIG item 5).
#
# Usage: run_vr_budget.sh <app-binary> <monado-manifest> <script.js> <reader> <bundle-root>
#
# The same monado wrapper as run_vr_app.sh — a private, SHORT XDG_RUNTIME_DIR
# (the 108-byte sun_path trap), the runtime named explicitly, the service killed
# by the pid recorded at spawn — with one difference: after the app has written
# its capture bundles this runs the reader on them, because the scripting engine
# has no file access. SKIPS (77) where the no-hardware runtime is not installed.
set -u

APP="${1:?app binary}"
MANIFEST="${2:?monado manifest}"
SCRIPT="${3:?script}"
READER="${4:?reader binary}"
BUNDLES="${5:?bundle root}"

command -v monado-service >/dev/null 2>&1 || { echo "SKIP: monado-service is not installed"; exit 77; }
[ -r "$MANIFEST" ] || { echo "SKIP: no Monado manifest at $MANIFEST"; exit 77; }
[ -x "$APP" ]      || { echo "SKIP: the app binary was not built ($APP)"; exit 77; }
[ -r "$SCRIPT" ]   || { echo "SKIP: no script at $SCRIPT"; exit 77; }
[ -x "$READER" ]   || { echo "SKIP: the reader was not built ($READER)"; exit 77; }
[ -n "${DISPLAY:-}" ] || { echo "SKIP: a Vulkan engine cannot boot with no display"; exit 77; }

# A RUN STARTS FROM NOTHING: a bundle left by an earlier run would be read as
# this run's answer if the app failed to write one.
rm -rf "$BUNDLES"
mkdir -p "$BUNDLES" || exit 1

XDG_DIR="$(mktemp -d /tmp/jahvr.XXXXXX)" || exit 1
chmod 700 "$XDG_DIR"

MON_PID=""
cleanup() {
    [ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
    [ -n "$MON_PID" ] && { sleep 1; kill -9 "$MON_PID" 2>/dev/null; }
    rm -rf "$XDG_DIR"
}
trap cleanup EXIT

XDG_RUNTIME_DIR="$XDG_DIR" SIMULATED_ENABLE=1 SIMULATED_LEFT=simple SIMULATED_RIGHT=simple \
    XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
    monado-service > "$XDG_DIR/monado.log" 2>&1 &
MON_PID=$!

for _ in $(seq 1 100); do
    [ -S "$XDG_DIR/monado_comp_ipc" ] && break
    kill -0 "$MON_PID" 2>/dev/null || { echo "monado-service died:"; cat "$XDG_DIR/monado.log"; exit 1; }
    sleep 0.2
done
[ -S "$XDG_DIR/monado_comp_ipc" ] || { echo "monado-service never opened its socket:"; tail -20 "$XDG_DIR/monado.log"; exit 1; }

rc=0
XDG_RUNTIME_DIR="$XDG_DIR" XR_RUNTIME_JSON="$MANIFEST" \
    "$APP" --vr --script "$SCRIPT" || rc=$?
[ "$rc" = 0 ] || { echo "the app half failed (rc=$rc)"; exit "$rc"; }

"$READER" "$BUNDLES"
