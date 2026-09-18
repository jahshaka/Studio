#!/usr/bin/env bash
#
# app.engine_selftest_validation — the `--engine-selftest` binary path, twice:
#
#   1. THE SELF-TEST, under the Khronos validation layer (the environment ctest
#      sets). Fails on the process's exit code AND on any "Validation Error" in
#      its output — the line that caught the stale depth buffer on resize. It
#      renders TWO POSES since 2026-09-18 (ENGINE-SMALL-B item 5) and prints a
#      sha256 line for each; this arm requires both lines and requires them to
#      DIFFER, which is what says the second pose's camera move took. The VALUES
#      are not asserted here — they are the hand-taken A/B of CLAUDE.md's hash
#      law, and a suite that pinned them would red on every deliberate pixel
#      change in the engine — but they are printed into the gate's log, so a
#      gate run carries both numbers.
#   2. THE BROKEN-GROUND ARM (smoke L10 item 3). On 2026-09-11 the default
#      scene's ground failed to load (`model :/models/ground.obj: error parsing
#      file` in the log) and the self-test rendered a groundless scene and
#      exited 0: its only pixel assertion was "the centre is not the clear
#      colour", which the SKY satisfies on its own. The self-test now asserts
#      the default scene's ground node carries real geometry before it renders,
#      and this arm proves the assertion bites: JAHSHAKA_SELFTEST_BREAK_GROUND=1
#      (read ONLY by the self-test runner) re-points the ground at a resource
#      that does not exist — the state a parse failure leaves — and the run
#      must exit 1 naming the ground. It fails before the frame pump, so the
#      arm costs one boot.
#
# usage: engine_selftest.sh <jahshaka-binary> <out-dir>
# cwd is the binary's directory (ctest sets it: media/ and the relative ogre
# log resolve the way a developer's own launch resolves them).
set -u

BIN="$1"
OUT="$2"

"$BIN" --engine-selftest "$OUT/selftest_validation.png" > "$OUT/validation.log" 2>&1
rc=$?
if grep -q 'Validation Error' "$OUT/validation.log"; then
    grep -m3 'Validation Error' "$OUT/validation.log"
    exit 1
fi
if [ "$rc" -ne 0 ]; then
    echo "engine_selftest: the self-test exited $rc"
    tail -20 "$OUT/validation.log"
    exit "$rc"
fi
grep -m1 'engine-selftest: default scene' "$OUT/validation.log"

# THE TWO POSES (ENGINE-SMALL-B item 5). Both lines must be there, and the two
# hashes must differ: a viewport that accepted the second pose and ignored it
# would print the same number twice, and the second observation would be a gate
# on nothing.
pose1=$(sed -n 's/^engine-selftest: pose 1 sha256 \([0-9a-f]*\) .*/\1/p' "$OUT/validation.log" | head -1)
pose2=$(sed -n 's/^engine-selftest: pose 2 sha256 \([0-9a-f]*\) .*/\1/p' "$OUT/validation.log" | head -1)
if [ -z "$pose1" ] || [ -z "$pose2" ]; then
    echo "engine_selftest: the self-test did not report both poses' hashes (pose1='$pose1' pose2='$pose2')"
    grep 'engine-selftest' "$OUT/validation.log" | tail -8
    exit 1
fi
if [ "$pose1" = "$pose2" ]; then
    echo "engine_selftest: the two poses hash identically ($pose1) — the camera move did not take"
    exit 1
fi
echo "engine_selftest: pose 1 $pose1"
echo "engine_selftest: pose 2 $pose2"

JAHSHAKA_SELFTEST_BREAK_GROUND=1 \
    "$BIN" --engine-selftest "$OUT/selftest_broken_ground.png" > "$OUT/broken_ground.log" 2>&1
brc=$?
if [ "$brc" -ne 1 ] || ! grep -q "engine-selftest: the default scene's ground" "$OUT/broken_ground.log"; then
    echo "engine_selftest: a GROUNDLESS default scene exited $brc (expected 1 with the ground named)"
    grep 'engine-selftest' "$OUT/broken_ground.log" | tail -5
    exit 1
fi
grep -m1 "engine-selftest: the default scene's ground" "$OUT/broken_ground.log"
echo "engine_selftest: both arms passed"
exit 0
