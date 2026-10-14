#!/usr/bin/env bash
# shadow.cutout_caster / shadow.two_sided_caster (CUTOUT-CASTER-1): runs shadow_casters.js.in
# RUNS times, each in its own process with a fresh data root, and reads the frame pairs. The gate
# rows run ONE process (the claim); their `.churn` twins run TEN (STAGE-CLOSE-1 U3: the cut-out's
# shadow used to be solid, absent or a GPU hang FROM RUN TO RUN — one process proves nothing about
# the next — and that churn is the stage-close batch's, beside gpu.cutout_soak).
# usage: shadow_casters.sh <cutout|twosided> <app> <script.js.in> <fixture.png> <workdir> <runs>
set -u
WHAT="$1"; APP="$2"; SCRIPT_IN="$3"; FIXTURE="$4"; WORK="$5"; RUNS="$6"
rm -rf "$WORK"; mkdir -p "$WORK" || exit 1
for i in $(seq 1 "$RUNS"); do
    sed -e "s#@OUT@#$WORK/r$i#g" -e "s#@FIXTURE@#$FIXTURE#g" -e "s#@WHAT@#$WHAT#g" "$SCRIPT_IN" > "$WORK/r$i.js"
    ( cd "$(dirname "$APP")" && timeout -k 10 300 "$APP" --data-root "$WORK/root$i" --script "$WORK/r$i.js" \
          > "$WORK/r$i.log" 2>&1 )
    rc=$?
    if [ $rc -ne 0 ] || ! grep -q "shadow_casters: PASS" "$WORK/r$i.log"; then
        echo "FAIL: run $i exited $rc (log: $WORK/r$i.log)"; grep -E 'assert failed|DEVICE WAS LOST' "$WORK/r$i.log" | head -3
        exit 1
    fi
    rm -rf "$WORK/root$i"
done
python3 - "$WHAT" "$WORK" "$RUNS" <<'PY'
import sys
from PIL import Image
what, work, runs = sys.argv[1], sys.argv[2], int(sys.argv[3])

import math
# THE MASK IS A STATEMENT ABOUT LIGHT (BISTRO-FIXES-3): a display code is taken back to the
# film's input - the sRGB decode, then Unreal's FilmToneMap at the engine's defaults (the toe
# 0.3) inverted by bisection - and a pixel is the caster's shadow where the casting frame's
# light falls below the MIDPOINT between the solid shadow's core and the full light. A PCF
# ramp is symmetric about the geometric edge, so the midpoint's contour IS the edge and an
# area measured on it is the caster's own (the cut-out's half-holes read 0.485 of the solid).
# In display codes a fixed 60 % sat wherever the film put it: the softer toe moved it deep into
# the ramp and the cut-out read 0.33.
def _film(x, slope=0.88, toe=0.3, shoulder=0.26, black=0.0, white=0.04):
    ts = 1 + black - toe; ss = 1 + white - shoulder
    bt = (0.18 + black) / ts - 1
    tm = math.log10(0.18) - 0.5 * math.log((1 + bt) / (1 - bt)) * (ts / slope)
    sm = (1 - toe) / slope - tm; shm = shoulder / slope - sm
    lc = math.log10(max(x, 1e-10)); st = slope * (lc + sm)
    tc = -black + 2 * ts / (1 + math.exp((-2 * slope / ts) * (lc - tm)))
    sc = (1 + white) - 2 * ss / (1 + math.exp((2 * slope / ss) * (lc - shm)))
    tc = tc if lc < tm else st; sc = sc if lc > shm else st
    t = min(max((lc - tm) / (shm - tm), 0.0), 1.0)
    if shm < tm: t = 1 - t
    t = (3 - 2 * t) * t * t
    return max(0.0, tc + (sc - tc) * t)
_INPUT = []
for c in range(256):
    v = c / 255.0
    shown = v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    lo, hi = -6.0, 2.0
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        if _film(10 ** mid) < shown: lo = mid
        else: hi = mid
    _INPUT.append(10 ** (0.5 * (lo + hi)) if c else 0.0)

def lum(path):
    im = Image.open(path).convert("RGB"); W, H = im.size; px = im.load()
    return W, H, [[0.2126 * _INPUT[px[x, y][0]] + 0.7152 * _INPUT[px[x, y][1]] + 0.0722 * _INPUT[px[x, y][2]]
                   for x in range(W)] for y in range(H)]

