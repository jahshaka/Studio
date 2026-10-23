#!/bin/bash
# rtreflect.contact (PHOTON-7 stage 6, AB E15 / ONE_PICTURE C3; READ_A findings 1 and 2) — THE CONTACT MEETS ITS
# REFLECTION, AND NO RAY HITS ITS OWN SURFACE. jahlab, 1920x1080 (the 45-degree view the projection below assumes), 50
# frames a shot (the mirror is the lobe's centre ray: exact on its first frame; 50 clears the young view's warm-up and
# the GI's first frames). Two sessions:
#   photon-ramp: the 0.00 strip's post on its 10 cm matte black FOOT (0.02-0.12 m), an emitter above it, from 2.4 m
#     (envcam 2) and 1 m (envcam 3), the reflections RAYS-only and BOTH (the screen trace first).
#   photon-contact: the same mark on a CHAINED, CORRUGATED mirror floor (a welded grid each of whose vertices sits 0.2 mm
#     above or below the plane: Atom's cut draws its coarse plane, which lies BELOW level 0's peaks, while every ray
#     traces level 0) from 2.4 m and 1 m (envcams 1, 2); the CREASE, a bright matte block standing on that floor, from
#     3.4 m (envcam 3, the Photon view's reflections alone); and the NEGATIVE CONTROL: the arm rays.originCut 0 (the
#     origin's drawn-cut term off) at envcams 1 and 2.
# THE LAWS (the mirror's geometry and the origin law, exact):
#   (a) THE SEAM AT THE CONTACT - the rows between the foot's contact line (y 0.02) and its reflection's first dark row -
#       <= 1 px, every arm (measured down the foot's column, c3.py's method, spikes/photon-2/round5/ramp).
#   (b) the foot's reflected top edge (the half-level crossing from the dark foot to the post's bright reflection)
#       within 1 px of where the mirror puts it FROM THE LIFTED ORIGIN - the origin law's own, bounded offset: the ray
#       starts the drawn cut's error (Epic's 1 px footprint at the floor point) above the floor, so it meets the foot that
#       much higher (~1 px) - on the RAYS arms of the flat strip; the BOTH arms' and the corrugated floor's (its zigzag
#       is a fifth of a pixel) are printed.
#   (c) NO RAY HITS ITS OWN FLOOR (READ_A finding 2): inside the band where the flat mirror reflects the emitter panel
#       over the far wall (y 0.8-7.9; a 0.15 m margin, the post, the foot, the block and the walls kept out) the picture is
#       the mirror image of one uniform emitter: no pixel more than 3 codes from its row's median (the metal's Fresnel
#       drifts 2 codes from row to row over the band, which the row's median follows) - the encode's dither, 2 codes a
#       channel, and the luminance's mix of independently dithered channels, which puts a row's median up to a code off
#       an integer (measured: 2.07 at most) - a ray started inside the traced level 0 hits it from below and reads some
#       other radiance (measured: up to 92 codes off). THE NEGATIVE CONTROL must be RED there (such pixels at 2.4 m or
#       1 m: measured 10,827 at 2.4 m): it proves the receiver can fail, the drawn cut below level 0 - the flat welded
#       grid this floor replaces could not (every level of a plane is the plane).
#   (d) THE CREASE (READ_A finding 1): along the block's base (placed on a stretch the cut draws coarse from 3.4 m,
#       measured), the floor's FIRST row below the crease steps from the second by no more than the second steps from
#       the third, plus the encode's 2 codes - a ray whose lift went SIDEWAYS (the depth's nearer neighbour across the
#       crease: the side pick before the fix) keeps the drawn plane under level 0 and hits its own floor: a one-row line
#       along the crease (measured with the old pick: 132 of 747 columns; with the fix 0).
# THE BASE (photon-2 round 5, the 1/2-cell bias + tMin, Both): at 1 m the reflected foot ENTIRELY missing (SEAM 99.3
# rows, the top edge 84.9 rows off); at 2.4 m SEAM 7.4 rows, the edge 5.7 off.
# 9 shots x 50 = 450 frames; two jahlab sessions.
# usage: contact.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/ramp.lab" <<S
reflections rays
envcam 2
frames 50
shot rays-2p4
envcam 3
frames 50
shot rays-1m
reflections both
envcam 2
frames 50
shot both-2p4
envcam 3
frames 50
shot both-1m
quit
S
cat > "$WD/chained.lab" <<S
reflections rays
envcam 1
frames 50
shot chained-2p4
envcam 2
frames 50
shot chained-1m
photon reflections
envcam 3
frames 50
shot crease-3p4
photon off
arm rays.originCut 0
envcam 1
frames 50
shot nocut-2p4
envcam 2
frames 50
shot nocut-1m
quit
S
for run in ramp:photon-ramp chained:photon-contact; do
    name=${run%%:*}; env=${run#*:}
    "$LAB" --headless --no-stdin --size 1920x1080 --data-root "$WD/data" --env "$env" --script "$WD/$name.lab" \
        > "$WD/$name.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($name) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$name.log" | head; tail -5 "$WD/$name.log"; exit 1; }
