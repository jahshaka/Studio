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
LOCK="${JAH_GPU_LOCK:-/tmp/jah-gpu-timing.lock}"
WAIT="${JAH_GPU_LOCK_WAIT:-900}"
[ "$#" -gt 0 ] || { echo "usage: gpu-exclusive.sh <command> [args...]" >&2; exit 64; }
# THE LOCK IS TAKEN ON A FILE DESCRIPTOR AND THE COMMAND EXEC'D IN PLACE — not `flock <file> <cmd>`,
# which forks the command as a CHILD of flock: ctest would then time out and kill flock, not the
# suite, and orphan the app it spawned. Here the pid ctest started IS the suite, fd 9 (the lock)
# is inherited by it and by every process it spawns, and the lock is released when the last of
# them exits — the GPU is not free before that either.
exec 9>>"$LOCK"
flock --verbose -E 75 -w "$WAIT" 9 >&2 || exit $?   # flock says how long it waited, on stderr
exec "$@"
