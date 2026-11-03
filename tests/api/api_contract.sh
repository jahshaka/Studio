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

# HOME and the working directory are the caller's scratch (CTest sets both) and
# the api_contract_home fixture WIPES that home before this runs, so the run
# gets a fresh library (a stale one of an older schema is refused, exit 4) and
# writes its logs/settings nowhere shared.
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
    echo "  regenerate: scripts/regen-api-docs.sh <build dir>  (the one way; the dump touches no GPU, and proves it)"
    echo ""
    echo "  (run it from build-linux/bin with the repo path to the docs file;"
    echo "   the file is GENERATED — never hand-edit it, and never resolve a"
    echo "   merge conflict in it by hand: delete it and regenerate.)"
    echo ""
    echo "--- first differences (committed vs generated) ---"
    diff -u "$COMMITTED" "$OUT" | head -60
    exit 1
fi

# THE DUMP's PROOF FIRES (TEST-STREAMLINE-1: the dump runs with no card lock because it PROVES it touched no GPU - a headless
# engine and no Vulkan driver mapped, src/app/cli/scriptrunner.cpp): its negative control - a loader manifest naming a
# library this process HAS mapped (libc) stands for a driver an instance loaded; the dump must refuse, exit 3, write nothing.
printf '{"file_format_version": "1.0.0", "ICD": {"library_path": "libc.so.6", "api_version": "1.0.0"}}\n' > toy_icd.json
rm -f proof.md
VK_DRIVER_FILES="$PWD/toy_icd.json" "$BIN" --dump-api-docs "$PWD/proof.md" > proof.log 2>&1
rc=$?
if [ $rc -ne 3 ] || [ -e proof.md ] || ! grep -q 'a Vulkan driver is mapped into this process (libc.so.6)' proof.log; then
    echo "api.contract: the dump's no-GPU proof did NOT fire on a mapped 'driver' (rc $rc; want 3, no file, the line)"
    tail -5 proof.log
    exit 1
fi
echo "api.contract: docs/SCRIPTING.md matches the registry byte for byte; the dump's no-GPU proof fires on a mapped driver (exit 3)."
exit 0