done
python3 -I - "$WD/data/shots" <<'PY'
import math, os, sys
import numpy as np
from PIL import Image
shots = sys.argv[1]
fails = []
TY = math.tan(math.radians(22.5))
def load(name):
    path = os.path.join(shots, name + '.png')
    if not os.path.exists(path):
        fails.append('%s: no shot' % name); return None
    img = np.asarray(Image.open(path).convert('RGB')).astype(float)
    return img @ np.array([0.2126, 0.7152, 0.0722])
def frame(eye, tgt, W, H):
    eye = np.array(eye, float); f = np.array(tgt, float) - eye; f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    tx = TY * W / H
    proj = lambda p: ((((p - eye) @ r) / (((p - eye) @ f) * tx) + 1) * .5 * W - .5,
                      (1 - ((p - eye) @ u) / (((p - eye) @ f) * TY)) * .5 * H - .5)
    return eye, f, r, u, tx, proj
def seam(name, eye, gateEdge):
    lum = load(name)
    if lum is None: return
    H, W = lum.shape
    eye, f, r, u, tx, proj = frame(eye, (-1.88, 0.1, -3.0), W, H)
    foot = proj(np.array([-1.88, 0.02, -3.0])); mirTop = proj(np.array([-1.88, -0.08, -3.0]))
    # THE LIFTED MIRROR: the floor point P reflecting the foot's top edge, the lift there (the drawn cut's error: the
    # lab's Epic, atomPixelError 1, one footprint = the view distance x 2 tan(22.5) / H), the edge it meets that much
    # higher - the mirror image of (0.12 + h) is (-0.08 + h)
    M = np.array([-1.88, -0.08, -3.0]); P = eye + (M - eye) * ((eye[1] - 0.02) / (eye[1] - M[1]))
    h = 1.0 * ((P - eye) @ f) * 2.0 * TY / H
    liftedTop = proj(np.array([-1.88, -0.08 + h, -3.0]))
    cx = int(round(foot[0])); col = lum[:, cx - 3:cx + 4].mean(1)
    fy = int(round(foot[1]))
    dark = col[fy - 20:fy - 5].mean()   # the foot itself, above the contact line
    bright = np.median(col[int(mirTop[1]) + 15:int(mirTop[1]) + 45])   # the post's reflection below the foot's
    thr = 0.5 * (dark + bright)
    # (a) the contact: rows from the contact line to the reflection's first dark row
    first = next((k for k in range(int(math.ceil(foot[1])), int(mirTop[1]) + 1) if col[k] < thr), None)
    gap = (first - foot[1]) if first is not None else float('inf')
    # (b) the reflected top edge: the dark -> bright crossing below the contact
    seen = float('nan')
    for k in range(int(math.ceil(foot[1])), H - 1):
        if col[k] < thr <= col[k + 1]:
            seen = k + (thr - col[k]) / max(col[k + 1] - col[k], 1e-9); break
    edge = seen - liftedTop[1]
    print('%-12s contact line row %.1f, first dark row %s -> SEAM %.1f px; top edge seen %.1f, the mirror %.1f, from the'
          ' lifted origin (%.2f mm) %.1f -> %+.1f px%s (dark %.1f bright %.1f)' %
          (name, foot[1], first, gap, seen, mirTop[1], 1000.0 * h, liftedTop[1], edge,
           '' if gateEdge else ' [printed]', dark, bright))
    if not (gap <= 1.0): fails.append('%s: the seam at the contact %.1f px > 1' % (name, gap))
    if gateEdge and not (abs(edge) <= 1.0):
        fails.append('%s: the reflected top edge %+.1f px from the lifted mirror (> 1)' % (name, edge))
# THE BOXES a reflected ray or a view ray must keep clear of (lo, hi), inflated by the margin
BOXES = [((-1.93, 0.02, -3.15), (-1.83, 0.62, -3.0)),        # the foot and the post
         ((1.0, 0.02, -2.57), (2.8, 0.8, -2.17))]            # the crease block
def segHitsBox(o, d, t, lo, hi, m):
    lo = np.array(lo) - m; hi = np.array(hi) + m
    inv = 1.0 / np.where(np.abs(d) < 1e-12, 1e-12, d)
    a = (lo - o) * inv; b = (hi - o) * inv
    tn = np.minimum(a, b).max(-1); tf = np.maximum(a, b).min(-1)
    return (np.maximum(tn, 0.0) <= np.minimum(tf, t))
