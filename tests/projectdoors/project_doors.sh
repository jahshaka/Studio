#!/usr/bin/env bash
#
# project.refuses_unreadable — A FILE THIS BUILD CANNOT READ IS REFUSED, NEVER HALF-READ (STUDIO-HYGIENE-1
# stage 6, forward-only). Three refusals, each red on the code before the stage:
#   1. an archive whose manifest is version 3 imported -> refused, naming both versions (it was read as v2);
#   2. a project whose scene blob is formatVersion 3 opened -> refused, nothing open (it was read as v2 and
#      the next save wrote it back as v2, without what the newer build added);
#   3. a project whose scene blob does not parse opened -> refused (it opened EMPTY and a save wrote that
#      over the row); and an archive whose scene does not parse imported -> refused (it imported EMPTY).
# Plus the control: the v2 source opens with its nodes. Two --headless processes and one python step.
#
# $1 = the Jahshaka binary, $2 = this directory (the JS half and the fixture maker)
set -u
BIN="$1"; HERE="$2"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

: "${JAHSHAKA_DATA_ROOT:?the row must run with its own data root}"
case "$HOME" in
    */projectdoors-home-refuse) ;;
    *) echo "FAIL: refusing to run outside the row's scratch HOME ($HOME)"; exit 1 ;;
esac
find "$PWD" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null
mkdir -p fixtures

{ cat "$HERE/project_doors.js"; echo "make(\"$PWD/fixtures\");"; } > make.run.js
"$BIN" --headless --script "$PWD/make.run.js" > make.log 2>&1
rc=$?
grep -E "^ok:|assert failed|Error" make.log
check $rc "process 1 (make the source project and its archive) passes (exit $rc)"
grep -q "make: ALL OK" make.log
check $? "process 1 reached its end"
[ $fail = 0 ] || exit 1

python3 -I "$HERE/rewrite_archive.py" "$PWD/fixtures/source.zip" "$PWD/fixtures" \
        "$JAHSHAKA_DATA_ROOT/JahLibrary.db" "Doors Corrupt Row"
check $? "the three archives are made from the export, and one library row is corrupted"
[ $fail = 0 ] || exit 1

{ cat "$HERE/project_doors.js"; echo "refuse(\"$PWD/fixtures\");"; } > refuse.run.js
"$BIN" --headless --script "$PWD/refuse.run.js" > refuse.log 2>&1
rc=$?
grep -E "^ok:|assert failed|manifest v3:|scene v3:|scene garbage:" refuse.log
check $rc "process 2 (the three refusals and the control) passes (exit $rc)"
grep -q "refuse: ALL OK" refuse.log
check $? "process 2 reached its end"
[ $fail = 0 ] && echo "project.refuses_unreadable: PASS" || echo "project.refuses_unreadable: FAIL"
exit $fail
