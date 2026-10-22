"""gi.light_is_world's judge (PHOTON-7 stage 1; light_is_world.sh says the arms, the band and the base).

A dump (the lab's `voxels <c> direct <file>`): one JSON header line {cascade, kind, res, origin, cell, multiplier, lo, dims}
then dims x RGBA float32, x fastest; voxel (i, j, k) of the dump is WORLD cell lo + (i, j, k), its centre
(world cell + 0.5) x cell. Two dumps of one cascade are joined on the world cell.
THE QUANTUM: the direct volume is RGBA16F - one step at a value x is 2^(floor(log2 x) - 10) (2^-24 under the normal range);
two placements' values of one voxel may differ by one step (their arithmetic is relative to different box corners).
usage: light_is_world.py <data dir>
"""
import json, math, os, sys
import numpy as np

D = sys.argv[1]
fails = []
L = np.array([0.0, math.cos(math.radians(40.0)), math.sin(math.radians(40.0))])   # toward the sun (labscene.cpp)
EPIC_CELLS = [0.078125, 0.15625, 0.46875, 1.875]


def load(name):
    p = os.path.join(D, name)
    if not os.path.exists(p):
        return None
    with open(p, 'rb') as f:
        h = json.loads(f.readline())
        a = np.fromfile(f, dtype='<f4')
    nx, ny, nz = h['dims']
    h['data'] = a.reshape(nz, ny, nx, 4)
    return h


def reach(c, cells):
    """The march's reach over cascade c's own shell: D_c (8 of its cells, cumulative over the cascades inside it), the
    hand-over to the next coarser plane (two of the next cascade's cells) and the face's offset (one of its own)."""
    d = sum(8.0 * cells[j] for j in range(c + 1))
    nxt = cells[c + 1] if c + 1 < len(cells) else cells[c]
    return d + 2.0 * nxt + cells[c]


def band_free(h, centres, t):
    """The voxels whose march toward the sun over [0, t] stays inside the dump's cascade box."""
    lo = np.array(h['origin'])
    hi = lo + np.array(h['cell']) * np.array(h['res'])
    end = centres + t * L[None, :]
    return np.all((centres >= lo) & (centres <= hi) & (end >= lo) & (end <= hi), axis=1)


def quantum(x):
    x = np.maximum(np.abs(x), 2.0 ** -14)
    return np.exp2(np.floor(np.log2(x)) - 10.0)


def joined(a, b):
    """The world cells both dumps hold: their centres and both values (N x 4)."""
    lo = np.maximum(np.array(a['lo']), np.array(b['lo']))
    hi = np.minimum(np.array(a['lo']) + np.array(a['dims']), np.array(b['lo']) + np.array(b['dims']))
    if np.any(hi <= lo):
        return None
    def cut(h):
        s = lo - np.array(h['lo']); e = hi - np.array(h['lo'])
        return h['data'][s[2]:e[2], s[1]:e[1], s[0]:e[0], :].reshape(-1, 4)
    ks, js, iss = np.meshgrid(np.arange(lo[2], hi[2]), np.arange(lo[1], hi[1]), np.arange(lo[0], hi[0]), indexing='ij')
    cell = np.array(a['cell'])
    centres = (np.stack([iss.ravel(), js.ravel(), ks.ravel()], axis=1) + 0.5) * cell[None, :]
    return centres, cut(a), cut(b)


def compare(arm, an, bn, cells, exclude=None, regions=None):
    a, b = load(an), load(bn)
    if a is None or b is None:
        fails.append('(%s) a dump is missing: %s %s' % (arm, an if a is None else '', bn if b is None else ''))
        return
    j = joined(a, b)
    if j is None:
        fails.append('(%s) the two placements share no voxel' % arm)
        return
    centres, va, vb = j
    t = reach(a['cascade'], cells)
    m = band_free(a, centres, t) & band_free(b, centres, t)
    m &= (np.max(va[:, :3], axis=1) > 0) | (np.max(vb[:, :3], axis=1) > 0)   # a voxel with light at either placement
    if exclude is not None:
        m &= ~exclude(centres)
    q = quantum(np.maximum(np.abs(va), np.abs(vb)))
    over = np.any(np.abs(va - vb) > q, axis=1) & m
    worst = float(np.max(np.abs(va - vb)[m] / q[m])) if m.any() else 0.0
    ratio = float(np.median(vb[m & (va[:, 0] > 0), 0] / va[m & (va[:, 0] > 0), 0])) if (m & (va[:, 0] > 0)).any() else 0.0
    print('(%s) cascade %d, %s vs %s: %d band-free lit voxels compared (reach %.3f m), %d differ by more than one step '
          '(worst %.1f steps; median ratio %.4f)' % (arm, a['cascade'], an, bn, int(m.sum()), t, int(over.sum()), worst, ratio))
    for name, region in (regions or {}).items():
        r = m & region(centres)
        print('   %s: %d compared, %d over one step' % (name, int(r.sum()), int((over & r).sum())))
    if not m.any():
        fails.append('(%s) no band-free lit voxel to compare' % arm)
    elif over.any():
        fails.append('(%s) %d band-free voxels of cascade %d differ between the placements by more than one RGBA16F step '
                     '(worst %.1f steps)' % (arm, int(over.sum()), a['cascade'], worst))


