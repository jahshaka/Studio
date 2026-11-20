#!/usr/bin/env bash
#
# project.remove_folder — THE ONE DELETE REMOVES THE WHOLE FOLDER, OFF THE CALLING THREAD (STUDIO-HYGIENE-1
# stage 2). A project with a real exports/ folder (a web export) is removed through project.remove — the
# Desktop tile's Delete calls the same ProjectService::removeProject: its row goes, the OPEN project and an
# unknown guid are refused by a sentence, and after the process has ended (its shutdown joins the worker) the
# folder is gone and nothing is left aside in its Projects directory; and with the Projects directory read-only
# the delete is REFUSED with the row and every file kept (the base deleted the files and kept the rows).
# Three --headless processes.
#
# $1 = the Jahshaka binary, $2 = the JS half (project_remove.js), $3 = --headless (forwarded to both)
set -u
BIN="$1"; JS="$2"; MODE="${3:?the run mode (--headless)}"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

: "${JAHSHAKA_DATA_ROOT:?the row must run with its own data root}"
case "$HOME" in
    */projectdoors-home-remove) ;;
    *) echo "FAIL: refusing to run outside the row's scratch HOME ($HOME)"; exit 1 ;;
esac
# THE WIPE IS GUARDED BY ITS OWN DIRECTORY, not by HOME: a hand run from anywhere else empties nothing.
case "$PWD" in
    */projectdoors-home-remove/run) find "$PWD" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null ;;
    *) echo "FAIL: refusing to wipe $PWD (the row runs in */projectdoors-home-remove/run)"; exit 1 ;;
esac

{ cat "$JS"; echo "make();"; } > make.run.js
"$BIN" "$MODE" --script "$PWD/make.run.js" > make.log 2>&1
rc=$?
grep -E "^ok:|assert failed" make.log
check $rc "process 1 (a project with a web export, a second one) passes (exit $rc)"
FOLDER="$(grep -o 'REMOVE FOLDER=.*' make.log | cut -d= -f2-)"
[ -n "$FOLDER" ] || { echo "FAIL: process 1 named no folder"; exit 1; }
PROJECTS="$(dirname "$FOLDER")"
n=$(find "$FOLDER/exports" -type f 2>/dev/null | wc -l)
[ "$n" -ge 2 ]
check $? "the folder holds an exports/ tree before the delete ($n files under $FOLDER/exports)"

# Process 2a: the Projects directory READ-ONLY. The base deleted every file inside the folder, failed its rmdir
# and kept the rows: a partial delete. The one door renames aside or refuses — nothing goes.
before=$(find "$FOLDER" -type f | sort | md5sum)
chmod 555 "$PROJECTS"
{ cat "$JS"; echo "readOnly();"; } > ro.run.js
"$BIN" "$MODE" --script "$PWD/ro.run.js" > ro.log 2>&1
rc=$?
chmod 755 "$PROJECTS"
grep -E "^ok:|assert failed|^read-only:" ro.log
check $rc "process 2a (a read-only Projects directory) passes (exit $rc)"
[ "$(find "$FOLDER" -type f | sort | md5sum)" = "$before" ]
check $? "...and every file of the folder is still there"

{ cat "$JS"; echo "remove();"; } > remove.run.js
"$BIN" "$MODE" --script "$PWD/remove.run.js" > remove.log 2>&1
rc=$?
grep -E "^ok:|assert failed|^open:" remove.log
check $rc "process 2 (the refusals, the delete) passes (exit $rc)"
[ ! -e "$FOLDER" ]
check $? "the project's folder is gone"
left=$(find "$PROJECTS" -mindepth 1 -maxdepth 1 -name '.removing-*' | wc -l)
[ "$left" = "0" ]
check $? "nothing is left aside in $PROJECTS ($left)"
[ $fail = 0 ] && echo "project.remove_folder: PASS" || echo "project.remove_folder: FAIL"
exit $fail
