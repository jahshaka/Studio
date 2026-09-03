#!/usr/bin/env python3
"""Generates the .bvh MOCAP fixtures for tests/avatar.

Why generated and not a downloaded capture: a .bvh is the one file format in
the tree that is ANIMATION ONLY by construction (a joint hierarchy and a table
of per-frame channel values — there is no geometry in the format at all), so it
is exactly what the Avatar module's cross-file clip path exists for. But every
real mocap library file is third-party content and hundreds of kilobytes. BVH
is also plain ASCII, so a hand-written one is reviewable line by line.

The clip -> bone join is BY NAME (AvatarPreviewModel::loadAnimation scores a
file's channel names against the loaded rig's scene-node names), so these two
files mirror the rig2.glb fixture's chain deliberately:

    rig2_walk.bvh       ROOT jointRoot / JOINT jointTip — the SAME joint names
                        rig2.glb uses, so every bone channel matches and the
                        clip loads onto the character.
    rig_mismatch.bvh    ROOT hips / JOINT spine — a foreign rig, so the module
                        must REFUSE it by name instead of loading a clip that
                        moves nothing.

Measured facts about assimp's BVH importer that the suite asserts (assimp
v6.0.5, code/AssetLib/BVH/BVHLoader.cpp):

  * It is NOT a zero-mesh scene. Unless AI_CONFIG_IMPORT_NO_SKELETON_MESHES is
    set, BVHLoader runs SkeletonMeshBuilder, which synthesises a stick-figure
    mesh over the joint hierarchy and installs it on the root node. Our
    animation path never looks at meshes, so this is harmless there — but it is
    why .bvh must NOT be in Constants::MODEL_EXTS: importing one as a model
    would put a bogus stick figure in the asset library.
  * Every clip is named "Motion", hard-coded, for every BVH ever written — the
    same situation as Mixamo's "mixamo.com", which is why "motion" joins the
    junk-clip-name set and a BVH clip displays as its FILE's base name.
  * Channel names are the ROOT/JOINT names verbatim. `End Site` blocks become
    nodes named "EndSite_<parent>" but carry no channels, so they never reach
    the rig-match score.
  * mTicksPerSecond = 1 / (Frame Time) and mDuration = (Frames - 1), both in
    frames; Mesh::extractAnimations divides by mTicksPerSecond, so a 3-frame
    file at 0.5 s/frame is a 1.0 s clip.

A REAL Mixamo-rig BVH would need one more thing this fixture cannot supply:
Mixamo's glTF/FBX exports prefix every joint with "mixamorig:" while a BVH
exported from the same rig usually does not, so the name-keyed join scores 0
and the module refuses the file. That is the honest state of BVH support —
matching joint names are the user's job (rename in the mocap tool, or export
the character with matching names). Nothing here papers over it.

Run:  python3 make_bvh_fixtures.py        (writes both files beside this script)
"""

import os

HEADER = """HIERARCHY
ROOT {root}
{{
  OFFSET 0.00 0.00 0.00
  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation
  JOINT {child}
  {{
    OFFSET 0.00 1.00 0.00
    CHANNELS 3 Zrotation Xrotation Yrotation
    End Site
    {{
      OFFSET 0.00 1.00 0.00
    }}
  }}
}}
MOTION
Frames: 3
Frame Time: 0.500000
"""

# Three frames at 0.5 s = a 1.0 s clip. The root's Z rotation sweeps
# 0 -> -30 -> -60 degrees (so the track MOVES between its first and last key,
# which is the assertion the avatar suite makes), and the root's translation
# walks forward in X so the root-motion policy has something to strip.
FRAMES = [
    #  Xpos  Ypos  Zpos   Zrot  Xrot  Yrot | child Zrot Xrot Yrot
    "0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00",
    "0.50 0.00 0.00 -30.00 0.00 0.00 15.00 0.00 0.00",
    "1.00 0.00 0.00 -60.00 0.00 0.00 30.00 0.00 0.00",
]


def write(filename, root, child):
    text = HEADER.format(root=root, child=child) + "\n".join(FRAMES) + "\n"
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), filename)
    with open(out, "w", newline="\n") as f:
        f.write(text)
    print("wrote %s (%d bytes)" % (out, len(text)))


# The joint names rig2.glb uses, so the clip lands on the character.
write("rig2_walk.bvh", "jointRoot", "jointTip")
# A foreign rig: not one channel name exists in rig2.glb's tree.
write("rig_mismatch.bvh", "hips", "spine")
