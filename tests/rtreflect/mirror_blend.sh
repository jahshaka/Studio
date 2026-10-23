#!/bin/bash
# rtreflect.mirror_blend (PHOTON-7 stage 6, READ_B F2) — THE MIRROR TAKES THE COIN'S EXPECTATION: NO SEAM WHERE THE
# SCREEN'S TRUST FADES. The reflection's lobe CENTRE ray (a mirror's whole sample, every frame) in Both mode is answered
# by the screen trace where the screen vouches for it and by the ray where it does not; inside the fade (0 < vouch < 1)
# it takes BOTH, vouch x screen + (1 - vouch) x ray (rq_reflect.comp's (1)). The base took a FIXED coin of one half:
# the screen's answer whole where vouch > 1/2, the ray's whole elsewhere - a hard seam wherever the two disagree, that
# moved with the camera.
# THE LAW, read on Both (B) against Rays (R) at the same pose: the mirror is deterministic (the centre ray, no history,
# no spatial support), so the screen's share E = B - R is exactly 0 where the ray answers alone and
# vouch x (screen - ray) inside the fade. Its step between neighbouring pixels is then at most |screen - ray| x the
# fade's slope plus vouch x the step of (screen - ray): the fades are smoothsteps no narrower than a few dozen pixels
# here (the camera reversal's over 0.25..0.85 of the view-ray dot, the arrival's over 0.2, the frame edge's over 12 % of
# it), so a slope under 0.05 a pixel, and |screen - ray| in this room is under 60 codes - a step of under 3 codes from
# the fade. THE BAR: no pair of neighbouring mirror pixels (columns or rows), away from the ray picture's own edges
# (any step over 4 codes in a pixel or 8 across four, within 3 px: a reflected silhouette or material edge, where the
# screen's own same-surface acceptance is a binary
# test by design and the two answers disagree on where the silhouette lies) and 4 px off the mirror box's faces, whose
# E steps from <= 1 code on one side to >= 12 codes on the other - the whole screen-ray difference crossed in one
# pixel, the base's seam. 0 such pairs, at each of three
# poses of a slow pan (the camera 3 m up, 1.2 / 1.5 / 1.8 m to the side, looking down at photon-glass's mirror on the
# far wall: incidence near 28 degrees, where the camera-reversal fade crosses one half inside the mirror).
# THE BASE (the fixed coin, measured on this pan): red - the seam runs across the mirror's lower half at every pose.
# jahlab at 960x540; one session: a 60-frame prime, then per pose 8 frames in Both and 8 in Rays (108 frames).
# usage: mirror_blend.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
{
    echo 'reflections both'
    echo 'camera 1.5,3.0,1.0,0,0.8,-4'
    echo 'frames 60'
    for x in 1.2 1.5 1.8; do
        echo "camera $x,3.0,1.0,0,0.8,-4"
        echo 'reflections both'; echo 'frames 8'; echo "shot b$x"
        echo 'reflections rays'; echo 'frames 8'; echo "shot r$x"
    done
    echo 'quit'
} > "$WD/pan.lab"
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/pan.lab" \
    > "$WD/pan.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/pan.log" | head; exit 1; }
# THE MODE LABEL: each shot's sources as the engine ran them (b<x> Both, r<x> Rays)
HERE=$(cd "$(dirname "$0")" && pwd)
python3 -I "$HERE/../support/reflect_sources.py" shots "$WD/pan.log" -- b=both r=rays
srcrc=$?
python3 -I - "$WD/data/shots" <<'PY'
import math, os, sys
import numpy as np
from PIL import Image
shots = sys.argv[1]
W, H = 960, 540
TY = math.tan(math.radians(22.5)); TX = TY * W / H
fails = []
def lum(name):
    path = os.path.join(shots, name + '.png')
    if not os.path.exists(path):
        fails.append('%s: no shot' % name); return None
    return np.asarray(Image.open(path).convert('RGB')).astype(float) @ np.array([0.2126, 0.7152, 0.0722])
