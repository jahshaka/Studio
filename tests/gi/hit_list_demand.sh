#!/bin/bash
# gi.hit_list_demand (PHOTON-2 stage 2's tail, 1.6 A2) — THE HIT LIST IS SIZED BY ITS DEMAND, AND A DROP IS NO SAMPLE.
# jahlab on photon-room at Epic, 960x540, with the arm gather.decodeHits on (every gather ray is a hit record, ~1.25 W H
# a frame: the demand the old fixed list - 0.5625 W H, kHitListHeightFactor - held less than half of).
# THE LAW (kHitListWidth's in EnginePrivate.h): the list's rows follow the records the traces asked for (the counter
# counts past the capacity), grown to 2x that demand as soon as it passes 3/4 of the list; a record the list cannot
# hold is NO SAMPLE for its ray (the gather's filter and SH sums leave it out: kAtlasNoSample), never a black ray.
#   (a) SIZED: after the settle the list drops nothing (dropped 0), holds the demand (capacity >= records) and is
#       sized FROM it (capacity <= 2 x records + one row) - never a constant;
#   (b) NO DARKENING: the overflow arm rq.hitListCap holding the list to a third of that demand from the settle on (the
#       truck: a list that overflows on a walk) FORCES drops (dropped > 0) and the floor's mean (the picture's lower
#       third) is within 1 code of the undropped session's. A list overflows at its END - the probes dispatched last
#       lose every ray - so those probes have NO SAMPLE and their pixels keep their history (rq_probe_integrate.comp);
#       a probe that lost some rays is integrated over the rest (rq_probe_filter.comp).
# THE BASE (irisgl fc0d7e92, the fixed list and the black drop): (a) dropped about half the demand every frame;
# (b) the overflow arm did not exist - a drop wrote radiance 0 at the hit's distance, a black ray in the probe's SH.
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
run sized 0
REC=$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['hits']['records'])" "$WD/sized.json") || { echo "FAIL: the sized session's status"; exit 1; }
CAP=$(( REC / 3 ))
[ "$CAP" -gt 0 ] || { echo "FAIL: the decode-all session appended no record ($REC)"; exit 1; }
run capped "$CAP"
python3 -I - "$WD" "$CAP" <<'PY'
import json, os, sys
wd, cap = sys.argv[1], int(sys.argv[2])
fails = []
s = json.load(open(os.path.join(wd, "sized.json")))["hits"]
c = json.load(open(os.path.join(wd, "capped.json")))["hits"]
W = 1024
print("(a) sized: records %d, dropped %d, capacity %d (2 x records + a row = %d)" % (s["records"], s["dropped"],
      s["capacity"], 2 * s["records"] + W))
if s["dropped"] != 0: fails.append("(a) the sized list dropped %d records" % s["dropped"])
if s["capacity"] < s["records"]: fails.append("(a) capacity %d < records %d" % (s["capacity"], s["records"]))
if s["capacity"] > 2 * s["records"] + W: fails.append("(a) capacity %d not sized from the demand %d" % (s["capacity"], s["records"]))
print("(b) capped at %d: records %d, dropped %d" % (cap, c["records"], c["dropped"]))
if not c["dropped"] > 0: fails.append("(b) the overflow arm forced no drop")
try:
    from PIL import Image
except ImportError:
    Image = None
if not Image:
    fails.append("(b) PIL is missing: the pictures cannot be compared")
else:
    def floor(name):
        im = Image.open(os.path.join(wd, "data", "shots", name + ".png")).convert("RGB")
        w, h = im.size
        px = list(im.crop((0, 2 * h // 3, w, h)).get_flattened_data()) if hasattr(im, 'get_flattened_data') else list(im.crop((0, 2 * h // 3, w, h)).getdata())
        n = len(px)
        return [sum(p[k] for p in px) / n for k in range(3)], im
    a, ia = floor("sized")
    b, ib = floor("capped")
    d = max(abs(a[k] - b[k]) for k in range(3))
    flat = lambda im: list(im.get_flattened_data()) if hasattr(im, 'get_flattened_data') else list(im.getdata())
    da, db = flat(ia), flat(ib)
    whole = [sum(p[k] for p in da) / len(da) - sum(p[k] for p in db) / len(db) for k in range(3)]
    print("(b) the floor's mean: undropped %s, dropped %s - |diff| max %.3f codes (the whole picture's signed diff %s)" % (
          "/".join("%.2f" % v for v in a), "/".join("%.2f" % v for v in b), d, "/".join("%+.3f" % v for v in whole)))
    if d > 1.0: fails.append("(b) the floor within 1 code of the undropped session: %.3f" % d)
for f in fails: print("FAIL:", f)
print("gi.hit_list_demand: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
