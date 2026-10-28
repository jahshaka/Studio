#!/usr/bin/env bash
# gpu-exclusive.sh [--run-timeout <s>] [--wait-max <s>] [--label <text>] <command> [args...] — run <command> holding THE CARD: the one
# lock on the GPU (DOCS/PROCESS.md; TESTING-SIMPLE). An exclusive flock on /tmp/jah-gpu.lock (JAH_GPU_LOCK overrides),
# waited for with one `gpu-lock: waiting …` line and one `gpu-lock: waited <s> s` line; the command runs with
# JAH_GPU_LOCK_HELD=1, so a row that takes the lock itself under a holder that has it (spec.sh test / merge and
# sprint.sh ship hold it around their ctest) runs at once. Inside one ctest the width per class is ctest's own
# (tests/cmake/vram_rows.cmake: the card's 8 slots). Every app or GPU binary run by hand outside ctest, and every
# measurement, runs through this. --run-timeout: the command's own bound, counted after the wait (timeout(1)).
# --wait-max <s>: wait for the card at most <s> seconds, else exit 75 with one `gpu-lock: the card stayed held …` line and
# nothing run (URGENT FIX LIST #26: a caller with a foreground limit bounds its whole call; it is still the lock's one wait).
# The kernel's GPU faults while the command ran are printed after it (scripts/kernel_xid.py): an Xid is never
# environmental. The desktop route by default: JAHSHAKA_VR=0 unless the caller set JAHSHAKA_VR (`--vr` runs set it).
export JAHSHAKA_VR="${JAHSHAKA_VR-0}"
here="$(cd "$(dirname "$0")" && pwd)"
rt="" label=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --run-timeout) rt="${2:?--run-timeout needs seconds}"; shift 2 ;;
        --wait-max) wm="${2:?--wait-max needs seconds}"; shift 2 ;;
        --label) label="${2:?--label needs text}"; shift 2 ;;
        *) break ;;
    esac
done
[ "$#" -gt 0 ] || { echo "usage: gpu-exclusive.sh [--run-timeout <s>] [--wait-max <s>] [--label <text>] <command> [args...]" >&2; exit 64; }
bound=(); [ -n "$rt" ] && bound=(timeout -k 10 "$rt")
[ "${JAH_GPU_LOCK_HELD:-}" = 1 ] && exec "${bound[@]}" "$@"
# THE DRIVER's SHADER CACHE REACHES EVERY GPU RUN (URGENT #91 + #99, 2026-10-28): /etc/profile.d is not sourced in an agent's
# shell, and a row's fresh HOME has no driver cache at all, so every run redid the driver's work. Every GPU run passes here.
export __GL_SHADER_DISK_CACHE_SIZE="${__GL_SHADER_DISK_CACHE_SIZE:-8589934592}"
# the driver appends GLCache/ itself: the path is the PARENT, so the rows share the box's own cache (#97's diagnosis)
export __GL_SHADER_DISK_CACHE_PATH="${__GL_SHADER_DISK_CACHE_PATH:-/home/jahshaka/.cache/nvidia}"
lock="${JAH_GPU_LOCK:-/tmp/jah-gpu.lock}"
exec 9>>"$lock"
if ! flock -n 9; then
    echo "gpu-lock: waiting for the card (held by: $(cat "$lock.holder" 2>/dev/null || echo '?'))"
    t0=$(date +%s)
    if [ -n "${wm:-}" ]; then
        flock -w "$wm" 9 || { echo "gpu-lock: the card stayed held ${wm} s (by: $(cat "$lock.holder" 2>/dev/null || echo '?')) - nothing ran"; exit 75; }
    else
        flock 9
    fi
    echo "gpu-lock: waited $(( $(date +%s) - t0 )) s"
fi
echo "pid $$ ${label:-$1} since $(date '+%F %T')" > "$lock.holder"
start=$(date +%s)
JAH_GPU_LOCK_HELD=1 "${bound[@]}" "$@"
rc=$?
python3 "$here/kernel_xid.py" since "$start" 2>&1 | sed 's/^/gpu-lock: /'
exit $rc