def floor_xz(x0, x1, z0, z1):
    return lambda c: (c[:, 0] >= x0) & (c[:, 0] <= x1) & (c[:, 2] >= z0) & (c[:, 2] <= z1) & (c[:, 1] < 0.4)


shells = {'under A': floor_xz(-1.1, 1.1, 2.28, 4.08), 'under B': floor_xz(-1.1, 1.1, -7.4, -5.6),
          'under the partial plane': floor_xz(3.0, 7.0, -3.66, 0.34)}
compare('opaque', 's-c0-p0.vox', 's-c0-p1.vox', EPIC_CELLS, regions=shells)
compare('outer', 's-c1-p0.vox', 's-c1-p1.vox', EPIC_CELLS, regions=shells)
lamp = np.array([-7.0, 1.0, 7.0])
compare('multiplier', 's-c0-p0.vox', 's-c0-pm.vox', EPIC_CELLS,
        exclude=lambda c: np.linalg.norm(c - lamp[None, :], axis=1) < 1.5 + 2.0 * EPIC_CELLS[0])

# THE PINNED SCROLL: the eye walked off a pinned chain (no abort; the store where the pin says, bit-equal to before)
p0, walked = load('s-c0-p0b.vox'), load('s-c0-walked.vox')
if p0 is None or walked is None:
    fails.append('(pinned walk) a dump is missing (the session ended early: an abort?)')
else:
    same = p0['origin'] == walked['origin'] and np.array_equal(p0['data'], walked['data'])
    print('(pinned walk) cascade 0 after the eye walked 2.8 m off its pin: origin %s (the pin\'s %s), store %s'
          % (walked['origin'], p0['origin'], 'bit-equal' if same else 'DIFFERENT'))
    if not same:
        fails.append('(pinned walk) the store moved with the eye, not the pin')
sc, wh = load('s-c0-scroll.vox'), load('s-c0-whole.vox')
if sc is None or wh is None:
    fails.append('(pinned scroll) a dump is missing')
else:
    placed = sc['origin'] == wh['origin'] and sc['origin'] != (p0['origin'] if p0 else None)
    q = quantum(np.maximum(np.abs(sc['data']), np.abs(wh['data'])))
    over = int(np.any(np.abs(sc['data'] - wh['data']) > q, axis=3).sum()) if sc['data'].shape == wh['data'].shape else -1
    print('(pinned scroll) cascade 0 re-placed by the scroll to the moved pin: origin %s, a whole rebuild there %s; '
          '%d voxels over one step' % (sc['origin'], wh['origin'], over))
    if not placed:
        fails.append('(pinned scroll) the scroll did not place cascade 0 where the pin says: %s vs %s' % (sc['origin'], wh['origin']))
    if over != 0:
        fails.append('(pinned scroll) the scrolled store differs from a whole rebuild at the pin: %d voxels' % over)

# THE ROOF: every cascade's floor voxels under the plate hold no direct light, at every tier
for tier, n in (('epic', 4), ('high', 4), ('medium', 4), ('low', 2)):
    for c in range(n):
        h = load('r-%s-c%d.vox' % (tier, c))
        if h is None:
            fails.append('(roof) %s cascade %d: no dump' % (tier, c)); continue
        v = h['data'][..., :3]
        lit = int((v.max(axis=3) > 2.0 ** -24).sum())
        print('(roof) %s cascade %d: %d voxels read, %d lit under the plate, peak %.6g' % (tier, c, v[..., 0].size, lit, float(v.max())))
        if lit: fails.append('(roof) %s cascade %d: %d floor voxels under the plate hold direct light (peak %.4g)' % (tier, c, lit, float(v.max())))

for f in fails:
    print('FAIL:', f)
sys.exit(1 if fails else 0)
