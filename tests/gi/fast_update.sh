#!/bin/bash
# gi.fast_update (PHOTON-7 stage 8B, B5's acceptance) - THE FAST UPDATE: a probe whose rays' hits MOVED this frame
# shortens the history of the pixels it lights, so the gather follows a moving thing's light instead of averaging it
# over the floor's frames. LUMEN's FastUpdateMode (LumenScreenProbeGatherTemporal.usf:486-497), mapped to ours:
#   a probe's moving share m = the share of its rays whose hit lies in a brick this frame's light work changed (the
#   finest cascade holding it), on a slot whose two poses differ, or on a skinned item posed this frame
#   (rq_probe_gather.comp fastRayMoving); the pixel's m is its probes', interpolated as its irradiance is;
#   F = saturate( min( ( saturate( m / 0.1 ) - 0.2 ) / 0.8, 0.9 ) ), max'ed with last frame's F (one frame of memory).
# OUR N' LAW, DERIVED (not copied): the integrate's blend is 1 / (1 + N) with N = min(count, Nmax) and Nmax = 1/floor - 1
# (the floor's frames: Lumen's MaxFramesAccumulated). Lumen caps N' = min(N, (1 - F) Nmax) and blends 1 / (1 + N'); the
# history then STORES N' (+ 1 this frame), so after the motion the mean restarts over the new pose's frames
# (1/(N'+2), 1/(N'+3) ...) instead of returning to the floor over a mean that still holds the old pose. With nothing
# moving F = 0 and the blend and the count are bit for bit what they were (gi.rest_oracle_* hold the rest).
# THE SCENE: photon-glass (the gather's own picture: the photon diffuse view, 480x270), its mover slid 1.5 m across the
# r0.3 pane in 60 frames, then still. THE LAW:
#   (1) the instrument sees the motion: probes with a moving ray, and probes whose own F passes one half, during the slide
#       (the status's peaks over the retired frames);
#   (2) THE SETTLE IS HALVED: the frames after the motion until the picture is within the bar of the final held one
#       (worst <= 16 codes, no pixel off by 10 - gi.cascades' motion law) are at most HALF the same scene's with the fast
#       update off (measured once with the arm gather.fastUpdateOff, committed and reverted: kOffSettle below).
# A NAMED LIMIT (the lead's ruling, 2026-10-29): the moving ray's brick input is "changed AT ALL", so in a sealed room one
# mover moves the bounce in almost every brick and nearly EVERY probe runs fast while anything moves (2115 of 2040 uniform
# probes + twins with F > 1/2 during the slide) - physics (the light did change), but most of it under a display code, so
# the whole picture takes the short history's noise for the motion. The law's own input would be the pixel's lag
# N'.|dL| against oneCodeAt(L) (tests/support/displaycode.h); a brick word cannot carry it - measured, the arm
# gather.fastThreshold (irisgl 9f890394 / 62fb3d3c, reverted), probes with F > 1/2 over six slides at N = 1/2/3/5/9:
# the brick mask's RELATIVE word x the ray's light 1827/1901/1891/1925/1900; the feedback's ABSOLUTE word (state [4]/[7])
# 2139/2138/2138/2139/2135 - a max over a brick's 512 voxels is not the hit's change. Lumen's own input (moved slot,
# posed skin) alone: 187 fast and a 6-frame settle (not halved). The exact input is the HIT VOXEL's own absolute change
# this frame (a per-voxel word): the spec's leftover.
# usage: fast_update.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
{
  echo "envcam 0"
  echo "photon diffuse"
  echo "frames 300"
  echo "slide mover -1.5,0,0 60"
  echo "status sm.json"
  echo "shot m"
  for k in $(seq 2 2 40); do echo "frames 2"; echo "shot s$k"; done
  echo "frames 260"
  echo "shot r"
  echo "quit"
} > "$WD/f.lab"
"$LAB" --headless --no-stdin --size 480x270 --data-root "$WD/data" --env photon-glass --script "$WD/f.lab" > "$WD/f.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: the run exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/f.log" | head; tail -5 "$WD/f.log"; exit 1; }
python3 -I - "$WD/data" <<'PY'
import json, os, sys, zlib, struct
d = sys.argv[1]
# THE SAME SCENE WITH THE FAST UPDATE OFF (the arm gather.fastUpdateOff, irisgl b0d55bc6, one run 2026-10-29, reverted):
# the motion's last frame 21/255 with 207 px >= 10 off the held picture, the bar reached 8 frames after the motion
# (on, the same build: 13/255, 43 px; 2 frames - the first shot)
K_OFF_SETTLE = 8
fails = []
def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: fails.append(what)
def png(path):
    # a minimal PNG reader (8-bit RGB/RGBA, filters 0-4): the shot's pixels, no third-party module
    data = open(path, "rb").read()
    pos, w, h, ct, idat = 8, 0, 0, 0, b""
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]; t = data[pos + 4:pos + 8]; c = data[pos + 8:pos + 8 + n]
        if t == b"IHDR": w, h, _, ct = struct.unpack(">IIBB", c[:10])
        elif t == b"IDAT": idat += c
        pos += 12 + n
    bpp = 4 if ct == 6 else 3
    raw, stride, out, prev = zlib.decompress(idat), w * bpp, bytearray(), bytearray(w * bpp)
    for y in range(h):
        f, row = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = row[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: row[i] = (row[i] + a) & 255
            elif f == 2: row[i] = (row[i] + b) & 255
            elif f == 3: row[i] = (row[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += row; prev = row
    return w, h, bpp, out
g = json.loads(open(os.path.join(d, "sm.json")).read().splitlines()[-1])["gi"]["gather"]
mv, fa = g.get("movingProbesPeak", 0), g.get("fastProbesPeak", 0)
check(mv > 0 and fa > 0, "(1) the slide's probes: %d with a moving ray, %d with their own F past one half (of %d)"
      % (mv, fa, g.get("probes", 0)))
shots = os.path.join(d, "shots")
wr, hr, br, r = png(os.path.join(shots, "r.png"))
def against(name):
    w, h, b, a = png(os.path.join(shots, name + ".png"))
    if (w, h, b) != (wr, hr, br): return 999, 1 << 30
    worst, over = 0, 0
    for i in range(0, len(a), b):
        m = max(abs(a[i + k] - r[i + k]) for k in range(3))
        if m > worst: worst = m
        if m >= 10: over += 1
    return worst, over
w0, o0 = against("m")
print("  the motion's last frame vs the held picture: worst %d/255, %d px >= 10" % (w0, o0))
settle = -1
for k in range(2, 41, 2):
    w, o = against("s%d" % k)
    print("  +%2d frames: worst %d/255, %d px >= 10" % (k, w, o))
    if settle < 0 and w <= 16 and o == 0: settle = k
print("  THE SETTLE: the bar reached %d frames after the motion (-1: not within 40); off (the arm, measured): %d"
      % (settle, K_OFF_SETTLE))
check(settle > 0 and K_OFF_SETTLE > 0 and 2 * settle <= K_OFF_SETTLE,
      "(2) the settle halved by the fast update: %d frames against %d off" % (settle, K_OFF_SETTLE))
print("gi.fast_update: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
