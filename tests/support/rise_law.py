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
  sigma = the MEDIAN ABSOLUTE SECOND DIFFERENCE v[f+1] - 2 v[f] + v[f-1] over the JUDGED frames whose three values are
          LIVE (above the quantum), over sqrt(6) x 0.67449 (the median of |N(0, 6 sigma^2)|): a locally linear decay cancels
          in it, a burst of any length moves the median by the few differences its edges make; floored at the row's QUANTUM
          (the larger of one pixel-code over the pixels averaged and the record's print precision), taken at the tail's
          level. (URGENT #138, 2026-11-02: it was the median FIRST difference of the series' TAIL, and on these rows the tail
          is the post-hold zeros - 0.00024 where the count's noise is ~0.002, eight times small; the lead's ruling: that
          figure is deleted.)
  THE WINDOWS: 10 frames from `start` to `end`; a final window shorter than 5 frames is merged into the one before (the
          read's R3: a 1-frame window has no successive difference, and a hold one frame into a window made it one).
  A RISE, between window i and window i + 1 (n_i, n_j frames; K = 5):
    (mean)  mean_j - mean_i > K sigma sqrt(1 / n_i + 1 / n_j)         - the series' level went up;
    (burst) any frame f of window j:  m[f] - mean_i > K sigma sqrt(1 + 1 / n_i)   - a burst of ANY length >= 1 frame
            and >= the bound (a window's mean dilutes a 1-frame burst ten times; a frame cannot hide in its window).
    (step)  any frame f from window 1 on:  v[f] - v[f-1] > K sigma sqrt(2)   - A RISE FROM THE LEVEL THE SERIES HAD ACTUALLY
            REACHED, judged phase-free (URGENT #138, 2026-11-02): (mean) and (burst) judge a frame against the PREVIOUS
            WINDOW's mean, so a one-frame rise right after a large drop inside one window was unseen, or seen by the window's
            phase alone - BISTRO-REST-1's lamp step: the reflection's rest hand-over drops the glossy change ~5x (0.0405 ->
            0.0074) then RISES one frame (0.0103), flagged on one settle start, unseen on the next, and photon-7 passed it four
            times. A rise is the picture changing MORE than the level it had reached (a pop: the no-flicker law).
            ONE EXCEPTION, A DIP (the lead's ruling): when frame f-1 stepped DOWN over the bound and f comes back to within the
            bound of f-2's level, f-1 was a one-frame dip - the change rate resuming, not extra light - and f is judged against
            f-2. A dip is not a rise, but it is PRINTED by the rows (its frame and depth in sigma): a frame whose change falls
            by a third and recovers smells of a skipped or partial update (gi.hold_after_stop's room lamp, 0.1174 -> 0.0828 ->
            0.1151, the lead's leftover for the bug lane).
            MEASURED (every rise-law series on disk, 2026-11-02: bistro-rest-1, photon-2, photon-7-9a): the hand-over's frame
            at 10.5-15.4 sigma in lamp_step_held's both sessions and still_after_stop's both, every other frame under 5.
  The first window has no window before it: neither a burst nor a step inside it is judged (the rows start their windows
  after the stop's own transient).
THE FALSE ALARM, measured (this module's own Monte Carlo, `python3 rise_law.py mc [frames] [series]`: series of i.i.d. normal
frames in the judged space), any test per window pair, at URGENT #138's law (the second differences' noise, the step test):
6.0e-4 at 52 frames (its last window merged), 4.2e-4 at 60, 1.3e-4 at 95 (the lamp step's), 3.4e-6 at 500 (the glass's K_max
hold) - so about 0.1 % for the lamp step's eight pairs and 0.02 % for the glass's fifty. (The tail figure's law before it,
the same Monte Carlo, 2026-11-02: 1.3e-3 per pair at 60 frames - its tail estimate was looser - and the statistic before
THAT, each window's own successive-difference error, 1.2e-4 per comparison, could not see a burst.)
SELF CHECK (`selfcheck`, run by every row before it judges): the read's synthetic cases - a decay with 1-, 2-, 3- and
5-frame bursts of 0.2, 1 and 5 codes inside it - are each a rise, and the same decay with no burst is none; and the MEASURED
cases (#138): the lamp step's hand-over series is a step rise at frame 158 from EVERY settle start's phase, and the room lamp's
dip is a dip, not a rise.
"""
import math
import random

K = 5.0
WINDOW = 10
MIN_WINDOW = 5
MEDIAN_ABS_NORMAL = 0.6744897501960817   # the median of |N(0, 1)|


def robust_sigma(v, raw, start, end, quantum, floor):
    """The series' per-frame noise (the module's header): the median |second difference| of the stabilised series `v` over
    the judged frames [start, end) whose three raw values (`raw`) are above `quantum`, floored at `floor`."""
    d = sorted(abs(v[f + 1] - 2.0 * v[f] + v[f - 1]) for f in range(max(start, 1), end - 1)
               if raw[f - 1] > quantum and raw[f] > quantum and raw[f + 1] > quantum)
    if not d:
        return floor
    n = len(d)
    med = d[n // 2] if n % 2 else 0.5 * (d[n // 2 - 1] + d[n // 2])
    return max(med / (math.sqrt(6.0) * MEDIAN_ABS_NORMAL), floor)


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
    the stabilised series (stabilised). Returns (spans, means, sigma, rises, dips) in that space: rises are
    (kind, window start or the frame, from, to, bound) - kind 'mean', 'burst' or 'step'; dips (frame, depth in sigma) - the
    one-frame dips the step test judged past (printed by the rows, never a failure)."""
    raw = [max(x, 0.0) for x in m[:end]]
    m, qfloor = stabilised(m[:end], quantum)
    spans = windows(start, end)
    sigma = robust_sigma(m, raw, start, end, quantum, qfloor)
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
    # (step) against the level reached: the frame before, or the level before a one-frame dip (URGENT #138)
    dips, sbound = [], K * sigma * math.sqrt(2.0)
    for f in range(spans[1][0] if len(spans) > 1 else end, end):
        if m[f] - m[f - 1] <= sbound:
            continue
        if f - 2 >= start and m[f - 1] < m[f - 2] - sbound and abs(m[f] - m[f - 2]) <= sbound:
            dips.append((f - 1, (m[f - 2] - m[f - 1]) / sigma))
        else:
            rises.append(("step", f, m[f - 1], m[f], sbound))
    return spans, means, sigma, rises, dips


def settle_start(rows, end):
    """WHERE A RESTING READER'S OWN SETTLE BEGINS in a stillness door's rows (PHOTON-7 8B, B2's re-derivation, the lead's
    ruling): 16 frames past the LATER of the rest's last start (refl_rest_frames back to <= 1) and the last frame that
    changed the reflection's INPUTS - a visible card relight (cards_visible rising) or a close the trace was handed at rest
    (refl_rest_movers rising: a listed region or a live brick mask). The old start, the rest's alone, stood on the premise
    that the GI's relights break the reflection's rest; under B2 they break it per texel, the rest begins while the cards
    still relight, and the readers' code-a-frame follow of that estimate is not the rest's settle (measured: eref's glossy
    0.13-0.37 codes a frame while cards_visible climbed 79 -> 124, rounds 13 -> 17, its decay after 159)."""
    def col(i, k):
        v = rows[i].get(k, "")
        return int(v) if v not in ("", "-") else 0
    rest = max([i for i in range(1, end) if col(i, "refl_rest_frames") <= 1] or [1])
    inputs = max([i for i in range(2, end) if col(i, "cards_visible") > col(i - 1, "cards_visible") or
                  col(i, "refl_rest_movers") > col(i - 1, "refl_rest_movers")] or [1])
    return max(rest, inputs) + 16


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
    for end in (60, 52, 51):   # (52 and 51: a hold one or two frames into a window - merged, R3; a trend-only decay)
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
    return fails + measured_cases()


# THE MEASURED CASES (URGENT #138): BISTRO-REST-1's lamp step at Studio 4a31da26f (arm X), rtreflect.lamp_step_held's estep
# session, glossy_mean frames 67-198 (its settle start 87, its hold 199): the rest hand-over's drop 0.0405 -> 0.0074 (frame 157)
# and its one-frame rise to 0.0103 (158); and gi.hold_after_stop's room lamp at photon-7 2187edd42, all_mean frames 0-74: the
# dip 0.1174 -> 0.0828 -> 0.1151 (frame 15).
HANDOVER = [0.0] * 67 + [float(x) for x in """
    0.1766,0.1780,0.1786,0.1769,0.1238,0.2308,0.2241,0.2235,0.2284,0.2348,0.2419,0.2508,0.2484,0.2362,0.2195,0.2066,
    0.1922,0.1806,0.1715,0.1614,0.1558,0.1483,0.1402,0.1355,0.1304,0.1245,0.1213,0.1188,0.1145,0.1094,0.1074,0.1033,
    0.0987,0.0980,0.0947,0.0925,0.0894,0.0877,0.0865,0.0849,0.0822,0.0809,0.0783,0.0779,0.0754,0.0732,0.0722,0.0703,
    0.0698,0.0703,0.0674,0.0667,0.0657,0.0644,0.0633,0.0620,0.0619,0.0612,0.0590,0.0584,0.0582,0.0573,0.0557,0.0550,
    0.0547,0.0547,0.0532,0.0532,0.0523,0.0518,0.0507,0.0497,0.0499,0.0497,0.0485,0.0479,0.0466,0.0467,0.0461,0.0447,
    0.0448,0.0436,0.0442,0.0433,0.0428,0.0422,0.0417,0.0416,0.0410,0.0405,0.0074,0.0103,0.0091,0.0064,0.0047,0.0023,
    0.0014,0.0008,0.0005,0.0003,0.0003,0.0003,0.0001,0.0001,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,
    0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,0.0000,
    0.0000,0.0000,0.0000,0.0000
""".replace("\n", "").split(",")]
ROOM_LAMP = [float(x) for x in """
    0.0000,0.4655,0.4668,0.4468,0.4023,0.3621,0.3591,0.2464,0.1793,0.3316,0.2662,0.1822,0.1197,0.1234,0.1174,0.0828,0.1151,
    0.1158,0.1134,0.1148,0.1140,0.1141,0.1126,0.1145,0.1140,0.1141,0.1140,0.1127,0.1137,0.1143,0.0847,0.0726,0.0644,0.0547,
    0.0414,0.0393,0.0381,0.0350,0.0354,0.0369,0.0391,0.0400,0.0386,0.0392,0.0391,0.0392,0.0381,0.0393,0.0390,0.0377,0.0380,
    0.0385,0.0376,0.0382,0.0381,0.0386,0.0371,0.0379,0.0380,0.0358,0.0339,0.0380,0.0378,0.0371,0.0360,0.0328,0.0340,0.0296,
    0.0285,0.0269,0.0273,0.0236,0.0255,0.0238,0.0223
""".replace("\n", "").split(",")] + [0.0] * 46


def measured_cases():
    """The measured cases' failures (the module's SELF CHECK): the hand-over a step rise at 158 from every phase of the settle
    start, nothing else a rise; the room lamp's frame 15 a dip and no rise."""
    fails = []
    for start in range(87, 97):
        r = judge(HANDOVER, start, 199, 1e-4)[3]
        if [(k, f) for k, f, *_ in r] != [("step", 158)]:
            fails.append("rise_law: the lamp step's hand-over (frame 158) from settle start %d: %s (must be one step rise at 158)"
                         % (start, [(k, f) for k, f, *_ in r][:4]))
    _, _, _, r, d = judge(ROOM_LAMP, 1, 75, max(1e-4, 1.0 / 518400))
    if r or [f for f, _ in d] != [15]:
        fails.append("rise_law: the room lamp's frame 15 is a dip, not a rise: rises %s, dips %s" % (r[:3], d))
    return fails


def _monte_carlo(series=60000, frames=60):
    rng = random.Random(7)
    pairs = mean_hits = burst_hits = step_hits = any_hits = 0
    for _ in range(series):
        m = [(4.0 + rng.gauss(0.0, 0.1)) ** 2 for _ in range(frames)]   # constant noise in the stabilised space
        spans, means, sigma, rises, _ = judge(m, 0, frames)
        pairs += len(spans) - 1
        kinds = {}
        for r in rises:
            kinds.setdefault(r[1] if r[0] == "mean" else (r[1] // WINDOW) * WINDOW, set()).add(r[0])
        for k in kinds.values():
            mean_hits += "mean" in k
            burst_hits += "burst" in k
            step_hits += "step" in k
            any_hits += 1
    print("pairs %d: mean %.2e, burst %.2e, step %.2e, any %.2e per window pair" %
          (pairs, mean_hits / pairs, burst_hits / pairs, step_hits / pairs, any_hits / pairs))


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "mc":
        _monte_carlo(series=int(sys.argv[3]) if len(sys.argv) > 3 else 60000, frames=int(sys.argv[2]) if len(sys.argv) > 2 else 60)
    else:
        f = selfcheck()
        print("\n".join(f) if f else "rise_law selfcheck: every synthetic burst is a rise, the flat tail is none, the measured "
              "hand-over a rise from every phase, the room lamp's dip a dip")
        sys.exit(1 if f else 0)
