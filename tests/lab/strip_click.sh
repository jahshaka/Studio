#!/bin/bash
# lab.strip_click (LAB-SCRIPTS-1 stage 2) - THE STRIP's RUN BUTTON RUNS THE SCRIPT: the lab in a WINDOW on the rig's display
# (`jahlab --js one.js`, its first run at boot), then a real pointer click (xdotool, the X test extension) at the centre of
# the strip's Run button, then the key F9 - each followed on stdin by `frames 3` and an echo, so the lab has pumped the
# window's events before the driver reads on.
# THE LAW: the click is the line `js run` through the one parser - `LAB OK (strip) js run`, the script's run 2 ending
# `ok (1 checks, 0 frames)` - before the click's echo.
# CONTROL: F9 prints the same run (`LAB OK (key) js run`, run 3): the window takes input at all.
# THE FRAMES: `--no-idle-frames` - the window draws only what is asked (the first frame, then 3 after each input: its
# events are read at the next frame drawn), so the row draws EXACTLY its declared 7 (`LAB END frames 7`), never a count
# paced by the driver's wall clock.
# The driver reads the lab's lines as they come (a coprocess): no sleep, no clock; ctest's timeout bounds a lab that never
# answers. usage: strip_click.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.strip_click: FAILED (1)"; exit 1; }
command -v xdotool > /dev/null || { echo "FAIL: no xdotool"; echo "lab.strip_click: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf '%s\n' 'host.check({ok: true, what: "one.js ran"});' > "$WD/one.js"
# the Run button's centre, from the lab's own table (labcommands.cpp stripButtons: Run [4, 68) x [0, 24))
RX=36; RY=12
coproc LABP { exec "$LAB" --no-idle-frames --size 320x180 --data-root "$WD/data" --js "$WD/one.js" 2>&1; }
pid=$LABP_PID
to=${LABP[1]}
wid=
while IFS= read -r -u "${LABP[0]}" line; do
    printf '%s\n' "$line" >> "$WD/run.log"
    case "$line" in
        "LAB WINDOW "*) wid=${line#LAB WINDOW } ;;
        "LAB JS END one.js "*)
            if [ "$(grep -c '^LAB JS END one.js ' "$WD/run.log")" = 1 ]; then
                xdotool mousemove --window "$wid" "$RX" "$RY" click 1 >> "$WD/xdotool.log" 2>&1
                printf 'frames 3\necho after-click\n' >&"$to"
            fi ;;
        "LAB ECHO after-click")
            xdotool key --window "$wid" F9 >> "$WD/xdotool.log" 2>&1
            printf 'frames 3\necho after-key\n' >&"$to" ;;
        "LAB ECHO after-key") echo "quit" >&"$to" ;;
    esac
done
wait "$pid"
rc=$?
grep -E '^LAB (WINDOW|JS (RUN|END)|OK \((strip|key)\)|ECHO|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" <<'PY'
import os, sys
wd, rc = sys.argv[1], int(sys.argv[2])
lines = open(os.path.join(wd, "run.log")).read().splitlines()
fails = []
def between(a, b):
    i = next((k for k, l in enumerate(lines) if l == a), None)
    j = next((k for k, l in enumerate(lines) if l == b), None)
    return lines[i + 1:j] if i is not None and j is not None and i < j else None
first = next((k for k, l in enumerate(lines) if l.startswith("LAB JS END one.js ")), None)
click = between(lines[first], "LAB ECHO after-click") if first is not None else None
if click is None:
    fails.append("the run at boot and the click's echo were not both seen")
elif not ("LAB OK (strip) js run" in click and "LAB JS END one.js ok (1 checks, 0 frames)" in click):
    fails.append("THE LAW: the click on Run did not run the script (no `LAB OK (strip) js run` and its END before the echo)")
key = between("LAB ECHO after-click", "LAB ECHO after-key")
if key is None or "LAB OK (key) js run" not in key:
    fails.append("CONTROL: F9 did not run the script (the window took no input)")
if rc != 0:
    fails.append("the lab exited %d" % rc)
if not any(l.startswith("LAB END frames 7,") for l in lines):
    fails.append("THE FRAMES: the lab did not draw exactly its declared 7 (%s)" % [l for l in lines if l.startswith("LAB END")])
for f in fails:
    print("FAIL: " + f)
print("lab.strip_click: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
