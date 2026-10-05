#!/usr/bin/env bash
# vr.verbs_session — the APP, with --vr and a script, against Monado's
# SIMULATED headset (SPECS/VR_SPEC.md §6).
#
# Usage: run_vr_app.sh <app-binary> <monado-manifest> <script.js>
#        run_vr_app.sh --launch <monado-manifest> -- <command…>
#
# The second form is a POOL's launcher (lane SUITE-POOL-1; tests/CMakeLists.txt,
# jah_add_pool LAUNCHER): the same runtime, alive for the whole pool, and the
# command — the pool driver, which starts `<app> --vr` for its arms and again
# after a crash — run inside it with the runtime's environment.
#
# The same wrapper as run_vr_session.sh: a private, SHORT XDG_RUNTIME_DIR (the
# 108-byte sun_path trap), a runtime named explicitly, the service spawned in
# the foreground and killed by the pid recorded at spawn. SKIPS (77) where the
# no-hardware runtime is not installed.
set -u

if [ "${1:-}" = "--launch" ]; then
    LAUNCH=1
    MANIFEST="${2:?monado manifest}"
    [ "${3:-}" = "--" ] || { echo "run_vr_app.sh --launch <manifest> -- <command…>"; exit 2; }
    shift 3
    [ $# -gt 0 ] || { echo "run_vr_app.sh --launch: no command"; exit 2; }
else
    LAUNCH=0
    APP="${1:?app binary}"
    MANIFEST="${2:?monado manifest}"
    SCRIPT="${3:?script}"
fi

command -v monado-service >/dev/null 2>&1 || { echo "SKIP: monado-service is not installed"; exit 77; }
[ -r "$MANIFEST" ] || { echo "SKIP: no Monado manifest at $MANIFEST"; exit 77; }
if [ "$LAUNCH" = 0 ]; then
    [ -x "$APP" ]      || { echo "SKIP: the app binary was not built ($APP)"; exit 77; }
    [ -r "$SCRIPT" ]   || { echo "SKIP: no script at $SCRIPT"; exit 77; }
fi
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
#
# JAH_MONADO_CONTROLLERS=none starts the headset ALONE (meshbake.shipped_bakes):
# with no controller bound to either hand, `vr.inject` is accepted — the one way
# to put a Touch profile in a hand on a box whose runtime has no Touch model.
CONTROLLERS=(SIMULATED_LEFT=simple SIMULATED_RIGHT=simple)
[ "${JAH_MONADO_CONTROLLERS:-simple}" = none ] && CONTROLLERS=()
start_runtime() {
    rm -f "$XDG_DIR/monado_comp_ipc"
    env XDG_RUNTIME_DIR="$XDG_DIR" SIMULATED_ENABLE=1 "${CONTROLLERS[@]}" \
        XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
        monado-service >> "$XDG_DIR/monado.log" 2>&1 &
    MON_PID=$!
    for _ in $(seq 1 100); do
        [ -S "$XDG_DIR/monado_comp_ipc" ] && return 0
        kill -0 "$MON_PID" 2>/dev/null || { echo "monado-service died:"; cat "$XDG_DIR/monado.log"; return 1; }
        sleep 0.2
    done
    echo "monado-service never opened its socket:"; tail -20 "$XDG_DIR/monado.log"; return 1
}
stop_runtime() {   # the runtime going away under a live session (VR-START-1 case e)
    [ -n "$MON_PID" ] || return 0
    kill -9 "$MON_PID" 2>/dev/null; wait "$MON_PID" 2>/dev/null
    MON_PID=""
    rm -f "$XDG_DIR/monado_comp_ipc"
}

# THE RUNNER'S EVENTS (VR-START-1): with JAH_VR_RUNNER_EVENTS=1 the app's stdout is
# watched for `VRRUNNER: stop-runtime` / `VRRUNNER: start-runtime` lines, which kill
# or (re)start the PRIVATE runtime under the running app — the headset dropping, the
# headset arriving after launch. JAH_VR_RUNNER_DEFER=1 launches the app with NO
# runtime up. Either way the runtime is the private one: XDG_RUNTIME_DIR is ours, so
# the client can never reach (or socket-activate) the user's system Monado.
if [ "${JAH_VR_RUNNER_DEFER:-0}" != 1 ]; then
    start_runtime || exit 1
fi

rc=0
if [ "$LAUNCH" = 1 ]; then
    XDG_RUNTIME_DIR="$XDG_DIR" XR_RUNTIME_JSON="$MANIFEST" "$@" || rc=$?
elif [ "${JAH_VR_RUNNER_EVENTS:-0}" = 1 ]; then
    OUT="$XDG_DIR/app.out"; : > "$OUT"
    # --script-live: the driver keeps rendering between the script's verbs, so
    # the VR button's per-frame follower (the one that tells the user a session
    # was dropped) runs exactly as it does for a person.
    XDG_RUNTIME_DIR="$XDG_DIR" XR_RUNTIME_JSON="$MANIFEST" \
        "$APP" --vr --script-live --script "$SCRIPT" > "$OUT" 2>&1 &
    APP_PID=$!
    seen=0
    while kill -0 "$APP_PID" 2>/dev/null; do
        n=$(grep -c '^VRRUNNER: ' "$OUT")
        while [ "$seen" -lt "$n" ]; do
            seen=$((seen + 1))
            ev=$(grep '^VRRUNNER: ' "$OUT" | sed -n "${seen}p")
            case "$ev" in
                "VRRUNNER: stop-runtime")  echo "runner: stopping the private runtime"; stop_runtime ;;
                "VRRUNNER: start-runtime") echo "runner: starting the private runtime"; start_runtime ;;
            esac
        done
        sleep 0.2
    done
    wait "$APP_PID"; rc=$?
    cat "$OUT"
else
    XDG_RUNTIME_DIR="$XDG_DIR" XR_RUNTIME_JSON="$MANIFEST" \
        "$APP" --vr --script "$SCRIPT" || rc=$?
fi
exit $rc
