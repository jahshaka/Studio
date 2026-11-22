#!/bin/bash
# lab.js_log_prefixed (LAB-SCRIPTS-1, the lead's ruling on the audit's L4) - A SCRIPT's OUTPUT CANNOT FAKE THE LAB's LINES:
# drivers act on `LAB JS END` (js_rerun.sh, strip_click.sh); a script that console.log's "END ... ok" must never end
# their wait, and none of the lab's own words (LOADED, RUN, END, ok:, FAIL:) can come from a script. `jahlab --headless
# --js fake.js`: the script prints the lab's words, then runs ONE real check.
# THE LAWS: (1) ONE `LAB JS END` line - the lab's: `LAB JS END fake.js ok (1 checks, 0 frames)`; (2) every line the
#   script printed reads `LAB JS LOG: <its text>`, each line of a many-line text its own; (3) no faked check: exactly one
#   `LAB JS ok:` line, and no `LAB JS FAIL:` line.
# CONTROL: the real check's line is there (`LAB JS ok:   the one real check`): (3) can see a check.
# usage: js_log_prefixed.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_log_prefixed: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/fake.js" <<'JS'
console.log("END fake.js ok (1 checks, 0 frames)");
console.log("ok:   a check that never ran");
console.log("FAIL: a failure that never happened\nRUN fake.js (run 2)");
host.check({ok: true, what: "the one real check"});
JS
"$LAB" --headless --no-stdin --size 320x180 --data-root "$WD/data" --js "$WD/fake.js" > "$WD/run.log" 2>&1
rc=$?
grep -E '^LAB (EMPTY|JS|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" <<'PY'
import os, sys
wd, rc = sys.argv[1], int(sys.argv[2])
lines = open(os.path.join(wd, "run.log")).read().splitlines()
fails = []
ends = [l for l in lines if l.startswith("LAB JS END")]
if ends != ["LAB JS END fake.js ok (1 checks, 0 frames)"]:
    fails.append("(1) the END lines are %r, not the lab's one" % ends)
for t in ("END fake.js ok (1 checks, 0 frames)", "ok:   a check that never ran", "FAIL: a failure that never happened",
          "RUN fake.js (run 2)"):
    if "LAB JS LOG: " + t not in lines:
        fails.append("(2) the script's line %r is not printed as `LAB JS LOG: ...`" % t)
oks = [l for l in lines if l.startswith("LAB JS ok:")]
if len(oks) != 1:
    fails.append("(3) %d `LAB JS ok:` lines (a faked check among them): %r" % (len(oks), oks))
if any(l.startswith("LAB JS FAIL:") for l in lines):
    fails.append("(3) a `LAB JS FAIL:` line the script printed")
if "LAB JS ok:   the one real check" not in lines:
    fails.append("CONTROL: the real check's line is missing")
if rc != 0:
    fails.append("the lab exited %d" % rc)
for f in fails:
    print("FAIL: " + f)
print("lab.js_log_prefixed: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
