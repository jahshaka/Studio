#!/usr/bin/env bash
# scripting.e2e.photon_pin_read (WORLD-MODE-1, brief §2) — the scene READER drops a stored `photon`
# pin. Stage `author` saves a scene at Epic with Photon Low; the project's scene blob is then given
# `worldOverrides.photon = 1` BY HAND (sqlite, the blob is the scene JSON); stage `reopen` opens it
# and asserts no pin, an honest Custom, and a re-pick that moves Photon; the blob saved after it
# carries no `photon` key.
# usage: photon_pin_read.sh <app> <script.js.in> <workdir>
set -u
APP="$1"; SCRIPT_IN="$2"; WORK="$3"
rm -rf "$WORK"; mkdir -p "$WORK/home/.cache" || exit 1
ROOT="$WORK/root"
run() {   # run <stage>
    sed -e "s#@STAGE@#$1#g" "$SCRIPT_IN" > "$WORK/$1.js"
    ( cd "$(dirname "$APP")" && env HOME="$WORK/home" XDG_CACHE_HOME="$WORK/home/.cache" \
          timeout -k 10 300 "$APP" --data-root "$ROOT" --script "$WORK/$1.js" > "$WORK/$1.log" 2>&1 )
    local rc=$?
    grep -E '^ok:|assert failed' "$WORK/$1.log"
    if [ $rc -ne 0 ] || ! grep -q "photon_pin_read: $1 PASS" "$WORK/$1.log"; then
        echo "FAIL: stage $1 exited $rc (log: $WORK/$1.log)"; tail -5 "$WORK/$1.log"; exit 1
    fi
}
blob() {   # blob <set|check> — pin photon in the stored scene, or check no pin is stored
    python3 - "$1" "$ROOT" <<'PYEOF'
import glob, json, sqlite3, sys
mode, root = sys.argv[1], sys.argv[2]
hits = []
for db in glob.glob(root + "/*.db"):
    c = sqlite3.connect(db)
    try:
        rows = c.execute("SELECT guid, scene FROM projects WHERE name = 'PhotonPinRead'").fetchall()
    except sqlite3.Error:
        continue
    for guid, scene in rows:
        hits.append((c, guid, scene))
if len(hits) != 1:
    print("FAIL: want one PhotonPinRead project under %s, found %d" % (root, len(hits))); sys.exit(1)
c, guid, scene = hits[0]
doc = json.loads(bytes(scene) if not isinstance(scene, str) else scene)
over = doc["scene"].get("worldOverrides", {})
if mode == "set":
    over["photon"] = 1
    doc["scene"]["worldOverrides"] = over
    c.execute("UPDATE projects SET scene = ? WHERE guid = ?", (json.dumps(doc).encode(), guid))
    c.commit()
    print("ok: the stored scene now pins photon: %s" % json.dumps(over))
else:
    if "photon" in over:
        print("FAIL: the scene saved after the reopen still pins photon: %s" % json.dumps(over)); sys.exit(1)
    print("ok: the scene saved after the reopen carries no photon pin: %s" % json.dumps(over))
PYEOF
}
run author
blob set || exit 1
run reopen
blob check || exit 1
echo "scripting.e2e.photon_pin_read: PASS"