def selfHits(name, eye):
    lum = load(name)
    if lum is None: return None
    H, W = lum.shape
    eye, f, r, u, tx, proj = frame(eye, (-1.88, 0.1, -3.0), W, H)
    ys, xs = np.mgrid[0:H, 0:W]
    xn = (xs + 0.5) / (0.5 * W) - 1.0; yn = 1.0 - (ys + 0.5) / (0.5 * H)
    d = f[None, None, :] + r[None, None, :] * (xn * tx)[..., None] + u[None, None, :] * (yn * TY)[..., None]
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    ok = d[..., 1] < -1e-6
    t = np.where(ok, (0.02 - eye[1]) / np.where(ok, d[..., 1], -1.0), 0.0)
    P = eye + d * t[..., None]
    m = 0.15
    ok &= (np.abs(P[..., 0]) < 4.0 - m) & (P[..., 2] > -4.0 + m) & (P[..., 2] < 4.0 - m)
    R = d * np.array([1.0, -1.0, 1.0])                       # the flat mirror's reflected direction
    ok &= R[..., 2] < -1e-6
    s = np.where(ok, (-3.95 - P[..., 2]) / np.where(ok, R[..., 2], -1.0), 0.0)
    Wp = P + R * s[..., None]
    ok &= (Wp[..., 1] > 0.8 + m) & (Wp[..., 1] < 7.9 - m) & (np.abs(Wp[..., 0]) < 3.9 - m)
    flat = lambda a: a.reshape(-1, 3)
    for lo, hi in BOXES:
        hitV = segHitsBox(flat(np.broadcast_to(eye, P.shape)), flat(d), t.reshape(-1), lo, hi, 0.05).reshape(H, W)
        hitR = segHitsBox(flat(P), flat(R), s.reshape(-1), lo, hi, 0.1).reshape(H, W)
        ok &= ~hitV & ~hitR
    band = lum[ok]
    if band.size < 1000:
        fails.append('%s: the panel band holds %d px (< 1000): the fixture moved' % (name, band.size)); return None
    # against its ROW's median: the emitter's mirror image is uniform along a row, and its row-to-row drift (the metal's
    # Fresnel with the angle, 2 codes over the band) is the row median's
    offPx = 0
    for y in range(H):
        row = lum[y][ok[y]]
        if row.size < 16: continue
        offPx += int((np.abs(row - np.median(row)) > 3.0).sum())
    print('%-12s the panel band: %d px, %d more than 3 codes off their row (rays that hit their own floor)' %
          (name, band.size, offPx))
    return offPx
def crease(name):
    lum = load(name)
    if lum is None: return
    H, W = lum.shape
    eye, f, r, u, tx, proj = frame((1.9, 1.3, 0.45), (1.9, 0.1, -2.75), W, H)
    a = proj(np.array([1.0, 0.02, -2.17])); b = proj(np.array([2.8, 0.02, -2.17]))
    lineCols = 0; cols = 0; worst = []
    for x in range(int(math.ceil(a[0])) + 30, int(b[0]) - 30):
        c = a[1] + (b[1] - a[1]) * (x - a[0]) / (b[0] - a[0])
        k = int(math.ceil(c))   # the first floor row below the crease line (its upper neighbour is the block's)
        cols += 1
        j1 = abs(lum[k, x] - lum[k + 1, x]); j2 = abs(lum[k + 1, x] - lum[k + 2, x])
        if j1 > j2 + 2.0:
            lineCols += 1
            if len(worst) < 5: worst.append((x, k, [int(v) for v in lum[k - 1:k + 3, x]]))
    print('%-12s the crease: %d columns, %d whose first floor row steps off the rows below (a line)%s' %
          (name, cols, lineCols, (' ' + str(worst)) if worst else ''))
    if cols < 200: fails.append('%s: the crease spans %d columns (< 200): the fixture moved' % (name, cols))
    if lineCols: fails.append('%s: %d columns of the crease hit their own floor (a line along it)' % (name, lineCols))
for name, eye, gate in (('rays-2p4', (-1.88, 1.02, -0.8), True), ('rays-1m', (-1.88, 0.62, -2.2), True),
                        ('both-2p4', (-1.88, 1.02, -0.8), False), ('both-1m', (-1.88, 0.62, -2.2), False),
                        ('chained-2p4', (-1.88, 1.02, -0.8), False), ('chained-1m', (-1.88, 0.62, -2.2), False)):
    seam(name, eye, gate)
for name, eye in (('chained-2p4', (-1.88, 1.02, -0.8)), ('chained-1m', (-1.88, 0.62, -2.2))):
    n = selfHits(name, eye)
    if n is not None and n > 0: fails.append('%s: %d rays hit their own floor in the panel band' % (name, n))
crease('crease-3p4')
neg = [selfHits(name, eye) for name, eye in (('nocut-2p4', (-1.88, 1.02, -0.8)), ('nocut-1m', (-1.88, 0.62, -2.2)))]
if not any(n for n in neg if n):
    fails.append('the NEGATIVE CONTROL is green: with the drawn-cut term off no ray hit its own floor - the chained '
                 'receiver cannot fail')
else:
    print('the negative control (rays.originCut 0) is red, as it must be: %s px off their row' % neg)
for f in fails: print('FAIL: ' + f)
print('rtreflect.contact: %s' % ('FAIL' if fails else 'PASS'))
sys.exit(1 if fails else 0)
PY
