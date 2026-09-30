#!/usr/bin/env bash
# devprocess.gpu_lock — THE TIMING ADMISSION HOLDS THE WHOLE CARD (lane DEVPROCESS-1; one admission
# since TEST-SELECTOR-1 G1+G2). scripts/gpu-exclusive.sh is `vram_tokens.py admit all --timing`: a
# timing row takes EVERY VRAM token, so no GPU row — timed or not — shares the card while it
# measures. Judged on timestamps and on the admission's own lines, never on a sleep of fixed length:
#
#   1. EXCLUSION FROM EVERYTHING. Timing holder A takes the card; an ordinary 1-token row U and a
#      second timing row B are started while A holds it, both print their wait, and neither's held
#      interval starts before A's ends. And the other way round: a timing row W started while a
#      1-token row holds a token waits for it (the card DRAINS to the timing row).
#   2. RELEASE. After all of them, every token is free.
#   3. THE BOUNDED WAIT. With a holder in place, a timing row with JAH_VRAM_WAIT=1 exits 75, never
#      runs its command, and prints the NOADMIT line (the run log's verdict for a row that never ran).
#   4. THE EXIT CODE PASSES THROUGH (`false` -> 1), and the command runs as the CHILD of the pid that
#      was started (the admission stays to read the kernel's word after it, TEST-SELECTOR-1 H4).
#   5. A KILLED HOLDER RELEASES: SIGKILL the holder, the tokens are free.
#   6. THE WAIT IS NOT THE ROW'S TIME (LOCK-WAIT-1): the wait is printed (`gpu-lock: waited <s> s`),
#      `--run-timeout` counts from AFTER the admission and is enforced, and scripts/gate_runlog.py
#      reads the wait (lockWaitS) and the NOADMIT verdict.
#
# usage: gpu_lock.sh <scripts/gpu-exclusive.sh> <scratch-dir>
set -u
WRAP="$1"; D="$2"
ADMIT="$(dirname "$WRAP")/gpu-admit.sh"
rm -rf "$D"; mkdir -p "$D"
export JAH_VRAM_DIR="$D/tokens" JAH_VRAM_TOKENS=4
unset JAH_VRAM_HELD JAH_VRAM_ALL
FAILS=0
ok()   { echo "  ok: $*"; }
bad()  { echo "  FAIL: $*"; FAILS=$((FAILS + 1)); }
now()  { date +%s%N; }
# poll with a deadline in POLLS (50 ms each), for a condition — never a fixed-length sleep
waitfor() { local n=0; until eval "$1"; do n=$((n + 1)); [ $n -gt 600 ] && return 1; sleep 0.05; done; }
free_tokens() { python3 "$(dirname "$WRAP")/vram_tokens.py" status | grep -c ' free'; }

