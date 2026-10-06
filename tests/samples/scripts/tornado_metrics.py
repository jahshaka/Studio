#!/usr/bin/env python3
"""The Tornado's torn silhouette, measured (LIVE-PERSIST-1; samples.archive_fidelity.Tornado).

usage: tornado_metrics.py <reference.jpg> <frame.png>...

Two numbers per image, both from the debug-runner's and TORNADO-2's measurements
(spikes/tornado-owner-view/frames/flaps.py, spikes/tornado-2/t2_metrics.py), on an
800x600 frame from tornado.jpg's camera, inside the tornado's window x 230..580:

  * DETACHED FLAP PIXELS: per row, every warm-or-white run beside the body's longest
    run (>= 2 px) — the cut-out shells' torn flaps standing off the funnel;
  * SEE-THROUGH: inside the silhouette's envelope (4 % below the top to 22 % above
    the bottom), the fraction of pixels that are NOT tornado — the holes the cut-out
    punches.

A sample that loses its cut-out opens with NO detached pixels and almost no
see-through; that is the failure this row exists for. The frames' MEAN over the
four pinned shader times is judged, because one still of a scrolling cut-out is
one sample of a moving pattern (the in-memory scene's flaps range 420-801 px).

Bounds:
  * within 25 % of the reference still (793 px; see-through 0.0101);
  * within 10 % of the IN-MEMORY scene make_tornado.js builds — measured on its
    frames at the same camera and times (constants below; re-measure them whenever
    the sample is re-authored on purpose: run make_tornado.js with the
    spikes/tornado-2 measure tail).
"""
import sys
from PIL import Image

# The in-memory scene (make_tornado.js at c10bf3352's look, TORNADO-2's frames
# fix_m_all_t{0_25,0_75,1_25,1_75}.png): detached 420/801/753/594, see-through
# 0.0166/0.0106/0.0099/0.0086 (LIVE-PERSIST-1 re-measured both on its own run).
IN_MEMORY = {"detached_px": 642.0, "see_through": 0.01143}
REF_TOL = 0.25
MEM_TOL = 0.10
XW0, XW1 = 230, 580


def load(path):
    return Image.open(path).convert("RGB").resize((800, 600))


def torn(p):
    r, g, b = p
    return (r - b > 30) or min(p) > 200


def detached(im):
    px = im.load()
    det = 0
    for y in range(60, 490):
        row = [torn(px[x, y]) for x in range(XW0, XW1)]
        runs, s = [], None
        for i, v in enumerate(row + [False]):
            if v and s is None:
                s = i
            if not v and s is not None:
                runs.append((s, i))
                s = None
        if not runs:
            continue
        k = max(range(len(runs)), key=lambda i: runs[i][1] - runs[i][0])
        det += sum(e - s for i, (s, e) in enumerate(runs) if i != k and e - s >= 2)
    return det


def see_through(im):
    W, H = im.size
    px = im.load()
    mask = [[XW0 <= x < XW1 and torn(px[x, y]) for x in range(W)] for y in range(H)]
    rows = [y for y in range(H) if sum(mask[y]) > 8]
    if not rows:
        return 0.0
    top, bot = rows[0], rows[-1]
    span = bot - top
    env = gap = 0
    for y in range(top + int(0.04 * span), bot - int(0.22 * span)):
        xs = [x for x in range(W) if mask[y][x]]
        if len(xs) < 5:
            continue
        env += xs[-1] - xs[0] + 1
        gap += sum(1 for x in range(xs[0], xs[-1] + 1) if not mask[y][x])
    return gap / env if env else 0.0


def measure(path):
    im = load(path)
    return {"detached_px": float(detached(im)), "see_through": see_through(im)}


def main():
    ref = measure(sys.argv[1])
    frames = [measure(p) for p in sys.argv[2:]]
    if not frames:
        print("FAIL: no frames")
        return 1
    for p, f in zip(sys.argv[2:], frames):
        print("frame %s: detached %d px, see-through %.4f" % (p.split("/")[-1], f["detached_px"], f["see_through"]))
    mean = {k: sum(f[k] for f in frames) / len(frames) for k in ref}
    print("reference: detached %d px, see-through %.4f" % (ref["detached_px"], ref["see_through"]))
    print("archive (mean of %d): detached %.1f px, see-through %.4f" % (len(frames), mean["detached_px"], mean["see_through"]))
    ok = True
    for key in ("detached_px", "see_through"):
        for label, want, tol in (("the reference", ref[key], REF_TOL), ("the in-memory scene", IN_MEMORY[key], MEM_TOL)):
            dev = abs(mean[key] - want) / want
            good = dev <= tol
            ok &= good
            print("%s: %s %.4g vs %s %.4g: %+.1f %% (bound %d %%)" % ("ok" if good else "FAIL", key, mean[key], label,
                                                                       want, 100.0 * (mean[key] - want) / want, tol * 100))
    print("archive_fidelity: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
