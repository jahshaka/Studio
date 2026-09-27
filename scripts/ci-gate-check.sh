#!/usr/bin/env bash
# ci-gate-check — REFUSE A MERGE WITHOUT A GREEN SCOPED GATE (MODULAR-GATE-1, T6; §7b).
#   scripts/ci-gate-check.sh <base>..<tip> [--build build-linux]
# Exit 0: the run log holds a green record at <tip> for every row (and arm) <base>..<tip>
# selects. Exit 1 with the reason otherwise. A hook or a CI job calls it; it needs no server.
exec python3 "$(dirname "$0")/ci_gate_check.py" "$@"
