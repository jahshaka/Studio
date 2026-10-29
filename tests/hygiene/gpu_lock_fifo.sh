#!/bin/bash
# tooling.gpu_lock_fifo - THE CARD's LOCK IS FIRST COME, FIRST SERVED (URGENT #113, HARNESS-3): scripts/gpu-exclusive.sh on a
# PRIVATE lock (JAH_GPU_LOCK in a scratch dir, a scratch HOME; no GPU: the commands are sleeps):
#   (0) an empty queue prints nothing;
#   (1) a holder, eight waiters arriving in order, a SHORT one arriving late: the grants in ticket order
#       (flock alone has no order: the late short one could win the release);
#   (2) a ticket whose pid is dead ahead of the queue is skipped and removed (a killed waiter blocks nobody);
#   (3) --wait-max still bounds the whole wait, the queue's included: exit 75, nothing run, its ticket gone, and the
#       waiter behind it served;
#   (4) no ticket left behind (an expired reservation removed by the next call);
#   (5) A BOUNDED WAITER KEEPS ITS PLACE: exit 75, a later arrival queues, the re-call under the same label is served
#       first (URGENT #113's loss: each 75 sent its re-call to the back of the line).
# usage: gpu_lock_fifo.sh <the tree's scripts/gpu-exclusive.sh>
set -u
LOCKSH=${1:?gpu-exclusive.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/gpu-lock-fifo.XXXXXX")
trap 'rm -rf "$T"' EXIT
export HOME="$T" JAH_GPU_LOCK="$T/lock"
unset JAH_GPU_LOCK_HELD
fails=0
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
log=$T/grants
# (0) AN EMPTY QUEUE PRINTS NOTHING (8B's master check: an unmatched glob's redirect printed "No such file" once)
"$LOCKSH" --label E true > "$T/E.out" 2>&1
want "$([ ! -s "$T/E.out" ] && echo 1)" "(0) an empty queue: the lock prints nothing ($(head -c 120 "$T/E.out"))"
# (1) THE ORDER: eight waiters (a release wakes every flock waiter at once and the first one SCHEDULED wins - with
# eight the race shows), then the late short one
"$LOCKSH" --label H sh -c "echo H >> '$log'; sleep 2" > "$T/H.out" 2>&1 &
sleep 0.5
for w in W1 W2 W3 W4 W5 W6 W7 W8; do
    "$LOCKSH" --label "$w" sh -c "echo $w >> '$log'; sleep 0.2" > "$T/$w.out" 2>&1 &
    sleep 0.15
done
"$LOCKSH" --label L sh -c "echo L >> '$log'" > "$T/L.out" 2>&1 &
wait
order=$(tr '\n' ' ' < "$log")
echo "   the grants: $order"
want "$([ "$order" = "H W1 W2 W3 W4 W5 W6 W7 W8 L " ] && echo 1)" "(1) the grants in ticket order: a holder, eight waiters, a late short one last"
want "$(grep -q 'gpu-lock: waiting for the card (held by: pid [0-9]* H since' "$T/W1.out" && grep -q '^gpu-lock: waited [0-9]* s$' "$T/W1.out" && echo 1)" \
     "(1) a waiter's two lines unchanged: 'waiting for the card (held by: …)' and 'waited N s'"
# (2) A DEAD TICKET AHEAD
dead=$(sh -c 'echo $$')
mkdir -p "$T/lock.q"
echo "$dead dead" > "$T/lock.q/000000000000"
t0=$(date +%s%N)
"$LOCKSH" --label D true > "$T/D.out" 2>&1
rd=$?
ms=$(( ($(date +%s%N) - t0) / 1000000 ))
want "$([ "$rd" -eq 0 ] && [ "$ms" -lt 3000 ] && [ ! -e "$T/lock.q/000000000000" ] && echo 1)" \
     "(2) a dead pid's ticket ahead is skipped and removed ($ms ms)"
# (3) THE BOUND: --wait-max bounds the whole wait (the queue's too); its reservation never idles the card
"$LOCKSH" --label H2 sleep 3 > "$T/H2.out" 2>&1 &
sleep 0.4
JAH_GPU_RESERVE_S=1 "$LOCKSH" --wait-max 1 --label B sh -c "echo B >> '$T/ran'" > "$T/B.out" 2>&1 &
pb=$!
sleep 0.3
"$LOCKSH" --label A sh -c "echo A >> '$T/ran'" > "$T/A.out" 2>&1 &
wait "$pb"; rb=$?
wait
want "$([ "$rb" -eq 75 ] && grep -q 'gpu-lock: the card stayed held 1 s' "$T/B.out" && [ "$(cat "$T/ran" 2>/dev/null)" = A ] && echo 1)" \
     "(3) --wait-max 1 behind a 3-s hold: exit 75 ($rb), nothing run; the waiter behind it served (the reservation passed over)"
# (5) THE PLACE KEPT ACROSS AN EXIT 75 (the loss URGENT #113 measured: a re-call went to the back of the line)
: > "$T/ran2"
"$LOCKSH" --label H3 sleep 3 > "$T/H3.out" 2>&1 &
sleep 0.4
"$LOCKSH" --wait-max 1 --label C sh -c "echo C >> '$T/ran2'" > "$T/C1.out" 2>&1
rc1=$?
"$LOCKSH" --label N sh -c "echo N >> '$T/ran2'" > "$T/N.out" 2>&1 &
sleep 0.5
"$LOCKSH" --label C sh -c "echo C >> '$T/ran2'" > "$T/C2.out" 2>&1 &
wait
order2=$(tr '\n' ' ' < "$T/ran2")
echo "   after the 75: $order2"
want "$([ "$rc1" -eq 75 ] && [ "$order2" = "C N " ] && echo 1)" \
     "(5) a bounded waiter's re-call under its label keeps its place: served before a later arrival ($rc1; $order2)"
# (4) NOTHING LEFT (the expired reservation removed by the next call's scan)
sleep 1.2
"$LOCKSH" true > /dev/null 2>&1
left=$(ls "$T/lock.q" | grep -c '^[0-9]' || true)
want "$([ "$left" -eq 0 ] && echo 1)" "(4) no ticket left behind ($left)"
echo "tooling.gpu_lock_fifo: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
exit $(( fails > 0 ))
