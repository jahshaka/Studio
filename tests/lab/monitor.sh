#!/bin/bash
# lab.monitor (PHOTON-7, the step before stage 10) - `jahlab monitor <frames> <file>`: THE ENGINE'S OWN FRAME MONITOR,
# per frame, from the lab. photon-area (one area light, the sun, the sky; Epic, rays on) at 960x540: the boot's
# 30 frames (the GI's build: the cards' captures), then a lamp step after a 60-frame settle and 30 frames of its relight
# (the injection, the bounce feedback, the cards' relight, the gather, the field, the reflection, the area visibility).
# THE LAWS:
#   (1) each file holds N frame lines (F) and the summary (# SUMMARY, S per name), every record with its GPU time;
#   (2) every named GI phase ran with a positive time: the injection (vct.inject.*), the bounce (vct.feedback*), the
#       cards' capture (their capture workspace's pass) and relight (cards.relight), the gather (gather.trace,
#       gather.integrate), the field (ifd.*), the reflection (rq.reflect.trace, rq.reflect.resolve), the area
#       visibility (area.vis, area.resolve);
#   (3) THE FRAME ADDS UP: on every frame the WORK - the top-level passes and rows less their measured bubbles
#       (rbubble), and the unnamed gaps - plus the IDLE (the gaps that crossed a submission and the rows' measured
#       bubbles) is the frame's GPU span within the timestamps' resolution: 1e-3 ms (law (5)'s rounding) a sample
#       boundary, two a top-level sample and its gap (2 x tops + 2). A row inside a pass is in that pass's time, never
#       counted twice; the busy span (span - idle) holds no wait for the CPU (urgent #64);
#   (4) NOTHING UNNAMED (PHOTON-7's naming step): on every frame the gaps no row names (unattr) are at most 0.1 ms, and
#       every GPU row that encloses others (gi.step) leaves at most 0.1 ms of its own time to no child LESS ITS MEASURED
#       BUBBLE (self: - bubble:; urgent #64: a submission inside it is no longer an exemption - the bubble is the fork's
#       own timestamp pair at each flush, the flushed buffer's last BOTTOM_OF_PIPE to the next one's first TOP_OF_PIPE,
#       never a CPU-side estimate; a row whose bubble could not be measured, bubblelost:, is red) - a dispatch through
#       the Photon door directly inside gi.step files its own row (photonDispatch). (4) JUDGES THE FRAMES THAT CREATE NO
#       PIPELINE (the record's own `compiles` 0; URGENT #148, 2026-11-02): a frame that builds pipelines in the frame
#       carries the driver's first-use work in our command stream, between two rows' timestamps where no engine marker
#       can stand - MEASURED, one hold, two fresh processes on photon-7 2187edd42's jahlab: a cold engine cache (compiles
#       42 / 13 / 1 on boot frames 1-3) read 0.115 / 0.102 / 0.060 ms unnamed at the warm run's own boundaries
#       (gi.step|rq.skin, gi.step|gi.late), the warm cache (compiles 0) 0.033 / 0.043 / 0.022 there; every frame with
#       compiles 0 alike in both (<= 0.048). The frames it skips are printed with their compiles and gaps, and a part
#       with no frame to judge is red.
#       (4) IS JUDGED ON THIS PROCESS's OWN TIMESTAMP SPANS - A FOREIGN CONTEXT's SLICE IS NOT A GAP (the landing audit's
#       F6, 2026-11-04; the card is shared for good: the Unreal session, a desktop browser, a width-2 neighbour). THE
#       DERIVATION: our timestamps bracket our own commands, but the GPU time-slices between contexts, so a span between
#       two of our timestamps (a gap, or a row's own stretch between its children) holds our unnamed work O plus any
#       foreign slice X >= 0 that ran there. One slice is ONE contiguous interval of the GPU's timeline, so it lands in
#       exactly ONE of our spans - measured (block C, spikes/test-streamline-1/card/C-width: 14 reds in 4 of its runs, 1-7
#       frames of a run's 60; 9 gaps, each the frame's widest at 0.19 - 4.56 ms with the REST of the frame's unnamed time
#       0.018 - 0.042 ms, and 5 rows' own stretches of 0.14 - 2.28 ms), while our own unnamed work is the same commands at
#       the same place every frame it runs (a green run: no gap over 0.025 ms, a frame's unnamed total 0.015 - 0.048 ms).
#       Hence:
#         (4a) per frame, the unnamed time LESS ITS WIDEST GAP is at most 0.1 ms - a single slice cannot enter it, so
#              what remains is ours alone, judged on every frame as before;
#         (4b) the widest gap and every enclosing row's own time less its bubble are judged by their MEDIAN over the
#              part's judged frames (each row over the frames it ran in), at most 0.1 ms: a slice of another context
#              lands on fewer than half of them (X is uncorrelated with our frame: on the reds above 1-3 frames of 30),
#              so the median is O's; an unnamed dispatch of ours that runs on most frames reds it as before. Every
#              single span past 0.1 ms is PRINTED with its frame and place (a slice, or ours on a minority of frames).
#       THE NAMED LIMIT: an unnamed dispatch of ours in a single gap on fewer than half the judged frames reads like a
#       slice and is printed, not red - the price of a card the process does not own; a bubble that could not be
#       measured (bubblelost:) stays red on its frame;
#   (4b) THE BUBBLE's SANITY: a row with no submission inside carries a bubble of EXACTLY 0, and so do a row's own
#       stretches with none (bubblenosub: is the breach - a credit the queue's own counters never saw);
#   (5) NO OVERRUN: no row's children sum past it beyond the timestamps' rounding (1e-3 ms; overrun:) - an overlap or a
#       double count the monitor reports and never clamps.
# usage: monitor.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
printf 'monitor 30 %s/boot.txt\nframes 60\nlamp area intensity 9\nmonitor 30 %s/step.txt\nquit\n' "$WD" "$WD" > "$WD/m.lab"
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-area --script "$WD/m.lab" > "$WD/m.log" 2>&1
rc=$?
grep -E '^LAB (MONITOR|SCRIPT STOPPED|END)' "$WD/m.log"
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; tail -5 "$WD/m.log"; exit 1; }
python3 -I - "$WD" <<'PY'
import os, re, statistics, sys
wd = sys.argv[1]
fails = []
names = {}
bubbles = {}     # part -> row -> the worst own-stretch bubble measured on a frame
widests = {}     # part -> the widest gap of each judged frame ((4b): its median)
owns = {}        # part -> row -> its own time less its bubble on each judged frame it ran in ((4b): their median)
spikes = []      # every single span past 0.1 ms: printed, judged by (4b)'s median
worst_own = {}   # part -> the worst own time less its bubble
for part, n in (('boot', 30), ('step', 30)):
    lines = open(os.path.join(wd, part + '.txt')).read().split('\n')
    F = [l for l in lines if l.startswith('F ')]
    S = [l for l in lines if l.startswith('S ')]
    head = [l for l in lines if l.startswith('# SUMMARY')]
    print('%s: %d frame lines, %d summary lines; %s' % (part, len(F), len(S), head[0] if head else 'NO SUMMARY'))
    if len(F) != n or not head or not S: fails.append('(1) %s: %d frame lines of %d, summary %s' % (part, len(F), n, bool(head)))
    if head and 'records without GPU time 0' not in head[0]: fails.append('(1) %s: a record without GPU time' % part)
    worst = 0.0
    judged, skipped = 0, []
    for l in F:
        kv = dict(re.findall(r'(\w+) (-?[\d.]+(?:e-?\d+)?|nan)', l.split('|')[0]))
        span, idle, busy, passes, rows, rbubble, unattr = (float(kv[k]) for k in (
            'span', 'idle', 'busy', 'passes', 'rows', 'rbubble', 'unattr'))
        tops = int(kv['tops'])
        work = passes + rows - rbubble + unattr
        err = abs(work + idle - span)
        tol = (2 * tops + 2) * 1e-3
        worst = max(worst, err)
        if not err <= tol or abs(busy + idle - span) > 1e-3:
            fails.append('(3) %s frame %s: work %.4f (passes %.4f + rows %.4f - their bubbles %.4f + gaps %.4f) + idle '
                         '%.4f vs span %.4f: %.4f ms off, over the resolution %.4f (busy %.4f)' % (
                             part, kv.get('F', '?'), work, passes, rows, rbubble, unattr, idle, span, err, tol, busy))
        tok = dict(re.findall(r' (\S+)=(-?[\d.eE+-]+)', l.split('|', 1)[1] if '|' in l else ''))
        for name, ms in tok.items():
            names[name] = max(names.get(name, 0.0), float(ms))
        where = re.search(r' gap (-?[\d.]+) (\S+) \|', l)
        compiles = int(kv.get('compiles', 0))
        if compiles > 0:   # (4) on a frame that creates no pipeline (the header: the driver's first-use work)
            skipped.append('%s (compiles %d, unnamed %.4f ms)' % (kv.get('F', '?'), compiles, unattr))
            tok = {k: v for k, v in tok.items() if not k.startswith(('self:row:', 'self:nested-row:'))}
        else:
            judged += 1
        widest = float(where.group(1)) if where else 0.0
        if compiles == 0:
            # (4a) the frame's unnamed time less its widest gap: no single slice of another context can enter it
            if unattr - widest > 0.1: fails.append('(4a) %s frame %s: %.4f ms of gaps no row names OUTSIDE the widest '
                                                   '(%.4f ms between %s) - more than one span: ours' % (
                part, kv.get('F', '?'), unattr - widest, widest, where.group(2) if where else '?'))
            widests.setdefault(part, []).append(widest)
            if widest > 0.1:
                spikes.append('%s frame %s: one gap %.4f ms between %s' % (part, kv.get('F', '?'), widest,
                                                                         where.group(2) if where else '?'))
        for name, ms in tok.items():
            if not name.startswith(('self:row:', 'self:nested-row:')):
                continue
            row = name[5:]
            if ('bubblelost:' + row) in tok:
                fails.append('(4) %s frame %s: %s - its bubble was not measured (the law cannot be judged)'
                             % (part, kv.get('F', '?'), row))
                continue
            bubble = float(tok.get('bubble:' + row, 0.0))
            own = float(ms) - bubble
            owns.setdefault(part, {}).setdefault(row, []).append(own)   # (4b): judged by its median below
            if own > 0.1:
                spikes.append('%s frame %s: %s left %.4f ms to no child (its own %.4f less its bubble %.4f)' % (
                    part, kv.get('F', '?'), row, own, float(ms), bubble))
            worst_own[part] = max(worst_own.get(part, 0.0), own)
        for name, ms in tok.items():
            if name.startswith('bubble:'):
                b = bubbles.setdefault(part, {})
                b[name[7:]] = max(b.get(name[7:], 0.0), float(ms))
            if name.startswith('bubblenosub:'):
                fails.append('(4b) %s frame %s: %s carries %.4f ms of bubble with no submission inside' % (
                    part, kv.get('F', '?'), name[12:], float(ms)))
        for name, ms in tok.items():
            if name.startswith('overrun:'):
                fails.append('(5) %s frame %s: the children of %s overrun it by %.4f ms' % (part, kv.get('F', '?'), name[8:], float(ms)))
        worst_self = max([float(v) for k, v in tok.items() if k.startswith(('self:row:', 'self:nested-row:'))] + [0.0])
        names['__worst_self_' + part] = max(names.get('__worst_self_' + part, 0.0), worst_self)
        names['__worst_unattr_' + part] = max(names.get('__worst_unattr_' + part, 0.0), unattr)
    print('%s: (4) judged on %d frames that create no pipeline; skipped %s' % (part, judged, skipped or 'none'))
    med = statistics.median(widests[part]) if widests.get(part) else 0.0
    if med > 0.1: fails.append('(4b) %s: the median widest gap %.4f ms over %d frames - an unnamed span of ours on most '
                               'frames' % (part, med, len(widests[part])))
    rmed = {r: statistics.median(v) for r, v in owns.get(part, {}).items()}
    for r, m in sorted(rmed.items()):
        if m > 0.1: fails.append('(4b) %s: %s leaves a median %.4f ms to no child over its %d frames (less its bubble)'
                                 % (part, r, m, len(owns[part][r])))
    print('%s: (4b) the median widest gap %.4f ms; the worst median own time %s' % (
        part, med, ('%.4f ms (%s)' % max((m, r) for r, m in rmed.items())) if rmed else 'none'))
    if judged == 0: fails.append('(4) %s: no frame creates no pipeline - the engine cache was cold, nothing to judge' % part)
    print('%s: the frame adds up - the worst |work + idle - span| %.4f ms; the worst unnamed gap %.4f ms, '
          'the worst row own time left to no child %.4f ms (%.4f ms less its measured bubble)' % (
              part, worst, names.get('__worst_unattr_' + part, 0.0), names.get('__worst_self_' + part, 0.0),
              worst_own.get(part, 0.0)))
    for row, b in sorted(bubbles.get(part, {}).items(), key=lambda kv: -kv[1]):
        print('%s:   measured bubble %-44s worst %.4f ms a frame' % (part, row, b))
