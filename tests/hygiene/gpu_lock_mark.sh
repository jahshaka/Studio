#!/bin/bash
# tooling.gpu_lock_mark - A ctest UNDER THE CARD's LOCK MARKS ITS TREE (TEST-STREAMLINE-1 block D's collision, 2026-11-03: a hand
# `gpu-exclusive.sh ctest --test-dir <build>` set no check mark, a builder relinked the tree's app 4 min into the run, the call
# void). scripts/gpu-exclusive.sh on a PRIVATE lock, a toy git tree and a fake `ctest` on PATH (no GPU):
#   (1) `ctest --test-dir <build in the tree>`: <gitdir>/jah-check.pid holds the lock's pid while ctest runs, gone after;
#   (2) `nice -n 10 ctest` from a cwd inside the tree (no --test-dir): the same;
#   (3) an OUTER mark of a live ANCESTOR (the check this call runs inside: spec.sh test's, width_pair's): kept through the
#       call and after it, never overwritten, the call runs;
#   (4) a STALE mark (a dead pid): taken, then removed;
#   (5) a command that is not ctest: no mark;
#   (6) an exit 75 (the card stayed held past --wait-max): no mark left behind;
#   (7) a live mark of ANOTHER process (a second check in the tree): REFUSED, exit 2, ctest never run, the mark untouched,
#       no ticket left (the landing audit's LOW, 2026-11-04: check-then-write let a second one overwrite it);
#   (8) TWO marked ctests started together in one tree: the first takes the mark (atomically, under the take's flock), the
#       second is refused (exit 2) - never both marked, never the first's mark removed under it.
# usage: gpu_lock_mark.sh <the tree's scripts/gpu-exclusive.sh>
set -u
LOCKSH=${1:?gpu-exclusive.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/gpu-lock-mark.XXXXXX")
trap 'kill $OUT 2> /dev/null; rm -rf "$T"' EXIT
export HOME="$T" JAH_GPU_LOCK="$T/lock" JAH_GPU_SWEEP_GRACE=1 JAH_GPU_NO_SCOPE=1
unset JAH_GPU_LOCK_HELD JAH_GPU_HOLD JAH_GPU_HOLD_LOG
git init -q "$T/tree" && mkdir -p "$T/tree/build" "$T/bin"
G=$(git -C "$T/tree" rev-parse --absolute-git-dir); M="$G/jah-check.pid"
printf '#!/bin/sh\ncat "%s" > "%s/seen" 2> /dev/null || : > "%s/seen"\n[ -n "${FAKE_CTEST_SLEEP:-}" ] && sleep "$FAKE_CTEST_SLEEP"\nexit 0\n' "$M" "$T" "$T" > "$T/bin/ctest"; chmod +x "$T/bin/ctest"
export PATH="$T/bin:$PATH"
fails=0; OUT=""
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
live_pid_seen() { read -r p t < "$T/seen" 2> /dev/null && [ -n "$p" ] && [ -n "${t:-}" ]; }
# (1)
"$LOCKSH" --label one ctest --test-dir "$T/tree/build" -R x > /dev/null 2>&1
want "$(live_pid_seen && [ ! -e "$M" ] && echo 1)" "(1) ctest --test-dir: the mark held while it ran ($(cat "$T/seen")), gone after"
# (2)
: > "$T/seen"; ( cd "$T/tree/build" && "$LOCKSH" --label two nice -n 10 ctest -R x > /dev/null 2>&1 )
want "$(live_pid_seen && [ ! -e "$M" ] && echo 1)" "(2) nice -n 10 ctest from a cwd in the tree: marked ($(cat "$T/seen")), gone after"
# (3) the outer check: a shell that holds the mark and runs the lock as its child
: > "$T/seen"
bash -c 'echo "$$" > "$1/outer"; echo "$$ 2026-11-03T22:30:00+0100" > "$2"; "$3" --label three ctest --test-dir "$1/tree/build" > /dev/null 2>&1; rc=$?
         read -r q _ < "$2" && [ "$q" = "$$" ] && echo $rc > "$1/outer.rc"' _ "$T" "$M" "$LOCKSH"
want "$(read -r o < "$T/outer" && read -r p _ < "$T/seen" && [ "$p" = "$o" ] && [ "$(cat "$T/outer.rc" 2> /dev/null)" = 0 ] && echo 1)" \
     "(3) an outer live mark of an ancestor: the call ran under it ($(cat "$T/seen")), kept after (rc $(cat "$T/outer.rc" 2> /dev/null))"
rm -f "$M"
# (7) a live mark of another process: refused
sleep 30 & OUT=$!; echo "$OUT 2026-11-03T22:30:00+0100" > "$M"; : > "$T/seen"
"$LOCKSH" --label seven ctest --test-dir "$T/tree/build" > "$T/seven.out" 2>&1; rc=$?
want "$([ $rc = 2 ] && [ ! -s "$T/seen" ] && read -r q _ < "$M" && [ "$q" = "$OUT" ] && grep -q "REFUSED (exit 2): another check is running in this tree" "$T/seven.out" \
        && [ -z "$(ls "$JAH_GPU_LOCK.q" 2> /dev/null | grep -v "^counter$")" ] && echo 1)" \
     "(7) another live check's mark: refused (rc $rc), ctest not run, the mark kept ($(cat "$M")), no ticket left"
kill $OUT 2> /dev/null; wait $OUT 2> /dev/null; OUT=""; rm -f "$M"
# (8) two marked ctests together
FAKE_CTEST_SLEEP=1 "$LOCKSH" --label eight-a ctest --test-dir "$T/tree/build" > /dev/null 2>&1 & A=$!
sleep 0.3
"$LOCKSH" --label eight-b ctest --test-dir "$T/tree/build" > "$T/eight.out" 2>&1; rb=$?
pa=""; read -r pa _ < "$M" 2> /dev/null; wait $A; ra=$?
want "$([ $ra = 0 ] && [ $rb = 2 ] && [ "$pa" = "$A" ] && [ ! -e "$M" ] && echo 1)" \
     "(8) two marked ctests together: the first ran marked (rc $ra, the mark $pa during the second's call), the second refused (rc $rb), gone after"
# (4)
echo "999999 2026-11-03T22:30:00+0100" > "$M"
"$LOCKSH" --label four ctest --test-dir "$T/tree/build" > /dev/null 2>&1
want "$(read -r p _ < "$T/seen" && [ "$p" != 999999 ] && [ -n "$p" ] && [ ! -e "$M" ] && echo 1)" "(4) a stale mark (a dead pid): taken ($(cat "$T/seen")), removed after"
# (5)
rm -f "$M"
"$LOCKSH" --label five sh -c "[ -e '$M' ] && exit 9; exit 0" > /dev/null 2>&1; rc=$?
want "$([ $rc = 0 ] && [ ! -e "$M" ] && echo 1)" "(5) a command that is not ctest: no mark (rc $rc)"
# (6)
rm -f "$M"
"$LOCKSH" --label hold sleep 1.5 > /dev/null 2>&1 & H=$!
sleep 0.3
"$LOCKSH" --wait-max 1 --label six ctest --test-dir "$T/tree/build" > /dev/null 2>&1; rc=$?
wait $H
want "$([ $rc = 75 ] && [ ! -e "$M" ] && echo 1)" "(6) an exit 75: no mark left (rc $rc)"
[ "$fails" = 0 ] && { echo "tooling.gpu_lock_mark: ok"; exit 0; }
echo "tooling.gpu_lock_mark: $fails FAIL"; exit 1
