#!/usr/bin/env python3
"""Generates the FBX SCENE fixture — scene_features.fbx and its Textures/ (BISTRO-IMPORT-1).

WHY IT EXISTS. The Bistro import (spikes/bistro/findings.md, 2026-10-13) found an FBX
scene's materials, lights, camera, root and mobility all imported wrong, silently. The
demo scene is never a test, so this ~10 KB file of OURS carries each of those shapes
once, hand-written ASCII FBX 7.4 like the sibling make_unit_fbx.py, and
test_importer_scene.cpp asserts them as counts:

  SpecCube   a 1 m cube whose material names Textures/CubeMat_Specular.png, which is
             ABSENT, while the prepared split CubeMat_Specular_roughness.png +
             CubeMat_Specular_metallic.png sit beside it (the Bistro convention) -> the
             pair binds as the roughness and metallic maps, nothing is missing.
  LeafPlane  a quad whose BaseColor PNG carries an alpha channel with holes -> cutout.
  GlassBox   a box whose material states a diffuse colour (0.6, 0.1, 0.1) and an opacity
             of 0.25 (TransparentColor 0.75 x TransparencyFactor 1) -> a tinted glass, and
             names Textures/TintGlass_Normal.png, which does not exist -> ONE missing map.
  Building   a Null scaled 0.01 (the file is centimetres under a metre declaration,
             UnitScaleFactor 100, as every Bistro file is) over Box2m, a 200 x 200 x 200
             cube carrying a RotationPivot and a ScalingPivot (assimp's $AssimpFbx$ pivot
             chain) -> exactly 2 m on a side.
  SunLight   a directional light authored at 45 degrees of elevation through Maya's
             PostRotation (-90, 0, 0) + Lcl Rotation;  ShotCam  a camera.
  Spinner    the ONE node the "Take 001" animation moves (a translation curve).

The TEXTURES are not tracked (source.no_renders_tracked: no image in git outside the
fixture allow-list): test_importer_scene.cpp writes them beside a copy of this file in its
own temporary directory — CubeMat_BaseColor.png (opaque RGB), the split pair
CubeMat_Specular_{roughness,metallic}.png (grey), LeafMat_BaseColor.png (RGBA, a diamond
of coverage with the corners cut to alpha 0).

Run:  python3 make_scene_fbx.py      (writes scene_features.fbx beside it)
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))


def num(x):
    return str(int(x)) if float(x) == int(x) else repr(float(x))


def box(lo, hi):
    """Eight corners and six quads of an axis-aligned box."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    q = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 4, 7, 3), (1, 2, 6, 5), (0, 1, 5, 4), (3, 7, 6, 2)]
    return v, q


def quad():
    return [(-0.5, 0, -0.5), (0.5, 0, -0.5), (0.5, 0, 0.5), (-0.5, 0, 0.5)], [(0, 3, 2, 1)]


out = []
w = out.append


def props(lines, indent="\t\t"):
    w(indent + "Properties70:  {\n")
    for l in lines:
        w(indent + "\tP: " + l + "\n")
    w(indent + "}\n")


def geometry(gid, name, verts, quads):
    idx = []
    for q in quads:
        idx += [str(q[0]), str(q[1]), str(q[2]), str(-(q[3] + 1))]
    vs = ",".join(num(c) for v in verts for c in v)
    w('\tGeometry: %d, "Geometry::%s", "Mesh" {\n' % (gid, name))
    w("\t\tVertices: *%d {\n\t\t\ta: %s\n\t\t}\n" % (len(verts) * 3, vs))
    w("\t\tPolygonVertexIndex: *%d {\n\t\t\ta: %s\n\t\t}\n" % (len(idx), ",".join(idx)))
    w("\t\tGeometryVersion: 124\n")
    w('\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
      '\t\t\tMappingInformationType: "AllSame"\n\t\t\tReferenceInformationType: "IndexToDirect"\n'
      '\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n')
    w('\t\tLayer: 0 {\n\t\t\tVersion: 100\n\t\t\tLayerElement:  {\n'
      '\t\t\t\tType: "LayerElementMaterial"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n\t\t}\n')
    w("\t}\n")


def model(mid, name, kind, p70):
    w('\tModel: %d, "Model::%s", "%s" {\n\t\tVersion: 232\n' % (mid, name, kind))
    props(['"RotationActive", "bool", "", "",1', '"InheritType", "enum", "", "",1'] + p70)
    w('\t\tShading: T\n\t\tCulling: "CullingOff"\n\t}\n')