want = [('the injection', r'vct\.inject\.'), ('the bounce feedback', r'vct\.feedback'),
        ('the cards\' capture', r'^pass:JahCardCapture'), ('the cards\' relight', r'cards\.relight$'),
        ('the gather\'s trace', r'gather\.trace$'), ('the gather\'s integrate', r'gather\.integrate$'),
        ('the field', r'ifd\.'), ('the reflection\'s trace', r'rq\.reflect\.trace$'),
        ('the reflection\'s resolve', r'rq\.reflect\.resolve$'), ('the area visibility\'s trace', r'area\.vis$'),
        ('the area visibility\'s resolve', r'area\.resolve$')]
for label, pat in want:
    hit = {k: v for k, v in names.items() if re.search(pat, k) and k.startswith(('pass:', 'nested:', 'row:', 'nested-row:'))}
    best = max(hit.values()) if hit else 0.0
    print('  %-30s %s' % (label, ('%d row(s), max %.4f ms' % (len(hit), best)) if hit else 'NONE'))
    if not hit or not best > 0.0: fails.append('(2) %s: no positive time (%s)' % (label, pat))
if spikes:
    print('  %d single span(s) past 0.1 ms ((4b): a slice of another context, or ours on a minority of frames)%s' % (
        len(spikes), ' - the first 12:' if len(spikes) > 12 else ':'))
for x in spikes[:12]: print('    ' + x)
for f in fails: print('FAIL: ' + f)
print('lab.monitor: %s' % ('FAIL' if fails else 'PASS'))
sys.exit(1 if fails else 0)
PY
