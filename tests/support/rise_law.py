"""THE RISE LAW of a settling series (PHOTON-7 stage 0's read, R2 and R3; rule R3: a tolerance belongs to its row, derived from
that row's quantum) - "the per-frame change is non-increasing until the hold", judged the ONE way both rows judge it
(gi.hold_after_stop (f), rtreflect.still_after_stop (a)).

THE NOISE IS ONE ROBUST FIGURE FOR THE WHOLE SERIES, never a window's own. A window's error estimated from that same
window lets a burst raise its own bound (an interior k-frame burst of height S lifts the window's successive-difference
sigma to ~S/3, so no burst of 5 frames or fewer was ever a rise, whatever its height - the read's R2).
  THE SPACE: the per-frame mean change is a COUNT of pixel-codes, its noise grows with its level (measured on the glass's
          rest, 20x down its decay: sigma / sqrt(level) 0.0030-0.0049 while sigma itself fell 0.00094 -> 0.00021) - one
          figure for the series exists only in sqrt(m), where the count's noise is level-free, so the series is judged
          there (a monotone map: non-increasing is non-increasing).
  sigma = the MEDIAN ABSOLUTE SUCCESSIVE DIFFERENCE of the series' TAIL (its second half - the settled part the hold ends),
          over sqrt(2) x 0.67449 (the median of |N(0, 2 sigma^2)|): a slow trend cancels in a difference, and a burst of
          any length moves the median by at most the two differences its edges make; floored at the row's QUANTUM (the
          larger of one pixel-code over the pixels averaged and the record's print precision), taken at the tail's level.
  THE WINDOWS: 10 frames from `start` to `end`; a final window shorter than 5 frames is merged into the one before (the
          read's R3: a 1-frame window has no successive difference, and a hold one frame into a window made it one).
  A RISE, between window i and window i + 1 (n_i, n_j frames; K = 5):
    (mean)  mean_j - mean_i > K sigma sqrt(1 / n_i + 1 / n_j)         - the series' level went up;
    (burst) any frame f of window j:  m[f] - mean_i > K sigma sqrt(1 + 1 / n_i)   - a burst of ANY length >= 1 frame
            and >= the bound (a window's mean dilutes a 1-frame burst ten times; a frame cannot hide in its window).
  The first window has no window before it: a burst inside it is not judged (the rows start their windows after the
  stop's own transient).
THE FALSE ALARM, measured (this module's own Monte Carlo, `python3 rise_law.py mc [frames]`: series of i.i.d. normal
frames in the judged space): either test, per window pair, 1.8e-3 at 52 frames (its last window merged), 1.2e-3 at 60,
3.4e-4 at 95 (the lamp step's), 3.1e-5 at 500 (the glass's K_max hold) - the median of a short tail's differences is a
loose estimate - so about 0.3 % for the lamp step's eight pairs and 0.15 % for the glass's fifty. (The statistic this replaced - each window's own successive-
difference error, 5 combined standard errors - false-alarmed 1.2e-4 per comparison by the same kind of Monte Carlo, the
read's R3: about 0.6 % for 50 comparisons and 0.12 % for 10, not the 5e-5 / 0.3 % / 0.1 % its comments stated - and it
could not see a burst.)
SELF CHECK (`selfcheck`, run by both rows before they judge): the read's synthetic cases - a decay with 1-, 2-, 3- and
5-frame bursts of 0.2, 1 and 5 codes inside it - are each a rise, and the same decay with no burst is none.
"""
import math
import random

K = 5.0
WINDOW = 10
MIN_WINDOW = 5
MEDIAN_ABS_NORMAL = 0.6744897501960817   # the median of |N(0, 1)|


