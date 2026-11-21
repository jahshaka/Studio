#!/usr/bin/env bash
#
# source.no_ui_folder_delete — NO WIDGET DELETES A FOLDER TREE (STUDIO-HYGIENE-1 stage 2).
#
# WHY. A recursive delete is unbounded I/O: a project folder holds its exports (a web export is a viewer,
# a glb and its textures), and QDir::removeRecursively walks and unlinks all of it on the calling thread.
# The Desktop's Delete did exactly that from its slot — the window frozen for the length of the delete, a
# PARTIAL delete on failure (it keeps going) with the rows kept — and it was a SECOND copy of the delete
# beside the verb's (project.remove), which also refused the open project and dropped the tile's
# thumbnail encode where the widget's copy did neither. ProjectService::removeProject is the one door:
# the folder renamed aside (one rename(2)), the rows deleted, the bytes removed on a worker.
#
# THE RULE: no CALL of removeRecursively under src/ui/ (a comment naming it is not a call).
#
# $1 = the repo root
set -u
ROOT="${1:?usage: no_ui_folder_delete.sh <source-root>}"
cd "$ROOT" || { echo "source.no_ui_folder_delete: no such root $ROOT"; exit 1; }

# A CALL: the code before any // on the line names removeRecursively(.
hits=$(grep -rnE 'removeRecursively[[:space:]]*\(' src/ui \
       | awk -F: '{ code = $0; sub(/^[^:]*:[^:]*:/, "", code); sub(/\/\/.*/, "", code);
                    if (code ~ /removeRecursively[[:space:]]*\(/) print }' || true)
if [ -n "$hits" ]; then
    echo "source.no_ui_folder_delete: FAIL — a widget deletes a folder tree"
    echo "$hits" | sed 's/^/    /'
    echo "    Call ProjectService::removeProject (the one door: renamed aside, removed on a worker)."
    exit 1
fi
echo "source.no_ui_folder_delete: ok — no removeRecursively call under src/ui"
exit 0
