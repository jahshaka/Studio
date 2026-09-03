#!/usr/bin/env python3
"""Generates tests/skeletal/fixtures/pivot_rig.fbx — the tree's FIRST FBX fixture.

WHY THIS FILE EXISTS (ANIMATION_ENGINE_MIGRATION_SPEC §1.5 F5, §9 R1).
assimp defaults AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS to true and nothing in the
tree sets it, so a pivot-preserving FBX arrives as a chain of
`<bone>_$AssimpFbx$_Translation/_RotationPivot/_PreRotation/_Rotation/...`
scene nodes between every pair of real bones — and MOST of a real clip's
channels target those pivot nodes, not the bones (46 of Walking(1).fbx's 52,
`src/modules/avatar/avatarpreviewmodel.cpp`). Every glTF fixture in the tree
(tests/avatar/fixtures/*.glb) has NO pivot nodes, so the whole pivot path was
untested: a naive per-key clip translation passes all of them and produces a
frozen or scrambled character on every Mixamo FBX.

No third-party content is committed. This writes ~10 KB of ASCII FBX 7.5 by
hand — the same format assimp's own test corpus uses
(thirdparty/assimp/test/models/FBX/cubes_with_mirroring_and_pivot.fbx) — with
no dependencies beyond the standard library, so the fixture is reproducible and
reviewable.

SHAPE — deliberately the same two-bone arm as tests/avatar/fixtures/rig2.glb
and tests/skeletal/armrig.h, so the three suites can be reasoned about together:

    arm            the ONE skinned mesh (6 verts, 2 quads), skin = the 2 joints
    jointRoot      a LimbNode with a PreRotation  -> a 2-node pivot chain
      jointTip     a LimbNode with Lcl Translation + RotationPivot + PreRotation
                   -> a FIVE-node pivot chain between it and jointRoot

After import the node tree is (verified, not assumed — the suite asserts it):

    RootNode
      jointRoot_$AssimpFbx$_PreRotation
        jointRoot                                    <- BONE
          jointTip_$AssimpFbx$_Translation
            jointTip_$AssimpFbx$_RotationPivot
              jointTip_$AssimpFbx$_PreRotation
                jointTip_$AssimpFbx$_Rotation        <- the clip's rotation lands HERE
                  jointTip_$AssimpFbx$_RotationPivotInverse
                    jointTip                         <- BONE
      arm

so bone `jointTip`'s parent bone is `jointRoot` with FIVE non-bone nodes in
between, three of which the clip animates. That is exactly the composition
`ClipExtractor` has to perform.

CLIPS
    "Walk"        1.0 s. jointRoot rotates about Z at t = 0, 0.5, 1.0; jointTip
                  rotates about Z at t = 0, 1.0 and TRANSLATES at t = 0, 0.25,
                  1.0 — three different key-time sets over one bone chain, so
                  the key-time UNION of §3.1 step 2 is exercised and a naive
                  "take the bone's own channel" translation cannot pass.
    "mixamo.com"  a SINGLE key — a zero-length clip. Every Mixamo character
                  download ships exactly one (the T-pose) and the Avatar page
                  selects it by default; engine-side a zero-length clip is
                  fmod(t, 0) = NaN (§9 R3), so it must be padded, and the only
                  honest way to test that is with a file that really has one.

Run:  python3 make_pivot_fbx.py            (writes pivot_rig.fbx beside it)
"""

import math
import os

# ---------------------------------------------------------------------------
# A 4x4 matrix as a flat row-major list, with the handful of operations the
# bind-pose computation below needs. FBX stores matrices COLUMN-major.
# ---------------------------------------------------------------------------


def ident():
    return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def mul(a, b):
    out = [0.0] * 16
    for r in range(4):
        for c in range(4):
            out[r * 4 + c] = sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4))
    return out


def translation(x, y, z):
    m = ident()
    m[3], m[7], m[11] = x, y, z
    return m


def rot_z(degrees):
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    m = ident()
    m[0], m[1] = c, -s
    m[4], m[5] = s, c
    return m