cat > "$D/holder.sh" <<'H'
#!/usr/bin/env bash
# holder.sh <name> <dir> <wait-condition...>: runs INSIDE the admission.
name=$1; d=$2; shift 2
echo "$(date +%s%N)" > "$d/$name.start"
echo "${JAH_VRAM_HELD:-0}" > "$d/$name.tokens"
if [ $# -gt 0 ]; then n=0; until eval "$*"; do n=$((n + 1)); [ $n -gt 600 ] && break; sleep 0.05; done; fi
echo "$(date +%s%N)" > "$d/$name.end"
H
chmod +x "$D/holder.sh"

# ---- 1. exclusion from everything ------------------------------------------------------------
"$WRAP" "$D/holder.sh" A "$D" "[ -e '$D/A.release' ]" 2> "$D/A.err" &
PA=$!
waitfor "[ -s '$D/A.start' ]" || bad "timing holder A never started"
[ "$(cat "$D/A.tokens" 2>/dev/null)" = 4 ] && ok "a timing row holds ALL the tokens (4 of 4)" \
                                          || bad "the timing row holds $(cat "$D/A.tokens" 2>/dev/null) tokens, not 4"
now > "$D/U.requested"
"$ADMIT" 1 --label untimed -- "$D/holder.sh" U "$D" 2> "$D/U.err" &
PU=$!
waitfor "grep -q 'vram: waiting' '$D/U.err'" && ok "an untimed 1-token row WAITS while the timing row holds the card" \
                                             || bad "the untimed row did not wait: $(cat "$D/U.err")"
now > "$D/B.requested"
"$WRAP" "$D/holder.sh" B "$D" 2> "$D/B.err" &
PB=$!
sleep 0.3
touch "$D/A.release"
wait $PA; ra=$?; wait $PU; ru=$?; wait $PB; rb=$?
[ $ra = 0 ] && [ $ru = 0 ] && [ $rb = 0 ] || bad "a holder exited non-zero (A $ra, U $ru, B $rb)"
Ae=$(cat "$D/A.end"); Us=$(cat "$D/U.start"); Bs=$(cat "$D/B.start"); Ur=$(cat "$D/U.requested")
[ "$Ur" -lt "$Ae" ] && [ "$Us" -ge "$Ae" ] && ok "U asked during A and started after A ended (no overlap)" \
                                          || bad "OVERLAP or no proof: U requested $Ur, started $Us, A ended $Ae"
[ "$Bs" -ge "$Ae" ] && ok "the second timing row B started after A ended" || bad "OVERLAP: B started at $Bs before A ended at $Ae"

# ...the card drains TO a timing row: a 1-token holder first, then a timing row waits for it
"$ADMIT" 1 --label untimed -- "$D/holder.sh" V "$D" "[ -e '$D/V.release' ]" 2> /dev/null &
PV=$!
waitfor "[ -s '$D/V.start' ]" || bad "untimed holder V never started"
"$WRAP" "$D/holder.sh" W "$D" 2> "$D/W.err" &
PW=$!
waitfor "grep -q 'vram: waiting' '$D/W.err'" && ok "a timing row waits for an untimed row's token (the card drains)" \
                                             || bad "the timing row did not wait for the untimed holder: $(cat "$D/W.err")"
touch "$D/V.release"; wait $PV; wait $PW
[ "$(cat "$D/W.start")" -ge "$(cat "$D/V.end")" ] && ok "...and started only after it ended" \
                                                  || bad "the timing row started beside an untimed holder"

# ---- 2. release -------------------------------------------------------------------------------
[ "$(free_tokens)" = 4 ] && ok "every token is free after all of them" || bad "tokens still held: $(free_tokens) of 4 free"

# ---- 3. the bounded wait ------------------------------------------------------------------------
"$WRAP" "$D/holder.sh" H "$D" "[ -e '$D/C.done' ]" 2> /dev/null &
PH=$!
waitfor "[ -s '$D/H.start' ]" || bad "holder H never started"
JAH_VRAM_WAIT=1 "$WRAP" touch "$D/C.ran" 2> "$D/C.err"; rc=$?
touch "$D/C.done"; wait $PH
[ $rc = 75 ] && ok "a timing row that cannot be admitted in its wait exits 75" || bad "timed-out wait exited $rc, expected 75"
[ ! -e "$D/C.ran" ] && ok "...and never ran its command" || bad "the command ran WITHOUT the admission"
grep -q '^NOADMIT vram:' "$D/C.err" && ok "...and printed the NOADMIT line" || bad "no NOADMIT line: $(cat "$D/C.err")"

# ---- 4. exit code + the row as the admission's child ------------------------------------------
"$WRAP" false 2> /dev/null; rc=$?
[ $rc = 1 ] && ok "the command's exit code passes through (false -> 1)" || bad "false through the wrapper exited $rc"
"$WRAP" bash -c 'echo $PPID > "$0"; exec sleep 30' "$D/E.pid" 2> /dev/null &
PE=$!
waitfor "[ -s '$D/E.pid' ]" || bad "the probe never started"
[ "$(cat "$D/E.pid")" = "$PE" ] && ok "the command runs as the started pid's child (the admission supervises it)" \
                               || bad "the command's parent is $(cat "$D/E.pid"), not the started $PE"

# ---- 5. a killed holder releases ----------------------------------------------------------------
[ "$(free_tokens)" = 0 ] && ok "the running command holds every token" || bad "$(free_tokens) tokens free while the probe runs"
kill -KILL "$PE" 2> /dev/null; wait "$PE" 2> /dev/null
waitfor '[ "$(free_tokens)" = 4 ]' && ok "SIGKILL of the admission frees every token (its child dies with it)" \
                                   || bad "tokens outlived their SIGKILLed holder ($(free_tokens) of 4 free)"

# ---- 6. THE WAIT IS NOT THE ROW'S TIME (LOCK-WAIT-1) --------------------------------------------
# A holder keeps the card ~2 s; a row whose own budget is 1 s still runs its 0.3 s command: the
# budget starts AFTER the admission, and the wait is printed as the one line the run log reads.
"$WRAP" "$D/holder.sh" R0 "$D" "[ -e '$D/R0.release' ]" 2> /dev/null &
PR=$!
waitfor "[ -s '$D/R0.start' ]" || bad "holder R0 never started"
( sleep 2; touch "$D/R0.release" ) &
"$WRAP" --run-timeout 1 --label r:engine bash -c 'sleep 0.3; touch "$0"' "$D/R.ran" 2> "$D/R.err"; rc=$?
wait $PR
waited=$(sed -n 's/^gpu-lock: waited \([0-9.]*\) s$/\1/p' "$D/R.err")
[ $rc = 0 ] && [ -e "$D/R.ran" ] && ok "a row with a 1 s budget ran after a longer wait (the budget starts after the admission)" \
                                 || bad "the queued row did not run to completion (exit $rc)"
awk -v w="${waited:-0}" 'BEGIN { exit !(w >= 1.5) }' && ok "the wait is printed: gpu-lock: waited $waited s" \
                                                     || bad "no or wrong wait line ('$waited'): $(cat "$D/R.err")"
"$WRAP" --run-timeout 1 sleep 5 2> "$D/T.err"; rc=$?
[ $rc = 124 ] && grep -q '^timeout: sending signal' "$D/T.err" && ok "the row's own budget stops it (124, timeout's line)" \
                                                              || bad "a 5 s command in a 1 s budget exited $rc"
python3 - "$(dirname "$WRAP")/gate_runlog.py" "$D/C.err" <<'PY' && ok "the run log parses the wait and the NOADMIT verdict" || bad "the run log does not parse the admission lines"
import importlib.util, sys
spec = importlib.util.spec_from_file_location("gate_runlog", sys.argv[1]); m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
assert m.lock_wait("x\ngpu-lock: waited 812.4 s\ny") == 812.4
assert m.lock_wait("nothing") is None
v, st, _ = m.row_verdict("Failed", open(sys.argv[2]).read(), [])
assert v == "NOADMIT", v
v, st, _ = m.row_verdict("Failed", "gpu-lock: waited 3.0 s\ntimeout: sending signal TERM to command 'x'", [])
assert v == "TIMEOUT", v
PY

rm -rf "$D"
if [ $FAILS -ne 0 ]; then echo "devprocess.gpu_lock: FAILED ($FAILS)"; exit 1; fi
echo "devprocess.gpu_lock: PASSED"