def mirrorMask(eye, tgt):
    # the mirror's front face (x -0.8 .. 0.8, y 0.4 .. 2.4, z -3.97), eroded 6 px: each pixel's ray against its plane
    eye = np.array(eye, float); f = np.array(tgt, float) - eye; f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    ys, xs = np.mgrid[0:H, 0:W]
    xn = ((xs + 0.5) / (0.5 * W) - 1.0) * TX; yn = (1.0 - (ys + 0.5) / (0.5 * H)) * TY
    d = f[None, None, :] + r[None, None, :] * xn[..., None] + u[None, None, :] * yn[..., None]
    t = (-3.97 - eye[2]) / d[..., 2]
    P = eye + d * t[..., None]
    m = 0.06
    inside = (P[..., 0] > -0.8 + m) & (P[..., 0] < 0.8 - m) & (P[..., 1] > 0.4 + m) & (P[..., 1] < 2.4 - m)
    return erode(inside, 4)   # ...and 4 px off the box's side faces, which the front plane's test cannot see
def erode(mask, k):
    out = mask.copy()
    for dy in range(-k, k + 1):
        for dx in range(-k, k + 1):
            out &= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out
def dilate(mask, k):
    out = mask.copy()
    for dy in range(-k, k + 1):
        for dx in range(-k, k + 1):
            out |= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out
total = 0
for x in ('1.2', '1.5', '1.8'):
    B, R = lum('b' + x), lum('r' + x)
    if B is None or R is None: continue
    M = mirrorMask((float(x), 3.0, 1.0), (0.0, 0.8, -4.0))
    E = B - R
    band = int((M & (np.abs(E) > 1.0)).sum())
    # THE RAY PICTURE'S EDGES, 3 px around (a reflected silhouette): there the screen's own same-surface acceptance is
    # a binary test by design - the stochastic texels share it - and the screen and the ray disagree on where the
    # silhouette lies (that disagreement is the screen - ray difference itself), so a step there is not the coin's
    # (a step over 4 codes in one pixel, or over 8 across four: a SOFT edge - the ray answers a reflected material edge
    # from the hit's card, softer than the screen's picture of it)
    edgeR = np.zeros_like(M)
    edgeR[:, :-1] |= np.abs(R[:, 1:] - R[:, :-1]) > 4.0
    edgeR[:-1, :] |= np.abs(R[1:, :] - R[:-1, :]) > 4.0
    edgeR[:, 2:-2] |= np.abs(R[:, 4:] - R[:, :-4]) > 8.0
    edgeR[2:-2, :] |= np.abs(R[4:, :] - R[:-4, :]) > 8.0
    smooth = M & ~dilate(edgeR, 3)
    seams = 0
    for ax in (0, 1):
        a = (slice(None), slice(0, -1)) if ax == 1 else (slice(0, -1), slice(None))
        b = (slice(None), slice(1, None)) if ax == 1 else (slice(1, None), slice(None))
        both = smooth[a] & smooth[b]
        lo = np.minimum(np.abs(E[a]), np.abs(E[b])); hi = np.maximum(np.abs(E[a]), np.abs(E[b]))
        seams += int((both & (lo <= 1.0) & (hi >= 12.0)).sum())
    print('pose x %s: the mirror %d px, %d of them carrying a screen share (|B - R| > 1 code); neighbour pairs whose '
          'screen share steps from <= 1 to >= 12 codes (a seam): %d' % (x, int(M.sum()), band, seams))
    if M.sum() < 20000: fails.append('pose x %s: the mirror covers %d px (< 20000): the fixture moved' % (x, int(M.sum())))
    if band < 500: fails.append('pose x %s: only %d mirror px carry a screen share - the fade is not in view' % (x, band))
    total += seams
if total: fails.append('%d seam pairs: the mirror takes the screen or the ray whole across the fade' % total)
for f in fails: print('FAIL: ' + f)
print('rtreflect.mirror_blend: %s' % ('FAIL' if fails else 'PASS'))
sys.exit(1 if fails else 0)
PY
rc=$?
[ $srcrc -eq 0 ] || { echo "rtreflect.mirror_blend: FAIL (the mode label)"; exit 1; }
exit $rc
