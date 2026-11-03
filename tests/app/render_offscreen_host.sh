#!/bin/sh
# app.render_offscreen_host — THE HEADLESS HOST's DOOR THROUGH THE REAL APP, on the CPU (TEST-STREAMLINE-1 3a; the
# reader's P4/P5 through the binary). The card half — the host booting Vulkan offscreen on the rig's Xvfb and its
# verbs drawing — is card block B; this is what the CPU can prove, with DISPLAY unset and no card:
#   1. `--headless --script` is the NULL route: the script runs (exit 0) and the session log's DEVICE block names
#      `render route : null`.
#   2. `--headless --render-offscreen --script` with DISPLAY unset is REFUSED before any script runs: exit 3, ONE line
#      naming why (Ogre-Next's Vulkan connects to $DISPLAY at plugin load) - never a document-only run in its place.
#   3. `--render-offscreen` without --headless is a CLI error: exit 2, the flag's own line, nothing boots.
# LAW: the host never degrades silently - every refusal is one named line and a non-zero exit.
# Usage: render_offscreen_host.sh <Jahshaka> <home>
app=$1
home=$2
fail=0
check() { if [ "$1" = 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; fail=1; fi; }
script="$home/host_probe.js"
printf 'print("host-probe-ran"); 0\n' > "$script"

out=$(cd "$home/run" && "$app" --headless --script "$script" 2>&1); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "host-probe-ran"; check $? "1. --headless: the NULL route runs the script (exit $rc)"
grep -q "render route *: null" "$home/run/logs/latest.log" 2>/dev/null; check $? "1. ...and the DEVICE block names 'render route : null'"

out=$(cd "$home/run" && "$app" --headless --render-offscreen --script "$script" 2>&1); rc=$?
[ $rc = 3 ]; check $? "2. --render-offscreen with DISPLAY unset: exit 3 (got $rc)"
echo "$out" | grep -q "^Jahshaka: --render-offscreen: the headless host did not start (--render-offscreen needs an X display (DISPLAY is unset)"
check $? "2. ...with ONE line naming why"
echo "$out" | grep -q "host-probe-ran"; [ $? != 0 ]; check $? "2. ...and no script ran in a document-only run in its place"

out=$(cd "$home/run" && "$app" --render-offscreen --script "$script" 2>&1); rc=$?
[ $rc = 2 ] && echo "$out" | grep -q "render-offscreen: needs --headless"
check $? "3. --render-offscreen without --headless: a CLI error (exit $rc), its own line"

[ $fail = 0 ] && echo "app.render_offscreen_host: all ok" || echo "app.render_offscreen_host: FAILED"
exit $fail
