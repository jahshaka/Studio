#!/bin/bash
# gi.walk_rough (PHOTON-7 stage 7, 7-3; re-pointed in 10-3 R12; THE LAW RE-DERIVED in ROUGH-LOBE-HISTORY-1 stage 2C) — THE
# ROUGH SPECULAR'S WALK LAGS ITS CONVERGED PICTURE BY NO MORE THAN ITS HISTORY'S OWN LAG AND ITS OWN NOISE: photon-rough's
# four rough metal strips (0.45 - 0.75), the reflection's cutoff 0 (no traced reflection: the strips' environment term is
# the probes' prefiltered lobe whole, riding the gather's history), Epic, 960x540; a 2 m walk along them (0,1.6,2.4 ->
# 0,1.6,0.4 looking at 0,0,-3, 60 frames from a converged hold), and at walk frames k = 14, 28 and 59, per ROUGH pixel
# (the class map's 2)
#     |walk(k) - conv(k)| <= (1 - 1/n_eff) (|conv(k) - conv(k - h)| + 1) + 3 sd + 1,   n_eff = min(n + 1/2, h),
#     sd = sigma sqrt(V(n) + 1/R)
# conv(j) the picture converged at the walk's pose j (the GI at rest there: CONVERGED, below), n the gather's history
# count at the pixel (the dump's .ghc: the frames its mean holds, saturating at 15), h the history's floor in frames and R
# the reference's held frames (both READ from the walk's record, below), sd the spread of walk(k) - conv(k) (THE NOISE
# TERM, below), sigma the pixel's per-frame standard deviation of the RAW lobe at pose k (gather.temporal 0: twelve
# frames' estimates alone, in the same session - the lead's option B: a test's statistic, no engine channel), the +1 the
# two 8-bit pictures' rounding. THE BAR: the breaking share <= 1 % of
# the rough pixels at each k (at 3 sigma an honest estimate breaks ~0.3 % by chance). (It replaced the live residual
# against "the cone's recorded 48.32 %", the producer the lobe replaced - a picked bar.)
# THE LAG TERM, DERIVED (the arithmetic is LAW_PY below, self-checked by gi.walk_rough.law):
#   THE HISTORY (rq_probe_integrate.comp:1257-1294; the rough lobe takes the diffuse's blend, Lumen's one N for both):
#     m_j = (1 - b_j) m_(j-1) + b_j x_j,  b_j = max(1 / n_j, alpha),  alpha = 1 / h,
#   n_j the count (this frame included); the fast update only RAISES b (N' = min(N, (1 - F)(h - 1)), b = 1 / (1 + N'),
#   the stored count round(N') + 1: so b >= 1 / (n + 1/2) at the stored count n). Its expectation's lag behind c:
#     lag_j = c_j - E[m_j] = (1 - b_j) (lag_(j-1) + c_j - c_(j-1))
#   - the sum Sum_j alpha (1 - alpha)^j (c(k) - c(k - j)) at the floor. For a RAMP (c_j - c_(j-1) = s) its fixed point
#   at b = alpha is lag = ((1 - alpha) / alpha) s = (h - 1) s; and as b_j >= alpha on every frame, lag <= (h - 1) s holds
#   whatever the history's past (refusals, fast cuts) - so one frame at b >= 1 / n_eff from it bounds
#     lag <= (1 - 1/n_eff) h s,   n_eff = min(n + 1/2, h)   ((h - 1) s for every n >= h - every saturated count; 0 at n = 0).
#   THE SLOPE is read BACKWARD over the window the EMA remembers: s = |c(k) - c(k - h)| / h, from the converged pose h
#   frames behind k. Both pictures are 8-bit, so |c(k) - c(k - h)| is known to +-1 code (the slope to +-1/h a frame), and
#   the bound grants that rounding (else a true lag at the bound breaks by the pictures' rounding alone).
#   The law the row held before (N - 1) |conv(k+1) - conv(k)| took the COUNT for the memory (15 against h = 10) and ONE
#   FORWARD 8-bit step (+-1 code: a ramp of 0.3 code a frame lags 2.7 codes and reads a step of 0 at 70 % of the rounding
#   offsets - granted 0 - and 1 at the rest - granted 14); on k14 it read 1.8066 % where this law reads 0.8915 %, the
#   same pixels and noise (spikes/rough-lobe-history-1/REFUTE-2C.log). A pixel starved of samples (blend 0: the hit list
#   full) keeps its mean and lags past the bound - the law shows it.
# THE NOISE TERM, DERIVED (ROUGH-LOBE-HISTORY-1 stage 2C, the lead's ruling; LAW_PY's walk_var / noise_sd, self-checked):
#   walk(k) = Sum_j w_j x_(k-j), the raw estimates x iid of spread sigma, so Var = sigma^2 Sum_j w_j^2 = sigma^2 V: V = 1/n
#   for a history of n <= h frames (the true mean), then V = q/h + (alpha / (2 - alpha))(1 - q), q = (1 - alpha)^(2(n - h)),
#   falling with age to the floor's alpha / (2 - alpha) (1/19 at h = 10). conv(k) is the HELD REST MEAN of R frames
#   (restLengthOf = settleFramesOf(h) = the walk's settle_frames at the lab's default tuning: 16), Var = sigma^2 / R, its
#   draws not the walk's. So sd(walk - conv) = sigma sqrt(V + 1/R). The count bounds V as it bounds the lag: a fast cut
#   stores round(N') + 1, so b <= 1 / (n - 1/2), and V(n) is the fresh form at n - 1/2 (at least 1 frame); a saturated count
#   (15) hides the age, and V only falls with age, so V(15) is the form at 14.5 - the youngest history that saturates.
#   The term the row held before, sigma / sqrt(count), left out the reference's own noise (1/R) and took the count for the
#   EMA's effective samples (2h - 1 = 19 at the floor): it was SHORT of the true spread. At count 15 the true spread is
#   0.3393 sigma for an old history (the floor form, +31 % on the old 0.2582) and at most 0.3654 sigma (+42 %) at the
#   youngest saturating one - the term this row takes. That is a CORRECTION, not a loosening: the old term was narrower
#   than the quantity it bounds: 3 of the old sd is 2.28 of the true one at the floor, which gaussian noise passes 2.2 % of
#   the time, not the ~0.3 % the bar assumes (8x, before the +1 code). At a young count V(n) = 1/(n - 1/2) is wider than
#   the old 1/n (a fast cut's rounding), and 1/R widens it more: at every count the term is wider than the old one.
#   h IS READ, NEVER WRITTEN HERE: the walk's OWN stillness record carries the history's floor itself (rough-walk.tsv
#   history_frames, GatherStatus::historyFrames: the shipped kHistoryFrames or the A/B door gather.historyFrames,
#   OgreScreenProbeGather.cpp historyFramesOf) and the rest mean's length R beside it (settle_frames, settleFramesOf(h));
#   the row refuses a walk whose record names no single h or no single settle.
# THE SHARE ACROSS SESSIONS (measured, stage 2C): the walk and the converged pictures are byte-identical whatever the
# session's frame index (a second session, every session 30 frames later: |walk - conv| mean and p99 equal to the digit);
# only the twelve raw frames are other draws, so sigma is. This law's share moved 0.8338 -> 0.8345 % (k14) and 0.9336 ->
# 0.9403 % (k28) (spikes/rough-lobe-history-1/SPREAD-2C.log), as a 12-sample sigma re-drawn predicts (chi-square, 11
# dof: sd 0.004 points, REFUTE-2C-noise-and-spread.txt). The old law moved 1.6484 -> 1.8066 % at k14 on such a re-draw:
# its allowance was the noise term alone on 89 % of its breakers.
# THE TAIL UNDER MOTION, measured (2026-10-27): NOT a bias of the moving rule (the moving rule held at pose 59 -
# gather.restOff - for 64 frames against the rest mean there: -0.015 codes, each strip within 0.3) and NOT the statistics
# (at that fixed pose the moving error's p99 2.5 codes sits inside the noise term): the excess exists only under MOTION.
# B4's VIRTUAL-POINT history (PHOTON-7 8B) was built and measured here and REVERTED: one mean distance across a lobe
# spanning a depth break (a near strip edge before the far walls) names a point on neither (k 14 doubled).
# CONVERGED (HARNESS-3 (a), RE-DERIVED; it was a picked 600 frames): a converged picture is the one the engine's own settle
# predicate says has stopped moving - GiStatus::giAtRest, the one --engine-selftest waits on: the chain owes nothing, the
# cards' rest decision is taken, and the gather's history is settled (an EMA at h = 10 takes ceil(ln(1/D) / ln(1 - 1/h))
# frames to bring a D-code step under one code: 16 at D = 5, 53 at the full 255) and HELD (the rest mean of N = 16 rest
# frames). No count derived from N alone bounds it: measured on this fixture (one session, 2026-10-29) the predicate was
# false for 200 frames at every pose, the gather's rest started 172-245 frames after a pose change (the cards' queue
# drained at 98-158, then the rest), and the picture at 53 frames read > 1 code from the held one on 3.2-5.8 % of pixels
# (12-25 codes at worst) - the live EMA's noise around the rest mean. So every converged pose here is the lab's
# `rest 600`: frames until giAtRest, REFUSED past 600 (the old count kept as the refusal, never a wait).
# THE SPLIT BY PROOF (HARNESS-3 (a); the row whole ran 55.78 / 57.69 s against the 45-s quiet law): ONE ROW PER WALK FRAME k,
# its law alone, on the walk's dump - a fixture chain:
#   gi.walk_rough.walk   `rough_lag.sh <jahlab> <walk dir> walk`: the walk session (a converged hold at the start, the
#                        teleport, hold:1, the 60-frame walk, the door's 8 frames and its 60-frame trail, the dump) - its
#                        dump pruned to frames 14 / 28 / 59 (FIXTURES_SETUP gi_walk_rough_dump);
#   gi.walk_rough.k<k>   `rough_lag.sh <jahlab> <walk dir> <k> <own dir>`: the converged poses k - h (the slope) and k and
#                        the twelve raw frames at k, then the law at k on the walk's frame k.
# 960x540; one jahlab session a row. Each row prints its law's numbers as LAW lines (tests/support/lawline.h).
# usage: rough_lag.sh law (the law's self-check: no card, no display) | rough_lag.sh <jahlab> <walk dir> walk |
#        rough_lag.sh <jahlab> <walk dir> 14|28|59 <work dir>
set -u
# THE LAW's ARITHMETIC (the one copy: the self-check below runs it; the header derives it)
LAW_PY=$(cat <<'PY'
import math
import numpy as np
KCOUNT_MAX = 15        # the packed count's four bits (jah_probe_history.glsl kHistoryCountMax)
def lag_steps(n, h):
    """(1 - 1/n_eff) h, n_eff = min(n + 1/2, h): the lag's bound in frames of the slope at the stored count n (0: none)"""
    n = np.asarray(n, dtype=np.float64)
    return np.where(n >= 1.0, h * (1.0 - 1.0 / np.minimum(n + 0.5, float(h))), 0.0)
def weights_var(x, h):
    """Sum_j w_j^2 of a history grown from one frame to x frames (x real, >= 1): 1/x to the floor h, then the EMA's"""
    x = np.maximum(np.asarray(x, dtype=np.float64), 1.0)
    a = 1.0 / h
    q = (1.0 - a) ** (2.0 * np.maximum(x - h, 0.0))
    return np.where(x <= h, 1.0 / x, q / h + a / (2.0 - a) * (1.0 - q))
def walk_var(n, h):
    """the bound on Sum_j w_j^2 at the stored count n (refusals and fast cuts included): the fresh form at n - 1/2"""
    return weights_var(np.minimum(np.asarray(n, dtype=np.float64), float(KCOUNT_MAX)) - 0.5, h)
def noise_sd(n, h, R):
    """sd(walk - conv) / sigma: the walk's history beside the reference's held mean of R frames"""
    return np.sqrt(walk_var(n, h) + 1.0 / R)
def lag_allowance(conv, k, n, h):
    """the lag the law grants at walk frame k: conv maps a pose to its converged picture in codes (the larger channel's
    change where the picture has channels), n the history count, h the floor read from the walk's record"""
    d = np.abs(np.asarray(conv[k], dtype=np.float64) - np.asarray(conv[k - h], dtype=np.float64))
    if d.ndim == 3:
        d = d.max(axis=2)
    return lag_steps(n, h) / h * (d + 1.0)
PY
)
if [ "${1:-}" = law ]; then
    # gi.walk_rough.law: THE LAW's ARITHMETIC CHECKED ON SYNTHETIC RAMPS (no card, no display; RED for the old form: a
    # ramp of 0.3 code a frame read through one forward step granted 0 for a lag of 2.7)
    python3 -I -c "$LAW_PY
