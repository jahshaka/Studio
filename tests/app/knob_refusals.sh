#!/bin/bash
# app.knob_refusals — THE RENDER THREAD'S TEST KNOBS REFUSE WHAT THEY CANNOT HONOUR (FRAME-MONITOR-DOOR-1 stage 2;
# ENGINE-UPGRADE-U3 worth-a-look 5), before QApplication, with no display: exit 2 and a message naming the way out.
#  1. JAH_FREE_RUNNING=1 with a script run and no --script-live: the Off policy owns the loop, no driver tick ever runs
#     in free-running (the 21 h free2k hang, spikes/free-running-hang) — refused, naming --script-live.
#  2. the same with --script-live (here --headless: no engine, no display needed) — NOT refused: the run runs.
#  3. JAH_ENGINE_ONE_THREAD=1 with a free-running run — refused (one thread has no render loop), as it was.
# Usage: knob_refusals.sh <Jahshaka binary>
set -u
APP="${1:?the Jahshaka binary}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
echo 'console.log("knob run: ran");' > "$T/ok.js"
fails=0
check() { if [ "$1" = 0 ]; then echo "ok:   $2"; else echo "FAIL: $2"; fails=$((fails + 1)); fi; }
run() { env -u DISPLAY -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen HOME="$T/home" "$@" > "$T/out" 2>&1; echo $?; }

rc=$(run env JAH_FREE_RUNNING=1 "$APP" --script "$T/ok.js" --headless --data-root "$T/data")
grep -q -- "--script-live" "$T/out"; named=$?
check $(( rc == 2 && named == 0 ? 0 : 1 )) "JAH_FREE_RUNNING=1 --script without --script-live: exit 2 naming --script-live (rc $rc)"
grep -q "knob run: ran" "$T/out" && check 1 "...and the script never ran"

rc=$(run env JAH_FREE_RUNNING=1 "$APP" --script "$T/ok.js" --headless --script-live --data-root "$T/data")
grep -q "knob run: ran" "$T/out"; ran=$?
check $(( rc == 0 && ran == 0 ? 0 : 1 )) "JAH_FREE_RUNNING=1 --script --script-live: not refused, the script ran (rc $rc)"
[ "$rc" = 0 ] || sed 's/^/  | /' "$T/out" | tail -5

rc=$(run env JAH_FREE_RUNNING=1 JAH_ENGINE_ONE_THREAD=1 "$APP" --script "$T/ok.js" --headless --script-live --data-root "$T/data")
grep -q "JAH_ENGINE_ONE_THREAD=1 is for lockstep runs" "$T/out"; named=$?
check $(( rc == 2 && named == 0 ? 0 : 1 )) "JAH_ENGINE_ONE_THREAD=1 with a free-running run: exit 2 (rc $rc)"

[ "$fails" = 0 ] && echo "knob_refusals: PASS" || echo "knob_refusals: FAILED ($fails)"
exit $(( fails ? 1 : 0 ))