def rot_euler_xyz(rx, ry, rz):
    """FBX Lcl/Pre rotations are Euler XYZ degrees; assimp composes R = Rx*Ry*Rz."""
    ax, ay, az = math.radians(rx), math.radians(ry), math.radians(rz)
    cx, sx = math.cos(ax), math.sin(ax)
    cy, sy = math.cos(ay), math.sin(ay)
    cz, sz = math.cos(az), math.sin(az)
    mx = ident(); mx[5], mx[6], mx[9], mx[10] = cx, -sx, sx, cx
    my = ident(); my[0], my[2], my[8], my[10] = cy, sy, -sy, cy
    mz = ident(); mz[0], mz[1], mz[4], mz[5] = cz, -sz, sz, cz
    return mul(mul(mx, my), mz)


def column_major(m):
    """Row-major flat -> the column-major order FBX writes matrices in."""
    return [m[r * 4 + c] for c in range(4) for r in range(4)]


# ---------------------------------------------------------------------------
# The rig
# ---------------------------------------------------------------------------
HW = 0.15
POSITIONS = [
    (-HW, 0.0, 0.0), (HW, 0.0, 0.0),
    (-HW, 1.0, 0.0), (HW, 1.0, 0.0),
    (-HW, 2.0, 0.0), (HW, 2.0, 0.0),
]
# Two quads, FBX's negative-encoded last index per polygon.
POLYGONS = [(0, 1, 3, 2), (2, 3, 5, 4)]

ROOT_PREROT = (0.0, 0.0, 30.0)          # jointRoot: PreRotation only
TIP_TRANSLATION = (0.0, 1.0, 0.0)       # jointTip: Lcl Translation
TIP_ROT_PIVOT = (0.0, 0.25, 0.0)        # jointTip: RotationPivot (+ its inverse)
TIP_PREROT = (0.0, 0.0, -15.0)          # jointTip: PreRotation

# Bind (rest) globals — exactly the chain product assimp will build, so the
# cluster TransformLink matrices agree with the node tree by construction.
BIND_ROOT = rot_euler_xyz(*ROOT_PREROT)
BIND_TIP = mul(
    BIND_ROOT,
    mul(translation(*TIP_TRANSLATION),
        mul(translation(*TIP_ROT_PIVOT),
            mul(rot_euler_xyz(*TIP_PREROT),
                translation(-TIP_ROT_PIVOT[0], -TIP_ROT_PIVOT[1], -TIP_ROT_PIVOT[2])))))

# ---------------------------------------------------------------------------
# Ids. Any distinct 64-bit values will do; these are readable on purpose.
# ---------------------------------------------------------------------------
ID_GEOM = 100000
ID_MODEL_MESH = 100001
ID_MODEL_ROOT = 100002
ID_MODEL_TIP = 100003
ID_ATTR_ROOT = 100004
ID_ATTR_TIP = 100005
ID_MATERIAL = 100006
ID_SKIN = 100010
ID_CLUSTER_ROOT = 100011
ID_CLUSTER_TIP = 100012
ID_NEXT = [200000]


def new_id():
    ID_NEXT[0] += 1
    return ID_NEXT[0]


FBX_TIME_UNIT = 46186158000  # FBX ticks per second


def fbx_time(seconds):
    return int(round(seconds * FBX_TIME_UNIT))


