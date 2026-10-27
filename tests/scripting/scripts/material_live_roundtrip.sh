#!/usr/bin/env bash
# material.live_roundtrip.<row> (LIVE-PERSIST-1): a live graph material with a cut-out survives
# save -> reopen (EMPTY piece cache) -> export -> EMPTY library -> import. One process per stage
# (the stages of material_live_roundtrip.js.in); HOME and XDG_CACHE_HOME are scratch, so no
# per-user cache can stand in for what the bundle must carry. The import's frames must agree with
# the author's and the reopen's. Then the graph material is deleted, the project saved without it,
# the material restored: the reopen must come back live (the save kept `customPieceGraph`).
# THREE ROWS, ONE CHAIN (TEST-CUT-1; CLAUDE.md Testing: every row at most 60 s — five processes in
# one row were 85 s), every stage and every comparison kept, in the same order on the same data:
#   author  — the author stage, the piece cache deleted, the reopen stage (its export): the setup
#   import  — the import into an EMPTY library, then F0/F1/F2/Fsolid compared   (requires author)
#   restore — the drop stage, the catalog restored, the restore stage            (requires author, import)
# (the chain runs in the old row's order — the stages share the scratch HOME, as they always did)
# usage: material_live_roundtrip.sh <app> <script.js.in> <fixture.png> <workdir> <author|import|restore>
set -u
APP="$1"; SCRIPT_IN="$2"; FIXTURE="$3"; WORK="$4"; ROW="$5"
# THE DRIVER'S SHADER CACHE IS THE BOX'S (2026-10-27, the lead's tool line): the scratch XDG_CACHE_HOME below would move
# the NVIDIA driver's disk cache into the fresh home - cold on every run (61-62 s at the 60 s cap) - and the row's law
# is the live graph's persistence, never the driver's: the real home's cache, named before HOME is replaced (the
# driver APPENDS "GLCache" to the path it is given: the value is the directory above it)
REAL_GLCACHE="${__GL_SHADER_DISK_CACHE_PATH:-$HOME/.cache/nvidia}"
# THE SHADER CACHE COMES WARM (2026-10-27): each stage's data root links the tree's ONE shared app cache
# (<build>/tests/.shadercache, tests/support/fresh_home.sh --warm's: linked under its lock, shared; the cache writes by
# rename, so a link is never written through; its lock file never linked) - the row's law is the live graph's
# PERSISTENCE, and the PIECE cache it empties (ShaderPieces) is another store. Cold, the author boot compiled 297
# shaders (43 s) and the row crossed its 60 s cap.
seed_shaders() {   # seed_shaders <data root>
    local shared
    shared="$(cd "$(dirname "$APP")/.." && pwd)/tests/.shadercache"
    [ -d "$shared" ] || return 0
    mkdir -p "$1/shadercache"
    (
        flock -s -w 60 9 || exit 0
        ( cd "$shared" && find . -type f ! -name cache.lock -print ) | while IFS= read -r f; do
            mkdir -p "$1/shadercache/$(dirname "$f")"
            ln "$shared/$f" "$1/shadercache/$f" 2>/dev/null || cp -p "$shared/$f" "$1/shadercache/$f" 2>/dev/null || true
        done
    ) 9>"$shared.lock"
}
run() {   # run <stage> <data root>
    sed -e "s#@STAGE@#$1#g" -e "s#@WORK@#$WORK#g" -e "s#@FIXTURE@#$FIXTURE#g" "$SCRIPT_IN" > "$WORK/$1.js"
    ( cd "$(dirname "$APP")" && env HOME="$WORK/home" XDG_CACHE_HOME="$WORK/home/.cache" \
          __GL_SHADER_DISK_CACHE_PATH="$REAL_GLCACHE" \
          timeout -k 10 300 "$APP" --data-root "$2" --script "$WORK/$1.js" > "$WORK/$1.log" 2>&1 )
    local rc=$?
    grep -E '^ok:|assert failed' "$WORK/$1.log"
    if [ $rc -ne 0 ] || ! grep -q "material_live_roundtrip: $1 PASS" "$WORK/$1.log"; then
        echo "FAIL: stage $1 exited $rc (log: $WORK/$1.log)"; tail -5 "$WORK/$1.log"; exit 1
    fi
}
case "$ROW" in
author)
    rm -rf "$WORK"; mkdir -p "$WORK/home/.cache" || exit 1
    seed_shaders "$WORK/root-a"
    run author "$WORK/root-a"
    [ -d "$WORK/root-a/ShaderPieces" ] || { echo "FAIL: the author stage wrote no piece cache under its data root"; exit 1; }
    rm -rf "$WORK/root-a/ShaderPieces"
    run reopen "$WORK/root-a"
    echo "material.live_roundtrip: author PASS"; exit 0 ;;
import|restore)
    [ -s "$WORK/live_roundtrip.zip" ] && [ -d "$WORK/root-a" ] \
        || { echo "FAIL: the author row left no archive / library in $WORK (material.live_roundtrip.author runs first)"; exit 1; } ;;
*)  echo "FAIL: row '$ROW' — author | import | restore"; exit 2 ;;
esac
if [ "$ROW" = restore ]; then
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
    rm -rf "$WORK/root-a.bak"
    echo "material.live_roundtrip: restore PASS"; exit 0
fi
seed_shaders "$WORK/root-b"
run import "$WORK/root-b"
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
