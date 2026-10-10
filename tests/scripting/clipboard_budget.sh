#!/usr/bin/env bash
#
# scripting.e2e.clipboard_budget — THE COPY'S REFUSALS (STAGE-CLOSE-TRIAGE-1 round 7, read 7 F2/F3), API-first:
# a row's stored JSON is charged to the inline budget first and a row that does not fit refuses the
# copy BY NAME (the clipboard unchanged, a refused cut deleting nothing); a budget under its 64 KB floor
# refuses every copy with that reason. Two --headless processes, each with its own data root whose
# jahsettings.ini carries the budget — the setting the app reads on every copy.
#
# usage: clipboard_budget.sh <jahshaka-binary> <script.js>      cwd = a scratch run dir
set -u
BIN="$1"; JS="$2"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }
rm -rf root-blob root-floor budget_parts.obj blob.js floor.js blob.log floor.log
# A MODEL OF 600 PARTS: its Object row's node tree is far over 64 KB, its file (a few KB) is not.
python3 -I - "$PWD/budget_parts.obj" <<'PY'
import sys
with open(sys.argv[1], "w") as f:
    for i in range(600):
        x = (i % 30) * 0.5; z = (i // 30) * 0.5
        f.write("o part_%03d\nv %g 0 %g\nv %g 0 %g\nv %g 0.4 %g\nf %d %d %d\n"
                % (i, x, z, x + 0.3, z, x, z + 0.3, 3 * i + 1, 3 * i + 2, 3 * i + 3))
PY
seed() {   # $1 = data root, $2 = budget in bytes
    mkdir -p "$1"
    printf '[clipboard]\ninlineLimitBytes=%s\n' "$2" > "$1/jahsettings.ini"
}
seed "$PWD/root-blob" 65536
{ echo 'var MODE = "blob";'; echo "var MODEL = \"$PWD/budget_parts.obj\";"; cat "$JS"; } > blob.js
"$BIN" --headless --data-root "$PWD/root-blob" --script blob.js > blob.log 2>&1
check $? "the blob run exits 0"
grep -q "CLIPBOARD_BUDGET blob PASS" blob.log; check $? "an over-budget node tree refuses copy and cut by name, the clipboard unchanged"
seed "$PWD/root-floor" 1000
{ echo 'var MODE = "floor";'; echo 'var MODEL = "";'; cat "$JS"; } > floor.js
"$BIN" --headless --data-root "$PWD/root-floor" --script floor.js > floor.log 2>&1
check $? "the floor run exits 0"
grep -q "CLIPBOARD_BUDGET floor PASS" floor.log; check $? "a budget under 64 KB refuses the copy with its reason"
if [ $fail -ne 0 ]; then echo "---- blob.log"; grep -E 'ok:|assert|Error' blob.log | tail -15; echo "---- floor.log"; grep -E 'ok:|assert|Error' floor.log | tail -8; fi
[ $fail -eq 0 ] && echo "scripting.e2e.clipboard_budget: PASS" || echo "scripting.e2e.clipboard_budget: FAIL"
exit $fail
