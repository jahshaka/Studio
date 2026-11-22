#!/bin/bash
# lab.js_error_survives (LAB-SCRIPTS-1 stage 1) - A SCRIPT's ERROR NEVER ENDS THE LAB: `jahlab --js bad.js` (it throws on
# its line 3), then `js load good.js` and `js run` on stdin.
# THE LAWS: (1) the bad run ends `LAB JS END bad.js FAILED (1)` - its check passed, so the one fault is the stop (the
# host's verdict, unchanged) - and a `LAB JS` line names bad.js:3 (the error's stack);
# (2) the lab goes on: the good script runs and ends `ok`; (3) the bad run failed the process: `LAB END ... exit 1` and
# the lab's exit code 1.
# CONTROL: the good script's one check ran (`ok (1 checks, ...)`) - the law (2) can see a run.
# usage: js_error_survives.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_error_survives: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf '%s\n' '// bad.js: one check, then it throws on its line 3 (the stop is its one fault)' \
    'host.check({ok: true, what: "bad.js runs up to its throw"});' 'throw new Error("boom");' > "$WD/bad.js"
printf '%s\n' 'host.check({ok: true, what: "the good script runs after the bad one"});' > "$WD/good.js"
printf 'js load %s\njs run\nquit\n' "$WD/good.js" |
    "$LAB" --headless --size 320x180 --data-root "$WD/data" --js "$WD/bad.js" > "$WD/run.log" 2>&1
rc=$?
grep -E '^LAB (EMPTY|JS|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" <<'PY'
import os, re, sys
wd, rc = sys.argv[1], int(sys.argv[2])
log = open(os.path.join(wd, "run.log")).read()
fails = []
if not re.search(r"^LAB JS END bad\.js FAILED \(1\)$", log, re.M):
    fails.append("(1) no 'LAB JS END bad.js FAILED (1)'")
if not re.search(r"^LAB JS .*bad\.js:3\b", log, re.M):
    fails.append("(1) no LAB JS line names bad.js:3")
if not re.search(r"^LAB JS END good\.js ok \(1 checks, 0 frames\)$", log, re.M):
    fails.append("(2) / CONTROL: the good script did not run its one check after the bad one")
if not re.search(r"^LAB END frames \d+, \d+ ms, exit 1$", log, re.M) or rc != 1:
    fails.append("(3) the bad run did not fail the process (LAB END ... exit 1, the code %d)" % rc)
for f in fails:
    print("FAIL: " + f)
print("lab.js_error_survives: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
