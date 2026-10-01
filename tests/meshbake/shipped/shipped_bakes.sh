#!/usr/bin/env bash
#
# meshbake.shipped_bakes — SHIPPED-BAKES-1's app arm: assimp is an IMPORT-time
# dependency, and the app makes NO runtime parse.
#
# RUN 1 — a FRESH data root boots (the seed bakes every shipped mesh: the
#   primitives, the samples' Ground and Teapot, the preview docks' subjects, the
#   VR controllers), opens the Avatar space and its room, imports and opens a
#   character and its walk clip (each read from its bake), plays the clip,
#   opens the Materials page's preview, renders a material thumbnail and — when
#   the box has Monado — runs a VR session; `app.openStats` reads ZERO runtime
#   parses over the whole run.
# RUN 2 — the CLIP BAKES ARE DELETED (a stand-in for a stale producer): the
#   project's open REBUILDS them from the clip's own source on a worker before
#   the scene shows — a bake build, never a parse — and the character's clip is
#   back.
#
# usage: shipped_bakes.sh <jahshaka-binary> <run1.js> <run2.js> <vr-runner> <monado-manifest>
# cwd = the scratch run dir; JAHSHAKA_DATA_ROOT = the scratch data root (wiped here).
set -u
BIN="$1"; RUN1="$2"; RUN2="$3"; VR_RUNNER="$4"; MANIFEST="$5"
ROOT="${JAHSHAKA_DATA_ROOT:?the row sets JAHSHAKA_DATA_ROOT}"
rm -rf "$ROOT"; mkdir -p "$ROOT"

clip_bakes() { find "$ROOT" -path '*objects*' -name '*.jcb' | wc -l; }

# RUN 1, inside Monado's simulated headset when the box has it (the runner
# SKIPS — 77 — without it, and then the run goes on without the VR case).
if command -v monado-service >/dev/null 2>&1 && [ -r "$MANIFEST" ]; then
    printf 'var VR_RUNTIME = true;\n' > run1.js
else
    printf 'var VR_RUNTIME = false;\n' > run1.js
fi
cat "$RUN1" >> run1.js
if grep -q 'VR_RUNTIME = true' run1.js; then
    JAH_MONADO_CONTROLLERS=none bash "$VR_RUNNER" --launch "$MANIFEST" -- "$BIN" --vr --data-root "$ROOT" --script "$PWD/run1.js" \
        > run1.log 2>&1
else
    "$BIN" --data-root "$ROOT" --script "$PWD/run1.js" > run1.log 2>&1
fi
rc=$?
grep -E "^(ok|FAIL|census)" run1.log
if [ "$rc" -ne 0 ]; then echo "shipped_bakes: run 1 exited $rc"; tail -40 run1.log; exit 1; fi

n=$(clip_bakes)
echo "shipped_bakes: run 1 left $n clip bake object(s)"
[ "$n" -gt 0 ] || { echo "shipped_bakes: FAIL — the clip import wrote no clip bake"; exit 1; }
find "$ROOT" -path '*objects*' -name '*.jcb' -delete
echo "shipped_bakes: deleted them"

# RUN 2 — the library's background sweep is OFF, so it is the OPEN that must
# rebuild (the sweep is the same rebuild at the lowest priority).
JAHSHAKA_BAKE_SWEEP=0 "$BIN" --data-root "$ROOT" --script "$RUN2" > run2.log 2>&1
rc=$?
grep -E "^(ok|FAIL|census)" run2.log
if [ "$rc" -ne 0 ]; then echo "shipped_bakes: run 2 exited $rc"; tail -40 run2.log; exit 1; fi
n=$(clip_bakes)
echo "shipped_bakes: run 2 rebuilt $n clip bake object(s)"
[ "$n" -gt 0 ] || { echo "shipped_bakes: FAIL — the open did not rebuild the clip bake"; exit 1; }
echo "shipped_bakes: PASS"
