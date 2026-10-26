#!/bin/bash
# lab.card_set_binds (PHOTON-7 stage 10-3, F3 and its fit) - THE REST SET IS THE POSE's AT ONE FRUSTUM, BY TWO PATHS, WHERE
# THE ATLAS BINDS. atom-street (40 facades: its demand fills the 256 pages) at 1920 x 1080: path A rests at the street's
# pose (400 frames); then a teleport 172 m down the street (400 frames: its own rest decision) and back to the same pose
# (400 frames): path B. THE LAWS:
#   (1) the atlas BINDS at both rests (pagesUsed == pages), or the row measures nothing;
#   (2) both rests are at rest (gi.atRest) after their decision;
#   (3) the resident set at B is A's (cards.residentHash), and the far pose's is not (the path went somewhere else).
# THE DEFECTS (red before): the face mark was written only when no card answered a hit, so the demand followed the
# cache's capture state; and the decision admitted into FREE room only, so with the atlas full its set followed the
# history of what was resident. Now every hit marks its face bucket, and the decision's set is the cards of the buckets
# hit in the rest span, ranked by one run-independent key (span read count, size, node, index), the longest prefix the
# packer fits.
# usage: card_set_binds.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf 'camera 0,3,22,0,3,-400\nframes 400\nstatus %s/s.txt\ncamera 0,3,-150,0,3,-400\nframes 400\nstatus %s/s.txt\ncamera 0,3,22,0,3,-400\nframes 400\nstatus %s/s.txt\nquit\n' \
    "$WD" "$WD" "$WD" > "$WD/b.lab"
"$LAB" --headless --no-stdin --data-root "$WD/data" --env atom-street --script "$WD/b.lab" > "$WD/b.log" 2>&1
rc=$?
grep -E '^LAB (SCRIPT STOPPED|END)' "$WD/b.log"
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; tail -5 "$WD/b.log"; exit 1; }
python3 -I - "$WD/s.txt" <<'PY'
import json, sys
L = [json.loads(l) for l in open(sys.argv[1]) if l.strip().startswith('{')]
if len(L) != 3:
    print("FAIL: %d status lines, want 3" % len(L)); sys.exit(1)
names = ("A (the pose)", "the far pose", "B (the pose again)")
fails = []
for n, d in zip(names, L):
    c = d["gi"]["cards"]
    print("card_set_binds: %s: %d cards on %d of %d pages, at rest %s, %d rest decision(s), %d refused by the fit; set %s"
          % (n, c["cardsResident"], c["pagesUsed"], c["pages"], d["gi"]["atRest"], c["restDecisions"],
             c["decisionRefused"], c["residentHash"]))
    if c["pagesUsed"] != c["pages"]:
        fails.append("%s: the atlas does not bind (%d of %d pages): the row measures nothing" % (n, c["pagesUsed"], c["pages"]))
    if d["gi"]["atRest"] is not True:
        fails.append("%s: not at rest" % n)
a, q, b = (d["gi"]["cards"]["residentHash"] for d in L)
if a == q:
    fails.append("the far pose rests at A's set: the path went nowhere")
if a != b:
    fails.append("THE REST SET FOLLOWED THE PATH: %s at A, %s back at the same pose" % (a, b))
for f in fails: print("FAIL: " + f)
if fails: sys.exit(1)
print("ok: one rest set at one pose by two paths, the atlas full")
PY
