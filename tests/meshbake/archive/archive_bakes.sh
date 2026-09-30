#!/usr/bin/env bash
#
# meshbake.archive_bakes — FORWARD-ONLY-1 item 1: an archive import BAKES what it
# brings, a world opens from bakes and never parses, and a model whose bake is
# gone shows as MISSING with a scene issue — never a parse.
#
# usage: archive_bakes.sh <jahshaka-binary> <import.js> <open.js>
# cwd is the scratch run dir; JAHSHAKA_DATA_ROOT is the scratch data root (wiped
# here, so every run starts from a fresh library).
set -u
BIN="$1"; IMPORT_JS="$2"; OPEN_JS="$3"
ROOT="${JAHSHAKA_DATA_ROOT:?the row sets JAHSHAKA_DATA_ROOT}"
rm -rf "$ROOT"; mkdir -p "$ROOT"

# Run 1: import the shipped samples into the fresh root; every model baked, the
# open reads bakes only, and the log carries no "parsing the source" line.
"$BIN" --data-root "$ROOT" --script "$IMPORT_JS" > run1.log 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then echo "archive_bakes: run 1 exited $rc"; tail -40 run1.log; exit 1; fi
grep -E "^(ok|FAIL)" run1.log
if grep -q "parsing the source" run1.log; then
    echo "archive_bakes: FAIL — a 'parsing the source' line in run 1"; exit 1
fi

# The bakes go (a stand-in for a bake this build cannot read: the reader's
# answer to both is the same null).
n=$(find "$ROOT" -path '*objects*' -name '*.jmb' | wc -l)
echo "archive_bakes: removing $n bake object(s)"
[ "$n" -gt 0 ] || { echo "archive_bakes: FAIL — run 1 left no bake objects"; exit 1; }
find "$ROOT" -path '*objects*' -name '*.jmb' -delete

# Run 2: the same world opens with its models MISSING and a scene issue naming
# them — and still no parse.
"$BIN" --data-root "$ROOT" --script "$OPEN_JS" > run2.log 2>&1
rc=$?
grep -E "^(ok|FAIL)" run2.log
if [ "$rc" -ne 0 ]; then echo "archive_bakes: run 2 exited $rc"; tail -40 run2.log; exit 1; fi
if grep -q "parsing the source" run2.log; then
    echo "archive_bakes: FAIL — a 'parsing the source' line in run 2"; exit 1
fi
echo "archive_bakes: PASS"
