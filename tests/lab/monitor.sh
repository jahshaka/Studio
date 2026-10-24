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
#       within 2 % (or 0.01 ms) - a row inside a pass is in that pass's time, never counted twice.
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
        for name, ms in re.findall(r' (\S+)=(-?[\d.]+)', l.split('|', 1)[1] if '|' in l else ''):
            names[name] = max(names.get(name, 0.0), float(ms))
    print('%s: the frame adds up - the worst |passes + rows + gaps - busy| / busy %.3f %%' % (part, 100 * worst))
want = [('the injection', r'vct\.inject\.'), ('the bounce feedback', r'vct\.feedback'),
        ('the cards\' capture', r'^pass:JahCardCapture'), ('the cards\' relight', r'cards\.relight$'),
        ('the gather\'s trace', r'gather\.trace$'), ('the gather\'s integrate', r'gather\.integrate$'),
        ('the field', r'ifd\.'), ('the reflection\'s trace', r'rq\.reflect\.trace$'),
        ('the reflection\'s resolve', r'rq\.reflect\.resolve$'), ('the area visibility\'s trace', r'area\.vis$'),
        ('the area visibility\'s resolve', r'area\.resolve$')]
for label, pat in want:
    hit = {k: v for k, v in names.items() if re.search(pat, k)}
    best = max(hit.values()) if hit else 0.0
    print('  %-30s %s' % (label, ('%d row(s), max %.4f ms' % (len(hit), best)) if hit else 'NONE'))
    if not hit or not best > 0.0: fails.append('(2) %s: no positive time (%s)' % (label, pat))
for f in fails: print('FAIL: ' + f)
print('lab.monitor: %s' % ('FAIL' if fails else 'PASS'))
sys.exit(1 if fails else 0)
PY
