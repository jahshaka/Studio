#!/bin/bash
# lab.script_stops (URGENT #50, 2026-10-24) - A REFUSED SCRIPT LINE STOPS THE SCRIPT: a --script line the lab refuses
# (the grammar's or a verb's own refusal) ends the run there, nonzero, its LAB SCRIPT STOPPED line naming the line's
# number, the line and the reason - so no later line runs on a session that is not what its script says (2026-10-24: a
# refused `arm rays.originCut 2`, out of its 0..1 range, left a "control" shot that was the base picture again, and a
# finding was drawn from it). A line opts out EXPLICITLY - `try <line>`: its refusal is said (LAB TRY) and the script goes
# on, the run's exit untouched.
# THE LAWS, three sessions on photon-contact (a frame each):
#   (1) a refused ARM (out of its range) on line 2: exit nonzero, "LAB SCRIPT STOPPED at line 2" with the line and the
#       arm's own reason, and line 3's shot NOT taken;
#   (2) an unknown VERB on line 2 (the grammar's refusal): the same, with the grammar's reason;
#   (3) the same arm under `try`: exit 0, LAB TRY says the refusal, and the shot after it taken.
# usage: script_stops.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf 'frames 1\narm rays.originCut 2\nshot after-arm\nquit\n' > "$WD/arm.lab"
printf 'frames 1\nbogusverb 1\nshot after-verb\nquit\n' > "$WD/verb.lab"
printf 'frames 1\ntry arm rays.originCut 2\nshot after-try\nquit\n' > "$WD/try.lab"
fail=0
for s in arm verb try; do
    "$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/data" --env photon-contact --script "$WD/$s.lab" \
        > "$WD/$s.log" 2>&1
    echo "$?" > "$WD/$s.rc"
    grep -E '^LAB (SCRIPT STOPPED|TRY|REFUSED|END)' "$WD/$s.log" | sed "s/^/  $s: /"
done
for s in arm verb; do
    rc=$(cat "$WD/$s.rc")
    [ "$rc" != 0 ] || { echo "FAIL: ($s) a refused line left the run's exit 0"; fail=1; }
    grep -qE "^LAB SCRIPT STOPPED at line 2: '[^']+' - .+" "$WD/$s.log" ||
        { echo "FAIL: ($s) no LAB SCRIPT STOPPED line naming line 2, the line and its reason"; fail=1; }
    [ ! -e "$WD/data/shots/after-$s.png" ] || { echo "FAIL: ($s) the line after the refused one ran (its shot exists)"; fail=1; }
done
grep -qE "^LAB SCRIPT STOPPED at line 2: 'arm rays.originCut 2' - .*out of its range" "$WD/arm.log" ||
    { echo "FAIL: (arm) the STOPPED line does not carry the arm's own reason"; fail=1; }
grep -qE "^LAB SCRIPT STOPPED at line 2: 'bogusverb 1' - .*bogusverb" "$WD/verb.log" ||
    { echo "FAIL: (verb) the STOPPED line does not carry the grammar's reason"; fail=1; }
[ "$(cat "$WD/try.rc")" = 0 ] || { echo "FAIL: (try) an opted-out refusal changed the exit ($(cat "$WD/try.rc"))"; fail=1; }
grep -qE "^LAB TRY \(line 2\) 'arm rays.originCut 2' refused - .+ - the script goes on" "$WD/try.log" ||
    { echo "FAIL: (try) no LAB TRY line saying the refusal"; fail=1; }
[ -e "$WD/data/shots/after-try.png" ] || { echo "FAIL: (try) the line after the opted-out one did not run"; fail=1; }
echo "lab.script_stops: $([ $fail = 0 ] && echo PASS || echo FAIL)"
exit $fail
