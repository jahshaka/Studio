#!/usr/bin/env python3
"""Bake one of the webxr-input-profiles controller glTF files into ONE mesh.

WHY A DERIVED FILE AND NOT THE .glb ITSELF (lane VR-INPUT-1E, 2026-09-17).
The upstream asset places its six parts (body, trigger, squeeze, thumbstick and
two buttons) by NODE TRANSFORM, and this tree's one assimp read site hands a
caller `aiScene::mMeshes` with no node tree at all
(`GraphicsHelper::loadAllMeshesFromAssimpScene`) — six parts loaded that way
land on top of each other at the file's origin. Rather than add a second
assimp read path with `aiProcess_PreTransformVertices` (a new read site in a
bake-key-guarded file, for a helper mesh), the transforms are baked HERE, once,
and the result is an ordinary OBJ of the kind this tree already loads from qrc.

WHAT IT PRODUCES. One OBJ in the file's own root space — which is the WebXR
GRIP space, i.e. the OpenXR grip pose's frame (metres, Y up, -Z forward out of
the fist) — with positions and normals, no UVs, no materials: the proxies are
drawn unlit grey (no texture crosses the helper boundary).

Usage: make-controller-obj.py <in.glb> <out.obj>
"""
import json, struct, sys, math

def read_glb(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'glTF', 'not a glb'
    off, js, bins = 12, None, None
    while off < len(d):
        ln, kind = struct.unpack_from('<II', d, off)
        chunk = d[off + 8:off + 8 + ln]
        if kind == 0x4E4F534A: js = json.loads(chunk)
        elif kind == 0x004E4942: bins = chunk
        off += 8 + ln + ((4 - ln % 4) % 4 if ln % 4 else 0)
    return js, bins

CTYPE = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2),
         5125: ('I', 4), 5126: ('f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}

def accessor(js, bins, index):
    a = js['accessors'][index]
    fmt, size = CTYPE[a['componentType']]
    n = NCOMP[a['type']]
    v = js['bufferViews'][a['bufferView']]
    base = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = v.get('byteStride') or size * n
    out = []
    for i in range(a['count']):
        out.append(struct.unpack_from('<' + fmt * n, bins, base + i * stride))
    return out

def mat_of(node):
    if 'matrix' in node:
        m = node['matrix']          # column-major
        return [[m[0], m[4], m[8], m[12]], [m[1], m[5], m[9], m[13]],
                [m[2], m[6], m[10], m[14]], [m[3], m[7], m[11], m[15]]]
    t = node.get('translation', [0, 0, 0])
    q = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    x, y, z, w = q
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[i][j] * s[j] for j in range(3)] + [t[i]] for i in range(3)] + \
           [[0, 0, 0, 1]]

def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

def xform(m, p, w):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] * w for i in range(3))

def main():
    src, dst = sys.argv[1], sys.argv[2]
    js, bins = read_glb(src)
    scene = js['scenes'][js.get('scene', 0)]
    verts, norms, faces = [], [], []

    def walk(idx, parent):
        node = js['nodes'][idx]
        world = mul(parent, mat_of(node))
        if 'mesh' in node:
            for prim in js['meshes'][node['mesh']]['primitives']:
                assert prim.get('mode', 4) == 4, 'triangles only'
                pos = accessor(js, bins, prim['attributes']['POSITION'])
                nor = accessor(js, bins, prim['attributes']['NORMAL']) \
                    if 'NORMAL' in prim['attributes'] else [(0, 1, 0)] * len(pos)
                idxs = [i[0] for i in accessor(js, bins, prim['indices'])]
                base = len(verts)
                for p in pos: verts.append(xform(world, p, 1.0))
                for n in nor:
                    v = xform(world, n, 0.0)
                    L = math.sqrt(sum(c * c for c in v)) or 1.0
                    norms.append(tuple(c / L for c in v))
                for i in range(0, len(idxs), 3):
                    faces.append((base + idxs[i] + 1, base + idxs[i + 1] + 1, base + idxs[i + 2] + 1))
        for c in node.get('children', []): walk(c, world)

    ident = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    for n in scene['nodes']: walk(n, ident)

    with open(dst, 'w') as f:
        f.write('# %s, baked from %s by make-controller-obj.py\n' % (dst.split('/')[-1], src.split('/')[-1]))
        f.write('# WebXR grip space = the OpenXR grip pose frame: metres, Y up, -Z forward.\n')
        for v in verts: f.write('v %.6f %.6f %.6f\n' % v)
        for n in norms: f.write('vn %.6f %.6f %.6f\n' % n)
        for a, b, c in faces: f.write('f %d//%d %d//%d %d//%d\n' % (a, a, b, b, c, c))
    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    print('%s: %d verts, %d tris, aabb min (%.4f %.4f %.4f) max (%.4f %.4f %.4f)'
          % (dst, len(verts), len(faces), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]))

main()
