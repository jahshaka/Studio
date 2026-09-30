#!/usr/bin/env bash
#
# app.library_generation — FORWARD-ONLY-1 (services/librarygeneration.h):
#   1. a fresh data root is NOT wiped (outcome noLibrary, then current);
#   2. a library carrying an older generation (PRAGMA user_version) IS wiped at
#      startup — its projects are gone and the outcome says "wiped";
#   3. an older library whose data root another instance HOLDS (the
#      `<db>.lock` QLockFile) is REFUSED: the process exits 4 with the reason,
#      and nothing is wiped.
#
# usage: generation.sh <jahshaka-binary>      cwd = a scratch run dir
set -u
BIN="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$PWD/gen-root"
DB="$ROOT/JahLibrary.db"
rm -rf "$ROOT" boot*.log
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }
val() { grep "^$1=" "$2" | tail -1 | cut -d= -f2-; }
boot() { "$BIN" --headless --data-root "$ROOT" --script "$HERE/generation_probe.js" > "$1" 2>&1; }
setgen() { python3 -c "import sqlite3,sys; c=sqlite3.connect(sys.argv[1]); c.execute('PRAGMA user_version=%d' % int(sys.argv[2])); c.commit()" "$DB" "$1"; }
getgen() { python3 -c "import sqlite3,sys; print(sqlite3.connect(sys.argv[1]).execute('PRAGMA user_version').fetchone()[0])" "$DB"; }

boot boot1.log; check $? "boot 1 (fresh root) exits 0"
[ "$(val GEN_OUTCOME boot1.log)" = "noLibrary" ]; check $? "boot 1: no library -> no wipe ($(val GEN_OUTCOME boot1.log))"
GEN="$(val GEN_GENERATION boot1.log)"
boot boot2.log; check $? "boot 2 exits 0"
[ "$(val GEN_OUTCOME boot2.log)" = "current" ] && [ "$(val GEN_WIPED boot2.log)" = "false" ]
check $? "boot 2: this build's own library is CURRENT and is not wiped"
[ "$(val GEN_PROJECTS boot2.log)" -ge 1 ]; check $? "boot 2 sees boot 1's project"

OLD=$((GEN - 1))
setgen "$OLD"
boot boot3.log; check $? "boot 3 (older generation) exits 0"
[ "$(val GEN_OUTCOME boot3.log)" = "wiped" ] && [ "$(val GEN_WIPED boot3.log)" = "true" ] \
  && [ "$(val GEN_ONDISK boot3.log)" = "$OLD" ]
check $? "boot 3: generation $OLD is WIPED (outcome $(val GEN_OUTCOME boot3.log), onDisk $(val GEN_ONDISK boot3.log))"
[ "$(val GEN_PROJECTS boot3.log)" = "0" ]; check $? "boot 3: the old library's projects are gone"
[ "$(getgen)" = "$GEN" ]; check $? "boot 3: the recreated library carries generation $GEN"

# A HELD data root: a live process's QLockFile (pid / process name / host).
setgen "$OLD"
sleep 120 & HOLDER=$!
printf '%s\nsleep\n%s\n' "$HOLDER" "$(hostname)" > "$DB.lock"
boot boot4.log; rc=$?
kill "$HOLDER" 2>/dev/null; rm -f "$DB.lock"
[ "$rc" = "4" ]; check $? "boot 4 (older library, data root held) is REFUSED with exit 4 (rc $rc)"
grep -q "another Jahshaka instance" boot4.log; check $? "boot 4 names the reason"
[ "$(getgen)" = "$OLD" ]; check $? "boot 4 wiped nothing (the library still carries generation $OLD)"

if [ "$fail" != "0" ]; then tail -20 boot4.log; exit 1; fi
echo "app.library_generation: PASS"
