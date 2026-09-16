#!/usr/bin/env bash
# vr.session — the phase-2 VR session, run against Monado's SIMULATED headset.
#
# Usage: run_vr_session.sh <test-binary> <monado-manifest>
#
# The same shape as run_vr_spike.sh beside it (and the same two hard-won rules):
# a PRIVATE, SHORT XDG_RUNTIME_DIR, and a runtime named explicitly rather than
# inherited. SKIPS (77) where the no-hardware runtime is not installed.
set -u

TEST="${1:?test binary}"
MANIFEST="${2:?monado manifest}"

command -v monado-service >/dev/null 2>&1 || { echo "SKIP: monado-service is not installed"; exit 77; }
[ -r "$MANIFEST" ] || { echo "SKIP: no Monado manifest at $MANIFEST"; exit 77; }
[ -x "$TEST" ]     || { echo "SKIP: the test binary was not built ($TEST)"; exit 77; }
[ -n "${DISPLAY:-}" ] || { echo "SKIP: a Vulkan engine cannot boot with no display"; exit 77; }

# <= 91 CHARACTERS. Monado 25.0.0 strcpy()s "$XDG_RUNTIME_DIR/monado_comp_ipc"
# into sockaddr_un::sun_path (108 bytes) with no length check and ABORTS on a
# long one (phase 1a FINDINGS §5). A ctest suite's own home directory is well
# inside the danger zone, so the runtime directory is NOT the suite's home.
XDG_DIR="$(mktemp -d /tmp/jahvr.XXXXXX)" || exit 1
chmod 700 "$XDG_DIR"

MON_PID=""
cleanup() {
    # Only the pid recorded at spawn — never by name, never by socket path.
    [ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
    [ -n "$MON_PID" ] && { sleep 1; kill -9 "$MON_PID" 2>/dev/null; }
    rm -rf "$XDG_DIR"
}
trap cleanup EXIT

# XRT_COMPOSITOR_NULL: the runtime needs no window and no display of its own.
# The ENGINE still needs the rig's DISPLAY, as every Vulkan suite does.
XDG_RUNTIME_DIR="$XDG_DIR" SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
    monado-service > "$XDG_DIR/monado.log" 2>&1 &
MON_PID=$!

for _ in $(seq 1 100); do
    [ -S "$XDG_DIR/monado_comp_ipc" ] && break
    kill -0 "$MON_PID" 2>/dev/null || { echo "monado-service died:"; cat "$XDG_DIR/monado.log"; exit 1; }
    sleep 0.2
done
[ -S "$XDG_DIR/monado_comp_ipc" ] || { echo "monado-service never opened its socket:"; tail -20 "$XDG_DIR/monado.log"; exit 1; }

rc=0
XDG_RUNTIME_DIR="$XDG_DIR" XR_RUNTIME_JSON="$MANIFEST" JAH_VR_EXPECT_RUNTIME=Monado \
    "$TEST" || rc=$?
exit $rc
