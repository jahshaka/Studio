#!/usr/bin/env bash
# shadow.cutout_caster / shadow.two_sided_caster (CUTOUT-CASTER-1): runs shadow_casters.js.in
# RUNS times, each in its own process with a fresh data root (the cut-out's shadow used to be
# solid, absent or a GPU hang FROM RUN TO RUN: one process proves nothing about the next), and
# reads the frame pairs.
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

def lum(path):
    im = Image.open(path).convert("RGB"); W, H = im.size; px = im.load()
    return W, H, [[0.2126 * px[x, y][0] + 0.7152 * px[x, y][1] + 0.0722 * px[x, y][2] for x in range(W)] for y in range(H)]

def shadow(run, name):
    """The shadow alone: pixels the casting frame darkens below 60 % of the non-casting one."""
    W, H, on = lum("%s/r%d_%s_on.png" % (work, run, name))
    _, _, off = lum("%s/r%d_%s_off.png" % (work, run, name))
    return [[off[y][x] > 12 and on[y][x] < 0.6 * off[y][x] for x in range(W)] for y in range(H)]

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
