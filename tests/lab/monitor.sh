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
#   (3) THE FRAME ADDS UP: on every frame the top-level passes and rows and the unnamed gaps sum to the busy GPU span
#       within 2 % (or 0.01 ms) - a row inside a pass is in that pass's time, never counted twice;
#   (4) NOTHING UNNAMED (PHOTON-7's naming step): on every frame the gaps no row names (unattr) are at most 0.1 ms, and
#       every GPU row that encloses others (gi.step) leaves at most 0.1 ms of its own time to no child LESS ITS MEASURED
#       BUBBLE (self: - bubble:; urgent #64: a submission inside it is no longer an exemption - the bubble is the fork's
#       own timestamp pair at each flush, the flushed buffer's last BOTTOM_OF_PIPE to the next one's first TOP_OF_PIPE,
#       never a CPU-side estimate; a row whose bubble could not be measured, bubblelost:, is red) - a dispatch through
#       the Photon door directly inside gi.step files its own row (photonDispatch);
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
import os, re, sys
wd = sys.argv[1]
fails = []
names = {}
bubbles = {}     # part -> row -> the worst own-stretch bubble measured on a frame
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
    for l in F:
        kv = dict(re.findall(r'(\w+) (-?[\d.]+(?:e-?\d+)?|nan)', l.split('|')[0]))
        busy, passes, rows, unattr = (float(kv[k]) for k in ('busy', 'passes', 'rows', 'unattr'))
        err = abs(passes + rows + unattr - busy)
        worst = max(worst, err / max(busy, 1e-9))
        if err > max(0.02 * busy, 0.01): fails.append('(3) %s frame %s: passes %.4f + rows %.4f + gaps %.4f vs busy %.4f'
                                                      % (part, kv['F'] if 'F' in kv else '?', passes, rows, unattr, busy))
        tok = dict(re.findall(r' (\S+)=(-?[\d.eE+-]+)', l.split('|', 1)[1] if '|' in l else ''))
        for name, ms in tok.items():
            names[name] = max(names.get(name, 0.0), float(ms))
        where = re.search(r' gap (-?[\d.]+) (\S+) \|', l)
        if unattr > 0.1: fails.append('(4) %s frame %s: %.4f ms of gaps no row names (the widest %s ms between %s)' % (
            part, kv.get('F', '?'), unattr, where.group(1) if where else '?', where.group(2) if where else '?'))
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
            if own > 0.1:
                fails.append('(4) %s frame %s: %s leaves %.4f ms to no child (its own time %.4f less its measured '
                             'bubble %.4f)' % (part, kv.get('F', '?'), row, own, float(ms), bubble))
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
    print('%s: the frame adds up - the worst |passes + rows + gaps - busy| / busy %.3f %%; the worst unnamed gap %.4f ms, '
          'the worst row own time left to no child %.4f ms (%.4f ms less its measured bubble)' % (
              part, 100 * worst, names.get('__worst_unattr_' + part, 0.0), names.get('__worst_self_' + part, 0.0),
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
for f in fails: print('FAIL: ' + f)
print('lab.monitor: %s' % ('FAIL' if fails else 'PASS'))
sys.exit(1 if fails else 0)
PY
