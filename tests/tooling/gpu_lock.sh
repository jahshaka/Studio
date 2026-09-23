#!/usr/bin/env bash
# devprocess.gpu_lock — two wrapped commands started concurrently never HOLD the lock at the same
# time (lane DEVPROCESS-1). Judged on TIMESTAMPS and on the kernel's own lock table, never on a
# sleep of fixed length:
#
#   1. EXCLUSION. Holder A takes the lock, then waits until /proc/locks shows a SECOND process
#      BLOCKED on the lock file's inode (B, started after A) — the kernel's word that B is queued,
#      not a guess about timing. Only then does A end. B's held interval must start at or after
#      A's end, B must have REQUESTED before A ended (or the test proved nothing), and a
#      `flock -n` probe from inside each holder must fail (the lock is really held).
#   2. RELEASE. After both, `flock -n` succeeds.
#   3. THE BOUNDED WAIT. With a holder in place, a wrapped command with JAH_GPU_LOCK_WAIT=1 exits
#      75 and never runs its command.
#   4. THE EXIT CODE PASSES THROUGH (`false` -> 1), and the command runs as the pid that was
#      started (exec, not a child of flock: a ctest timeout must kill the suite itself).
#   5. A KILLED HOLDER RELEASES: SIGKILL the holder, the lock is free.
#   6. The default lock is /tmp/jah-gpu-timing.lock (one box-wide file).
#
# usage: gpu_lock.sh <scripts/gpu-exclusive.sh> <scratch-dir>
set -u
WRAP="$1"; D="$2"
rm -rf "$D"; mkdir -p "$D"
export JAH_GPU_LOCK="$D/lock"
: > "$JAH_GPU_LOCK"
FAILS=0
ok()   { echo "  ok: $*"; }
bad()  { echo "  FAIL: $*"; FAILS=$((FAILS + 1)); }
now()  { date +%s%N; }
# poll with a deadline in POLLS (50 ms each), for a condition — never a fixed-length sleep
waitfor() { local n=0; until eval "$1"; do n=$((n + 1)); [ $n -gt 600 ] && return 1; sleep 0.05; done; }
inode=$(stat -c %i "$JAH_GPU_LOCK")

cat > "$D/holder.sh" <<'H'
#!/usr/bin/env bash
# holder.sh <name> <dir> <wait-condition...>: runs INSIDE the lock.
name=$1; d=$2; shift 2
echo "$(date +%s%N)" > "$d/$name.start"
if flock -n "$JAH_GPU_LOCK" true; then echo 0 > "$d/$name.held"; else echo 1 > "$d/$name.held"; fi
if [ $# -gt 0 ]; then n=0; until eval "$*"; do n=$((n + 1)); [ $n -gt 600 ] && break; sleep 0.05; done; fi
echo "$(date +%s%N)" > "$d/$name.end"
H
chmod +x "$D/holder.sh"
export inode

# ---- 1. exclusion -----------------------------------------------------------
"$WRAP" "$D/holder.sh" A "$D" "grep -E -- '-> FLOCK .*:$inode ' /proc/locks > /dev/null 2>&1" 2> "$D/A.err" &
PA=$!
waitfor "[ -s '$D/A.start' ]" || bad "holder A never started"
now > "$D/B.requested"
"$WRAP" "$D/holder.sh" B "$D" 2> "$D/B.err" &
PB=$!
wait $PA; ra=$?; wait $PB; rb=$?
[ $ra = 0 ] && [ $rb = 0 ] || bad "a holder exited non-zero (A $ra, B $rb)"
for f in A.start A.end B.start B.end B.requested; do [ -s "$D/$f" ] || bad "missing $f"; done
As=$(cat "$D/A.start"); Ae=$(cat "$D/A.end"); Bs=$(cat "$D/B.start"); Be=$(cat "$D/B.end"); Br=$(cat "$D/B.requested")
echo "  A held [$As, $Ae]  B requested $Br, held [$Bs, $Be]  (ns)"
[ "$Br" -lt "$Ae" ] && ok "B asked for the lock while A held it (requested $(( (Ae - Br) / 1000000 )) ms before A's end)" \
                    || bad "B requested after A ended — the run proved nothing"
[ "$Bs" -ge "$Ae" ] && ok "B's held interval starts after A's ends (gap $(( (Bs - Ae) / 1000000 )) ms): no overlap" \
                    || bad "OVERLAP: B started at $Bs before A ended at $Ae"
[ "$(cat "$D/A.held")" = 1 ] && [ "$(cat "$D/B.held")" = 1 ] && ok "a flock -n probe fails inside each holder (the lock is really held)" \
                    || bad "a probe inside a holder took the lock (A $(cat "$D/A.held"), B $(cat "$D/B.held"))"
grep -q "timeout" "$D/A.err" "$D/B.err" && bad "a holder timed out"

# ---- 2. release -------------------------------------------------------------
flock -n "$JAH_GPU_LOCK" true && ok "the lock is free after both" || bad "the lock is still held after both exited"

# ---- 3. the bounded wait ----------------------------------------------------
"$WRAP" "$D/holder.sh" H "$D" "[ -e '$D/C.done' ]" 2> /dev/null &
PH=$!
waitfor "[ -s '$D/H.start' ]" || bad "holder H never started"
JAH_GPU_LOCK_WAIT=1 "$WRAP" touch "$D/C.ran" 2> "$D/C.err"; rc=$?
touch "$D/C.done"; wait $PH
[ $rc = 75 ] && ok "a wrapped command that cannot get the lock in its wait exits 75" || bad "timed-out wait exited $rc, expected 75"
[ ! -e "$D/C.ran" ] && ok "...and never ran its command" || bad "the command ran WITHOUT the lock"

# ---- 4. exit code + exec ------------------------------------------------------
"$WRAP" false 2> /dev/null; rc=$?
[ $rc = 1 ] && ok "the command's exit code passes through (false -> 1)" || bad "false through the wrapper exited $rc"
"$WRAP" bash -c 'echo $$ > "$0"; exec sleep 30' "$D/E.pid" 2> /dev/null &
PE=$!
waitfor "[ -s '$D/E.pid' ]" || bad "the exec probe never started"
[ "$(cat "$D/E.pid")" = "$PE" ] && ok "the command runs AS the started pid (exec in place, not a child of flock)" \
                               || bad "the command is pid $(cat "$D/E.pid"), not the started $PE"

# ---- 5. a killed holder releases --------------------------------------------
flock -n "$JAH_GPU_LOCK" true && bad "the lock was free while the exec probe held it" || ok "the running command holds the lock"
kill -KILL "$PE" 2> /dev/null; wait "$PE" 2> /dev/null
flock -n "$JAH_GPU_LOCK" true && ok "SIGKILL of the holder releases the lock" || bad "the lock outlived its SIGKILLed holder"

# ---- 6. the default path ------------------------------------------------------
grep -q 'JAH_GPU_LOCK:-/tmp/jah-gpu-timing.lock' "$WRAP" && ok "the default lock is /tmp/jah-gpu-timing.lock" \
                                                     || bad "the wrapper's default lock is not /tmp/jah-gpu-timing.lock"

rm -rf "$D"
if [ $FAILS -ne 0 ]; then echo "devprocess.gpu_lock: FAILED ($FAILS)"; exit 1; fi
echo "devprocess.gpu_lock: PASSED"
