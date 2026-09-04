#!/usr/bin/env bash
#
# api.contract — the checked-in docs/SCRIPTING.md still matches the registry.
#
# AI_SURFACE_PROGRAM_SPEC §2.0: "the docs cannot drift" was a claim about the
# GENERATOR, not about the file in the repo. Nothing asserted that the committed
# markdown was ever regenerated after a verb was added, renamed or re-documented
# — so every consumer of that file (a reader, a model reading it as ground
# truth, api_docs' future search) could be reading a surface that no longer
# exists. This runs the real binary's --dump-api-docs (which forces --headless,
# src/app/cli/clioptions.cpp) into a scratch file and byte-compares.
#
# $1 = the Jahshaka binary   $2 = the checked-in docs/SCRIPTING.md
set -u

BIN="$1"
COMMITTED="$2"
OUT="$PWD/SCRIPTING.generated.md"

if [ ! -f "$COMMITTED" ]; then
    echo "api.contract: no committed docs at $COMMITTED"
    exit 1
fi

# HOME and the working directory are the caller's scratch (CTest sets both), so
# the run gets a fresh library and writes its logs/settings nowhere shared.
"$BIN" --dump-api-docs "$OUT" > dump.log 2>&1
rc=$?
if [ $rc -ne 0 ]; then
    echo "api.contract: --dump-api-docs exited $rc"
    tail -30 dump.log
    exit 1
fi

if ! cmp -s "$OUT" "$COMMITTED"; then
    echo "api.contract: docs/SCRIPTING.md does NOT match the live ApiRegistry."
    echo ""
    echo "  regenerate: ./Jahshaka --dump-api-docs docs/SCRIPTING.md"
    echo ""
    echo "  (run it from build-linux/bin with the repo path to the docs file;"
    echo "   the file is GENERATED — never hand-edit it, and never resolve a"
    echo "   merge conflict in it by hand: delete it and regenerate.)"
    echo ""
    echo "--- first differences (committed vs generated) ---"
    diff -u "$COMMITTED" "$OUT" | head -60
    exit 1
fi

echo "api.contract: docs/SCRIPTING.md matches the registry byte for byte."
exit 0