$(cat <<'PY'
import sys
fails = []
def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        fails.append(what)
# THE SHADER's HISTORY, frame by frame (rq_probe_integrate.comp:1257-1290): count 0 takes the frame whole; the blend
# max(1 / (count + 1), 1 / h); the fast update at amount F caps N' = min(1 / b - 1, (1 - F)(h - 1)), b = 1 / (1 + N'), and
# stores round(N') + 1; the count saturates at 15. A REFUSAL (another surface's history) restarts it at count 1.
def history(xs, h, events):
    m, cnt, out = None, 0, []
    for j, x in enumerate(xs):
        ev = events.get(j)
        if m is None or ev == 'refuse':
            m, cnt = x, 1
        else:
            b = max(1.0 / (cnt + 1), 1.0 / h)
            nxt = cnt + 1
            if isinstance(ev, float):
                nf = min(1.0 / b - 1.0, (1.0 - ev) * (h - 1.0))
                b = 1.0 / (1.0 + nf)
                nxt = int(nf + 0.5) + 1
            m = m + b * (x - m)
            cnt = min(nxt, KCOUNT_MAX)
        out.append((m, cnt))
    return out
# 2. THE CLOSED FORM: on a ramp at the floor the lag is (1/alpha - 1) s = (h - 1) s exactly
worst = 0.0
for h in (2, 5, 10, 15):
    for s in (0.05, 0.3, 3.0):
        xs = [s * j for j in range(600)]
        m, _ = history(xs, h, {})[-1]
        worst = max(worst, abs((xs[-1] - m) - (h - 1.0) * s) / ((h - 1.0) * s))
check(worst < 1e-9, 'an EMA on a ramp lags (1/alpha - 1) s: the largest relative error %.1e over h 2/5/10/15, s 0.05/0.3/3' % worst)
# 3. THE BOUND HOLDS WHATEVER THE PAST: random refusals and fast cuts; at every frame 0 <= lag <= lag_steps(count, h) s
rng = np.random.default_rng(20261121)
over, frames = 0.0, 0
for trial in range(1500):
    h = int(rng.integers(2, KCOUNT_MAX + 1)); s = float(rng.uniform(0.01, 3.0))
    ev = {}
    for j in rng.choice(np.arange(1, 160), size=int(rng.integers(0, 12)), replace=False):
        ev[int(j)] = 'refuse' if rng.random() < 0.3 else float(rng.uniform(0.01, 0.9))
    xs = [s * j for j in range(160)]
    for j, (m, cnt) in enumerate(history(xs, h, ev)):
        lag = xs[j] - m
        over = max(over, lag - float(lag_steps(cnt, h)) * s, -lag)
        frames += 1
check(over <= 1e-9, 'the lag never passes (1 - 1/n_eff) h s, n_eff = min(n + 1/2, h), at the stored count: %d frames of '
      '1500 histories with refusals and fast cuts, the largest excess %.2e codes' % (frames, over))
# 4. READ FROM TWO 8-BIT CONVERGED PICTURES (display rounding), at every slope 0..3 code a frame and every rounding offset:
#    the allowance is a BOUND on the true lag (h - 1) s, and no looser than the pictures' rounding (2 (1 - 1/h) codes)
k = 30
s = np.arange(0.0, 3.0005, 0.01)[:, None]; a = np.arange(0.0, 1.0, 0.01)[None, :]
for h in (5, 10, 15):
    conv = {j: np.floor(100.0 + a + s * j + 0.5) for j in range(k - h, k + 2)}
    grant = lag_allowance(conv, k, np.full(s.shape, 15.0), h) * np.ones_like(a)
    true = (h - 1.0) * s * np.ones_like(a)
    short, loose = float((true - grant).max()), float((grant - true).max())
    check(short <= 1e-9 and loose <= 2.0 * (1.0 - 1.0 / h) + 1e-9,
          'h %d: the allowance from 8-bit pictures bounds the true lag at every slope and offset (short by at most %.3f '
          'codes; loose by at most %.3f, the rounding\'s %.3f)' % (h, max(short, 0.0), loose, 2.0 * (1.0 - 1.0 / h)))
    if h == 10:
        i = int(round(0.3 / 0.01))
        check(float(grant[i].min()) >= 2.7 - 1e-9, 'h 10, a ramp of 0.3 code a frame lags 2.70 codes: the law grants %.2f..%.2f'
              % (float(grant[i].min()), float(grant[i].max())))
# 5. THE NOISE TERM: a simulated history of iid unit noise (the shader's rule) against a 16-frame held mean - the sample
#    sd of the difference equals sqrt(V + 1/R) within its own error (1/sqrt(2M) relative), at the floor and at every
#    young count; RED for the old 1/sqrt(count)
M, R = 40000, 16
tol = 4.0 / math.sqrt(2.0 * M)
for h in (5, 10, 15):
    x = rng.standard_normal((260, M))
    m = x[0].copy(); cnt = 1
    sds = {}
    for j in range(1, 260):
        b = max(1.0 / (cnt + 1), 1.0 / h)
        m += b * (x[j] - m); cnt = min(cnt + 1, KCOUNT_MAX)
        if j + 1 <= KCOUNT_MAX:
            sds[j + 1] = m.copy()
    conv = rng.standard_normal((R, M)).mean(axis=0)
    floor = float(np.std(m - conv, ddof=1)); want = math.sqrt((1.0 / h) / (2.0 - 1.0 / h) + 1.0 / R)
    old = 1.0 / math.sqrt(KCOUNT_MAX)
    check(abs(floor / want - 1.0) < tol and float(noise_sd(KCOUNT_MAX, h, R)) >= want,
          'h %d, an old history: sd(walk - conv) %.4f against the floor form %.4f (tolerance %.1f %%); the row takes %.4f '
          '(the youngest saturating history); the old sigma/sqrt(count) %.4f is %.0f %% short'
          % (h, floor, want, 100 * tol, float(noise_sd(KCOUNT_MAX, h, R)), old, 100 * (1 - old / floor)))
    young = [(c, float(np.std(v - conv, ddof=1)), math.sqrt(float(weights_var(c, h)) + 1.0 / R)) for c, v in sds.items()]
    bad = [(c, s, w) for c, s, w in young if abs(s / w - 1.0) > tol]
    check(not bad, 'h %d, a history aged n = 2..15: sd(walk - conv) equals sqrt(V(n) + 1/R) at every n (the worst %.2f %%)%s'
          % (h, 100 * max(abs(s / w - 1.0) for c, s, w in young), (': off at %s' % bad[:3]) if bad else ''))
# 6. THE ROW's V BOUNDS EVERY HISTORY: Sum w^2 propagated exactly (V' = (1 - b)^2 V + b^2) under refusals and fast cuts
worst = 0.0
for trial in range(1500):
    h = int(rng.integers(2, KCOUNT_MAX + 1)); V, cnt = None, 0
    ev = {}
    for j in rng.choice(np.arange(1, 160), size=int(rng.integers(0, 20)), replace=False):
        ev[int(j)] = 'refuse' if rng.random() < 0.3 else float(rng.uniform(0.01, 0.9))
    for j in range(160):
        e = ev.get(j)
        if V is None or e == 'refuse':
            V, cnt = 1.0, 1
        else:
            b = max(1.0 / (cnt + 1), 1.0 / h); nxt = cnt + 1
            if isinstance(e, float):
                nf = min(1.0 / b - 1.0, (1.0 - e) * (h - 1.0)); b = 1.0 / (1.0 + nf); nxt = int(nf + 0.5) + 1
            V = (1.0 - b) ** 2 * V + b * b; cnt = min(nxt, KCOUNT_MAX)
        worst = max(worst, V / float(walk_var(cnt, h)))
check(worst <= 1.0 + 1e-12, 'the row\'s V(n) bounds Sum w^2 of 1500 histories with refusals and fast cuts (the largest '
      'ratio %.4f)' % worst)
print('gi.walk_rough.law: %s' % ('FAIL (%d)' % len(fails) if fails else 'PASS'))
sys.exit(1 if fails else 0)
PY
)"
    exit $?
