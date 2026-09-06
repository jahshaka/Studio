#!/usr/bin/env bash
#
# log.events — the verbs and the event markers against the real binary
# (SESSION_LOG_SPEC §10 phase 2 gate).
#
# The JavaScript half (e2e_log_events.js) asserts everything readable through
# the verbs. THIS half asserts the artifact on disk, which is the thing a bug
# reporter is asked to send and which no verb can vouch for: the file exists,
# it is bracketed, the blocks are in it in order, the ogre sibling and the
# latest.log pointer are beside it, and the close summary is the last thing in
# it.
#
# --log-dir, not HOME: `HOME=` does not isolate app data on macOS
# (CFFIXED_USER_HOME is what CoreFoundation honours), so the log directory is
# named explicitly and is the same isolation on all three platforms (spec §9-R6).
#
# usage: log_events.sh <jahshaka-binary> <script.js>
# cwd is the scratch run dir (ctest sets it); HOME is the scratch home.
set -u

BIN="$1"
SCRIPT="$2"
LOGDIR="$PWD/logs-events"

rm -rf "$LOGDIR"
mkdir -p "$LOGDIR"

"$BIN" --script "$SCRIPT" --log-dir "$LOGDIR" > run.log 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "log.events: the script run exited $rc"
    tail -60 run.log
    exit 1
fi

SESSION=$(ls "$LOGDIR"/jahshaka-*.log 2>/dev/null | grep -v -- '-ogre\.log$' | head -1)
if [ -z "$SESSION" ]; then
    echo "log.events: FAIL — no session file in $LOGDIR"
    ls -la "$LOGDIR"
    exit 1
fi
echo "log.events: session file $SESSION"

fail=0
have() {   # $1 = grep -E pattern, $2 = what it means
    if grep -Eq "$1" "$SESSION"; then
        echo "log.events: ok — $2"
    else
        echo "log.events: FAIL — $2"
        fail=1
    fi
}

# The script ran to the end. Without this every assertion below could pass
# vacuously on a truncated file.
have "e2e: LOG EVENTS COMPLETE" "the payload script reached its sentinel"

have "Log file open," "the file opens with the session bracket"
have "=== SESSION START ===" "the startup header block is present"
have "=== SCENE OPEN ===" "a scene-open block was written"
have "=== SCENE SAVE ===" "a save block was written"
have "=== PLAY START ===" "a PLAY START bracket was written"
have "=== PLAY STOP ===.*rendered [0-9]+" "PLAY STOP carries its frame-stats delta"
have "\]script: Display: script: .* ok in [0-9]+ ms" "the script-run record was written"
have "\]ui: space: .* -> editor" "the space switch was recorded"

# ORDER, in the file itself. Line numbers, not the ring.
order_ok=1
for pair in "Log file open,:=== SESSION START ===" \
            "=== SESSION START ===:=== SCENE OPEN ===" \
            "=== SCENE OPEN ===:=== SCENE SAVE ===" \
            "=== SCENE SAVE ===:=== PLAY START ===" \
            "=== PLAY START ===:=== PLAY STOP ==="; do
    a="${pair%%:*}"
    b="${pair##*:}"
    la=$(grep -n -m1 -F "$a" "$SESSION" | cut -d: -f1)
    lb=$(grep -n -m1 -F "$b" "$SESSION" | cut -d: -f1)
    if [ -z "$la" ] || [ -z "$lb" ] || [ "$la" -ge "$lb" ]; then
        echo "log.events: FAIL — '$a' (line ${la:-none}) must precede '$b' (line ${lb:-none})"
        order_ok=0
    fi
done
[ "$order_ok" = 1 ] && echo "log.events: ok — the blocks are in chronological order"
[ "$order_ok" = 1 ] || fail=1

# The scene-open block carries the ledger AND the scene stats.
have "=== SCENE OPEN ===.*in [0-9.]+ ms" "the open block states a duration"
have "scene: [0-9]+ nodes .*materials" "the open block carries the scene stats"

# The latest.log pointer (fork F2-B). The per-session OGRE sibling is phase 3's
# and is gated by log.engine, not here.
if [ -L "$LOGDIR/latest.log" ] && [ "$(readlink "$LOGDIR/latest.log")" = "$SESSION" ]; then
    echo "log.events: ok — latest.log points at this session"
else
    echo "log.events: FAIL — latest.log missing or pointing elsewhere"
    ls -la "$LOGDIR"
    fail=1
fi

# The close bracket is LAST. An absent one is the signal that a session died,
# so a suite that does not check its position cannot tell the two apart.
have "=== SESSION SUMMARY ===" "the close summary was written"
if tail -1 "$SESSION" | grep -q "Log file closed,"; then
    echo "log.events: ok — the last line of the file is the close bracket"
else
    echo "log.events: FAIL — the last line is not the close bracket:"
    tail -3 "$SESSION"
    fail=1
fi

# The JS half's own report.
if ! grep -q "log.events: all checks passed" run.log; then
    echo "log.events: FAIL — the script half did not report success"
    tail -40 run.log
    fail=1
fi

exit $fail
