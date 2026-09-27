#!/usr/bin/env bash
# gpu-admit.sh <tokens> [--label <text>] -- <command> [args...] — run <command> holding <tokens> of
# THE BOX-WIDE VRAM BUDGET (lane GATE-ADMIT-1, docs/TESTING_GATE.md §4b; the model is
# scripts/gpu-exclusive.sh). 12 flock tokens under /tmp/jah-vram/ (JAH_VRAM_TOKENS overrides,
# 0 = admission off), 1 token ~ 1 GB of the 16 GB card; every Vulkan test row takes its class's
# tokens (app/pool process 2, engine suite 1, VR 3, the selftest 2 — tests/CMakeLists.txt,
# jah_gpu_row), all or nothing, lowest free first, waiting (one `vram: waiting …` line) instead of
# failing an allocation. The command is EXEC'D IN PLACE with the token fds inherited: the pid
# ctest started is the suite, and the tokens are freed when it (and everything it spawned) exits
# for any reason. A wait past JAH_VRAM_WAIT (900 s) exits 75 and never runs the command.
# `scripts/gpu-admit.sh status` lists the holders. The implementation is scripts/vram_tokens.py
# (run_pool.py imports the same code to take a pool's tokens once per app process).
here="$(cd "$(dirname "$0")" && pwd)"
[ "$#" -gt 0 ] || { echo "usage: gpu-admit.sh <tokens> [--label <text>] -- <command> [args...] | status" >&2; exit 64; }
if [ "$1" = status ]; then exec python3 "$here/vram_tokens.py" status; fi
exec python3 "$here/vram_tokens.py" admit "$@"
