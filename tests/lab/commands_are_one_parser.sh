#!/bin/bash
# lab.commands_are_one_parser (LAB-1 stage 1): the THREE sources of a command — a KEY on the window, a --script LINE and
# a STDIN line — reach the one parser and leave the same state. Each source puts the scene on the Atom LEVELS view
# (the keys: F2 twice from off, "view next" x 2 through the key table; the script: `view levels`; stdin: `view levels`),
# and each is read back two ways: the lab's status (what it pushed — the engine keeps no getter for the Atom view) and
# THE ENGINE'S OWN PICTURE (the on-screen frame capture): the three levels pictures are one picture, byte for byte, and
# differ from the `view off` picture taken between them. Runs ON THE WINDOW (DISPLAY = the rig's Xvfb), xdotool sends
# the keys (tests/app/input_keys.sh's tool).
# usage: commands_are_one_parser.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
HERE=$(cd "$(dirname "$0")" && pwd)
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (this row drives the window)"; exit 1; }
command -v xdotool >/dev/null || { echo "FAIL: xdotool is not installed"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
python3 -I "$HERE/kitbash_obj.py" "$WD/kitbash.obj" || exit 1
cat > "$WD/s.txt" <<'S'
settle
view levels
frames 4
shot script
status st.json
view off
frames 4
shot off
echo ready
S
mkfifo "$WD/in"
"$LAB" --size 960x540 --data-root "$WD/data" --script "$WD/s.txt" "$WD/kitbash.obj" < "$WD/in" > "$WD/out.log" 2>&1 &
LABPID=$!
exec 3> "$WD/in"
cleanup() { exec 3>&-; kill "$LABPID" 2>/dev/null; wait "$LABPID" 2>/dev/null; }
trap cleanup EXIT
# wait for the script's end (the lab prints it); bounded: 200 x 0.1 s
for i in $(seq 1 200); do
    grep -q '^LAB ECHO ready' "$WD/out.log" && break
    kill -0 "$LABPID" 2>/dev/null || break
    sleep 0.1
done
grep -q '^LAB ECHO ready' "$WD/out.log" || { echo "FAIL: the script did not finish"; tail -20 "$WD/out.log"; exit 1; }
WIN=$(sed -n 's/^LAB WINDOW //p' "$WD/out.log" | head -1)
# THE KEYS: F2 twice (off -> triangles -> levels). The two stdin frames after them pump the window's events first.
xdotool key --window "$WIN" F2 F2
printf 'frames 4\nshot key\nstatus st.json\nview off\nframes 4\nview levels\nframes 4\nshot stdin\nstatus st.json\nquit\n' >&3
exec 3>&-
wait "$LABPID"; rc=$?
trap - EXIT
grep -E '^LAB (OK|FAILED|REFUSED|SHOT)' "$WD/out.log" | grep -E 'key|stdin|script|SHOT|FAILED|REFUSED' | cut -c1-160
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; tail -20 "$WD/out.log"; exit 1; }
python3 -I - "$WD" <<'PY'
import hashlib, json, os, sys
wd = sys.argv[1]
d = os.path.join(wd, "data")
rows = [json.loads(l) for l in open(os.path.join(d, "st.json")) if l.strip()]
views = [r["lab"]["view"] for r in rows]
print("status views (script, key, stdin):", views)
assert views == ["levels", "levels", "levels"], views
log = open(os.path.join(wd, "out.log")).read()
assert "LAB OK (key) view next" in log, "the key path did not run 'view next' through the parser"
h = {n: hashlib.sha256(open(os.path.join(d, "shots", n + ".png"), "rb").read()).hexdigest()[:16]
     for n in ("script", "key", "stdin", "off")}
print("pictures:", h)
assert h["script"] == h["key"] == h["stdin"], "the three sources' levels pictures differ"
assert h["off"] != h["script"], "the levels view painted nothing (equal to the off picture)"
print("lab.commands_are_one_parser: PASS")
PY