# ---------------------------------------------------------------------------
# Clips.  (clip name, [ (model id, property, [ (axis, [(t, value), ...]) ]) ])
# ---------------------------------------------------------------------------
CLIPS = [
    ("Walk", [
        # jointRoot swings about Z across THREE key times.
        (ID_MODEL_ROOT, "Lcl Rotation", [
            ("X", [(0.0, 0.0), (0.5, 0.0), (1.0, 0.0)]),
            ("Y", [(0.0, 0.0), (0.5, 0.0), (1.0, 0.0)]),
            ("Z", [(0.0, 0.0), (0.5, -25.0), (1.0, -60.0)]),
        ]),
        # jointTip swings about Z across TWO — a different key-time set, on the
        # same bone chain, so the union in §3.1 step 2 is not a formality.
        (ID_MODEL_TIP, "Lcl Rotation", [
            ("X", [(0.0, 0.0), (1.0, 0.0)]),
            ("Y", [(0.0, 0.0), (1.0, 0.0)]),
            ("Z", [(0.0, 0.0), (1.0, 45.0)]),
        ]),
        # ...and TRANSLATES across a THIRD set. This channel lands on
        # `jointTip_$AssimpFbx$_Translation`, a node that is not a bone: the
        # single fact this whole fixture exists to test.
        (ID_MODEL_TIP, "Lcl Translation", [
            ("X", [(0.0, 0.0), (0.25, 0.1), (1.0, 0.0)]),
            ("Y", [(0.0, 1.0), (0.25, 1.0), (1.0, 1.2)]),
            ("Z", [(0.0, 0.0), (0.25, 0.0), (1.0, 0.0)]),
        ]),
    ]),
    # The zero-length T-pose clip every Mixamo character download ships: ONE
    # key, so the clip length is 0.0 s. The value has to differ from the node's
    # own Lcl Rotation or assimp's IsRedundantAnimationData drops the channel
    # (a single key equal to the rest transform carries no information) and the
    # whole clip disappears from the file — which is how the first draft of
    # this fixture silently lost it.
    ("mixamo.com", [
        (ID_MODEL_ROOT, "Lcl Rotation", [
            ("X", [(0.0, 0.0)]),
            ("Y", [(0.0, 0.0)]),
            ("Z", [(0.0, 20.0)]),
        ]),
    ]),
]


# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------
def arr(name, values, fmt="%.10g"):
    body = ",".join(fmt % v for v in values)
    return "\t\t%s: *%d {\n\t\t\ta: %s\n\t\t}\n" % (name, len(values), body)


def matrix_arr(name, m):
    return arr(name, column_major(m))


