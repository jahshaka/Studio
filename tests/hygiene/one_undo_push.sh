#!/usr/bin/env bash
#
# source.one_undo_push — EVERY SCENE COMMAND IS PUSHED THROUGH UndoService (STUDIO-HYGIENE-1 audit).
#
# WHY. UndoService::push is the spine: the edit gate, the hover preview's end, the script run's lazy macro and
# the PUSH COUNT that "unsaved changes" reads (UndoService::isDirty: a push moves it). A command pushed on the
# scene's QUndoStack directly moves none of them: after a save and an undo, a direct push lands at the saved
# index with the push count unchanged and the scene reads CLEAN though it lacks the undone edit — the close
# then does not ask. assets.createFolder and assets.moveToFolder did exactly that.
#
# THE RULE: no `undoStack->push(` / `stack()->push(` under src/ but in services/undoservice.cpp. The material
# graph's own QUndoStack (src/modules/materials/graph/, its document's stack, not the scene's) is its own spine.
#
# $1 = the repo root
set -u
ROOT="${1:?usage: one_undo_push.sh <source-root>}"
cd "$ROOT" || { echo "source.one_undo_push: no such root $ROOT"; exit 1; }
hits=$(grep -rnE '(undoStack|stack\(\))[[:space:]]*->[[:space:]]*push[[:space:]]*\(' src \
       | grep -vE '^src/services/undoservice\.cpp:|^src/modules/materials/graph/' \
       | awk -F: '{ code = $0; sub(/^[^:]*:[^:]*:/, "", code); sub(/\/\/.*/, "", code);
                    if (code ~ /push[[:space:]]*\(/) print }' || true)
if [ -n "$hits" ]; then
    echo "source.one_undo_push: FAIL — a command pushed past UndoService"
    echo "$hits" | sed 's/^/    /'
    echo "    Push through UndoService::push (host.services->undo->push / services->undo->push)."
    exit 1
fi
echo "source.one_undo_push: ok — every scene command goes through UndoService::push"
exit 0
