#!/usr/bin/env bash
# vr.verbs_session — the APP, with --vr and a script, against Monado's
# SIMULATED headset (SPECS/VR_SPEC.md §6).
#
# Usage: run_vr_app.sh <app-binary> <monado-manifest> <script.js>
#
# The same wrapper as run_vr_session.sh: a private, SHORT XDG_RUNTIME_DIR (the
# 108-byte sun_path trap), a runtime named explicitly, the service spawned in
# the foreground and killed by the pid recorded at spawn. SKIPS (77) where the
# no-hardware runtime is not installed.
set -u

APP="${1:?app binary}"
MANIFEST="${2:?monado manifest}"
SCRIPT="${3:?script}"

command -v monado-service >/dev/null 2>&1 || { echo "SKIP: monado-service is not installed"; exit 77; }
[ -r "$MANIFEST" ] || { echo "SKIP: no Monado manifest at $MANIFEST"; exit 77; }
[ -x "$APP" ]      || { echo "SKIP: the app binary was not built ($APP)"; exit 77; }
[ -r "$SCRIPT" ]   || { echo "SKIP: no script at $SCRIPT"; exit 77; }
[ -n "${DISPLAY:-}" ] || { echo "SKIP: a Vulkan engine cannot boot with no display"; exit 77; }

XDG_DIR="$(mktemp -d /tmp/jahvr.XXXXXX)" || exit 1
chmod 700 "$XDG_DIR"

MON_PID=""
cleanup() {
    [ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
    [ -n "$MON_PID" ] && { sleep 1; kill -9 "$MON_PID" 2>/dev/null; }
    rm -rf "$XDG_DIR"
}
trap cleanup EXIT

# SIMULATED_LEFT/RIGHT=simple: TWO SIMPLE CONTROLLERS beside the simulated
# HMD (lane VR-4). Monado's simulated builder makes none by default (the
# variables take a controller TYPE — `simple`, `wmr` or `ml2`; a bare `1`
# logs "Unsupported controller '1'" and creates nothing), and without them
# there is nothing for the grip-pose actions to bind to and the hand half of
# the suite has no subject. `simple` is the profile our action set suggests
# bindings for, which is the point.
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
exit $rc
