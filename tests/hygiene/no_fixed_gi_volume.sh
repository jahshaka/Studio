#!/usr/bin/env bash
#
# source.no_fixed_gi_volume — THE SINGLE SCENE-FITTED VOXEL VOLUME STAYS DELETED
# (D4-PHOTON-TIERS; the owner's law: no fixed GI volume, no "room" in any lighting
# definition). The voxels are always the camera's cascade chain. What this holds:
#
#   * NO READER OR WRITER of the deleted document keys — `giCascades` (the scene's
#     switch) and `giBoundsExcluded` (the per-node flag, renamed probeGridExcluded
#     and no longer read under its old name: forward-building, no old key is read);
#   * NO ENGINE HALF of the arm — GiParams::cascades, refreshVctFast, buildVoxelArm,
#     giEscapeSignature, the single volume's resolution lever, kAutoGiBoundsMax;
#   * and none of them anywhere a suite could quietly exercise them again (a
#     script asserting their ABSENCE — `=== undefined`, `!byId[...]` — is allowed).
#
# The probe grid's PLACEMENT region (computeProbeRegion / kProbeGridFitMax /
# probeGridExcluded) is a different thing and is not covered: it places
# reflection probes where rays do not trace reflections, pending A9.
#
# $1 = the repo root
set -u
ROOT="${1:?usage: no_fixed_gi_volume.sh <source-root>}"
cd "$ROOT" || { echo "source.no_fixed_gi_volume: no such root $ROOT"; exit 1; }

PATTERN='giCascades\b|[gG]iBoundsExcluded|\bcascades\s*=\s*(true|false)\b|mGi\.cascades\b|refreshVctFast|buildVoxelArm|giEscapeSignature|testVoxelResolution|kAutoGiBoundsMax|voxelResolution\b|"cascades"\s*:\s*(true|false)|cascades:\s*(true|false)'
hits=$(grep -rn -E "$PATTERN" \
         src irisgl/engine/src irisgl/engine/include irisgl/mirror irisgl/document tests \
         --include='*.cpp' --include='*.h' --include='*.js' --include='*.js.in' \
         --include='*.glsl' --include='*.any' 2>/dev/null |
       grep -v -E '^tests/hygiene/' |
       grep -v -E ':\s*(//|///|\*)' |
       grep -v -E '=== undefined|!byId\[|== nullptr' |   # a suite asserting the ABSENCE is the point
       # The debug voxel visualiser's own shader uniform, by name (OWN-GI-1: Ogre's
       # VoxelVisualizer moved into irisgl as photon/voxel/PhotonVoxelVisualizer.cpp) —
       # the size of the cascade it draws, not the deleted single volume's lever.
       grep -v -E 'setNamedConstant\( "voxelResolution"' )
if [ -n "$hits" ]; then
    echo "source.no_fixed_gi_volume: the deleted single-volume arm (or a reader of its keys) is back:"
    echo "$hits" | head -40
    exit 1
fi
echo "source.no_fixed_gi_volume: ok — no reader, writer or engine half of the single volume"
exit 0
