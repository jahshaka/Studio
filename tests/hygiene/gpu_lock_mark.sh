#!/bin/bash
# tooling.gpu_lock_mark - A ctest UNDER THE CARD's LOCK MARKS ITS TREE (TEST-STREAMLINE-1 block D's collision, 2026-11-03: a hand
# `gpu-exclusive.sh ctest --test-dir <build>` set no check mark, a builder relinked the tree's app 4 min into the run, the call
# void). scripts/gpu-exclusive.sh on a PRIVATE lock, a toy git tree and a fake `ctest` on PATH (no GPU):
#   (1) `ctest --test-dir <build in the tree>`: <gitdir>/jah-check.pid holds the lock's pid while ctest runs, gone after;
#   (2) `nice -n 10 ctest` from a cwd inside the tree (no --test-dir): the same;
#   (3) an OUTER mark of a live pid (a check in flight): kept through the call and after it, never overwritten;
#   (4) a STALE mark (a dead pid): taken, then removed;
#   (5) a command that is not ctest: no mark;
#   (6) an exit 75 (the card stayed held past --wait-max): no mark left behind.
# usage: gpu_lock_mark.sh <the tree's scripts/gpu-exclusive.sh>
set -u
LOCKSH=${1:?gpu-exclusive.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/gpu-lock-mark.XXXXXX")
trap 'kill $OUT 2> /dev/null; rm -rf "$T"' EXIT
export HOME="$T" JAH_GPU_LOCK="$T/lock" JAH_GPU_SWEEP_GRACE=1 JAH_GPU_NO_SCOPE=1
unset JAH_GPU_LOCK_HELD JAH_GPU_HOLD JAH_GPU_HOLD_LOG
git init -q "$T/tree" && mkdir -p "$T/tree/build" "$T/bin"
G=$(git -C "$T/tree" rev-parse --absolute-git-dir); M="$G/jah-check.pid"
printf '#!/bin/sh\ncat "%s" > "%s/seen" 2> /dev/null || : > "%s/seen"\nexit 0\n' "$M" "$T" "$T" > "$T/bin/ctest"; chmod +x "$T/bin/ctest"
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
# (3)
sleep 30 & OUT=$!; echo "$OUT 2026-11-03T22:30:00+0100" > "$M"
"$LOCKSH" --label three ctest --test-dir "$T/tree/build" > /dev/null 2>&1
want "$(read -r p _ < "$T/seen" && [ "$p" = "$OUT" ] && read -r q _ < "$M" && [ "$q" = "$OUT" ] && echo 1)" \
     "(3) an outer live mark: kept during and after the call ($(cat "$M" 2> /dev/null))"
kill $OUT 2> /dev/null; wait $OUT 2> /dev/null; OUT=""; rm -f "$M"
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
