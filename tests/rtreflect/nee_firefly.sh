#!/bin/bash
# rtreflect.nee_firefly (PHOTON-7 stage 11, 11-1b) — THE REFLECTION FINDS ITS EMITTERS BY NEXT-EVENT ESTIMATION.
# jahlab on photon-firefly (photon-room's shell and lamp, a rough metal plate at perceptual 0.30 under one small bright
# emitter - a 0.3 m cube of radiance 30, 1.2 m up), in Both, at the env's stop; two sessions through the stillness door,
# the arm reflect.nee 1 (shipped: each VNDF draw paired with ONE ray in the emitter's bounding-sphere cone, the balance
# heuristic) and 0 (the BRDF-only estimator, the NEGATIVE CONTROL). THE LAWS:
#   (a) THE TABLE AND THE RAYS: one emitter; NEE rays traced with it on, none with it off (the counter's negative
#       control); the cone's HIT RATE printed (the lead's condition: under 25 % the cone is owed the oriented box).
#   (b) THE VARIANCE: at rest frame 300 the texels still converging with NEE are at most a QUARTER of the BRDF-only
#       estimator's (the stop is the displayed standard error under its share of a code, so the count still converging
#       after R frames is the count whose sigma^2 exceeds R k share^2: a lower variance, fewer). Measured at 960x540:
#       3,221 against 82,273 at rest 300 (1 : 25); the 4,096-sample references put sigma a sample at 27.3 codes against
#       285 (the bias -0.05 codes, 1.5 of the references' SE: spikes/photon-7/stage11/11-1b/reference.txt).
#   (c) THE HOLD: with NEE the reflection HOLDS inside the window with no capped texel; the BRDF-only control does not.
# 480x270; two sessions, each 230 frames of the GI's settle (its rest from ~frame 217) and a 520-frame window -
# about 1,500 frames.
# Every judgement is a FAIL line, never a traceback.
# usage: nee_firefly.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
POSE=0,1.6,2.4,0,0,-0.5
WIN=520
for run in on off; do
    v=1; [ "$run" = off ] && v=0
    printf 'reflections both\narm reflect.nee %s\ncamera %s\nframes 230\nstillness teleport:%s/hold:1 %s out=nee-%s\nstatus\nquit\n' \
        "$v" "$POSE" "$POSE" "$WIN" "$run" > "$WD/$run.lab"
    "$LAB" --headless --no-stdin --size 480x270 --data-root "$WD/data" --env photon-firefly --script "$WD/$run.lab" \
        > "$WD/$run.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($run) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$run.log" | head; tail -5 "$WD/$run.log"; exit 1; }
done
HERE=$(cd "$(dirname "$0")" && pwd)
python3 -I "$HERE/../support/reflect_sources.py" tsv "$WD/data/stillness/nee-on.tsv" "$WD/data/stillness/nee-off.tsv" -- both || {
    echo "rtreflect.nee_firefly: FAIL (the mode label)"; exit 1; }
python3 -I - "$WD" <<'PY'
import csv, json, re, sys
wd = sys.argv[1]
fails = []
def status(run):
    for line in open("%s/%s.log" % (wd, run)):
        if line.startswith("LAB STATUS "):
            return json.loads(line[len("LAB STATUS "):]).get("reflect", {})
    return {}
def rows(run):
    return [r for r in csv.DictReader(open("%s/data/stillness/nee-%s.tsv" % (wd, run)), delimiter="\t") if r["window"] == "walk"]
st = { r: status(r) for r in ("on", "off") }
rs = { r: rows(r) for r in ("on", "off") }
# (a)
on, off = st["on"], st["off"]
rate = on.get("neeHits", 0) / on["neeRays"] if on.get("neeRays") else 0.0
print("(a) the table: %s emitter(s); NEE rays on %s (hits %s: the cone's hit rate %.1f %%), off %s" %
      (on.get("emitters"), on.get("neeRays"), on.get("neeHits"), 100.0 * rate, off.get("neeRays")))
if on.get("emitters") != 1: fails.append("(a) one emitter in the table (%s)" % on.get("emitters"))
if not on.get("neeRays", 0) > 0: fails.append("(a) NEE rays traced with the arm on")
if off.get("neeRays", -1) != 0: fails.append("(a) THE NEGATIVE CONTROL: no NEE ray with the arm off (%s)" % off.get("neeRays"))
# (b)
def active_at(run, k):
    for r in rs[run]:
        if int(r["refl_rest_frames"]) == k and int(r["refl_rest_active"]) >= 0:
            return int(r["refl_rest_active"])
    return None
a_on, a_off = active_at("on", 300), active_at("off", 300)
print("(b) texels still converging at rest frame 300: NEE %s, BRDF-only %s" % (a_on, a_off))
if a_on is None or a_off is None: fails.append("(b) both sessions reached rest frame 300 (the window)")
elif not (4 * a_on <= a_off): fails.append("(b) NEE's still-converging texels at most a quarter of the BRDF-only's (%d vs %d)" % (a_on, a_off))
# (c)
def hold(run):
    for r in rs[run]:
        if r["refl_held"] == "1":
            return int(r["frame"]), int(r["refl_rest_frames"]), int(r["refl_rest_capped"])
    return None
h_on, h_off = hold("on"), hold("off")
print("(c) the hold: NEE %s, BRDF-only %s (window frame, rest frames, capped)" % (h_on, h_off))
if h_on is None: fails.append("(c) with NEE the reflection holds inside the %d-frame window" % len(rs["on"]))
elif h_on[2] > 0: fails.append("(c) no capped texel at the hold (%d)" % h_on[2])
if h_off is not None: fails.append("(c) the BRDF-only control does not hold inside the window (held at %s)" % (h_off,))
for f in fails: print("FAIL:", f)
print("rtreflect.nee_firefly: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
