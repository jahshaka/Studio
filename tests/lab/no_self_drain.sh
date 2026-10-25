#!/bin/bash
# lab.no_self_drain (PHOTON-7's walk hitch) — A MID-FRAME SUBMISSION NEVER MAKES THE CPU WAIT THE FRAME'S OWN WORK.
# The Bistro walks' 60-90 ms peaks were the upload staging's fence flushes (every quarter of its 4 MB ring uploaded:
# the card tables, the field's probe records) submitting the graphics queue mid-frame, and the queue then parking the
# flush's fence on the frame slot being RECORDED, so the next command buffer (and any dynamic map) waited it: the CPU
# blocked on everything submitted so far, the previous frame's GPU work included (measured 69 / 86 self-drains a walk,
# up to 42.6 ms a frame). The fork's VulkanQueue now holds a FlushOnly's fence apart until the frame's end submission.
# jahlab on photon-glass at 960x540 (Epic), its boot (the first capture bursts) and a walk across the room (field
# follows, captures), under `monitor`: LAWS, per frame from the fork's counters (JahQueueCounters) -
#   (1) the run exercised it: a fenced mid-frame flush (qfenced) on some frame, a field follow (why:ifd.follow) and a
#       card capture (why:cards.capture) in the walk;
#   (2) NO SELF-DRAIN: qdrain 0 on every frame (a fact the queue counts, never a timing).
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
drains = []
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
        if int(kv['qdrain']):
            drains.append((part, kv['F'], int(kv['qdrain']), float(kv['qdrainms'])))
    print('%s: %d frames' % (part, n))
print('fenced mid-frame flushes %d; walk frames with a field follow %d, with a capture %d; self-drains %d (%.2f ms)' % (
    seen['qfenced'], seen['follow'], seen['capture'], sum(d[2] for d in drains), sum(d[3] for d in drains)))
if not seen['qfenced']: fails.append('(1) no fenced mid-frame flush: the run did not exercise the law')
if not seen['follow']: fails.append('(1) no field follow in the walk')
if not seen['capture']: fails.append('(1) no card capture in the walk')
for d in drains[:12]:
    fails.append('(2) %s frame %s: %d self-drain(s), %.2f ms the CPU waited the frame\'s own work' % d)
for f in fails: print('FAIL:', f)
print('lab.no_self_drain:', 'FAIL' if fails else 'PASS')
sys.exit(1 if fails else 0)
PY
