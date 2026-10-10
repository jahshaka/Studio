#!/usr/bin/env bash
#
# scripting.e2e.clipboard_budget — THE COPY UNDER THE INLINE BUDGET (STAGE-CLOSE-TRIAGE-1 rounds 7-8), API-first:
# a row's node tree is never budgeted and always travels (a 600-part model copies under 64 KB, its
# file by reference, and pastes whole into a foreign library); a budget under its 64 KB floor refuses
# copy and cut with that reason (nothing deleted). Three --headless processes, each with its own data
# root whose jahsettings.ini carries the budget — the setting the app reads on every copy.
#
# usage: clipboard_budget.sh <jahshaka-binary> --headless <script.js>   cwd = the row's fresh home's run dir
#   (`--headless` is forwarded to every process; the row's command carries it so the selector sees a
#   headless script row — the home is wiped by tests/support/fresh_home.sh before this runs)
set -u
BIN="$1"; MODEFLAG="$2"; JS="$3"
[ "$MODEFLAG" = "--headless" ] || { echo "FAIL: usage: clipboard_budget.sh <bin> --headless <js>"; exit 2; }
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }
# A MODEL OF 600 PARTS, padded past the budget: its Object row's node tree is ~2.2 MB (never budgeted),
# its FILE ~100 KB (over the 64 KB budget: it goes by reference).
python3 -I - "$PWD/budget_parts.obj" <<'PY'
import sys
with open(sys.argv[1], "w") as f:
    f.write(("# padding: the file is over the 64 KB inline budget\n") * 2000)
    for i in range(600):
        x = (i % 30) * 0.5; z = (i // 30) * 0.5
        f.write("o part_%03d\nv %g 0 %g\nv %g 0 %g\nv %g 0.4 %g\nf %d %d %d\n"
                % (i, x, z, x + 0.3, z, x, z + 0.3, 3 * i + 1, 3 * i + 2, 3 * i + 3))
PY
seed() {   # $1 = data root, $2 = budget in bytes
    mkdir -p "$1"
    printf '[clipboard]\ninlineLimitBytes=%s\n' "$2" > "$1/jahsettings.ini"
}
run() {    # $1 = mode, $2 = data root, $3 = extra script prefix file (or empty)
    { echo "var MODE = \"$1\";"; echo "var MODEL = \"$PWD/budget_parts.obj\";"; [ -n "$3" ] && cat "$3"; cat "$JS"; } > "$1.js"
    "$BIN" "$MODEFLAG" --data-root "$2" --script "$1.js" > "$1.log" 2>&1
}
seed "$PWD/root-a" 65536
run copy "$PWD/root-a" ""
check $? "library A's copy run exits 0"
grep -q "CLIPBOARD_BUDGET copy PASS" copy.log; check $? "a 600-part model copies under a 64 KB budget, its file by reference"
# library B gets A's payload as a script literal (the payload IS JSON) and A's source count
{ printf 'var SOURCE_COUNT = %s;\n' "$(sed -n 's/.*CLIPBOARD_SOURCE_COUNT //p' copy.log | head -1)"
  printf 'var PAYLOAD = '; sed -n 's/.*CLIPBOARD_PAYLOAD //p' copy.log | head -1; printf ';\n'; } > paste-prefix.js
seed "$PWD/root-b" 65536
run paste "$PWD/root-b" paste-prefix.js
check $? "library B's paste run exits 0"
grep -q "CLIPBOARD_BUDGET paste PASS" paste.log; check $? "the paste into a foreign library has the source's node count (the tree travelled whole)"
seed "$PWD/root-floor" 1000
run floor "$PWD/root-floor" ""
check $? "the floor run exits 0"
grep -q "CLIPBOARD_BUDGET floor PASS" floor.log; check $? "a budget under 64 KB refuses copy and cut with its reason, nothing deleted"
if [ $fail -ne 0 ]; then for m in copy paste floor; do echo "---- $m.log"; grep -E '^ok:|assert|Error' $m.log | cut -c1-300 | tail -10; done; fi
[ $fail -eq 0 ] && echo "scripting.e2e.clipboard_budget: PASS" || echo "scripting.e2e.clipboard_budget: FAIL"
exit $fail
