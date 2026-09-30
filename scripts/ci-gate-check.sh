#!/usr/bin/env bash
# ci-gate-check — REFUSE A MERGE WITHOUT AN ANSWERED GATE (MODULAR-GATE-1 T6; the flake law since
# TEST-SELECTOR-1; docs/TESTING_GATE.md §4).
#   scripts/ci-gate-check.sh <base>..<tip> [--build build-linux] [--verdict "<text>" [--verdict-suite s…]]
# Exit 0: every row (and arm) <base>..<tip> selects ran at <tip> and its reds are answered (3/3 solo
# for the contention class in <workspace>/testing/contention.json, a recorded verdict otherwise).
# Exit 1 with the reasons; exit 2 when it cannot judge. scripts/lead/merge-dbuild-lane.sh calls it.
exec python3 "$(dirname "$0")/ci_gate_check.py" "$@"
