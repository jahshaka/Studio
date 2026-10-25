#!/bin/bash
# lab.no_self_drain (PHOTON-7's walk hitch) — A MID-FRAME SUBMISSION NEVER MAKES THE CPU WAIT THE FRAME'S OWN WORK.
# THE BAR (the lead's ruling on the hitch step's 1c, 2026-10-25): NO DRAIN - the queue's self-drains, a fact, are 0 on every
# frame - and, on the Bistro walks, the hitch law on the WALL frame (its peak at most 2x the walk's median). A submission
# inside a row by itself is no cost (the GPU waiting on a CPU-bound frame after a submit; upstream's staging "flush for
# accuracy" is kept): this row prints the submissions inside the GI plan and the card tables, and asserts the drains.
# The Bistro walks' 60-90 ms peaks were the upload staging's fence flushes (every quarter of its 4 MB ring uploaded:
# the card tables, the field's probe records) submitting the graphics queue mid-frame, and the queue then parking the
# flush's fence on the frame slot being RECORDED, so the next command buffer (and any dynamic map) waited it: the CPU
# blocked on everything submitted so far, the previous frame's GPU work included (measured 69 / 86 self-drains a walk,
# up to 42.6 ms a frame). The fork's VulkanQueue now holds a FlushOnly's fence apart until the frame's end submission.
# ...and the tables upload their CHANGED rows only (DeltaUpload: the card tables, the field's probe records), so the
# walk's uploads stay far under the ring's quarter and no fence flush falls inside the GI plan or the card tables.
# jahlab on photon-glass at 960x540 (Epic), its boot (the first capture bursts) and a walk across the room (field
# follows, captures), under `monitor`: LAWS, per frame (facts the monitor and the fork's counters record, never timings) -
#   (1) the walk crossed a field follow (why:ifd.follow) and a card capture (why:cards.capture);
#   (2) NO SELF-DRAIN: qdrain 0 on every frame (the queue's counter; the fenced flushes it survived, and the submissions
#       inside gi.plan / cards.tables(.pass), are printed - 54 of them before the changed-rows uploads, 0 after).
# usage: no_self_drain.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
cat > "$WD/w.lab" <<S
camera 0,1.8,3.4,0,0.5,-3
monitor 40 $WD/boot.txt
monitor 80 $WD/walk.txt 0,1.6,-3.0,0,0.5,-6
quit
S
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-glass --script "$WD/w.lab" > "$WD/w.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: jahlab exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/w.log" | head; tail -5 "$WD/w.log"; exit 1; }
python3 -I - "$WD" <<'PY'
import os, re, sys
wd = sys.argv[1]
fails, seen = [], {'qfenced': 0, 'follow': 0, 'capture': 0}
drains, subs = [], []
for part in ('boot', 'walk'):
    p = os.path.join(wd, part + '.txt')
    if not os.path.exists(p):
        fails.append('%s: no monitor file' % part); continue
    n = 0
    for l in open(p):
        if not l.startswith('F '): continue
        n += 1
        head, _, rest = l.partition('|')
        kv = dict(re.findall(r'(\w+) (-?[\d.]+|nan)', head))
        if 'qdrain' not in kv:
            fails.append('%s: the frame line carries no queue counts (qdrain)' % part); break
        seen['qfenced'] += int(kv['qfenced'])
        if part == 'walk':
            seen['follow'] += 1 if 'why:ifd.follow:' in rest else 0
            seen['capture'] += 1 if 'why:cards.capture:' in rest else 0
        for name in re.findall(r' submitted:(?:nested-)?row:(gi\.plan|cards\.tables|cards\.tables\.pass)=', rest):
            subs.append((part, kv['F'], name))
        if int(kv['qdrain']):
            drains.append((part, kv['F'], int(kv['qdrain']), float(kv['qdrainms'])))
    print('%s: %d frames' % (part, n))
print('fenced mid-frame flushes %d; walk frames with a field follow %d, with a capture %d; submissions inside the plan or '
      'the tables %d; self-drains %d (%.2f ms)' % (seen['qfenced'], seen['follow'], seen['capture'], len(subs),
                                                   sum(d[2] for d in drains), sum(d[3] for d in drains)))
if not seen['follow']: fails.append('(1) no field follow in the walk')
if not seen['capture']: fails.append('(1) no card capture in the walk')
for d in drains[:12]:
    fails.append('(2) %s frame %s: %d self-drain(s), %.2f ms the CPU waited the frame\'s own work' % d)
for f in fails: print('FAIL:', f)
print('lab.no_self_drain:', 'FAIL' if fails else 'PASS')
sys.exit(1 if fails else 0)
PY