_THR = {}
def threshold(run):
    """The midpoint between the solid shadow's core (the median light ratio of the pixels it
    takes below half the light) and the full light, per run."""
    if run not in _THR:
        W, H, on = lum("%s/r%d_solid_on.png" % (work, run))
        _, _, off = lum("%s/r%d_solid_off.png" % (work, run))
        deep = sorted(on[y][x] / off[y][x] for y in range(H) for x in range(W)
                      if off[y][x] > _INPUT[12] and on[y][x] < 0.5 * off[y][x])
        core = deep[len(deep) // 2] if deep else 0.0
        _THR[run] = 0.5 * (1.0 + core)
        print("   run %d: the solid shadow's core reads %.3f of the light; the mask's midpoint %.3f" % (run, core, _THR[run]))
    return _THR[run]

def shadow(run, name):
    """The shadow alone: pixels the casting frame darkens below the midpoint (threshold)."""
    W, H, on = lum("%s/r%d_%s_on.png" % (work, run, name))
    _, _, off = lum("%s/r%d_%s_off.png" % (work, run, name))
    t = threshold(run)
    return [[off[y][x] > _INPUT[12] and on[y][x] < t * off[y][x] for x in range(W)] for y in range(H)]

def area(m): return sum(sum(r) for r in m)

def box(m):
    ys = [y for y, r in enumerate(m) if sum(r) > 8]
    xs = [x for x in range(len(m[0])) if sum(m[y][x] for y in range(len(m))) > 8]
    return min(xs), max(xs), min(ys), max(ys)

def cells(m, b):
    """The 4 x 4 cells of the solid shadow's box, each read over its inner half: the
    fraction shadowed."""
    x0, x1, y0, y1 = b; out = []
    for j in range(4):
        row = []
        for i in range(4):
            cx0 = x0 + (x1 - x0) * (i + 0.25) / 4; cx1 = x0 + (x1 - x0) * (i + 0.75) / 4
            cy0 = y0 + (y1 - y0) * (j + 0.25) / 4; cy1 = y0 + (y1 - y0) * (j + 0.75) / 4
            n = s = 0
            for y in range(int(cy0), int(cy1) + 1):
                for x in range(int(cx0), int(cx1) + 1):
                    n += 1; s += m[y][x]
            row.append(s / max(n, 1))
        out.append(row)
    return out

def checker(c):
    """The analytic projection: every cell solid (>= 85 % shadowed) or a hole (<= 15 %),
    alternating. Returns the parity of cell (0, 0) or None."""
    bits = [[v >= 0.85 for v in r] for r in c]
    clean = all(v >= 0.85 or v <= 0.15 for r in c for v in r)
    alt = all(bits[j][i] == (bits[0][0] ^ ((i + j) & 1)) for j in range(4) for i in range(4))
    return bits[0][0] if clean and alt else None

def fmt(c): return " ".join("".join("#" if v >= 0.85 else ("." if v <= 0.15 else "?") for v in r) for r in c)

ok = True
def check(good, msg):
    global ok
    ok &= bool(good)
    print("%s: %s" % ("ok" if good else "FAIL", msg))

ref = None
for r in range(1, runs + 1):
    solid = shadow(r, "solid"); a0 = area(solid)
    check(a0 >= 8000, "run %d: the opaque plane casts (%d px; the 3 m plane's shadow is ~20,000)" % (r, a0))
    if a0 < 8000: continue
    b = box(solid)
    if what == "twosided":
        for name in ("twosided", "twosided_flipped"):
            a = area(shadow(r, name))
            check(0.9 <= a / a0 <= 1.1, "run %d: the two-sided plane%s casts the opaque plane's shadow: %d px / %d px"
                  % (r, " turned over" if "flipped" in name else "", a, a0))
        continue
    cut = cells(shadow(r, "cutout"), b)
    par = checker(cut)
    check(par is not None, "run %d: the cut-out's shadow is the 4 x 4 checker of its map: %s" % (r, fmt(cut)))
    fr = area(shadow(r, "cutout")) / a0
    check(0.35 <= fr <= 0.65, "run %d: it covers %.2f of the solid shadow (the map is half holes)" % (r, fr))
    s0 = cells(shadow(r, "scroll0"), b); s1 = cells(shadow(r, "scroll1"), b)
    p0, p1 = checker(s0), checker(s1)
    check(p0 is not None and p0 == par, "run %d: scrolling, t = 0: the same checker %s" % (r, fmt(s0)))
    check(p1 is not None and p0 is not None and p1 != p0,
          "run %d: scrolling, t = 1 (one square further): the holes MOVED, every cell flipped %s" % (r, fmt(s1)))
    if ref is None: ref = par
    check(par == ref, "run %d: the same pattern as run 1 (deterministic)" % r)
print("shadow.%s: %s" % ("cutout_caster" if what == "cutout" else "two_sided_caster", "PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
PY
