#!/usr/bin/env bash
# app.start_in_vr (VR-SETTING-1) — THE START IN VR PREFERENCE, three boots of one fresh home:
#   1. the preference's default (ON) with NO OpenXR runtime -> the desktop route + the one notice;
#   2. the preference OFF -> a plain boot, no notice;
#   3. JAHSHAKA_VR=0 over the preference ON -> forced off, no notice.
# XR_RUNTIME_JSON names a file that does not exist, so no runtime is found whatever the box has
# installed (a WiVRn or Monado manifest must not decide this suite). The wall time of each boot is
# printed for the record, never asserted (tests count frames, not time).
# usage: start_in_vr.sh <jahshaka-binary> <script.js>     (cwd = the scratch run dir)
set -u
BIN="$1"; SCRIPT="$2"
export XR_RUNTIME_JSON="$PWD/no-openxr-runtime-here.json"
fail=0
boot() {   # $1 = arm, $2 = expected "source=… thisRun=…", then env assignments
    local arm="$1" want="$2"; shift 2
    local t0=$(date +%s%N)
    env "$@" "$BIN" --script "$SCRIPT" > "boot$arm.log" 2>&1
    local rc=$? t1=$(date +%s%N)
    echo "start_in_vr: boot $arm exit $rc in $(( (t1 - t0) / 1000000 )) ms"
    if [ $rc -ne 0 ] || ! grep -q "start_in_vr: $want" "boot$arm.log"; then
        echo "start_in_vr: FAIL — boot $arm wanted '$want'"; grep -E 'start_in_vr:|assert|Error|VR' "boot$arm.log" | head -20
        fail=1
    else
        grep -E '^ok:|start_in_vr:' "boot$arm.log"
    fi
}
boot 1 "source=setting thisRun=true"  -u JAHSHAKA_VR
boot 2 "source=setting thisRun=false" -u JAHSHAKA_VR
boot 3 "source=JAHSHAKA_VR=0 thisRun=false" JAHSHAKA_VR=0
[ $fail -eq 0 ] && echo "start_in_vr: PASS"
exit $fail
