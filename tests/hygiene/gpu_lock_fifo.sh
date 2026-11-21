#!/bin/bash
# tooling.gpu_lock_fifo - THE CARD's LOCK IS FIRST COME, FIRST SERVED (URGENT #113, HARNESS-3): scripts/gpu-exclusive.sh on a
# PRIVATE lock (JAH_GPU_LOCK in a scratch dir, a scratch HOME; no GPU: the commands are sleeps):
#   (0) an empty queue prints nothing;
#   (1) a holder, five waiters arriving in order, a SHORT one arriving late: the grants in ticket order
#       (flock alone has no order: the late short one could win the release);
#   (2) a ticket whose pid is dead ahead of the queue is skipped and removed (a killed waiter blocks nobody);
#   (3) --wait-max still bounds the whole wait, the queue's included: exit 75, nothing run, its ticket gone, and the
#       waiter behind it served;
#   (4) no ticket left behind (an expired reservation removed by the next call);
#   (5) A BOUNDED WAITER KEEPS ITS PLACE: exit 75, a later arrival queues, the re-call under the same label is served
#       first (URGENT #113's loss: each 75 sent its re-call to the back of the line).
#   (7) SHORT HOLDS PASS (CARD-QUEUE-1, CODING_RULES § 7): a --short waiter is granted before a long one that queued first; its
#       ticket <n>.s<s>, the HOLD lines' 7th field the class, the passed long's credit file written;
#   (8) the fairness law: past the long's credit (a toy allowance, JAH_GPU_PASS_ALLOW_S=2) the next short waits behind it;
#   (9) an OVERRUN is logged and said, never cut, and its caller's next --short holds long; --short outside 1-90 is 64;
#       --report lists the short holds and the overrun. With no short ticket, (1)-(6) are the grant order, unchanged.
# THE CASES RUN SIDE BY SIDE, EACH ON ITS OWN PRIVATE LOCK (V2 plan 0.2's source.row_budget red, 2026-11-21: run one after
# another its holds summed 9 s of sleeps and the row read 11.6 s solo against its 10-s class budget): every law here is a law of
# ONE lock's queue, so seven locks - (1); (2); (3)+(5)+(4), which share one because (4) reads what (3) and (5) left; (6); (7); (8);
# (9) - are independent proofs at once. Each keeps its own sleeps as its margins (CODING_RULES §6), none shortened; the row's wall is the
# longest chain's.
# usage: gpu_lock_fifo.sh <the tree's scripts/gpu-exclusive.sh>
set -u
LOCKSH=${1:?gpu-exclusive.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/gpu-lock-fifo.XXXXXX")
trap 'rm -rf "$T"' EXIT
export HOME="$T" JAH_GPU_LOCK="$T/lock"
unset JAH_GPU_LOCK_HELD
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fi; }   # (counted from the chains' outputs)
log=$T/grants
# (0) AN EMPTY QUEUE PRINTS NOTHING (8B's master check: an unmatched glob's redirect printed "No such file" once)
"$LOCKSH" --label E true > "$T/E.out" 2>&1
want "$([ ! -s "$T/E.out" ] && echo 1)" "(0) an empty queue: the lock prints nothing ($(head -c 120 "$T/E.out"))" > "$T/c0.out"
cat "$T/c0.out"
chain1() {
# (1) THE ORDER: five waiters, then the late short one (measured: the kernel itself served blocked flock waiters in
# arrival order 4 runs of 4 - (1) guards the queue's order; (0), (2), (4) and (5) are red on the plain flock)
"$LOCKSH" --label H sh -c "echo H >> '$log'; sleep 1.5" > "$T/H.out" 2>&1 &
sleep 0.3
for w in W1 W2 W3 W4 W5; do
    "$LOCKSH" --label "$w" sh -c "echo $w >> '$log'" > "$T/$w.out" 2>&1 &
    sleep 0.12
done
"$LOCKSH" --label L sh -c "echo L >> '$log'" > "$T/L.out" 2>&1 &
wait
order=$(tr '\n' ' ' < "$log")
echo "   the grants: $order"
want "$([ "$order" = "H W1 W2 W3 W4 W5 L " ] && echo 1)" "(1) the grants in ticket order: a holder, five waiters, a late short one last"
want "$(grep -q 'gpu-lock: waiting for the card (held by: pid [0-9]* H since' "$T/W1.out" && grep -qE '^gpu-lock: waited [0-9]+(\.[0-9])? s$' "$T/W1.out" && echo 1)" \
     "(1) a waiter's two lines unchanged: 'waiting for the card (held by: …)' and 'waited N.N s' (tenths, #95)"
}
chain2() {
# (2) A DEAD TICKET AHEAD
dead=$(sh -c 'echo $$')
mkdir -p "$T/lock2.q"
echo "$dead dead" > "$T/lock2.q/000000000000"
t0=$(date +%s%N)
"$LOCKSH" --label D true > "$T/D.out" 2>&1
rd=$?
ms=$(( ($(date +%s%N) - t0) / 1000000 ))
want "$([ "$rd" -eq 0 ] && [ "$ms" -lt 3000 ] && [ ! -e "$T/lock2.q/000000000000" ] && echo 1)" \
     "(2) a dead pid's ticket ahead is skipped and removed ($ms ms)"
}
chain35() {
# (3) THE BOUND: --wait-max bounds the whole wait (the queue's too); its reservation never idles the card
"$LOCKSH" --label H2 sleep 2 > "$T/H2.out" 2>&1 &
sleep 0.4
JAH_GPU_RESERVE_S=1 "$LOCKSH" --wait-max 1 --label B sh -c "echo B >> '$T/ran'" > "$T/B.out" 2>&1 &
pb=$!
sleep 0.3
"$LOCKSH" --label A sh -c "echo A >> '$T/ran'" > "$T/A.out" 2>&1 &
wait "$pb"; rb=$?
wait
want "$([ "$rb" -eq 75 ] && grep -q 'gpu-lock: the card stayed held 1 s' "$T/B.out" && [ "$(cat "$T/ran" 2>/dev/null)" = A ] && echo 1)" \
     "(3) --wait-max 1 behind a 2-s hold: exit 75 ($rb), nothing run; the waiter behind it served (the reservation passed over)"
# (5) THE PLACE KEPT ACROSS AN EXIT 75 (the loss URGENT #113 measured: a re-call went to the back of the line)
: > "$T/ran2"
"$LOCKSH" --label H3 sleep 2 > "$T/H3.out" 2>&1 &
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
# (4) NOTHING LEFT (the expired reservation removed by the next call's scan: (3)'s 1-s reservation expired during (5))
"$LOCKSH" true > /dev/null 2>&1
left=$(ls "$T/lock3.q" | grep -c '^[0-9]' || true)
want "$([ "$left" -eq 0 ] && echo 1)" "(4) no ticket left behind ($left)"
}
chain6() {
# (6) AN UNTICKETED HOLDER IS NAMED (URGENT #141): an old copy's hold - a bare flock and a holder line with no ticketed
# mark - makes the waiter print ONE loud line naming it and log it to <lock>.log; a ticketed holder is never named
( exec 9>>"$T/lock6"; flock 9; echo "pid $BASHPID ue-old-copy since now" > "$T/lock6.holder"; sleep 2 ) &
sleep 0.4
"$LOCKSH" --label W true > "$T/W.out" 2>&1
wait
n=$(grep -c 'AN UNTICKETED HOLDER: pid [0-9]* ue-old-copy - an old gpu-exclusive.sh copy' "$T/W.out")
want "$([ "$n" = 1 ] && grep -q "waiter 'W'.*AN UNTICKETED HOLDER: pid [0-9]* ue-old-copy" "$T/lock6.log" && echo 1)" \
     "(6) an unticketed holder (an old copy's bare flock) named ONCE by the waiting head and in <lock>.log ($n)"
"$LOCKSH" --label H6 sleep 1.5 > /dev/null 2>&1 &
sleep 0.4
"$LOCKSH" --label W2 true > "$T/W2.out" 2>&1
wait
want "$(grep -q 'held by: pid [0-9]* H6 since .* ticketed' "$T/W2.out" && ! grep -q 'UNTICKETED' "$T/W2.out" && echo 1)" \
     "(6) a ticketed holder (this script) is never named: its holder line carries the mark"
}
chain7() {
# (7) A SHORT HOLD PASSES A WAITING LONG ONE (CARD-QUEUE-1, CODING_RULES § 7 SHORT HOLDS PASS): a holder, a long waiter, then a
# --short waiter: the short is granted before the long one that queued first; its ticket is named .s<s>; the HOLD lines carry the
# class (the 7th field); the long's credit file holds what passed it (red on the old script: plain first come, first served)
local g=$T/grants7
"$LOCKSH" --label H7 sh -c "echo H7 >> '$g'; sleep 1.2" > /dev/null 2>&1 &
sleep 0.3
"$LOCKSH" --label L7 sh -c "echo L7 >> '$g'" > "$T/L7.out" 2>&1 &
sleep 0.3
"$LOCKSH" --short 5 --label S7 sh -c "echo S7 >> '$g'; ls '$JAH_GPU_LOCK.q' > '$T/q7'" > "$T/S7.out" 2>&1 &
wait
want "$([ "$(tr '\n' ' ' < "$g")" = "H7 S7 L7 " ] && echo 1)" "(7) a short hold passes a waiting long one: $(tr '\n' ' ' < "$g")(want H7 S7 L7)"
want "$(grep -qP '^HOLD\t\d{12}\.s5\t\d+\t\S+\t[0-9.]+\tS7\tshort:5$' "$JAH_GPU_LOCK.holds" && grep -qP '^HOLD\t\d{12}\t\d+\t\S+\t[0-9.]+\tL7\tlong$' "$JAH_GPU_LOCK.holds" && echo 1)" \
     "(7) the hold log: the short's ticket <n>.s5 and class short:5, the long's class long (7 fields)"
want "$(cat "$JAH_GPU_LOCK.q/.credit/"* 2> /dev/null | grep -qE '^0 [01] 1$' && echo 1)" \
     "(7) the passed long's credit: 'held passed count' = 0 <the short's actual s> 1 ($(cat "$JAH_GPU_LOCK.q/.credit/"* 2> /dev/null))"
want "$(! grep -q 'No such file' "$T/S7.out" "$T/L7.out" && echo 1)" \
     "(7) a waiter whose long ticket has no credit yet prints nothing of it (a missing credit file is read silently: $(grep -c 'No such file' "$T/S7.out") line(s))"
}
chain8() {
# (8) THE FAIRNESS LAW: a long ticket is passed only while passed + s <= its allowance (+ 15 % of its own held s): with a toy
# allowance of 2 s (JAH_GPU_PASS_ALLOW_S) the first short (declared 2 s, holds ~1 s) passes the waiting long, the second
# (2 more s: over the allowance) waits behind it
local g=$T/grants8
export JAH_GPU_PASS_ALLOW_S=2
"$LOCKSH" --label H8 sh -c "echo H8 >> '$g'; sleep 1.2" > /dev/null 2>&1 &
sleep 0.3
"$LOCKSH" --label L8 sh -c "echo L8 >> '$g'" > /dev/null 2>&1 &
sleep 0.3
"$LOCKSH" --short 2 --label S8a sh -c "echo S8a >> '$g'; sleep 1.1" > /dev/null 2>&1 &
sleep 0.3
"$LOCKSH" --short 2 --label S8b sh -c "echo S8b >> '$g'" > /dev/null 2>&1 &
wait
want "$([ "$(tr '\n' ' ' < "$g")" = "H8 S8a L8 S8b " ] && echo 1)" \
     "(8) the fairness law: one short within the long's credit passes it, the next waits behind it: $(tr '\n' ' ' < "$g")(want H8 S8a L8 S8b)"
}
chain9() {
# (9) OVERRUN: a short hold past its declared time is logged (an OVERRUN line), said, never cut (its command ends on its own),
# and its CALLER loses the class: the same caller's next --short holds long, said; --short outside 1-90 is refused (64)
sh -c "\"$LOCKSH\" --short 1 --label O9 sleep 2.2; \"$LOCKSH\" --short 5 --label P9 true" > "$T/O9.out" 2>&1
"$LOCKSH" --short 91 --label X9 true > "$T/X9.out" 2>&1; rx=$?
"$LOCKSH" --short abc --label X9 true > /dev/null 2>&1; ry=$?
want "$(grep -qP '^OVERRUN\t\d{12}\.s1\t\d+\t1\t[23]\tO9$' "$JAH_GPU_LOCK.holds" && grep -q "OVERRUN - 'O9' declared a short hold of 1 s" "$T/O9.out" \
        && grep -q 'NOT HONOURED' "$T/O9.out" && grep -qP '^HOLD\t\d{12}\t\d+\t\S+\t[0-9.]+\tP9\tlong$' "$JAH_GPU_LOCK.holds" && echo 1)" \
     "(9) an overrun: its OVERRUN line and its loud line, the command not cut; the caller's next --short held long"
want "$([ $rx = 64 ] && [ $ry = 64 ] && grep -q 'takes the hold.s declared seconds, 1 to 90' "$T/X9.out" && echo 1)" "(9) --short 91 / abc refused: 64 ($rx/$ry)"
"$LOCKSH" --report > "$T/R9.out" 2>&1
want "$(grep -q '^SHORT HOLDS - 1: ' "$T/R9.out" && grep -q 'OVERRUNS (a fault of the caller) - 1:' "$T/R9.out" && grep -q 'declared 1 s, held [23] s .* O9' "$T/R9.out" && echo 1)" \
     "(9) --report: the one short hold (P9 held long), the overrun listed ($(grep -E '^SHORT|declared' "$T/R9.out" | tr '\n' '|'))"
}
for c in 1 2 35 6 7 8 9; do ( export JAH_GPU_LOCK="$T/lock${c:0:1}"; "chain$c" ) > "$T/chain$c.out" 2>&1 & done
wait
cat "$T/chain1.out" "$T/chain2.out" "$T/chain35.out" "$T/chain6.out" "$T/chain7.out" "$T/chain8.out" "$T/chain9.out"
fails=$(cat "$T"/chain*.out "$T/c0.out" 2>/dev/null | grep -c '^  FAIL')
echo "tooling.gpu_lock_fifo: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
exit $(( fails > 0 ))
