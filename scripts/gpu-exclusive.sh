#!/usr/bin/env bash
# gpu-exclusive.sh <command> [args...] — run <command> holding THE BOX-WIDE GPU-TIMING LOCK
# (lane DEVPROCESS-1, docs/TESTING_GATE.md §4).
#
# A suite that measures TIME or GPU BUDGET prints a number that reads as a red when a sibling
# lane's gate or app shares the GPU with it (measured 2026-09-23: VRAM is not the constraint —
# 2.3 GB of 16 GB with three GPU processes live — GPU TIME is). RUN_SERIAL serialises only
# inside ONE ctest process, and every lane is its own ctest process, so the exclusivity has to
# live outside ctest: one advisory flock(1) on one file, taken by every timing suite (the CMake
# helper jah_gpu_exclusive_test in tests/CMakeLists.txt registers them through this script) and
# by every measurement run (`scripts/gpu-exclusive.sh ./Jahshaka --script measure.js`).
# Everything else — pixel and logic suites — shares the GPU freely and never takes it.
#
# /tmp IS FINE FOR A LOCK FILE even though /tmp is RAM on this box: the file is empty, the
# lock lives in the kernel's open-file table (not in the bytes), it is released when the
# holder exits for ANY reason (a crash, a ctest timeout kill), and a reboot that wipes /tmp
# also ends every holder. Nothing durable is written.
#
# The wait is bounded at 900 s (JAH_GPU_LOCK_WAIT overrides — the tooling suite uses it); a
# wrapped suite's ctest TIMEOUT carries those 900 s on top of its own budget. A timed-out wait
# exits 75 (EX_TEMPFAIL) with flock's own message, never runs the command, and so never
# prints a contended number. --verbose makes flock say how long the wait took, which is the
# first thing to read when a wrapped suite looks slow.
#
# THE WAIT IS NOT THE ROW'S TIME (LOCK-WAIT-1, stage close 1: perf.epic_steady_state ran ~30 s
# solo and was killed at its ctest TIMEOUT in the stage tier by GPU-lock QUEUE time). The
# wrapper prints the wait it measured as ONE line, `gpu-lock: waited <s> s`, which the run log
# records per row as `lockWaitS` and subtracts from the row's seconds; a wait past the bound
# prints `NOLOCK gpu-lock: …` (the run log's NOLOCK verdict: the row never ran — the queue,
# not its code). And with `--run-timeout <s>` (what jah_gpu_exclusive_test passes: the row's
# own budget) THE ROW'S CLOCK STARTS AFTER THE LOCK: the budget is enforced from here by
# timeout(1) — after the VRAM tokens too, when gpu-admit.sh follows (it reads
# JAH_GPU_RUN_TIMEOUT) — and ctest's TIMEOUT is only the backstop (budget + wait bounds).
LOCK="${JAH_GPU_LOCK:-/tmp/jah-gpu-timing.lock}"
WAIT="${JAH_GPU_LOCK_WAIT:-900}"
RUN=""
if [ "${1:-}" = "--run-timeout" ]; then RUN="${2:?--run-timeout needs seconds}"; shift 2; fi
[ "$#" -gt 0 ] || { echo "usage: gpu-exclusive.sh [--run-timeout <s>] <command> [args...]" >&2; exit 64; }
# THE LOCK IS TAKEN ON A FILE DESCRIPTOR AND THE COMMAND EXEC'D IN PLACE — not `flock <file> <cmd>`,
# which forks the command as a CHILD of flock: ctest would then time out and kill flock, not the
# suite, and orphan the app it spawned. Here the pid ctest started IS the suite, fd 9 (the lock)
# is inherited by it and by every process it spawns, and the lock is released when the last of
# them exits — the GPU is not free before that either.
exec 9>>"$LOCK"
t0=$(date +%s.%N)
flock -E 75 -w "$WAIT" 9 || {
    rc=$?
    echo "NOLOCK gpu-lock: waited $WAIT s for $LOCK and never got it — the command did not run" >&2
    exit $rc
}
waited=$(awk -v a="$t0" -v b="$(date +%s.%N)" 'BEGIN { printf "%.1f", b - a }')
echo "gpu-lock: waited $waited s" >&2
if [ -n "$RUN" ]; then
    # A VRAM admission next applies the budget itself, after its own wait; otherwise here.
    case "$(basename "$1")" in
        gpu-admit.sh) export JAH_GPU_RUN_TIMEOUT="$RUN"; exec "$@" ;;
        *) exec timeout --verbose -k 15 "$RUN" "$@" ;;
    esac
fi
exec "$@"
