"""EVERY GI ROW AFTER A STOP IS OWED BY THE MOTION, within its bound (PHOTON-7 stage 5, 5-5, the lead's ruling for a walk
inside one lattice cell) - judged the ONE way both rows judge it (gi.hold_after_stop's ROOM sub-cell walk,
rtreflect.still_after_stop's glass walk). The demand's read-back lags, so a face the walk's last frames first hit
arrives after the stop, and admitting it is right; a plane the window entered before the stop is refined after it. So:
  A CARD ROW is a DEMAND ARRIVAL whose stamps all predate the stop, admitted within the demand's period + its read-back
    lag of it (CardCacheStatus::demandReadEvery + demandReadLag) and captured the frame after;
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
    D = int(w[0]["ifd_restart_depth"]) + int(w[0]["ifd_mark_lag"])
    follows0 = int(w[0]["ifd_follows"])
    arrivals = []   # (window frame, cache frame, the arrivals' newest mark, cards)
    for k in range(len(w)):
        got = int(w[k]["cards_admitted"]) - int(w[k - 1]["cards_admitted"]) if k else 0
        if got > 0:
            arrivals.append((int(w[k]["frame"]), int(w[k]["cards_frame"]), int(w[k]["cards_arrival_mark"]), got))
    owed, stray = [], []
    for r in w[1:]:
        f, names = int(r["frame"]), rows_of(r)
        if not names:
            continue
        cards = [n for n in names if n.startswith("cards.")]
        ifd = [n for n in names if n.startswith("ifd.")]
        other = [n for n in names if not n.startswith("cards.") and not n.startswith("ifd.")]
        if cards:
            a = [x for x in arrivals if x[0] <= f and x[0] >= f - 1]
            ok = bool(a) and all(m <= stop and c - stop <= L for _, c, m, _ in a)
            (owed if ok else stray).append((f, "cards: arrival %s (stop %d, bound %d)" % (a or "none", stop, L)))
        if ifd:
            ok = int(r["ifd_follows"]) == follows0 and f <= D
            (owed if ok else stray).append(
                (f, "ifd: the refinement of a plane entered before the stop (window moves since %d, bound %d)"
                 % (int(r["ifd_follows"]) - follows0, D)))
        if other:
            stray.append((f, "no motion's cause: %s" % other))
    last = max([f for f, _ in owed] or [0])
    return owed, stray, last
