#!/usr/bin/env bash
# vr.spike_1a — the VR phase 1a spike, run against Monado's SIMULATED headset.
#
# Usage: run_vr_spike.sh <spike-binary> <monado-manifest> <frames>
#
# SKIPS (exit 77, the suite's SKIP_RETURN_CODE) when the no-hardware runtime is
# not installed on this box. It never fails for a missing runtime: a box without
# monado-service is a supported box (VR_SPEC §6).
set -u

SPIKE="${1:?spike binary}"
MANIFEST="${2:?monado manifest}"
FRAMES="${3:-60}"

command -v monado-service >/dev/null 2>&1 || { echo "SKIP: monado-service is not installed"; exit 77; }
[ -r "$MANIFEST" ] || { echo "SKIP: no Monado manifest at $MANIFEST"; exit 77; }
[ -x "$SPIKE" ]    || { echo "SKIP: the spike binary was not built ($SPIKE)"; exit 77; }
[ -n "${DISPLAY:-}" ] || { echo "SKIP: a Vulkan engine cannot boot with no display"; exit 77; }

# THE RUNTIME DIRECTORY MUST BE SHORT. Monado 25.0.0 strcpy()s
# "$XDG_RUNTIME_DIR/monado_comp_ipc" into sockaddr_un::sun_path (108 bytes) with
# no length check and ABORTS with "*** buffer overflow detected ***" on a long
# one — measured 2026-09-16 with a 107-character scratch path. A private
# directory per run also keeps two lanes' services apart.
XDG_DIR="$(mktemp -d /tmp/jahvr.XXXXXX)" || exit 1
chmod 700 "$XDG_DIR"

MON_PID=""
cleanup() {
    # Only the pid we recorded at spawn, never by name and never by socket path
    # (CLAUDE.md: a number is not an identity).
    [ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
    [ -n "$MON_PID" ] && { sleep 1; kill -9 "$MON_PID" 2>/dev/null; }
    rm -rf "$XDG_DIR"
}
trap cleanup EXIT

XDG_RUNTIME_DIR="$XDG_DIR" SIMULATED_ENABLE=1 XRT_COMPOSITOR_NULL=1 XRT_NO_STDIN=1 \
    monado-service > "$XDG_DIR/monado.log" 2>&1 &
MON_PID=$!

for _ in $(seq 1 100); do
    [ -S "$XDG_DIR/monado_comp_ipc" ] && break
    kill -0 "$MON_PID" 2>/dev/null || { echo "monado-service died:"; cat "$XDG_DIR/monado.log"; exit 1; }
    sleep 0.2
done
[ -S "$XDG_DIR/monado_comp_ipc" ] || { echo "monado-service never opened its socket:"; tail -20 "$XDG_DIR/monado.log"; exit 1; }

OUT="$XDG_DIR/out"; mkdir -p "$OUT"
rc=0
XDG_RUNTIME_DIR="$XDG_DIR" XR_RUNTIME_JSON="$MANIFEST" \
    "$SPIKE" --frames "$FRAMES" --out "$OUT" --expect-runtime Monado || rc=1

# The parity arm: the same scene on an OGRE-created device. Equal bytes = the
# runtime's device and Ogre's own device render the same picture, which is what
# ogre-patch 0068's exported extension list and feature chain exist to make true.
"$SPIKE" --plain --size 320 240 --out "$OUT" || rc=1
if [ -f "$OUT/parity-xr.ppm" ] && [ -f "$OUT/parity-plain.ppm" ]; then
    a=$(sha256sum "$OUT/parity-xr.ppm" | cut -d' ' -f1)
    b=$(sha256sum "$OUT/parity-plain.ppm" | cut -d' ' -f1)
    if [ "$a" = "$b" ]; then
        echo "ok     the external (runtime-created) device renders the plain device's picture ($a)"
    else
        echo "FAIL  external-device picture $a != plain-device picture $b"
        rc=1
    fi
else
    echo "FAIL  a parity picture is missing"
    rc=1
fi
exit $rc
