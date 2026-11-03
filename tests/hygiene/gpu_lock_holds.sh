#!/bin/bash
# tooling.gpu_lock_holds - EVERY HOLD OF THE CARD IS COUNTED ONCE (TEST-STREAMLINE-1 1d): scripts/gpu-exclusive.sh on a PRIVATE
# lock (JAH_GPU_LOCK in a scratch dir, a scratch HOME; no GPU: the commands are sleeps) appends to <lock>.holds a HOLD line when
# it takes the card and a DONE line when it releases it, paired by the ticket:
#   (1) two holds in turn: two HOLD/DONE pairs, the second's wait > 0, each DONE with its command's rc and its held seconds;
#   (2) a nested call under a holder (JAH_GPU_LOCK_HELD) writes nothing of its own;
#   (3) an exit 75 (the card stayed held past --wait-max) writes nothing: it never held;
#   (4) a SIGKILLed holder leaves its HOLD line alone (a kill writes no end) and the next holder's pair follows;
#   (5) ten holders at once: ten well-formed pairs, every ticket once (whole lines from concurrent appends).
# usage: gpu_lock_holds.sh <the tree's scripts/gpu-exclusive.sh>
set -u
LOCKSH=${1:?gpu-exclusive.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/gpu-lock-holds.XXXXXX")
trap 'rm -rf "$T"' EXIT
export HOME="$T" JAH_GPU_LOCK="$T/lock" JAH_GPU_SWEEP_GRACE=1
unset JAH_GPU_LOCK_HELD JAH_GPU_HOLD JAH_GPU_HOLD_LOG
H="$T/lock.holds"
fails=0
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
pairs() {   # the number of tickets with exactly one HOLD and one DONE, every line well formed
    awk -F'\t' '($1 == "HOLD" && NF == 6) { h[$2]++ } ($1 == "DONE" && NF == 6) { d[$2]++ } ($1 != "HOLD" && $1 != "DONE") || NF != 6 { bad++ }
                END { n = 0; for (t in h) if (h[t] == 1 && d[t] == 1) n++; print (bad ? -1 : n) }' "$H" 2> /dev/null || echo 0
}
# (1) two holds in turn
"$LOCKSH" --label A sh -c 'sleep 0.6; exit 3' > /dev/null 2>&1 &
sleep 0.2
"$LOCKSH" --label B sh -c 'exit 0' > /dev/null 2>&1
wait
a=$(awk -F'\t' '$1 == "HOLD" && $6 == "A" { print $2 }' "$H"); b=$(awk -F'\t' '$1 == "HOLD" && $6 == "B" { print $2 }' "$H")
want "$([ "$(pairs)" = 2 ] && [ -n "$a" ] && [ -n "$b" ] && awk -F'\t' -v a="$a" '$1 == "DONE" && $2 == a && $5 == 3' "$H" | grep -q . &&
        awk -F'\t' -v b="$b" '$1 == "HOLD" && $2 == b && $5 > 0' "$H" | grep -q . && awk -F'\t' -v b="$b" '$1 == "DONE" && $2 == b && $5 == 0' "$H" | grep -q . && echo 1)" \
     "(1) two holds in turn: two HOLD/DONE pairs by ticket, A's rc 3, B's wait > 0 and rc 0 ($(tr '\t' ' ' < "$H" | tr '\n' ';'))"
# (2) a nested call under a holder writes nothing of its own
: > "$H"
"$LOCKSH" --label N sh -c "\"$LOCKSH\" --label inner true" > /dev/null 2>&1
want "$([ "$(pairs)" = 1 ] && ! grep -q inner "$H" && echo 1)" "(2) a nested call under a holder: one pair, the outer's alone"
# (3) an exit 75 writes nothing
: > "$H"
"$LOCKSH" --label X sleep 1.5 > /dev/null 2>&1 &
sleep 0.3
"$LOCKSH" --wait-max 1 --label W true > "$T/w.out" 2>&1; rc=$?
wait
want "$([ $rc = 75 ] && ! grep -q "	W$" "$H" && [ "$(pairs)" = 1 ] && echo 1)" "(3) an exit 75: no line of its own (rc $rc), the holder's pair alone"
# (4) a SIGKILLed holder leaves its HOLD line alone
: > "$H"
setsid "$LOCKSH" --label K sleep 30 > /dev/null 2>&1 & K=$!
for _ in $(seq 50); do grep -q "	K$" "$H" 2> /dev/null && break; sleep 0.05; done
kill -9 -- -"$K" 2> /dev/null; wait "$K" 2> /dev/null
"$LOCKSH" --label L true > /dev/null 2>&1
k=$(awk -F'\t' '$1 == "HOLD" && $6 == "K" { print $2 }' "$H")
want "$([ -n "$k" ] && ! awk -F'\t' -v k="$k" '$1 == "DONE" && $2 == k' "$H" | grep -q . && [ "$(pairs)" = 1 ] && echo 1)" \
     "(4) a SIGKILLed holder: its HOLD line alone (no end), the next holder's pair after it"
# (5) ten holders at once
: > "$H"
for i in $(seq 10); do "$LOCKSH" --label "c$i" true > /dev/null 2>&1 & done
wait
want "$([ "$(pairs)" = 10 ] && [ "$(wc -l < "$H")" = 20 ] && echo 1)" "(5) ten holders at once: ten well-formed pairs, every ticket once ($(pairs))"
echo "tooling.gpu_lock_holds: $([ $fails = 0 ] && echo PASS || echo "FAIL ($fails)")"
exit $((fails > 0))
