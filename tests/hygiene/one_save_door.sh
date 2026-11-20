#!/usr/bin/env bash
#
# source.one_save_door — THE SCENE HAS ONE SAVE DOOR (STUDIO-HYGIENE-1 stage 1).
#
# WHY. There were three: saveOpenScene (Ctrl+S, the close, the switch, the export), saveInitialScene (the
# create) and saveProjectBlob (project.save) — and two of them ignored the write's result and marked the
# undo stack saved anyway, so a failed save cleared the dirty state and the close did not even ask. One
# function, ProjectService::save(guid, reason), now writes the row and marks saved only on a written row.
#
# THE RULE: none of the three old doors exists anywhere under src/; outside the database itself the scene
# row's two writers (updateProject, updateProjectBlob) are CALLED only by services/projectservice.cpp.
#
# $1 = the repo root
set -u
ROOT="${1:?usage: one_save_door.sh <source-root>}"
cd "$ROOT" || { echo "source.one_save_door: no such root $ROOT"; exit 1; }
fail=0
hits=$(grep -rnE '\b(saveOpenScene|saveInitialScene|saveProjectBlob)\b' src | grep -vE '^[^:]*:[0-9]+:[[:space:]]*//' || true)
if [ -n "$hits" ]; then
    echo "source.one_save_door: FAIL — an old save door is still there"
    echo "$hits" | sed 's/^/    /'; fail=1
fi
hits=$(grep -rnE '(\.|->)updateProject(Blob)?[[:space:]]*\(' src \
       | grep -vE '^src/services/projectservice\.cpp:|^src/data/database/database\.(h|cpp):' || true)
if [ -n "$hits" ]; then
    echo "source.one_save_door: FAIL — the scene row written outside ProjectService::save"
    echo "$hits" | sed 's/^/    /'; fail=1
fi
[ $fail = 0 ] && echo "source.one_save_door: ok — ProjectService::save is the scene's one door"
exit $fail
