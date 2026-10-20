#!/bin/bash
# shadercache.save_off_frame's driver: the scripted run COLD (the fresh data root fresh_home.sh made), then WARM on the
# same root (the cold run's clean-quit save loaded). Usage: save_off_frame.sh <Jahshaka> <e2e_save_off_frame.js> [--timing]
# --timing (shadercache.save_off_frame.timing, a measure row, alone on the card): also the wall-clock judgements the script
# prints on its SAVE_OFF_FRAME line — every message of every save <= 2 ms, every chunk <= 1 ms, and NO frame overrun on
# the frames that carried the save's messages (save.frameOverruns == 0: a frame overrun is work against the display
# period, a clock; under the guards' 8-wide pass a sibling's load overruns any frame).
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
        if echo "$line" | python3 -c 'import json,re,sys; s=json.loads(re.search(r"save=(\{.*?\})", sys.stdin.read()).group(1)); sys.exit(0 if s["worstMs"] <= 2.0 and s["chunkWorstMs"] <= 1.0 and s["frameOverruns"] == 0 else 1)'; then
            echo "ok:   [$run] every message <= 2 ms, every chunk <= 1 ms, no frame overrun on the save's frames"
        else
            echo "FAIL: [$run] a message over 2 ms, a chunk over 1 ms or a frame overrun on the save's frames: $line"; fails=$((fails + 1))
        fi
    fi
done
[ "$fails" = 0 ] && echo "shadercache.save_off_frame: PASS"
exit $(( fails ? 1 : 0 ))
