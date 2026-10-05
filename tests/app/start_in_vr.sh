#!/usr/bin/env bash
# app.start_in_vr (VR-START-1) — THE VR PREFERENCES AND THE POLICY CHECK, three boots of one
# fresh home, none of which lets the OpenXR loader open a runtime:
#   1. the defaults (Start in VR on, WiVRn) with no active manifest -> the notice; the button -> the dialog;
#   2. Start in VR off, the active manifest names Monado -> the button's dialog says so, nothing opened;
#   3. JAHSHAKA_VR=0 -> no VR in the run.
# XR_RUNTIME_JSON pins the "active manifest" whatever the box has installed. The Monado manifest is
# a stand-in whose library does not exist: were the policy ever bypassed, the loader would fail to
# load it rather than reach (or socket-activate) any runtime.
# usage: start_in_vr.sh <jahshaka-binary> <script.js>     (cwd = the scratch run dir)
set -u
BIN="$1"; SCRIPT="$2"
cat > monado-standin.json <<'JSON'
{ "file_format_version": "1.0.0",
  "runtime": { "name": "Monado", "library_path": "/nonexistent/libopenxr_monado.so" } }
JSON
fail=0
boot() {   # $1 = arm, $2 = expected "source=… startCheck=…", then env assignments
    local arm="$1" want="$2"; shift 2
    env "$@" "$BIN" --script "$SCRIPT" > "boot$arm.log" 2>&1
    local rc=$?
    if [ $rc -ne 0 ] || ! grep -q "start_in_vr: $want" "boot$arm.log"; then
        echo "start_in_vr: FAIL — boot $arm (exit $rc) wanted '$want'"
        grep -E 'start_in_vr:|assert|Error|VR' "boot$arm.log" | head -20
        fail=1
    else
        grep -E '^ok:|start_in_vr:' "boot$arm.log"
    fi
}
boot 1 "source=setting startCheck=true"  -u JAHSHAKA_VR XR_RUNTIME_JSON="$PWD/no-openxr-runtime-here.json"
boot 2 "source=setting startCheck=false" -u JAHSHAKA_VR XR_RUNTIME_JSON="$PWD/monado-standin.json"
boot 3 "source=JAHSHAKA_VR=0 startCheck=false" JAHSHAKA_VR=0 XR_RUNTIME_JSON="$PWD/no-openxr-runtime-here.json"
[ $fail -eq 0 ] && echo "start_in_vr: PASS"
exit $fail