def material(mid, name, p70):
    # Every Bistro material is a Phong with a shininess (assimp derives its
    # roughnessFactor from it, which puts the material on the PBR path): so is each
    # of ours, or the fixture would test the legacy path the Bistro never takes.
    p70 = p70 + ['"ShininessExponent", "Number", "", "A",20', '"SpecularColor", "Color", "", "A",0.5,0.5,0.5']
    w('\tMaterial: %d, "Material::%s", "" {\n\t\tVersion: 102\n\t\tShadingModel: "phong"\n'
      '\t\tMultiLayer: 0\n' % (mid, name))
    props(p70)
    w("\t}\n")


def texture(tid, name, rel):
    w('\tTexture: %d, "Texture::%s", "" {\n\t\tType: "TextureVideoClip"\n\t\tVersion: 202\n'
      '\t\tTextureName: "Texture::%s"\n\t\tFileName: "%s"\n\t\tRelativeFilename: "%s"\n\t}\n'
      % (tid, name, name, rel, rel))


def T(x, y, z):
    return '"Lcl Translation", "Lcl Translation", "", "A",%s,%s,%s' % (num(x), num(y), num(z))


def R(x, y, z):
    return '"Lcl Rotation", "Lcl Rotation", "", "A",%s,%s,%s' % (num(x), num(y), num(z))


def S(s):
    return '"Lcl Scaling", "Lcl Scaling", "", "A",%s,%s,%s' % (num(s), num(s), num(s))


conns = []

w("; FBX 7.4.0 project file\n")
w("; Generated by tests/importer/fixtures/make_scene_fbx.py - do not hand-edit.\n\n")
w("FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n")
w("GlobalSettings:  {\n\tVersion: 1000\n")
props(['"UpAxis", "int", "Integer", "",1', '"UpAxisSign", "int", "Integer", "",1',
       '"FrontAxis", "int", "Integer", "",2', '"FrontAxisSign", "int", "Integer", "",1',
       '"CoordAxis", "int", "Integer", "",0', '"CoordAxisSign", "int", "Integer", "",1',
       '"UnitScaleFactor", "double", "Number", "",100',
       '"OriginalUnitScaleFactor", "double", "Number", "",1'], "\t")
w("}\n")
w("Objects:  {\n")

# ---- SpecCube: the split specular pair --------------------------------------
v, q = box((-0.5, 0, -0.5), (0.5, 1, 0.5))
geometry(1000, "SpecCube", v, q)
model(1001, "SpecCube", "Mesh", [T(-3, 0, 0)])
material(1002, "CubeMat", ['"DiffuseColor", "Color", "", "A",1,1,1'])
texture(1003, "CubeMat_BaseColor", "Textures/CubeMat_BaseColor.png")
texture(1004, "CubeMat_Specular", "Textures/CubeMat_Specular.png")
conns += [("OO", 1001, 0), ("OO", 1000, 1001), ("OO", 1002, 1001),
          ("OP", 1003, 1002, "DiffuseColor"), ("OP", 1004, 1002, "SpecularColor")]

# ---- LeafPlane: the cutout ---------------------------------------------------
v, q = quad()
geometry(1100, "LeafPlane", v, q)
model(1101, "LeafPlane", "Mesh", [T(-1.5, 0.5, 0)])
material(1102, "LeafMat", ['"DiffuseColor", "Color", "", "A",1,1,1'])
texture(1103, "LeafMat_BaseColor", "Textures/LeafMat_BaseColor.png")
conns += [("OO", 1101, 0), ("OO", 1100, 1101), ("OO", 1102, 1101),
          ("OP", 1103, 1102, "DiffuseColor")]

# ---- GlassBox: the coloured glass and the one missing map ----------------------
v, q = box((-0.25, 0, -0.25), (0.25, 0.5, 0.25))
geometry(1200, "GlassBox", v, q)
model(1201, "GlassBox", "Mesh", [T(0, 0, 0)])
material(1202, "TintGlass", ['"DiffuseColor", "Color", "", "A",0.6,0.1,0.1',
                             '"TransparentColor", "Color", "", "A",0.75,0.75,0.75',
                             '"TransparencyFactor", "Number", "", "A",1'])
texture(1203, "TintGlass_Normal", "Textures/TintGlass_Normal.png")
conns += [("OO", 1201, 0), ("OO", 1200, 1201), ("OO", 1202, 1201),
          ("OP", 1203, 1202, "NormalMap")]

# ---- Building > Box2m: the pivot chain and the known 2 m box --------------------
model(1300, "Building", "Null", [T(5, 0, 0), S(0.01)])
v, q = box((0, 0, 0), (200, 200, 200))
geometry(1301, "Box2m", v, q)
model(1302, "Box2m", "Mesh", [T(10, 0, 20),
                               '"RotationPivot", "Vector3D", "Vector", "",100,100,100',
                               '"ScalingPivot", "Vector3D", "Vector", "",100,0,100',
                               '"RotationOffset", "Vector3D", "Vector", "",0,0,0'])
