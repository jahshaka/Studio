#!/usr/bin/env bash
#
# meshbake.quit_during_bake — F5 (STAGE-CLOSE-TRIAGE-1 round 4): a run that ends while an import's
# bake is IN FLIGHT stops it at a cancel point inside the job and exits 0 — never waiting the job out,
# never the forced exit 86. Asserted: the import returned with its bake pending, the session log says
# the bake was cancelled, the run's code is 0 and its log carries the close bracket.
#
# usage: bake_quit.sh <jahshaka-binary>      cwd = a scratch run dir
set -u
BIN="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$PWD/quit-root"
rm -rf "$ROOT" quit.log dense.obj dense.jbundle
mkdir -p "$ROOT"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }
# A DENSE GRID (a few hundred thousand triangles): its bake's stages run for seconds, so the exit
# lands inside them.
python3 -I - "$PWD/dense.obj" <<'PY'
import sys, math
n = 300
with open(sys.argv[1], "w") as f:
    for j in range(n + 1):
        for i in range(n + 1):
            x, z = i / n * 4.0 - 2.0, j / n * 4.0 - 2.0
            f.write("v %.5f %.5f %.5f\n" % (x, 0.15 * math.sin(7 * x) * math.cos(5 * z), z))
    for j in range(n):
        for i in range(n):
            a = j * (n + 1) + i + 1
            b, c, d = a + 1, a + n + 1, a + n + 2
            f.write("f %d %d %d\nf %d %d %d\n" % (a, c, b, b, c, d))
PY
"$BIN" --headless --data-root "$ROOT" --script "$HERE/bake_quit.js" > quit.log 2>&1
rc=$?
check $rc "the run ends 0 with a bake in flight (code $rc; 86 = the forced exit)"
grep -q "QUIT_IMPORTED 1 pending [1-9]" quit.log; check $? "the share-file import returned with its bake pending"
grep -q "QUIT_RUNNING [1-9]" quit.log; check $? "the run ended while the worker was INSIDE the bake"
grep -q "QUIT_END" quit.log; check $? "the script ran to its end"
grep -qE "mesh bake: .*cancelled" quit.log; check $? "the bake in flight was CANCELLED inside its job"
grep -q "forcing process exit" quit.log; [ $? -ne 0 ]; check $? "no forced exit"
[ $fail -eq 0 ] && echo "meshbake.quit_during_bake: PASS" || { tail -30 quit.log; echo "meshbake.quit_during_bake: FAIL"; }
exit $fail
