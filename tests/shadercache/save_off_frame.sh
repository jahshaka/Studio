#!/bin/bash
# shadercache.save_off_frame's driver: the scripted run COLD (the fresh data root fresh_home.sh made), then WARM on the
# same root (the cold run's clean-quit save loaded). Usage: save_off_frame.sh <Jahshaka> <e2e_save_off_frame.js> [--timing]
# --timing (shadercache.save_off_frame.timing, a measure row): also the milliseconds the script prints — every message of
# every save <= 2 ms, every chunk <= 1 ms — read from its SAVE_OFF_FRAME line.
set -u
APP="${1:?the Jahshaka binary}" SCRIPT="${2:?the script}" TIMING="${3:-}"
fails=0
for run in cold warm; do
    out="$(JAH_FREE_RUNNING=1 "$APP" --script "$SCRIPT" --script-live 2>&1)"; rc=$?
    echo "$out" | grep -E "^(ok:|SAVE_OFF_FRAME|save_off_frame|.*assert failed|Jahshaka shader cache: (copy step|copied|serialized|saved))" | sed "s/^/[$run] /"
    if [ "$rc" != 0 ] || ! echo "$out" | grep -q "save_off_frame $run: PASS"; then
        echo "FAIL: the $run run (rc $rc)"; fails=$((fails + 1))
    elif [ "$TIMING" = "--timing" ]; then
        line="$(echo "$out" | grep -m1 "^SAVE_OFF_FRAME $run ")"
        if echo "$line" | python3 -c 'import json,re,sys; s=json.loads(re.search(r"save=(\{.*?\})", sys.stdin.read()).group(1)); sys.exit(0 if s["worstMs"] <= 2.0 and s["chunkWorstMs"] <= 1.0 else 1)'; then
            echo "ok:   [$run] every message <= 2 ms and every chunk <= 1 ms"
        else
            echo "FAIL: [$run] a message over 2 ms or a chunk over 1 ms: $line"; fails=$((fails + 1))
        fi
    fi
done
[ "$fails" = 0 ] && echo "shadercache.save_off_frame: PASS"
exit $(( fails ? 1 : 0 ))
