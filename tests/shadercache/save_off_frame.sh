#!/bin/bash
# shadercache.save_off_frame's driver: the scripted run COLD (the fresh data root fresh_home.sh made), then WARM on the
# same root (the cold run's clean-quit save loaded). Usage: save_off_frame.sh <Jahshaka> <e2e_save_off_frame.js>
set -u
APP="${1:?the Jahshaka binary}" SCRIPT="${2:?the script}"
fails=0
for run in cold warm; do
    out="$(JAH_FREE_RUNNING=1 "$APP" --script "$SCRIPT" --script-live 2>&1)"; rc=$?
    echo "$out" | grep -E "^(ok:|SAVE_OFF_FRAME|save_off_frame|.*assert failed|Jahshaka shader cache: (copy step|copied|serialized|saved))" | sed "s/^/[$run] /"
    if [ "$rc" != 0 ] || ! echo "$out" | grep -q "save_off_frame $run: PASS"; then
        echo "FAIL: the $run run (rc $rc)"; fails=$((fails + 1))
    fi
done
[ "$fails" = 0 ] && echo "shadercache.save_off_frame: PASS"
exit $(( fails ? 1 : 0 ))
