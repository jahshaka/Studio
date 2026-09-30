#!/usr/bin/env bash
# gpu-exclusive.sh [--run-timeout <s>] [--label <text>] <command> [args...] — run <command> holding
# THE WHOLE GPU: every token of the box-wide VRAM budget (scripts/vram_tokens.py `admit all`;
# docs/TESTING_GATE.md §4, §4b).
#
# A suite that measures TIME or GPU BUDGET prints a number that reads as a red when anything else
# shares the GPU with it. ONE ADMISSION (TEST-SELECTOR-1 G1+G2, plan 9ab GPU-RATIO-LOCK-1): this
# used to be a separate flock that excluded only the OTHER timing rows, so a ratio row measured
# beside up to ten tokens of sibling GPU work (gi.rt_reflect_cost 19 and gi.field_scroll 12
# first-run reds). Now a timing row asks the same admission every Vulkan row asks, for ALL the
# tokens: the turnstile keeps the queue behind it, the card drains, and the row measures alone.
# Every timing suite is registered through this (jah_gpu_exclusive_test, tests/CMakeLists.txt),
# and so is every measurement run by hand (`scripts/gpu-exclusive.sh ./Jahshaka --script m.js`).
#
# THE WAIT IS NOT THE ROW'S TIME (LOCK-WAIT-1): the admission's wait is printed as ONE line,
# `gpu-lock: waited <s> s`, which the run log records per row as `lockWaitS` and subtracts from
# the row's seconds; a wait past the bound (JAH_VRAM_WAIT, 900 s) prints `NOADMIT vram: …`, exits
# 75 and never runs the command. With `--run-timeout <s>` (what jah_gpu_exclusive_test passes: the
# row's own budget) THE ROW'S CLOCK STARTS AFTER THE ADMISSION — timeout(1) enforces it from there,
# and ctest's TIMEOUT is only the backstop (budget + the wait's bound). The command is exec'd in
# place with the token fds inherited: the tokens die with it, whatever ends it.
here="$(cd "$(dirname "$0")" && pwd)"
opts=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --run-timeout) opts+=(--run-timeout "${2:?--run-timeout needs seconds}"); shift 2 ;;
        --label) opts+=(--label "${2:?--label needs text}"); shift 2 ;;
        *) break ;;
    esac
done
[ "$#" -gt 0 ] || { echo "usage: gpu-exclusive.sh [--run-timeout <s>] [--label <text>] <command> [args...]" >&2; exit 64; }
exec python3 "$here/vram_tokens.py" admit all --timing "${opts[@]}" -- "$@"