material(1303, "Concrete", ['"DiffuseColor", "Color", "", "A",0.5,0.5,0.5'])
conns += [("OO", 1300, 0), ("OO", 1302, 1300), ("OO", 1301, 1302), ("OO", 1303, 1302)]

# ---- SunLight: one directional light, oriented the way Maya writes it -----------
# An FBX light shines down its local -Y; Maya's lights shine down -Z, so its exporter
# writes PostRotation (-90, 0, 0) and the light's own Lcl Rotation (the Bistro's
# directionalLight1: Lcl Rotation (-61.2, 0, 0) under the same PostRotation). This one is
# authored at 45 degrees of elevation, yawed 30: it travels (-0.3536, -0.7071, -0.6124).
model(1400, "SunLight", "Light", [T(0, 10, 0), R(-45, 30, 0),
                                  '"PostRotation", "Vector3D", "Vector", "",-90,0,0'])
w('\tNodeAttribute: 1401, "NodeAttribute::SunLight", "Light" {\n')
props(['"Color", "Color", "", "A",1,0.9,0.8', '"LightType", "enum", "", "",1',
       '"Intensity", "Number", "", "A",300', '"CastShadows", "bool", "", "",1'])
w('\t\tTypeFlags: "Light"\n\t\tGeometryVersion: 124\n\t}\n')
conns += [("OO", 1400, 0), ("OO", 1401, 1400)]

# ---- ShotCam: one camera --------------------------------------------------------
model(1500, "ShotCam", "Camera", [T(0, 1.5, 6), R(0, 90, 0)])
w('\tNodeAttribute: 1501, "NodeAttribute::ShotCam", "Camera" {\n')
props(['"AspectWidth", "double", "Number", "",1920', '"AspectHeight", "double", "Number", "",1080',
       '"FilmWidth", "Number", "", "A",1.417', '"FilmHeight", "Number", "", "A",0.945',
       '"ApertureMode", "enum", "", "",3', '"FocalLength", "Number", "", "A",35',
       '"NearPlane", "double", "Number", "",0.1', '"FarPlane", "double", "Number", "",500'])
w('\t\tTypeFlags: "Camera"\n\t\tGeometryVersion: 124\n\t}\n')
conns += [("OO", 1500, 0), ("OO", 1501, 1500)]

# ---- Spinner + "Take 001": the ONE animated node -----------------------------------
model(1600, "Spinner", "Null", [T(0, 2, -3)])
conns += [("OO", 1600, 0)]
SECOND = 46186158000   # one second in FBX KTime
w('\tAnimationStack: 1700, "AnimStack::Take 001", "" {\n')
props(['"LocalStop", "KTime", "Time", "",%d' % SECOND, '"ReferenceStop", "KTime", "Time", "",%d' % SECOND])
w("\t}\n")
w('\tAnimationLayer: 1701, "AnimLayer::BaseLayer", "" {\n\t}\n')
w('\tAnimationCurveNode: 1702, "AnimCurveNode::T", "" {\n')
props(['"d|X", "Number", "", "A",0', '"d|Y", "Number", "", "A",2', '"d|Z", "Number", "", "A",-3'])
w("\t}\n")
w('\tAnimationCurve: 1703, "AnimCurve::", "" {\n\t\tDefault: 0\n\t\tKeyVer: 4008\n'
  '\t\tKeyTime: *3 {\n\t\t\ta: 0,%d,%d\n\t\t}\n'
  '\t\tKeyValueFloat: *3 {\n\t\t\ta: 0,1,0\n\t\t}\n'
  '\t\tKeyAttrFlags: *1 {\n\t\t\ta: 24836\n\t\t}\n'
  '\t\tKeyAttrDataFloat: *4 {\n\t\t\ta: 0,0,255790911,0\n\t\t}\n'
  '\t\tKeyAttrRefCount: *1 {\n\t\t\ta: 3\n\t\t}\n\t}\n' % (SECOND // 2, SECOND))
conns += [("OO", 1701, 1700), ("OO", 1702, 1701), ("OP", 1702, 1600, "Lcl Translation"),
          ("OP", 1703, 1702, "d|X")]
w("}\n")

w("Connections:  {\n")
for c in conns:
    if c[0] == "OO":
        w('\tC: "OO",%d,%d\n' % (c[1], c[2]))
    else:
        w('\tC: "OP",%d,%d, "%s"\n' % (c[1], c[2], c[3]))
w("}\n")

with open(os.path.join(HERE, "scene_features.fbx"), "w") as f:
    f.write("".join(out))

print("wrote scene_features.fbx in %s" % HERE)
