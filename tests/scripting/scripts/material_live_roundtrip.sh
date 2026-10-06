#!/usr/bin/env bash
# material.live_roundtrip (LIVE-PERSIST-1): a live graph material with a cut-out survives
# save -> reopen (EMPTY piece cache) -> export -> EMPTY library -> import. Three processes
# (the stages of material_live_roundtrip.js.in); HOME and XDG_CACHE_HOME are scratch, so no
# per-user cache can stand in for what the bundle must carry. The three frames must agree.
# Then the graph material is deleted, the project saved without it, the material restored:
# the reopen must come back live (the save kept `customPieceGraph`).
# usage: material_live_roundtrip.sh <app> <script.js.in> <fixture.png> <workdir>
set -u
APP="$1"; SCRIPT_IN="$2"; FIXTURE="$3"; WORK="$4"
rm -rf "$WORK"; mkdir -p "$WORK/home/.cache" || exit 1
run() {   # run <stage> <data root>
    sed -e "s#@STAGE@#$1#g" -e "s#@WORK@#$WORK#g" -e "s#@FIXTURE@#$FIXTURE#g" "$SCRIPT_IN" > "$WORK/$1.js"
    ( cd "$(dirname "$APP")" && env HOME="$WORK/home" XDG_CACHE_HOME="$WORK/home/.cache" \
          timeout -k 10 300 "$APP" --data-root "$2" --script "$WORK/$1.js" > "$WORK/$1.log" 2>&1 )
    local rc=$?
    grep -E '^ok:|assert failed' "$WORK/$1.log"
    if [ $rc -ne 0 ] || ! grep -q "material_live_roundtrip: $1 PASS" "$WORK/$1.log"; then
        echo "FAIL: stage $1 exited $rc (log: $WORK/$1.log)"; tail -5 "$WORK/$1.log"; exit 1
    fi
}
run author "$WORK/root-a"
[ -d "$WORK/root-a/ShaderPieces" ] || { echo "FAIL: the author stage wrote no piece cache under its data root"; exit 1; }
rm -rf "$WORK/root-a/ShaderPieces"
run reopen "$WORK/root-a"
run import "$WORK/root-b"
# THE GRAPH GOES MISSING AND COMES BACK: the drop stage deletes it and saves; the restore
# puts its catalog rows (every table but the projects' scenes) and its sidecar back from
# a copy taken before the drop, then the restore stage reopens.
rm -rf "$WORK/root-a.bak"; cp -a "$WORK/root-a" "$WORK/root-a.bak"
run drop "$WORK/root-a"
db=$(cd "$WORK/root-a" && ls *.db | head -1)
python3 - "$WORK/root-a/$db" "$WORK/root-a.bak/$db" <<'PYEOF' || { echo "FAIL: could not restore the catalog rows"; exit 1; }
import sqlite3, sys
c = sqlite3.connect(sys.argv[1])
c.execute("ATTACH DATABASE ? AS bak", (sys.argv[2],))
for (t,) in c.execute("SELECT name FROM bak.sqlite_master WHERE type='table'").fetchall():
    if t in ("projects",) or t.startswith("sqlite_"): continue
    c.execute('INSERT OR IGNORE INTO main."%s" SELECT * FROM bak."%s"' % (t, t))
c.commit()
PYEOF
cp -rn "$WORK/root-a.bak/AssetStore/." "$WORK/root-a/AssetStore/"
run restore "$WORK/root-a"
python3 - "$WORK" <<'EOF'
import sys
from PIL import Image
w = sys.argv[1]
def load(n): return Image.open("%s/%s.png" % (w, n)).convert("RGB")
def diff(p, q):
    a = p.load(); b = q.load(); W, H = p.size
    big = tot = 0
    for y in range(H):
        for x in range(W):
            d = max(abs(a[x, y][i] - b[x, y][i]) for i in range(3))
            tot += d; big += d > 24
    return tot / (W * H), big / (W * H)
f0 = load("F0"); ok = True
_, holes = diff(f0, load("Fsolid"))
good = holes >= 0.02
ok &= good
print("%s: the cut-out is visible: %.2f %% of pixels change when it is switched off (bound >= 2 %%)"
      % ("ok" if good else "FAIL", 100 * holes))
for n in ("F1", "F2"):
    mean, frac = diff(f0, load(n))
    good = mean <= 3.0 and frac <= 0.01
    ok &= good
    print("%s: %s vs F0: mean |d| %.2f codes, %.2f %% of pixels over 24 codes (bounds 3.0 / 1 %%)"
          % ("ok" if good else "FAIL", n, mean, 100 * frac))
print("material.live_roundtrip: %s" % ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
EOF
