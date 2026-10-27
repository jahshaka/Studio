#!/bin/bash
# THE ROUGH SPECULAR'S TWO LAWS (PHOTON-7 stage 7, 7-3; the lead's ruling: the split sum on the probes, B + history).
# jahlab on photon-rough (its four rough metal strips, 0.45 - 0.75), Epic, 960x540, the walk's start pose 0,1.6,2.4 ->
# 0,0,-3, the reflection's cutoff 0 (no traced reflection: the strips' environment term is the probes' lobe whole), the
# strips' masks from the stillness door's geometry dump (the class map and the view distance at the pose).
#   --quadrature  gi.rough_lobe_quadrature - THE SPLIT SUM KEEPS THE ENERGY: the shipped read (the prefiltered levels
#                 along the dominant direction) against the BRUTE-FORCE quadrature of THE SAME probe map (the door
#                 gather.roughQuadrature: 1 the shipped read with the filtered map kept, 2 every texel of every tap
#                 weighed by the GGX BRDF at the pixel's own view), one frame each on the same rays (gather.freezeFrame:
#                 the sample sequence held; gather.temporal 0: this frame's lobe, no history between the two). An
#                 approximation of the lobe may BLUR, never add or remove light: THE LAW is the rough class's mean
#                 SIGNED error within one code (8-bit codes, the channels' mean). The per-pixel error is PRINTED (mean,
#                 p99, per strip; about 3 codes at the ruling, 7-3) and is the split sum's inherent own, from three
#                 sources: the n = v = r isotropic lobe against the true view-stretched one (wide lobes, grazing
#                 views), the lerp between levels 0.2 apart, and the clamp at the last level (0.7: the 0.75 strip). A
#                 level weighting or a normalisation error shifts the mean - the law fails.
#   --reference   gi.rough_reference - THE LOBE AGAINST THE TRUTH (re-pointed in 10-3, R12): the held picture against
#                 the traced reflection's reference (cutoff 100, the rays alone, reflect.restReference: 4,096 raw GGX
#                 samples a texel at rest, composited unfiltered). THE LAW: the rough class's and every strip's mean SIGNED
#                 error (the energy, the channels' mean) within the reference's SE of the mean + 1 code (the SE from the
#                 reference's own pixel noise: a pixel against its four neighbours' mean); the per-pixel error printed
#                 (the split sum's blur and the probe map's resolution). DECLARED RED (2026-10-27): +5.27 codes (every
#                 strip +4.9 to +5.9; the plain floor +0.9) - its cause, by the estimators' arithmetic (an exact
#                 reference not yet run), the REFERENCE's weighting: its VNDF samples are
#                 averaged unweighted (jah_reflect_draw.glsl: "the estimator weight F G2 / G1 - NEITHER applied") and the
#                 mean multiplied by the split DFG, where the lobe and its brute-force quadrature weigh the probe map by
#                 D G2; a grazing view's VNDF mean leans to the darker horizon. With reflect.nee 0 the reference reads
#                 1.5 codes brighter on the strips than with NEE on (the emitter table's MIS against the BRDF-only).
# usage: rough_spec.sh <jahlab> <work dir> --quadrature|--reference
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; MODE=${3:?--quadrature|--reference}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WD"; mkdir -p "$WD/data"
P="0,1.6,2.4,0,0,-3"; E="0,1.6,0.4,0,0,-3"
session() {   # name - the script is $WD/<name>.lab
    "$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env photon-rough --tier epic \
        --script "$WD/$1.lab" > "$WD/$1.log" 2>&1
    local rc=$?
    [ $rc -eq 0 ] || { echo "FAIL: jahlab ($1) exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/$1.log" | head -5; tail -3 "$WD/$1.log"; exit 1; }
    grep -E '^LAB (END|LOAD)' "$WD/$1.log" | cut -c1-160 | sed "s/^/  $1: /"
}
# the pose's geometry (a cut onto it, one frame dumped), then the held picture
GEO="camera $E
frames 30
stillness teleport:$P/hold:1 30 age=1 trail=1 dump=geo out=geo"
if [ "$MODE" = "--quadrature" ]; then
    cat > "$WD/quad.lab" <<EOF
row reflectionRoughnessCutoff 0
$GEO
arm gather.roughQuadrature 1
arm gather.freezeFrame 1
arm gather.temporal 0
frames 200
shot shipped.png
arm gather.roughQuadrature 2
frames 3
shot brute.png
quit
EOF
    session quad
    python3 -I "$HERE/rough_spec.py" "$WD" quadrature
elif [ "$MODE" = "--reference" ]; then
    cat > "$WD/lobe.lab" <<EOF
row reflectionRoughnessCutoff 0
$GEO
frames 200
shot lobe.png
quit
EOF
    cat > "$WD/ref.lab" <<EOF
row reflectionRoughnessCutoff 100
reflections rays
arm reflect.restReference 1
camera $P
frames 4300
shot ref.png
quit
EOF
    session lobe
    session ref
    python3 -I "$HERE/rough_spec.py" "$WD" reference
else
    echo "FAIL: mode $MODE"; exit 1
fi
rc=$?
rm -rf "$WD/data/stillness/dump-"*
echo "rough_spec $MODE: $([ $rc -eq 0 ] && echo PASS || echo FAIL)"
exit $rc
