#!/usr/bin/env bash
#
# app.engine_selftest_validation — the `--engine-selftest` binary path, twice:
#
#   1. THE SELF-TEST, under the Khronos validation layer (the environment ctest
#      sets). Fails on the process's exit code AND on any "Validation Error" in
#      its output — the line that caught the stale depth buffer on resize — AND
#      unless the runner's validation line says the layer is live (H4). It
#      renders SIX POSES since TESTING-CLEANUP-2 H5 (four since lane FENCE-1,
#      two since ENGINE-SMALL-B item 5) and prints a sha256 line for each; this
#      arm requires all six lines and requires each PAIR to DIFFER, which is what
#      says the second pose's camera move took and that the ray tier moved a
#      pixel of a fixture built to show it. The VALUES are not asserted here —
#      they are the hand-taken A/B of the hash law (docs/TESTING_GATE.md §7), and
#      a suite that pinned them would red on every deliberate pixel change in the
#      engine — but they are printed into the gate's log, so a gate run carries
#      all four numbers.
#
#      POSE PAIR B is a purpose-built fixture built in the same process through
#      the SCRIPTING VERBS (a glossy floor, a cascade crossing, an emissive above
#      the voxel store's clip, a mirror pillar) at the VIEWPORT screenshot grade,
#      which is the only grade that carries the SSR prepass the ray tier rides.
#      B1 == B2 is LEGAL on a machine with no ray query — the runner says so and
#      exits 0 — so this arm asserts the inequality only when the runner did not
#      print that sentence.
#   2. THE BROKEN-GROUND ARM (smoke L10 item 3). A default scene whose floor
#      fails to load renders a groundless scene that still exits 0 on a pixel
#      check alone ("the centre is not the clear colour" — the SKY satisfies it).
#      The self-test asserts the Basic template's Floor (found by its
#      defaultFloor flag) carries real geometry before it renders, and this arm
#      proves the assertion bites: JAHSHAKA_SELFTEST_BREAK_GROUND=1 (read ONLY
#      by the self-test runner) drops the floor's mesh — the state a failed
#      bake read leaves — and the run must exit 1 naming the ground. It fails before the frame pump, so the
#      arm costs one boot.
#
# ONE ROW PER PROOF (the lead's ruling, 2026-10-28: the combined row was a cap item at 54-60 s, the validation layer's
# own cost on six poses): app.engine_selftest_validation.default runs the default scene's half (pose 1, pose 2, motion
# - and the broken-ground arm), .fixture pose pair B and the mover (JAHSHAKA_SELFTEST_POSES, selftestrunner.cpp).
# THE FOUR ARE FOUR (a default-scene pose never hashes as a fixture-B pose) is a property of the FULL SET: it lives
# where all six run, the six-hash step (no half here can see it).
# usage: engine_selftest.sh <jahshaka-binary> <out-dir> default|fixture
# cwd is the binary's directory (ctest sets it: media/ and the relative ogre
# log resolve the way a developer's own launch resolves them).
set -u

BIN="$1"
OUT="$2"
HALF="${3:?default|fixture}"
case "$HALF" in default|fixture) ;; *) echo "engine_selftest: the half '$HALF' (default|fixture)"; exit 1 ;; esac
LOG="$OUT/validation-$HALF.log"

JAHSHAKA_SELFTEST_POSES="$HALF" "$BIN" --engine-selftest "$OUT/selftest_validation_$HALF.png" > "$LOG" 2>&1
rc=$?
if grep -q 'Validation Error' "$LOG"; then
    grep -m3 'Validation Error' "$LOG"
    exit 1
fi
if [ "$rc" -ne 0 ]; then
    echo "engine_selftest: the self-test exited $rc"
    tail -20 "$LOG"
    exit "$rc"
fi
grep -m1 'engine-selftest: default scene' "$LOG"

# THE LAYER WAS REALLY THERE (TESTING-CLEANUP-2 H4). Everything above greps for
# "Validation Error", which a run whose layer never loaded cannot print: the
# runner's validation line must say the layer was asked for AND is live on the
# device (Engine::validation: vkCmdDraw resolves into the layer's library).
vline=$(grep -m1 '^engine-selftest: validation requested' "$LOG")
echo "${vline:-engine_selftest: (no validation line)}"
case "$vline" in
    "engine-selftest: validation requested yes active yes"*) ;;
    *) echo "engine_selftest: the run was not under a live validation layer — it proves nothing"
       exit 1 ;;
