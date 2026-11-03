#!/bin/bash
# tooling.gpu_lock_sweep - THE HOLD's PROCESSES DIE WITH THE HOLD (URGENT #144, 2026-11-02): scripts/gpu-exclusive.sh on a
# PRIVATE lock (JAH_GPU_LOCK in a scratch dir, a scratch HOME; no GPU: the commands are sleeps). Each hold leaves an ESCAPED
# child (setsid: another session and process group, as the Unreal hold's editor take was) and a waiter queued behind it
# records whether that child still lives when IT gets the card:
#   (1) a hold that exits: the child is in the hold's scope (jahgpu-*), swept (TERM), gone before the waiter's grant;
#   (2) a hold that hits its --run-timeout: exit 124, the child swept, gone before the waiter's grant;
#   (3) a child that ignores TERM: KILLED after the grace (JAH_GPU_SWEEP_GRACE=1 here; 10 s by default), gone before the grant;
#   (4) no user manager (JAH_GPU_NO_SCOPE=1): the hold's mark (JAH_GPU_HOLD in /proc/<pid>/environ) finds and sweeps it;
#   (5) a nested call under a holder (JAH_GPU_LOCK_HELD=1: util.CardLock) sweeps its own scope before it returns; one
#       already inside a hold (JAH_GPU_HOLD set: a row's lock call inside ctest) runs as before and leaves it to its hold;
#   (7) the command's arguments arrive verbatim through the scope (`$$`, `${HOME}`: systemd-run expands them unless told not);
#   (6) a clean hold is unchanged - it prints nothing - and the scope's added cost (systemd-run --scope + one
#       systemctl show) is under 0.2 s.
# usage: gpu_lock_sweep.sh <the tree's scripts/gpu-exclusive.sh>
set -u
LOCKSH=${1:?gpu-exclusive.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/gpu-lock-sweep.XXXXXX")
cleanup() { for f in "$T"/*.pid; do [ -f "$f" ] && kill -9 "$(cat "$f")" 2> /dev/null; done; rm -rf "$T"; }
trap cleanup EXIT
export HOME="$T" JAH_GPU_LOCK="$T/lock"
unset JAH_GPU_LOCK_HELD JAH_GPU_HOLD JAH_GPU_NO_SCOPE
# THE ROW's OWN CGROUP (ORPHAN-REDS-1, 2026-11-03): the row may itself run inside a hold's scope - spec.sh / sprint.sh hold
# the card around their ctest through this script, so the row's process tree sits in a jahgpu-* scope of THAT hold (the
# master checks' red at a9e64793c: case (4)'s child, never moved, read as "in a jahgpu- scope"). A case's scope is judged
# against this one: a hold's scope is a jahgpu-* cgroup OTHER than the row's own; the mark leaves the child in the row's own.
OWN_CG=$(cat /proc/self/cgroup)
fails=0
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
gone() { [ -s "$1" ] && ! kill -0 "$(cat "$1")" 2> /dev/null; }
alive() { [ -s "$1" ] && kill -0 "$(cat "$1")" 2> /dev/null; }
# an escaped child: its own session, its pid and cgroup recorded, then a long sleep (a TERM-proof one with `trap`)
esc() { echo "setsid sh -c '${2:-} echo \$\$ > $T/$1.pid; cat /proc/self/cgroup > $T/$1.cg; exec 0<&- 1>&- 2>&-; while :; do sleep 0.2; done' &"; }
waiter() {   # <case>: queue behind the hold, record whether the child lives at the grant
    "$LOCKSH" --label "W$1" sh -c "if kill -0 \$(cat $T/$1.pid) 2> /dev/null; then echo ALIVE; else echo GONE; fi > $T/$1.at_grant" \
        > "$T/W$1.out" 2>&1
}
hold_case() {   # <case> <the hold's extra args...> -- <the hold's tail>: the hold, a waiter behind it; rc in $T/<case>.rc
    local c=$1; shift
    ( "$LOCKSH" --label "H$c" "$@"; echo $? > "$T/$c.rc" ) > "$T/H$c.out" 2>&1 &
    local h=$!
    for _ in $(seq 50); do [ -s "$T/$c.pid" ] && break; sleep 0.1; done
    waiter "$c" & local w=$!
    wait "$h" "$w"
}
# (1) a hold that exits with its escaped child alive
hold_case 1 sh -c "$(esc 1) sleep 0.3"
want "$(grep -q 'jahgpu-' "$T/1.cg" && [ "$(cat "$T/1.cg")" != "$OWN_CG" ] && echo 1)" "(1) the escaped child sat in the hold's own scope ($(sed 's|.*/||' "$T/1.cg"); the row's own: ${OWN_CG##*/})"
want "$(gone "$T/1.pid" && [ "$(cat "$T/1.at_grant")" = GONE ] && [ "$(cat "$T/1.rc")" = 0 ] \
        && grep -q "gpu-lock: swept the hold's leftover process(es): .* (TERM) - the card released after them" "$T/H1.out" && echo 1)" \
     "(1) a hold that exits: its escaped child swept (TERM) and gone before the waiter's grant, rc 0 kept ($(cat "$T/1.at_grant") $(cat "$T/1.rc"); $(tail -1 "$T/H1.out"))"
# (2) a hold that hits its bound
hold_case 2 --run-timeout 1 sh -c "$(esc 2) sleep 600"
want "$(gone "$T/2.pid" && [ "$(cat "$T/2.at_grant")" = GONE ] && [ "$(cat "$T/2.rc")" = 124 ] && echo 1)" \
     "(2) a hold at its --run-timeout: exit 124, its escaped child gone before the waiter's grant ($(cat "$T/2.at_grant") $(cat "$T/2.rc"))"
# (3) a child that ignores TERM
JAH_GPU_SWEEP_GRACE=1 hold_case 3 sh -c "$(esc 3 'trap : TERM;') sleep 0.3"
want "$(gone "$T/3.pid" && [ "$(cat "$T/3.at_grant")" = GONE ] && grep -q "(TERM, KILL after 1 s)" "$T/H3.out" && echo 1)" \
     "(3) a TERM-proof child KILLED after the grace, gone before the waiter's grant ($(cat "$T/3.at_grant"); $(tail -1 "$T/H3.out"))"
# (4) no user manager: the mark
JAH_GPU_NO_SCOPE=1 hold_case 4 sh -c "$(esc 4) sleep 0.3"
want "$(gone "$T/4.pid" && [ "$(cat "$T/4.at_grant")" = GONE ] && [ "$(cat "$T/4.cg")" = "$OWN_CG" ] && grep -q "swept" "$T/H4.out" && echo 1)" \
     "(4) no scope (JAH_GPU_NO_SCOPE): the child stayed in the row's own cgroup, the hold's mark found it, swept before the waiter's grant ($(cat "$T/4.at_grant"); ${OWN_CG##*/})"
# (5) nested: under a holder (no lock taken) the call sweeps its own; inside a hold it leaves it to the hold
JAH_GPU_LOCK_HELD=1 "$LOCKSH" sh -c "$(esc 5) sleep 0.2" > "$T/H5.out" 2>&1
want "$(gone "$T/5.pid" && grep -q 'jahgpu-n' "$T/5.cg" && [ "$(cat "$T/5.cg")" != "$OWN_CG" ] && grep -q swept "$T/H5.out" && echo 1)" \
     "(5) a nested call under a holder (util.CardLock) swept its own scope before it returned"
JAH_GPU_LOCK_HELD=1 JAH_GPU_HOLD=outer "$LOCKSH" sh -c "$(esc 6) sleep 0.2" > "$T/H6.out" 2>&1
for _ in $(seq 20); do [ -s "$T/6.pid" ] && break; sleep 0.1; done
want "$(alive "$T/6.pid" && [ ! -s "$T/H6.out" ] && echo 1)" "(5) a call inside a hold runs as before: its child left to the hold's sweep"
kill -9 "$(cat "$T/6.pid")" 2> /dev/null
# (7) the arguments verbatim
"$LOCKSH" --label V sh -c 'printf "%s\n" "$1"' sh 'a$$b ${HOME} $X' > "$T/V.out" 2>&1
want "$([ "$(cat "$T/V.out")" = 'a$$b ${HOME} $X' ] && echo 1)" "(7) the command's arguments arrive verbatim through the scope ($(cat "$T/V.out"))"
# (6) a clean hold: silent, and the scope's own cost
"$LOCKSH" --label C true > "$T/C.out" 2>&1
want "$([ ! -s "$T/C.out" ] && echo 1)" "(6) a clean hold prints nothing ($(head -c 120 "$T/C.out"))"
if command -v systemd-run > /dev/null 2>&1; then
    a=$(date +%s.%N)
    for i in 1 2 3 4 5; do
        systemd-run --user --scope --quiet --collect --expand-environment=no --unit="jahgpu-cost-$$-$i" -- true
        systemctl --user show -p ControlGroup --value "jahgpu-cost-$$-$i.scope" > /dev/null
    done
    cost=$(awk -v a="$a" -v b="$(date +%s.%N)" 'BEGIN{printf "%.3f", (b-a)/5}')
    want "$(awk -v c="$cost" 'BEGIN{print (c < 0.2) ? 1 : 0}')" "(6) the scope's added cost on a clean hold: $cost s (< 0.2 s)"
fi
[ "$fails" = 0 ] && { echo "tooling.gpu_lock_sweep: PASS"; exit 0; }
echo "tooling.gpu_lock_sweep: FAILED ($fails)"; exit 1
