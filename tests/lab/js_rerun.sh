#!/bin/bash
# lab.js_rerun (LAB-SCRIPTS-1 stage 1) - A RUN IS A FRESH HOST: one lab runs one script twice (`--js`, then `js run` on its
# stdin), the driver rewriting a marker in the file between the runs. The script declares a top-level `const` and counts
# its runs in the realm's global (globalThis.n = (globalThis.n || 0) + 1), and checks n === 1.
# THE LAWS: run 2 RE-READS THE FILE (it prints the new marker, MARK-2) and runs in a FRESH realm - its `const` is no
# redeclaration and its n is 1 - so both runs end `ok (1 checks, 0 frames)` and the lab exits 0.
# CONTROL: run 1's own check (n === 1) passes - the check can see a run.
# The driver reads the lab's lines as they come (a coprocess): it rewrites the file on the first END line and sends the
# second `js run`, then `quit` on the second - no sleep, no clock; ctest's timeout bounds a lab that never answers.
# usage: js_rerun.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_rerun: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/rerun.js" <<'JS'
const marker = "MARK-1";
globalThis.n = (globalThis.n || 0) + 1;
console.log("marker " + marker + " n " + globalThis.n);
host.check({ok: globalThis.n === 1, what: "a fresh realm: n === 1 (it reads " + globalThis.n + ")"});
JS
coproc LABP { exec "$LAB" --headless --size 320x180 --data-root "$WD/data" --js "$WD/rerun.js" 2>&1; }
pid=$LABP_PID
to=${LABP[1]}
ends=0
while IFS= read -r -u "${LABP[0]}" line; do
    printf '%s\n' "$line" >> "$WD/run.log"
    case "$line" in
        "LAB JS END "*)
            ends=$((ends + 1))
            if [ "$ends" = 1 ]; then
                sed -i 's/MARK-1/MARK-2/' "$WD/rerun.js"
                echo "js run" >&"$to"
            else
                echo "quit" >&"$to"
            fi ;;
    esac
done
wait "$pid"
rc=$?
grep -E '^LAB (EMPTY|JS|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" <<'PY'
import os, re, sys
wd, rc = sys.argv[1], int(sys.argv[2])
lines = open(os.path.join(wd, "run.log")).read().splitlines()
fails = []
ends = [i for i, l in enumerate(lines) if l.startswith("LAB JS END rerun.js ")]
if len(ends) != 2:
    fails.append("two runs end (%d END lines)" % len(ends))
else:
    run1, run2 = lines[:ends[0] + 1], lines[ends[0] + 1:ends[1] + 1]
    if not any(l.startswith("LAB JS ok:   a fresh realm: n === 1") for l in run1):
        fails.append("CONTROL: run 1's own check did not pass")
    if not any(l == "LAB JS marker MARK-2 n 1" for l in run2):
        fails.append("run 2 did not re-read the file into a fresh realm (no 'LAB JS marker MARK-2 n 1')")
    for k, i in enumerate(ends):
        if lines[i] != "LAB JS END rerun.js ok (1 checks, 0 frames)":
            fails.append("run %d ended %r" % (k + 1, lines[i]))
if rc != 0:
    fails.append("the lab exited %d" % rc)
for f in fails:
    print("FAIL: " + f)
print("lab.js_rerun: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
