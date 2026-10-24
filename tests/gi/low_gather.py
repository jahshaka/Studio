"""gi.low_gather's analysis (PHOTON-7 stage 7, 7-4; low_gather.sh says the laws).
usage: low_gather.py <work dir>   (its data/shots/{medium,lowrow}-{0,1}.png, data/stillness/{low,medium}.tsv, {low,medium}.log)
"""
import csv, json, os, sys
import numpy as np
from PIL import Image

wd = sys.argv[1]
shots = os.path.join(wd, 'data', 'shots')
fails = 0
def check(ok, msg):
    global fails
    print(('  ok   ' if ok else '  FAIL ') + msg)
    if not ok: fails += 1

def img(name):
    return np.asarray(Image.open(os.path.join(shots, name)).convert('RGB')).astype(np.int32)

def statuses(tier):
    out = []
    for line in open(os.path.join(wd, tier + '.log')):
        if line.startswith('LAB STATUS '):
            out.append(json.loads(line[len('LAB STATUS '):]))
    return out

def gpu(tier):
    rows = list(csv.reader(open(os.path.join(wd, 'data', 'stillness', tier + '.tsv')), delimiter='\t'))
    h = rows[0]
    gather, busy = [], []
    for r in rows[1:]:
        d = dict(zip(h, r))
        if d.get('window') != 'walk-move': continue
        try:
            busy.append(float(d['frame_gpu_ms']))
        except ValueError:
            continue
        g = 0.0
        if d['gpu_rows'] not in ('', '-'):
            for kv in d['gpu_rows'].split(','):
                k, _, v = kv.partition(':')
                if k.startswith('gather.'): g += float(v)
        gather.append(g)
    return (float(np.median(gather)) if gather else -1.0, float(np.median(busy)) if busy else -1.0, len(busy))

L0, L1, M0, M1 = img('lowrow-0.png'), img('lowrow-1.png'), img('medium-0.png'), img('medium-1.png')
def dmax(a, b): return np.abs(a - b).max(axis=2)
lit = (L0.max(axis=2) > 8) & (M0.max(axis=2) > 8)
dLM, nL, nM = dmax(L0, M0)[lit], dmax(L0, L1)[lit], dmax(M0, M1)[lit]
def line(name, d):
    return '%s mean %.3f  p99 %.1f  >1 code %.2f %%' % (name, d.mean(), np.percentile(d, 99), 100.0 * (d > 1).mean())
print('   (at Medium: its own gather row against Low\'s, 32 px / 8x8, by the arms)')
print('   ' + line('Low row - Medium row', dLM))
print('   ' + line('Low row seed 0 - 1  ', nL))
print('   ' + line('Medium row seed 0 - 1', nM))
st = {t: statuses(t) for t in ('low', 'medium')}
for t in ('low', 'medium'):
    g = st[t][-1]['gi']['gather']
    gm, bm, n = gpu(t)
    print('   %-6s gather stride %d, %d rays a probe, %d probes, %.1f MB VRAM (history %.1f MB); over the %d-frame walk '
          'the gather GPU p50 %.2f ms, the frame GPU busy p50 %.2f ms' %
          (t, g['stride'], g['raysPerProbe'], g['probes'], g['vramBytes'] / 1e6, g['historyBytes'] / 1e6, n, gm, bm))
gl = st['low'][-1]['gi']['gather']
check(gl['running'] and gl['stride'] == 32 and gl['raysPerProbe'] == 64,
      '(1) LOW GATHERS: a probe per 32 x 32 px, 64 rays a probe')
check(dLM.mean() <= nL.mean() + nM.mean(),
      '(2) LOW\'S ROW IS MEDIUM\'S WITHIN THEIR SAMPLING NOISE: mean |Low row - Medium row| %.3f <= %.3f (the two seed '
      'noises added)' % (dLM.mean(), nL.mean() + nM.mean()))
rested = all(s['gi']['gather']['restFrames'] > 0 for s in st['medium'][:4])
check(lit.mean() > 0.5 and rested,
      '(3) not vacuous: %.0f %% of the picture lit, every shot at the gather\'s rest' % (100.0 * lit.mean()))
sys.exit(1 if fails else 0)
