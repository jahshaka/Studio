#!/bin/sh
# gi.feedback_closed_box.lamp.up's and .room's wrapper (BISTRO-REST-1): the fixture's laws, then THE PICTURE's per-frame change after the
# interior lamp step judged by the one rise law (tests/support/rise_law.py - gi.hold_after_stop (f)'s), the quantum one
# pixel-code over the picture's channels averaged (64 x 64 x 3; the room part 960 x 540 x 3).
# usage: feedback_lamp.sh <test_gi_feedback_closed_box> <work dir> [part: lamp.up (default) | room] [the room's ingredients]
set -u
BIN=${1:?the fixture}; WD=${2:?work dir}; PART=${3:-lamp.up}; ING=${4:-}
rm -rf "$WD"; mkdir -p "$WD"
"$BIN" "$PART" "$WD" $ING
rc=$?
HERE=$(cd "$(dirname "$0")" && pwd)
PX=$([ "$PART" = room ] && echo $((960 * 540 * 3)) || echo $((64 * 64 * 3)))
python3 -I - "$HERE/../support" "$WD/lamp_picture.txt" "$PX" <<'PY' || rc=1
import sys
sys.path.insert(0, sys.argv[1])
import rise_law
fails = list(rise_law.selfcheck())
m = [float(x) for x in open(sys.argv[2]).read().split()]
# from the step's own arrival (the series' peak) to its end: the change non-increasing
peak = max(range(len(m)), key=lambda i: m[i]) if m else 0
spans, means, sigma, rises, dips = rise_law.judge(m, peak + 1, len(m), 1.0 / int(sys.argv[3]))
# (a one-frame dip is printed, never a failure - rise_law.judge's contract, URGENT #138)
if dips: print("   (ii) a one-frame DIP after the lamp step (the change fell and came back; not a rise): %s"
               % " ".join("frame %d %.1f sigma" % d for d in dips))
print("   (ii) the picture after the lamp step: %d frames, the peak at %d, the rise law's noise %.5f, rises %d %s"
      % (len(m), peak, sigma, len(rises), rises[:4]))
if rises: fails.append("(ii) the picture's change non-increasing after the lamp step (the rise law): %s" % rises[:4])
for f in fails: print("  FAIL " + f)
sys.exit(1 if fails else 0)
PY
echo "gi.feedback_closed_box.$PART (the picture): $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
