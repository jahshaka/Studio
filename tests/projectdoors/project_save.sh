#!/usr/bin/env bash
#
# project.save_door — THE ONE SAVE DOOR: TRUE ONLY WHEN THE ROW WAS WRITTEN (STUDIO-HYGIENE-1 stage 1).
# Process 1: project.save round-trips (reopened, every node back) and the dirty laws through the verbs (push
# dirty, save clean, push after a save dirty, an opened scene clean). Between the processes, python gives one
# project's row a SQLite trigger that drops every UPDATE of it (RAISE(IGNORE): the statement succeeds and
# writes NO row - the shape of a row that is gone). Process 2: a save of that project is refused with the
# sentence, the scene still dirty, and the row's bytes unchanged. Two --headless processes, no clock.
#
# $1 = the Jahshaka binary, $2 = the JS half (project_save.js), $3 = --headless (forwarded to both)
set -u
BIN="$1"; JS="$2"; MODE="${3:?the run mode (--headless)}"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

: "${JAHSHAKA_DATA_ROOT:?the row must run with its own data root}"
case "$PWD" in
    */projectdoors-home-save/run) find "$PWD" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null ;;
    *) echo "FAIL: refusing to wipe $PWD (the row runs in */projectdoors-home-save/run)"; exit 1 ;;
esac
LIB="$JAHSHAKA_DATA_ROOT/JahLibrary.db"

{ cat "$JS"; echo "make();"; } > make.run.js
"$BIN" "$MODE" --script "$PWD/make.run.js" > make.log 2>&1
rc=$?
grep -E "^ok:|assert failed" make.log
check $rc "process 1 (the round trip, the dirty laws, the trap project) passes (exit $rc)"
grep -q "make: ALL OK" make.log
check $? "process 1 reached its end"
[ $fail = 0 ] || exit 1

python3 -I - "$LIB" > trap.txt <<'PY'
import hashlib, sqlite3, sys
conn = sqlite3.connect(sys.argv[1])
blob = conn.execute("SELECT scene FROM projects WHERE name = 'Doors Save Trap'").fetchone()[0]
conn.execute("CREATE TRIGGER doors_save_trap BEFORE UPDATE ON projects WHEN OLD.name = 'Doors Save Trap' "
             "BEGIN SELECT RAISE(IGNORE); END")
conn.commit()
conn.close()
print(hashlib.sha256(bytes(blob)).hexdigest())
PY
check $? "the trap project's row is made to drop every write"

{ cat "$JS"; echo "save();"; } > save.run.js
"$BIN" "$MODE" --script "$PWD/save.run.js" > save.log 2>&1
rc=$?
grep -E "^ok:|assert failed|^save:" save.log
check $rc "process 2 (the failed save) passes (exit $rc)"
grep -q "save: ALL OK" save.log
check $? "process 2 reached its end"
python3 -I - "$LIB" "$(cat trap.txt)" <<'PY'
import hashlib, sqlite3, sys
conn = sqlite3.connect(sys.argv[1])
blob = conn.execute("SELECT scene FROM projects WHERE name = 'Doors Save Trap'").fetchone()[0]
sys.exit(0 if hashlib.sha256(bytes(blob)).hexdigest() == sys.argv[2] else 1)
PY
check $? "the trap project's row is the bytes it was (nothing was written, and nothing claimed it was)"
[ $fail = 0 ] && echo "project.save_door: PASS" || echo "project.save_door: FAIL"
exit $fail
