#!/bin/bash
# lab.js_watched_no_checks (LAB-SCRIPTS-1, ruling 8's WINDOW half) - A SCRIPT THE OWNER WATCHES IS NOT A TEST: in the
# lab's WINDOW (the rig's display), a script that runs without a failure and checks nothing reads `ran: no checks` - its
# "checked nothing" fault neither printed nor counted, the process's exit untouched. (Headless it stays the host's
# verdict, FAILED (1): lab.js_error_survives and jahengine's law.) `jahlab --no-idle-frames --js demo.js`, then `quit`.
# THE LAWS: (1) `LAB JS END demo.js ran: no checks (0 frames)`; (2) no `LAB JS FAIL:` line; (3) `LAB END ... exit 0`,
#   the lab's code 0.
# CONTROL: the script's own line was printed (`LAB JS LOG: a demo draws, it checks nothing`): the run happened.
# usage: js_watched_no_checks.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; echo "lab.js_watched_no_checks: FAILED (1)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf '%s\n' 'console.log("a demo draws, it checks nothing");' > "$WD/demo.js"
printf 'quit\n' | "$LAB" --no-idle-frames --size 320x180 --data-root "$WD/data" --js "$WD/demo.js" > "$WD/run.log" 2>&1
rc=$?
grep -E '^LAB (WINDOW|JS|END)' "$WD/run.log"
python3 -I - "$WD" "$rc" <<'PY'
import os, re, sys
wd, rc = sys.argv[1], int(sys.argv[2])
log = open(os.path.join(wd, "run.log")).read()
fails = []
if not re.search(r"^LAB JS END demo\.js ran: no checks \(0 frames\)$", log, re.M):
    fails.append("(1) no `LAB JS END demo.js ran: no checks (0 frames)`")
if re.search(r"^LAB JS FAIL:", log, re.M):
    fails.append("(2) a `LAB JS FAIL:` line (the checked-nothing fault printed)")
if not re.search(r"^LAB END frames \d+, \d+ ms, exit 0$", log, re.M) or rc != 0:
    fails.append("(3) the lab's exit is not 0 (its code %d)" % rc)
if not re.search(r"^LAB JS LOG: a demo draws, it checks nothing$", log, re.M):
    fails.append("CONTROL: the script's own line was not printed (did it run?)")
for f in fails:
    print("FAIL: " + f)
print("lab.js_watched_no_checks: " + ("FAILED (%d)" % len(fails) if fails else "PASSED"))
sys.exit(1 if fails else 0)
PY