def build():
    out = []
    w = out.append

    w("; FBX 7.5.0 project file\n")
    w("; Generated by tests/skeletal/fixtures/make_pivot_fbx.py — do not hand-edit\n")
    w("; ----------------------------------------------------\n\n")

    w("FBXHeaderExtension:  {\n")
    w("\tFBXHeaderVersion: 1003\n")
    w("\tFBXVersion: 7500\n")
    w("\tCreator: \"jahshaka tests/skeletal make_pivot_fbx.py\"\n")
    w("}\n")

    # UnitScaleFactor 1 and a Y-up/Z-front/X-right basis, so no axis or unit
    # conversion is folded into the root node and the bind matrices below are
    # the transforms the importer really produces.
    w("GlobalSettings:  {\n")
    w("\tVersion: 1000\n")
    w("\tProperties70:  {\n")
    w("\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n")
    w("\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n")
    w("\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n")
    w("\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n")
    w("\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n")
    w("\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n")
    w("\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n")
    w("\t}\n")
    w("}\n")

    w("Definitions:  {\n")
    w("\tVersion: 100\n")
    w("\tCount: 20\n")
    w("\tObjectType: \"GlobalSettings\" {\n\t\tCount: 1\n\t}\n")
    for kind in ("Geometry", "Model", "Material", "NodeAttribute", "Deformer",
                 "AnimationStack", "AnimationLayer", "AnimationCurveNode",
                 "AnimationCurve"):
        w("\tObjectType: \"%s\" {\n\t\tCount: 8\n\t}\n" % kind)
    w("}\n")

    w("Objects:  {\n")

    # ---- geometry ----
    w("\tGeometry: %d, \"Geometry::arm\", \"Mesh\" {\n" % ID_GEOM)
    verts = [c for p in POSITIONS for c in p]
    w(arr("Vertices", verts))
    idx = []
    for poly in POLYGONS:
        idx.extend(poly[:-1])
        idx.append(-(poly[-1] + 1))
    w(arr("PolygonVertexIndex", idx, "%d"))
    w("\t\tGeometryVersion: 124\n")
    w("\t}\n")

    # ---- models ----
    w("\tModel: %d, \"Model::arm\", \"Mesh\" {\n" % ID_MODEL_MESH)
    w("\t\tVersion: 232\n")
    w("\t\tProperties70:  {\n")
    w("\t\t\tP: \"InheritType\", \"enum\", \"\", \"\",1\n")
    w("\t\t}\n")
    w("\t\tShading: T\n\t\tCulling: \"CullingOff\"\n")
    w("\t}\n")

    # jointRoot: a PreRotation is enough to force a chain (NeedsComplex-
    # TransformationChain tests every component that is not plain T/R/S).
    w("\tModel: %d, \"Model::jointRoot\", \"LimbNode\" {\n" % ID_MODEL_ROOT)
    w("\t\tVersion: 232\n")
    w("\t\tProperties70:  {\n")
    w("\t\t\tP: \"PreRotation\", \"Vector3D\", \"Vector\", \"\",%g,%g,%g\n" % ROOT_PREROT)
    w("\t\t\tP: \"RotationActive\", \"bool\", \"\", \"\",1\n")
    w("\t\t\tP: \"InheritType\", \"enum\", \"\", \"\",1\n")
    w("\t\t}\n")
    w("\t\tShading: T\n\t\tCulling: \"CullingOff\"\n")
    w("\t}\n")

    w("\tModel: %d, \"Model::jointTip\", \"LimbNode\" {\n" % ID_MODEL_TIP)
    w("\t\tVersion: 232\n")
    w("\t\tProperties70:  {\n")
    w("\t\t\tP: \"RotationPivot\", \"Vector3D\", \"Vector\", \"\",%g,%g,%g\n" % TIP_ROT_PIVOT)
    w("\t\t\tP: \"PreRotation\", \"Vector3D\", \"Vector\", \"\",%g,%g,%g\n" % TIP_PREROT)
    w("\t\t\tP: \"RotationActive\", \"bool\", \"\", \"\",1\n")
    w("\t\t\tP: \"InheritType\", \"enum\", \"\", \"\",1\n")
    w("\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",%g,%g,%g\n" % TIP_TRANSLATION)
    w("\t\t}\n")
    w("\t\tShading: T\n\t\tCulling: \"CullingOff\"\n")
    w("\t}\n")

    for attr_id, label in ((ID_ATTR_ROOT, "jointRoot"), (ID_ATTR_TIP, "jointTip")):
        w("\tNodeAttribute: %d, \"NodeAttribute::%s\", \"LimbNode\" {\n" % (attr_id, label))
        w("\t\tTypeFlags: \"Skeleton\"\n")
        w("\t}\n")

    # A MATERIAL. Without one assimp substitutes its default and the character
    # renders in a grey that is nearly the editor's background — which is how
    # the first end-to-end pixel gate on this fixture came back "nothing moved"
    # while the geometry was on screen the whole time. Red, like rig2.glb's.
    w("\tMaterial: %d, \"Material::armMat\", \"\" {\n" % ID_MATERIAL)
    w("\t\tVersion: 102\n")
    w("\t\tShadingModel: \"lambert\"\n")
    w("\t\tMultiLayer: 0\n")
    w("\t\tProperties70:  {\n")
    w("\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",0.9,0.15,0.15\n")
    w("\t\t\tP: \"Diffuse\", \"Vector3D\", \"Vector\", \"\",0.9,0.15,0.15\n")
    w("\t\t\tP: \"Emissive\", \"Vector3D\", \"Vector\", \"\",0,0,0\n")
    w("\t\t}\n")
    w("\t}\n")

    # ---- skin ----
    w("\tDeformer: %d, \"Deformer::skin\", \"Skin\" {\n" % ID_SKIN)
    w("\t\tVersion: 101\n")
    w("\t\tLink_DeformAcuracy: 50\n")
    w("\t}\n")

    for cid, label, indices, bind in (
        (ID_CLUSTER_ROOT, "jointRoot", [0, 1, 2, 3], BIND_ROOT),
        (ID_CLUSTER_TIP, "jointTip", [4, 5], BIND_TIP),
    ):
        w("\tDeformer: %d, \"SubDeformer::cluster_%s\", \"Cluster\" {\n" % (cid, label))
        w("\t\tVersion: 100\n")
        w("\t\tUserData: \"\", \"\"\n")
        w(arr("Indexes", indices, "%d"))
        w(arr("Weights", [1.0] * len(indices)))
        # Transform    = the MESH node's bind global, inverted (identity here).
        # TransformLink= the BONE's bind global. assimp forms the offset matrix
        #                as inverse(TransformLink) * meshGlobal, so these two
        #                are what fix the bind pose.
        w(matrix_arr("Transform", ident()))
        w(matrix_arr("TransformLink", bind))
        w("\t}\n")

    # ---- animation ----
    connections = []
    for clip_name, targets in CLIPS:
        stack_id, layer_id = new_id(), new_id()
        w("\tAnimationStack: %d, \"AnimStack::%s\", \"\" {\n" % (stack_id, clip_name))
        w("\t}\n")
        w("\tAnimationLayer: %d, \"AnimLayer::BaseLayer\", \"\" {\n" % layer_id)
        w("\t}\n")
        connections.append(("OO", layer_id, stack_id, None))
        for model_id, prop, axes in targets:
            node_id = new_id()
            w("\tAnimationCurveNode: %d, \"AnimCurveNode::%s\", \"\" {\n"
              % (node_id, prop.split()[-1]))
            w("\t\tProperties70:  {\n")
            for axis, keys in axes:
                w("\t\t\tP: \"d|%s\", \"Number\", \"\", \"A\",%.10g\n" % (axis, keys[0][1]))
            w("\t\t}\n")
            w("\t}\n")
            connections.append(("OO", node_id, layer_id, None))
            connections.append(("OP", node_id, model_id, prop))
            for axis, keys in axes:
                curve_id = new_id()
                w("\tAnimationCurve: %d, \"AnimCurve::\", \"\" {\n" % curve_id)
                w("\t\tDefault: %.10g\n" % keys[0][1])
                w("\t\tKeyVer: 4009\n")
                w(arr("KeyTime", [fbx_time(t) for t, _ in keys], "%d"))
                w(arr("KeyValueFloat", [v for _, v in keys]))
                w(arr("KeyAttrFlags", [24836], "%d"))
                w(arr("KeyAttrDataFloat", [0, 0, 0, 0]))
                w(arr("KeyAttrRefCount", [len(keys)], "%d"))
                w("\t}\n")
                connections.append(("OP", curve_id, node_id, "d|" + axis))

    w("}\n")

    # ---- connections ----
    w("Connections:  {\n")
    w("\tC: \"OO\",%d,0\n" % ID_MODEL_ROOT)          # jointRoot under the root
    w("\tC: \"OO\",%d,0\n" % ID_MODEL_MESH)          # the mesh under the root
    w("\tC: \"OO\",%d,%d\n" % (ID_MODEL_TIP, ID_MODEL_ROOT))
    w("\tC: \"OO\",%d,%d\n" % (ID_ATTR_ROOT, ID_MODEL_ROOT))
    w("\tC: \"OO\",%d,%d\n" % (ID_ATTR_TIP, ID_MODEL_TIP))
    w("\tC: \"OO\",%d,%d\n" % (ID_GEOM, ID_MODEL_MESH))
    w("\tC: \"OO\",%d,%d\n" % (ID_MATERIAL, ID_MODEL_MESH))
    w("\tC: \"OO\",%d,%d\n" % (ID_SKIN, ID_GEOM))
    w("\tC: \"OO\",%d,%d\n" % (ID_CLUSTER_ROOT, ID_SKIN))
    w("\tC: \"OO\",%d,%d\n" % (ID_CLUSTER_TIP, ID_SKIN))
    w("\tC: \"OO\",%d,%d\n" % (ID_MODEL_ROOT, ID_CLUSTER_ROOT))
    w("\tC: \"OO\",%d,%d\n" % (ID_MODEL_TIP, ID_CLUSTER_TIP))
    for kind, src, dst, prop in connections:
        if prop is None:
            w("\tC: \"%s\",%d,%d\n" % (kind, src, dst))
        else:
            w("\tC: \"%s\",%d,%d, \"%s\"\n" % (kind, src, dst, prop))
    w("}\n")

    return "".join(out)


if __name__ == "__main__":
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pivot_rig.fbx")
    with open(path, "w") as f:
        f.write(build())
    print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))
