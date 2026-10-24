"""EVERY GI ROW AFTER A STOP IS OWED BY THE MOTION, within its bound (PHOTON-7 stage 5, 5-5, the lead's ruling for a walk
inside one lattice cell) - judged the ONE way both rows judge it (gi.hold_after_stop's ROOM sub-cell walk,
rtreflect.still_after_stop's glass walk). The demand's read-back lags, so a face the walk's last frames first hit
arrives after the stop, and admitting it is right; a plane the window entered before the stop is refined after it. So:
  A CARD ROW is the WORK OF A DEMAND ARRIVAL whose stamps all predate the stop, admitted within the demand's period + its
    read-back lag of it (CardCacheStatus::demandReadEvery + demandReadLag): its n cards captured at the capture budget,
    C = cardBudgetTexels / page^2 cards a frame, and their indirect at the indirect budget, I = cardIndirectTexels /
    page^2 a frame (the tier table's, as the stillness record carries them - a card is at most a page, so the bound is
    an upper one), the arrivals in their order (a later one's work queues behind an earlier one's) - through frame
    a + max(ceil(n / C), ceil(n / I)) - 1, plus the frame after (a capture lands the frame after its admission). Every
    card row it admits names its arrival. (The photon-7r merge, the lead's ruling: 20 cards the gather's demand admitted
    at a room walk's stop are captured over 3 frames at 8 a frame and take their indirect over 5 at 4 a frame - the
    window of one frame read them stray.);
  A FIELD ROW is the REFINEMENT of a plane the window entered before the stop - no window move after it - within the
    restart's depth + the marks' lag (GiStatus::ifdRestartDepth + ifdMarkLag);
  any other row after the stop, or a row past its bound, is STRAY.
`w` is a stillness window's rows (jahlab's stillness door TSV, csv.DictReader): row 0 is the first still frame, the stop
(the cache's frame of the walk's last move is its cards_frame - 1), and is not judged - its rows are the last move's.
"""


def rows_of(r):
    """The GI rows a stillness frame ran, by name."""
    return [] if r["gi_rows"] == "-" else [it.split(":")[0] for it in r["gi_rows"].split(",")]


def judge(w):
    """(owed, stray, last): the post-stop GI rows owed by the motion and the stray ones, each (frame, its cause and
    stamp), and the last owed frame (0 with none)."""
    stop = int(w[0]["cards_frame"]) - 1
    L = int(w[0]["cards_demand_every"]) + int(w[0]["cards_demand_lag"])
    page = int(w[0]["cards_page_size"]) ** 2
    C = max(1, int(w[0]["cards_budget_texels"]) // page) if page else 1
    I = max(1, int(w[0]["cards_indirect_texels"]) // page) if page else 1
    D = int(w[0]["ifd_restart_depth"]) + int(w[0]["ifd_mark_lag"])
    follows0 = int(w[0]["ifd_follows"])
    arrivals = []   # (window frame, cache frame, the arrivals' newest mark, cards)
    for k in range(len(w)):
        got = int(w[k]["cards_admitted"]) - int(w[k - 1]["cards_admitted"]) if k else 0
        if got > 0:
            arrivals.append((int(w[k]["frame"]), int(w[k]["cards_frame"]), int(w[k]["cards_arrival_mark"]), got))
    # EACH LEGITIMATE ARRIVAL'S WORK WINDOW: its captures and its indirect at the budgets, queued behind the earlier ones
    windows = []   # (first frame, last frame, the arrival)
    cap_end = ind_end = None
    for a, c, m, n in arrivals:
        if not (m <= stop and c - stop <= L):
            continue
        cap_end = max(a - 1, cap_end if cap_end is not None else a - 1) + -(-n // C)
        ind_end = max(a - 1, ind_end if ind_end is not None else a - 1) + -(-n // I)
        windows.append((a, max(cap_end, ind_end) + 1, (a, c, m, n)))
    owed, stray = [], []
    for r in w[1:]:
        f, names = int(r["frame"]), rows_of(r)
        if not names:
            continue
        cards = [n for n in names if n.startswith("cards.")]
        ifd = [n for n in names if n.startswith("ifd.")]
        other = [n for n in names if not n.startswith("cards.") and not n.startswith("ifd.")]
        if cards:
            own = [x for x in windows if x[0] <= f <= x[1]]
            late = [x for x in arrivals if x[0] <= f and x[0] >= f - 1 and not (x[2] <= stop and x[1] - stop <= L)]
            if own and not late:
                a, c, m, n = own[0][2]
                owed.append((f, "cards: the arrival at frame %d (%d cards, mark %d, cache frame %d; stop %d, bound %d) - "
                                "its captures at %d a frame, its indirect at %d a frame, through frame %d"
                             % (a, n, m, c, stop, L, C, I, own[0][1])))
            else:
                stray.append((f, "cards: arrival %s (stop %d, bound %d; budgets %d / %d a frame)"
                              % (late or "none in its work window", stop, L, C, I)))
        if ifd:
            ok = int(r["ifd_follows"]) == follows0 and f <= D
            (owed if ok else stray).append(
                (f, "ifd: the refinement of a plane entered before the stop (window moves since %d, bound %d)"
                 % (int(r["ifd_follows"]) - follows0, D)))
        if other:
            stray.append((f, "no motion's cause: %s" % other))
    last = max([f for f, _ in owed] or [0])
    return owed, stray, last
