#!/bin/bash
# gi.low_gather (PHOTON-7 stage 7, 7-4; SPECS/PHOTON_7_SPEC.md stage 7: "LOW GAINS THE GATHER at stride 32 with 8 x 8 rays
# a probe") — LOW'S GATHER AGAINST MEDIUM'S ON THE COURT, WITHIN THEIR SAMPLING NOISE.
# jahlab on photon-court, 960x540, at the gi.walk court's start pose (0,3,18 looking down the court). THE GATHER ALONE IS
# COMPARED: the two World tiers differ in every post row and in their caches (Low's two cascades), so the comparison is
# made at ONE tier (Medium) between its own gather row (16 px, 6x6) and LOW'S ROW put on it by the test door's arms
# (gather.stride 32, gather.octRes 8) - each the converged rest picture at the rest sequence's seed 0 and then 1 (the arm
# gather.restSeed: the same estimator on other draws, its sampling noise). And the LOW TIER itself, once: it gathers,
# then a 30-frame walk down the court (the stillness door's live walk: the GPU rows of every moving frame), its gather's
# shape and memory (`status`); the Medium session's walk the same.
# THE LAWS (low_gather.py):
#   (1) LOW GATHERS: the Low tier's gather runs at a probe per 32 x 32 px with 64 rays a probe (the tier table's row);
#   (2) LOW'S ROW IS MEDIUM'S WITHIN THEIR SAMPLING NOISE: the mean |Low row - Medium row| over the lit pixels (8-bit
#       codes, max channel) is no larger than the two rows' own seed-to-seed means added;
#   (3) the comparison is not vacuous: most of the picture is lit, and every shot was at the gather's rest.
# Printed: the means, p99s and shares over one code; per tier the gather's GPU rows over the walk (p50 of their sum) and
# the frame's GPU busy (p50); the gather's VRAM.
# usage: low_gather.sh <jahlab> <work dir>
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WD"; mkdir -p "$WD/data"
session() {   # name, tier, the script's lines
    local n=$1 t=$2
    shift 2
    printf '%s\n' "$@" quit > "$WD/$n.lab"
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-court --tier "$t" \
        --script "$WD/$n.lab" > "$WD/$n.log" 2>&1
    local rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($n) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$n.log" | head -5; tail -3 "$WD/$n.log"; exit 1; }
    grep -E '^LAB (END|LOAD)' "$WD/$n.log" | cut -c1-160 | sed "s/^/  $n: /"
}
POSE="camera 0,3,18,0,3,-6"
WALK="stillness teleport:0,3,18,0,3,-6/hold:1/walk:0,3,3,0,3,-21:30 8 trail=30"
# (each shot after 300 frames: the court's GI rests at ~80 and the gather holds 17 later - gi.walk's measure - and a
# seed or a row switched restarts the gather's history)
session low low "$POSE" "frames 300" "status" "$WALK out=low" "status"
session medium medium "$POSE" "frames 300" "status" "shot medium-0.png" "arm gather.restSeed 1" "frames 300" "status" \
    "shot medium-1.png" "arm gather.restSeed 0" "arm gather.stride 32" "arm gather.octRes 8" "frames 300" "status" \
    "shot lowrow-0.png" "arm gather.restSeed 1" "frames 300" "status" "shot lowrow-1.png" "arm gather.restSeed 0" \
    "arm gather.stride 0" "arm gather.octRes 0" "frames 30" "$WALK out=medium" "status"
python3 -I "$HERE/low_gather.py" "$WD"
rc=$?
echo "gi.low_gather: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
