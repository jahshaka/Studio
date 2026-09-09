#!/usr/bin/env bash
# gate-scope — the SCOPED ctest tier from a change (see gate-scope.py for the rules).
exec python3 "$(dirname "$0")/gate-scope.py" "$@"