esac

if [ "$HALF" = default ]; then
# THE TWO POSES (ENGINE-SMALL-B item 5). Both lines must be there, and the two
# hashes must differ: a viewport that accepted the second pose and ignored it
# would print the same number twice, and the second observation would be a gate
# on nothing.
pose1=$(sed -n 's/^engine-selftest: pose 1 sha256 \([0-9a-f]*\) .*/\1/p' "$LOG" | head -1)
pose2=$(sed -n 's/^engine-selftest: pose 2 sha256 \([0-9a-f]*\) .*/\1/p' "$LOG" | head -1)
if [ -z "$pose1" ] || [ -z "$pose2" ]; then
    echo "engine_selftest: the self-test did not report both poses' hashes (pose1='$pose1' pose2='$pose2')"
    grep 'engine-selftest' "$LOG" | tail -8
    exit 1
fi
if [ "$pose1" = "$pose2" ]; then
    echo "engine_selftest: the two poses hash identically ($pose1) — the camera move did not take"
    exit 1
fi
echo "engine_selftest: pose 1 $pose1"
echo "engine_selftest: pose 2 $pose2"

motion=$(sed -n 's/^engine-selftest: pose motion sha256 \([0-9a-f]*\) .*/\1/p' "$LOG" | head -1)
if [ -z "$motion" ]; then
    echo "engine_selftest: the self-test did not report the motion pose"
    grep 'engine-selftest' "$LOG" | tail -10
    exit 1
fi
if [ "$motion" = "$pose2" ]; then
    echo "engine_selftest: the pose in motion hashes as the still one (motion=$motion pose2=$pose2)"
    exit 1
fi
echo "engine_selftest: pose motion       $motion"
fi
if [ "$HALF" = fixture ]; then
# ---- POSE PAIR B (lane FENCE-1) -------------------------------------------
b1=$(sed -n 's/^engine-selftest: pose B1 (rays) sha256 \([0-9a-f]*\) .*/\1/p' "$LOG" | head -1)
b2=$(sed -n 's/^engine-selftest: pose B2 (no rays) sha256 \([0-9a-f]*\) .*/\1/p' "$LOG" | head -1)
if [ -z "$b1" ] || [ -z "$b2" ]; then
    echo "engine_selftest: the self-test did not report pose pair B's hashes (B1='$b1' B2='$b2')"
    grep 'engine-selftest' "$LOG" | tail -10
    exit 1
fi
echo "engine_selftest: pose B1 (rays)    $b1"
echo "engine_selftest: pose B2 (no rays) $b2"
grep -m1 'the rays moved' "$LOG" || true
if [ "$b1" = "$b2" ]; then
    # The runner itself decides whether that is legal: it asks the ENGINE whether
    # this machine has ray queries, which is the only authority (the process latch
    # --no-ray-query keeps the extensions off the device entirely, so a run under
    # it IS a machine without the hardware). A host-side guess here would be a
    # second source of truth.
    if grep -q 'this machine has no ray query' "$LOG"; then
        echo "engine_selftest: B1 == B2 and this machine has no ray query — the correct answer"
    else
        echo "engine_selftest: pose B1 and B2 hash identically ($b1) on a machine WITH ray queries"
        exit 1
    fi
fi

mover=$(sed -n 's/^engine-selftest: pose mover sha256 \([0-9a-f]*\) .*/\1/p' "$LOG" | head -1)
if [ -z "$mover" ]; then
    echo "engine_selftest: the self-test did not report the mover pose"
    grep 'engine-selftest' "$LOG" | tail -10
    exit 1
fi
if [ "$mover" = "$b1" ] || [ "$mover" = "$b2" ]; then
    echo "engine_selftest: the pose in motion hashes as a still one (mover=$mover B1=$b1 B2=$b2)"
    exit 1
fi
echo "engine_selftest: pose mover        $mover"
echo "engine_selftest: the fixture half passed"
exit 0
fi
# (OFF THE LAYER, PHOTON-7 stage 10: the arm proves the ground assertion bites, before the frame pump - the API guard is
# arm 1's; under the layer this boot cost the row its margin under the 60 s)
env -u VK_INSTANCE_LAYERS JAHSHAKA_SELFTEST_BREAK_GROUND=1 \
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