fi
LAB=${1:?jahlab}; WALK=${2:?the walk dir}; PART=${3:?walk, 14, 28 or 59}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
case $PART in
walk) WD=$WALK ;;
14|28|59) WD=${4:?the work dir} ;;
*) echo "FAIL: the part is walk, 14, 28 or 59, not '$PART'"; exit 2 ;;
esac
rm -rf "$WD"; mkdir -p "$WD"
SRC="--data-root $WD/data --env photon-rough --tier epic"
session() {   # name (the script is $WD/<name>.lab)
    "$LAB" --headless --no-stdin --size 960x540 $SRC --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    rc=$?
    grep -E '^LAB REST' "$WD/$1.log"
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR|stillness:' "$WD/$1.log" | head -5; exit 1; }
}
DUMP=$WALK/data/stillness/dump-rough-walk
if [ "$PART" = walk ]; then
    S=0,1.6,2.4; T=0,0,-3; E=0,1.6,0.4
    printf 'row reflectionRoughnessCutoff 0\ncamera %s,%s\nrest 600\nstillness teleport:%s,%s/hold:1/walk:%s,%s:60 8 trail=60 dump=rough-walk out=rough-walk\nquit\n' \
        "$S" "$T" "$S" "$T" "$E" "$T" > "$WD/walk.lab"
    session walk
    # the dump pruned to the three frames the k rows read (each frame ~2.6 MB; the trail writes ~130)
    find "$DUMP" -type f ! -name 'rough-walk-t014.*' ! -name 'rough-walk-t028.*' ! -name 'rough-walk-t059.*' -delete
    n=$(ls "$DUMP" 2>/dev/null | wc -l)
    echo "gi.walk_rough.walk: the dump keeps $n files (frames 14 / 28 / 59)"
    [ "$n" -ge 12 ] || { echo "FAIL: the walk's dump lacks frames 14 / 28 / 59 ($n files)"; exit 1; }
    rm -rf "$WD/data/shots"
    echo "gi.walk_rough.walk: PASS"
    exit 0