def robust_sigma(series, quantum=0.0):
    """The series' per-frame noise from its tail's successive differences (the module's header), floored at `quantum`."""
    tail = series[len(series) // 2:]
    diffs = sorted(abs(tail[i + 1] - tail[i]) for i in range(len(tail) - 1))
    if not diffs:
        return quantum
    n = len(diffs)
    med = diffs[n // 2] if n % 2 else 0.5 * (diffs[n // 2 - 1] + diffs[n // 2])
    return max(med / (math.sqrt(2.0) * MEDIAN_ABS_NORMAL), quantum)


def windows(start, end):
    """The [a, b) spans of WINDOW frames over [start, end); a last span shorter than MIN_WINDOW merged into the one before."""
    spans = [[a, min(a + WINDOW, end)] for a in range(start, end, WINDOW)]
    if len(spans) >= 2 and spans[-1][1] - spans[-1][0] < MIN_WINDOW:
        spans[-2][1] = spans[-1][1]
        spans.pop()
    return [tuple(sp) for sp in spans]


def stabilised(m, quantum):
    """THE CHANGE IS A COUNT (the module's header): sqrt of the per-frame mean change, its noise level-free; and the
    quantum's size at the series' tail level in that space (the floor of its sigma)."""
    v = [math.sqrt(max(x, 0.0)) for x in m]
    tail = sorted(m[len(m) // 2:]) or [0.0]
    level = tail[len(tail) // 2]
    return v, math.sqrt(level + quantum) - math.sqrt(level)


def judge(m, start, end, quantum=0.0):
    """m: the per-frame series (indexed by frame), a mean change in codes; `quantum` the smallest step one frame's
    value can take (the larger of one pixel-code over the pixels averaged and the record's print precision). Judged on
    the stabilised series (stabilised). Returns (spans, means, sigma, rises) in that space: rises are
    (kind, window start or the frame, from, to, bound) - kind 'mean' or 'burst'."""
    m, qfloor = stabilised(m[:end], quantum)
    spans = windows(start, end)
    sigma = robust_sigma(m[start:end], qfloor)
    means = [sum(m[a:b]) / (b - a) for a, b in spans]
    rises = []
    for i in range(len(spans) - 1):
        ni = spans[i][1] - spans[i][0]
        a, b = spans[i + 1]
        nj = b - a
        bound = K * sigma * math.sqrt(1.0 / ni + 1.0 / nj)
        if means[i + 1] - means[i] > bound:
            rises.append(("mean", a, means[i], means[i + 1], bound))
        fbound = K * sigma * math.sqrt(1.0 + 1.0 / ni)
        for f in range(a, b):
            if m[f] - means[i] > fbound:
                rises.append(("burst", f, means[i], m[f], fbound))
    return spans, means, sigma, rises


def _decay(n, rng, c=0.004):
    """A decaying change whose noise is the count's (sigma = c sqrt(level): the glass's measured c 0.003-0.005)."""
    out = []
    for i in range(n):
        level = 0.5 * math.exp(-i / 8.0) + 0.02
        out.append(max(level + rng.gauss(0.0, c * math.sqrt(level)), 0.0))
    return out


def selfcheck():
    """The read's synthetic cases (the module's header). Returns the failures, empty when the law sees what it must."""
    fails = []
    rng = random.Random(20261023)
    base = _decay(60, rng)
    for end in (60, 52, 51):   # (52 and 51: a hold one or two frames into a window - merged, R3)
        if judge(base, 1, end)[3]:
            fails.append("rise_law: a flat (decaying) tail with no burst is a rise (hold %d): %s" % (end, judge(base, 1, end)[3][:2]))
    if windows(1, 52)[-1] != (41, 52):
        fails.append("rise_law: a final window under %d frames is merged into the one before: %s" % (MIN_WINDOW, windows(1, 52)))
    for height in (0.2, 1.0, 5.0):
        for length in (1, 2, 3, 5):
            for at in (24, 35, 52):
                m = list(base)
                for f in range(at, at + length):
                    m[f] += height
                if not judge(m, 1, 60)[3]:
                    fails.append("rise_law: a %d-frame burst of %.1f codes at frame %d is not a rise" % (length, height, at))
    return fails


def _monte_carlo(series=60000, frames=60):
    rng = random.Random(7)
    pairs = mean_hits = burst_hits = any_hits = 0
    for _ in range(series):
        m = [(4.0 + rng.gauss(0.0, 0.1)) ** 2 for _ in range(frames)]   # constant noise in the stabilised space
        spans, means, sigma, rises = judge(m, 0, frames)
        pairs += len(spans) - 1
        kinds = {}
        for r in rises:
            kinds.setdefault(r[1] if r[0] == "mean" else (r[1] // WINDOW) * WINDOW, set()).add(r[0])
        for k in kinds.values():
            mean_hits += "mean" in k
            burst_hits += "burst" in k
            any_hits += 1
    print("pairs %d: mean %.2e, burst %.2e, either %.2e per window pair" %
          (pairs, mean_hits / pairs, burst_hits / pairs, any_hits / pairs))


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "mc":
        _monte_carlo(frames=int(sys.argv[2]) if len(sys.argv) > 2 else 60)
    else:
        f = selfcheck()
        print("\n".join(f) if f else "rise_law selfcheck: every synthetic burst is a rise, the flat tail is none")
        sys.exit(1 if f else 0)
