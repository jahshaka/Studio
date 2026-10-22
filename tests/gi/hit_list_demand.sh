#!/bin/bash
# gi.hit_list_demand (PHOTON-2 stage 2's tail, 1.6 A2; PHOTON-7 stage 0: the forced-overflow arm only) — A DROPPED HIT
# RECORD IS NO SAMPLE, NEVER A BLACK RAY.
# jahlab on photon-room at Epic, 960x540, with the arm gather.decodeHits on (every gather ray is a hit record, ~1.25 W H
# a frame: over the list's creation size, 0.5625 W H, so the list grows - kHitListWidth's law in EnginePrivate.h: created
# at 0.5625 W H x the eyes, doubled on a frame that overflowed it, never shrunk).
# THE LAW: a record the list cannot hold is NO SAMPLE for its ray - the gather's filter and SH sums leave it out
# (kAtlasNoSample); a list overflows at its END, so the probes dispatched last lose every ray, those probes have no
# sample and their pixels keep their history (rq_probe_integrate.comp) - never black.
#   THE OVERFLOW ARM: rq.hitListCap holding the records to a third of the undropped session's demand from the settle on
#   (the truck: a list that overflows on a walk) FORCES drops (dropped > 0), and the floor's mean (the picture's lower
#   third) is within ONE CODE of the undropped session's - the picture's own quantum (one 8-bit code; rule R3): a black
#   ray read +13..+37 codes there. The undropped session is the reference only when it dropped nothing at its read.
# THE BASE (irisgl fc0d7e92, the fixed list and the black drop): the overflow arm did not exist - a drop wrote radiance 0
# at the hit's distance, a black ray in the probe's SH. PHOTON-7 stage 0 (the clean list): capped at 175,640 of 526,920
# records, 351,280 dropped, the floor 67.83 -> 67.87 (0.041 codes).
# usage: hit_list_demand.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
POSE=0,2.5,3,0,0.5,-4
# THE TRUCK (the readings): a held view traces nothing and appends nothing, so the last 30 frames move the eye 1 cm a
# frame - every frame traces, and the status reads a frame's whole demand
TRUCK=$(awk 'BEGIN { for (i = 1; i <= 30; ++i) printf "camera %.2f,2.5,3,%.2f,0.5,-4\nframes 1\n", i * 0.01, i * 0.01 }')
run() {   # name cap
    { printf 'arm gather.decodeHits 1\ncamera %s\nframes 120\n' "$POSE"
      [ "$2" -gt 0 ] && printf 'arm rq.hitListCap %s\n' "$2"
      printf '%s\nstatus %s/%s.json\nshot %s\nquit\n' "$TRUCK" "$WD" "$1" "$1"; } > "$WD/$1.lab"
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-room --script "$WD/$1.lab" \
        > "$WD/$1.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$1.log" | head; tail -5 "$WD/$1.log"; exit 1; }
}
run undropped 0
REC=$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['hits']['records'])" "$WD/undropped.json") || { echo "FAIL: the undropped session's status"; exit 1; }
CAP=$(( REC / 3 ))
[ "$CAP" -gt 0 ] || { echo "FAIL: the decode-all session appended no record ($REC)"; exit 1; }
run capped "$CAP"
python3 -I - "$WD" "$CAP" <<'PY'
import json, os, sys
wd, cap = sys.argv[1], int(sys.argv[2])
fails = []
s = json.load(open(os.path.join(wd, "undropped.json")))["hits"]
c = json.load(open(os.path.join(wd, "capped.json")))["hits"]
W = 1024
create = -(-(-(-(9 * 960 * 540) // 16)) // W)   # ceil(ceil(0.5625 W H) / 1024): the list's creation rows at 960x540
rows = s["capacity"] // W
print("the undropped session: records %d, dropped %d, capacity %d (%d rows = %.2f x the creation size's %d)" % (
      s["records"], s["dropped"], s["capacity"], rows, rows / create, create))
if s["dropped"] != 0: fails.append("the undropped session dropped %d records at its read: no reference" % s["dropped"])
print("the overflow arm at %d: records %d, dropped %d" % (cap, c["records"], c["dropped"]))
if not c["dropped"] > 0: fails.append("the overflow arm forced no drop")
try:
    from PIL import Image
except ImportError:
    Image = None
if not Image:
    fails.append("PIL is missing: the pictures cannot be compared")
else:
    def floor(name):
        im = Image.open(os.path.join(wd, "data", "shots", name + ".png")).convert("RGB")
        w, h = im.size
        px = list(im.crop((0, 2 * h // 3, w, h)).get_flattened_data()) if hasattr(im, 'get_flattened_data') else list(im.crop((0, 2 * h // 3, w, h)).getdata())
        n = len(px)
        return [sum(p[k] for p in px) / n for k in range(3)], im
    a, ia = floor("undropped")
    b, ib = floor("capped")
    d = max(abs(a[k] - b[k]) for k in range(3))
    flat = lambda im: list(im.get_flattened_data()) if hasattr(im, 'get_flattened_data') else list(im.getdata())
    da, db = flat(ia), flat(ib)
    whole = [sum(p[k] for p in da) / len(da) - sum(p[k] for p in db) / len(db) for k in range(3)]
    print("the floor's mean: undropped %s, dropped %s - |diff| max %.3f codes (the whole picture's signed diff %s)" % (
          "/".join("%.2f" % v for v in a), "/".join("%.2f" % v for v in b), d, "/".join("%+.3f" % v for v in whole)))
    if d > 1.0: fails.append("the floor within one code (the picture's quantum) of the undropped session: %.3f" % d)
for f in fails: print("FAIL:", f)
print("gi.hit_list_demand: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