fi
K=$PART
# THE FLOOR h AND THE REST's LENGTH R, READ from the walk's own stillness record (history_frames and settle_frames: one value
# each over the walk - the header)
TSV=$WALK/data/stillness/rough-walk.tsv
HF=$(python3 -I -c "$LAW_PY
$(cat <<'PY'
import csv, sys
try:
    rows = [r for r in csv.DictReader(open(sys.argv[1]), delimiter='\t') if r['window'] == 'walk-move']
    hs = sorted({int(r['history_frames']) for r in rows})
    settles = sorted({int(r['settle_frames']) for r in rows})
except (OSError, KeyError, ValueError) as e:
    print('FAIL: the walk\'s record %s is unreadable (%s)' % (sys.argv[1], e)); sys.exit(1)
if len(hs) != 1 or hs[0] < 1 or len(settles) != 1 or settles[0] < 1:
    print('FAIL: the walk\'s record names no single history floor and settle (history_frames %s, settle_frames %s over %d walk '
          'frames)' % (hs, settles, len(rows)))
    sys.exit(1)
print('%d %d' % (hs[0], settles[0]))
PY
)" "$TSV")
[ $? -eq 0 ] || { echo "$HF"; exit 1; }
read -r HH SETTLE <<< "$HF"
KB=$((K - HH))
[ "$KB" -ge 0 ] || { echo "FAIL: walk frame $K is younger than the history's floor $HH: no converged pose $KB"; exit 1; }
echo "gi.walk_rough.k$K: the history's floor h = $HH (the walk's settle $SETTLE); the slope's pose k - h = $KB"
{
    printf 'row reflectionRoughnessCutoff 0\n'
    for k in $KB $K; do
        python3 -I -c "
s=(0.0,1.6,2.4); e=(0.0,1.6,0.4); k=$k/60.0
print('camera %.6f,%.6f,%.6f,0,0,-3' % tuple(a+(b-a)*k for a,b in zip(s,e)))"
        printf 'rest 600\nshot conv%d\n' "$k"
        if [ "$k" = "$K" ]; then
            printf 'arm gather.temporal 0\nframes 2\n'
            for j in $(seq 0 11); do printf 'frames 1\nshot raw%d_%d\n' "$k" "$j"; done
            printf 'arm gather.temporal 1\n'
        fi
    done
    printf 'quit\n'
} > "$WD/conv.lab"
session conv
python3 -I -c "$LAW_PY
$(cat <<'PY'
import os, sys
import numpy as np
from PIL import Image
root, dump, k = sys.argv[1], sys.argv[2], int(sys.argv[3])
W, H = 960, 540
fails = []
def png(name):
    return np.asarray(Image.open(os.path.join(root, 'shots', name + '.png')).convert('RGB')).astype(np.float64)
base = os.path.join(dump, 'rough-walk-t%03d' % k)
need = [base + x for x in ('.rgb', '.cls', '.ghc', '.gsize')]
missing = [p for p in need if not os.path.exists(p)]
if missing:
    print('FAIL: walk frame %d: missing %s (gi.walk_rough.walk writes them)' % (k, ', '.join(os.path.basename(p) for p in missing)))
    sys.exit(1)
walk = np.fromfile(base + '.rgb', dtype=np.uint8).reshape(H, W, 3).astype(np.float64)
cls = np.fromfile(base + '.cls', dtype=np.uint8).reshape(H, W)
gw, gh = map(int, open(base + '.gsize').read().split())
n = np.fromfile(base + '.ghc', dtype=np.uint8).reshape(gh, gw).astype(np.float64)
if (gw, gh) != (W, H):   # (the history's grid onto the picture's)
    n = n[(np.arange(H) * gh // H)][:, (np.arange(W) * gw // W)]
hh, kb = int(sys.argv[5]), int(sys.argv[6])
c0, ckb = png('conv%d' % k), png('conv%d' % kb)
raw = np.stack([png('raw%d_%d' % (k, j)) for j in range(12)])
sigma = raw.std(axis=0, ddof=1).max(axis=2)
R = int(sys.argv[7])
N = np.maximum(n, 1.0)
se = sigma * noise_sd(n, hh, R)
lhs = np.abs(walk - c0).max(axis=2)
rough = cls == 2
if rough.sum() < 50000:
    print('FAIL: walk frame %d: %d rough pixels (< 50,000): the fixture moved' % (k, rough.sum())); sys.exit(1)
grant = lag_allowance({k: c0, kb: ckb}, k, n, hh)
slope = np.abs(c0 - ckb).max(axis=2)
breaks = rough & (lhs > grant + 3.0 * se + 1.0)
share = breaks.sum() / rough.sum()
print('walk frame %d: rough %d px, h %d (the slope from pose %d), R %d, n median %.0f, sigma median %.2f codes, sd median %.2f '
      '(p90 %.2f); |walk - conv| mean %.2f p99 %.1f; the lag granted median %.2f (p90 %.2f) codes; breaking %.2f %% (bar 1 %%)'
      % (k, rough.sum(), hh, kb, R, np.median(n[rough]), np.median(sigma[rough]), np.median(se[rough]),
         np.percentile(se[rough], 90), lhs[rough].mean(), np.percentile(lhs[rough], 99), np.median(grant[rough]),
         np.percentile(grant[rough], 90), 100 * share))
print('LAW k%d.breaking %.4f %% <= 1' % (k, 100 * share))
print('LAW k%d.lag_mean %.4f codes' % (k, lhs[rough].mean()))
print('LAW k%d.lag_p99 %.4f codes' % (k, np.percentile(lhs[rough], 99)))
# THE RESIDUE's DUMP (ROUGH-LOBE-HISTORY-1 stage 2.0; the reader's candidates, spikes/rough-lobe-history-1/RESIDUE.md):
# WHERE the breaking pixels sit, printed and kept - never a law, never a bar (no LAW line). Kept in the k's own work dir
# (rough_lag.sh removes it only at the row's next run): breaks.png (red a breaking EDGE pixel, white a breaking INTERIOR
# one, grey a rough pixel within the law, black the rest) and lag.png (|walk - conv| in codes, the law's left side).
#   EDGE (candidate A, another surface's history): within 2 px of a DEPTH BREAK - a relative jump over 5 % between two
#     4-neighbours of the walk dump's own view distance for this frame (.dist, the history's grid, mapped as .ghc is; 0 = no
#     surface, itself a break) - or of a CLASS-MAP BOUNDARY (.cls: a 4-neighbour of another class). INTERIOR: the rest.
#   SLOPE 0: the law's |conv(k) - conv(k - h)| reads 0 codes (the lag term at its rounding's grant alone).
#   LOW N (candidate D, young pixels): the history count N under the gather's young frames (kYoungFrames 12,
#     OgreScreenProbeGather.cpp - the young read's reach narrows to the bilinear at 12 frames); with the bottom band's share
#     (the breakers in the picture's lowest quarter of rows, against the rough class's own share there).
#   SIGN (candidate E, the clamp): the share of breakers whose walk is BRIGHTER than conv (the larger channel's sign).
#   The inputs are kept as law.npz beside the pictures (walk, the two converged poses, sigma, the count, the classes, the
#   edge mask, h), so a reading needs no second card run.
wd = sys.argv[4]
def dilate(m, r):
    p = np.pad(m, r)
    out = np.zeros_like(m)
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= p[r + dy:r + dy + m.shape[0], r + dx:r + dx + m.shape[1]]
    return out
dist = np.fromfile(base + '.dist', dtype=np.float32).reshape(gh, gw).astype(np.float64) if os.path.exists(base + '.dist') else None
edge = np.zeros((H, W), dtype=bool)
if dist is not None:
    if (gw, gh) != (W, H):
        dist = dist[(np.arange(H) * gh // H)][:, (np.arange(W) * gw // W)]
    brk = np.zeros((H, W), dtype=bool)
    for a, b, sl_a, sl_b in ((dist[:, :-1], dist[:, 1:], (slice(None), slice(None, -1)), (slice(None), slice(1, None))),
                             (dist[:-1, :], dist[1:, :], (slice(None, -1), slice(None)), (slice(1, None), slice(None)))):
        lo = np.minimum(a, b)
        jump = (lo <= 0.0) & (np.maximum(a, b) > 0.0) | (lo > 0.0) & (np.abs(a - b) > 0.05 * np.maximum(lo, 1e-9))
        brk[sl_a] |= jump
        brk[sl_b] |= jump
    edge |= brk
cb = np.zeros((H, W), dtype=bool)
cb[:, :-1] |= cls[:, :-1] != cls[:, 1:]; cb[:, 1:] |= cls[:, :-1] != cls[:, 1:]
cb[:-1, :] |= cls[:-1, :] != cls[1:, :]; cb[1:, :] |= cls[:-1, :] != cls[1:, :]
edge = dilate(edge | cb, 2)
band = np.zeros((H, W), dtype=bool); band[3 * H // 4:, :] = True
signed = (walk - c0)
bright = np.take_along_axis(signed, np.abs(signed).argmax(axis=2)[..., None], axis=2)[..., 0] > 0
def residue(tag, brk):
    nb = int(brk.sum())
    def sh(m):
        return 100.0 * (m & brk).sum() / max(nb, 1)
    print('RESIDUE k%d%s: %d breaking px of %d rough (%s); EDGE %.1f %% / INTERIOR %.1f %% (the rough class: %.1f %% edge); '
          'SLOPE 0 %.1f %% (the rough class: %.1f %%); LOW N (< 12) %.1f %% (the '
          'rough class: %.1f %%); bottom quarter %.1f %% (the rough class: %.1f %%); walk BRIGHTER %.1f %%' % (
          k, tag, nb, rough.sum(), 'the dump has .dist' if dist is not None else 'NO .dist: class seams only', sh(edge),
          100.0 - sh(edge), 100.0 * (edge & rough).sum() / rough.sum(),
          sh(slope == 0), 100.0 * ((slope == 0) & rough).sum() / rough.sum(),
          sh(N < 12), 100.0 * ((N < 12) & rough).sum() / rough.sum(), sh(band), 100.0 * (band & rough).sum() / rough.sum(),
          sh(bright)))
    if nb:
        print('RESIDUE k%d%s breakers: n p10/p50/p90 %.0f/%.0f/%.0f; |walk - conv| p50/p90 %.0f/%.0f codes; slope '
              '(|conv(k) - conv(k - h)|) p50/p90 %.0f/%.0f; the lag granted p50/p90 %.1f/%.1f codes' % (
              k, tag, *np.percentile(n[brk], [10, 50, 90]), *np.percentile(lhs[brk], [50, 90]),
              *np.percentile(slope[brk], [50, 90]), *np.percentile(grant[brk], [50, 90])))
residue('', breaks)
np.savez_compressed(os.path.join(wd, 'law.npz'), walk=walk.astype(np.uint8), c0=c0.astype(np.uint8),
                    ckb=ckb.astype(np.uint8), sigma=sigma.astype(np.float32), n=n.astype(np.uint8), cls=cls, edge=edge, h=hh,
                    kb=kb)
img = np.zeros((H, W, 3), dtype=np.uint8)
img[rough] = 96
img[breaks & ~edge] = 255
img[breaks & edge] = (255, 0, 0)
Image.fromarray(img).save(os.path.join(wd, 'breaks.png'))
Image.fromarray(np.clip(lhs, 0, 255).astype(np.uint8)).save(os.path.join(wd, 'lag.png'))
print('RESIDUE k%d: kept %s and %s' % (k, os.path.join(wd, 'breaks.png'), os.path.join(wd, 'lag.png')))
if share > 0.01:
    fails.append('walk frame %d: %.2f %% of the rough pixels lag their converged picture past their memory and noise'
                 % (k, 100 * share))
for f in fails: print('FAIL: ' + f)
sys.exit(1 if fails else 0)
PY
)" "$WD/data" "$DUMP" "$K" "$WD" "$HH" "$KB" "$SETTLE"
rc=$?
rm -rf "$WD/data/shots"
rm -f "$DUMP/rough-walk-t$(printf '%03d' "$K")."*   # (this k's frame read: the walk row writes it afresh)
echo "gi.walk_rough.k$K: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
