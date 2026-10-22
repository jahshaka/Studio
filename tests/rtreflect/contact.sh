#!/bin/bash
# rtreflect.contact (PHOTON-7 stage 6, AB E15 / ONE_PICTURE C3) — THE CONTACT MEETS ITS REFLECTION. jahlab on photon-ramp
# (the 0.00 strip's post on its 10 cm matte black FOOT, 0.02-0.12 m, an emitter above it) from 2.4 m (envcam 2) and 1 m
# (envcam 3), with the reflections RAYS-only and BOTH (the screen trace first), and on photon-contact (the same mark on
# a CHAINED mirror floor: a welded grid with its level chain, drawn through Atom's cut while the rays trace its level 0)
# from 2.4 m and 1 m. 1920x1080 (the 45-degree view the projection below assumes), 50 frames a shot (the mirror is the
# lobe's centre ray: exact on its first frame; 50 clears the young view's warm-up and the GI's first frames).
# THE LAW (the mirror's geometry, exact): the foot's reflection spans the rows from the CONTACT LINE (the foot's base,
# y 0.02) down to where the mirror puts its top edge (y 0.02 - 0.10 = -0.08 at the face z = -3.0). Measured down the
# foot's column (c3.py's method, spikes/photon-2/round5/ramp): (a) THE SEAM AT THE CONTACT - the rows between the
# contact line and the reflection's first dark row - <= 1 px, every arm; (b) the reflected top edge (the half-level
# crossing from the dark foot to the emitter's bright reflection) within 1 px of where the mirror puts it FROM THE
# LIFTED ORIGIN - the origin law's own, bounded offset: the ray starts the drawn cut's error (Epic's 1 px footprint at
# the floor point) above the floor, so it meets the foot that much higher and the reflected foot reads that much
# shorter (~1 px) - on the RAYS arms, whose answer is the ray's; the BOTH arms' top edge is the screen source's and is
# printed (THE ONE ESTIMATOR re-measures it, PHOTON-7 stage 6 commit 5).
# THE BASE (photon-2 round 5, the 1/2-cell bias + tMin, Both): at 1 m the reflected foot ENTIRELY missing (SEAM 99.3
# rows, the top edge 84.9 rows off); at 2.4 m SEAM 7.4 rows, the edge 5.7 off.
# 6 shots x 50 = 300 frames; two jahlab sessions (the chained floor's under RAYS: the origin on chained content).
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
def seam(name, eye):
    path = os.path.join(shots, name + '.png')
    if not os.path.exists(path):
        fails.append('%s: no shot' % name); return
    img = np.asarray(Image.open(path).convert('RGB')).astype(float); lum = img @ np.array([0.2126, 0.7152, 0.0722])
    H, W = lum.shape; eye = np.array(eye); tgt = np.array([-1.88, 0.1, -3.0])
    f = tgt - eye; f /= np.linalg.norm(f); r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    ty = math.tan(math.radians(22.5)); tx = ty * W / H
    proj = lambda p: ((((p - eye) @ r) / (((p - eye) @ f) * tx) + 1) * .5 * W - .5,
                      (1 - ((p - eye) @ u) / (((p - eye) @ f) * ty)) * .5 * H - .5)
    foot = proj(np.array([-1.88, 0.02, -3.0])); mirTop = proj(np.array([-1.88, -0.08, -3.0]))
    # THE LIFTED MIRROR: the floor point P reflecting the foot's top edge, the lift there (the drawn cut's error: the
    # lab's Epic, atomPixelError 1, one footprint = the view distance x 2 tan(22.5) / H), the edge it meets that much
    # higher - the mirror image of (0.12 + h) is (-0.08 + h)
    M = np.array([-1.88, -0.08, -3.0]); P = eye + (M - eye) * ((eye[1] - 0.02) / (eye[1] - M[1]))
    h = 1.0 * ((P - eye) @ f) * 2.0 * ty / H
    liftedTop = proj(np.array([-1.88, -0.08 + h, -3.0]))
    cx = int(round(foot[0])); col = lum[:, cx - 3:cx + 4].mean(1)
    fy = int(round(foot[1]))
    dark = col[fy - 20:fy - 5].mean()   # the foot itself, above the contact line
    bright = np.median(col[int(mirTop[1]) + 15:int(mirTop[1]) + 45])   # the emitter's reflection below the foot's
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
           '' if name.startswith(('rays', 'chained')) else ' [the screen source: printed]', dark, bright))
    if not (gap <= 1.0): fails.append('%s: the seam at the contact %.1f px > 1' % (name, gap))
    if name.startswith(('rays', 'chained')) and not (abs(edge) <= 1.0):
        fails.append('%s: the reflected top edge %+.1f px from the lifted mirror (> 1)' % (name, edge))
for name, eye in (('rays-2p4', (-1.88, 1.02, -0.8)), ('rays-1m', (-1.88, 0.62, -2.2)),
                  ('both-2p4', (-1.88, 1.02, -0.8)), ('both-1m', (-1.88, 0.62, -2.2)),
                  ('chained-2p4', (-1.88, 1.02, -0.8)), ('chained-1m', (-1.88, 0.62, -2.2))):
    seam(name, eye)
for f in fails: print('FAIL: ' + f)
print('rtreflect.contact: %s' % ('FAIL' if fails else 'PASS'))
sys.exit(1 if fails else 0)
PY
