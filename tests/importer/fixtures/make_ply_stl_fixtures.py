#!/usr/bin/env python3
"""Generates the .ply and .stl mesh fixtures for tests/importer.

Both formats are ASCII and both fixtures are a few hundred bytes, so they are
written out here rather than downloaded: no third-party content, and the exact
bytes under test are reviewable.

    colored_quad.ply    A two-triangle quad with per-vertex COLOURS and no
                        normals, no UVs and no material — the shape a scanner
                        or a photogrammetry export has. PLY's vertex-colour
                        property is the one interesting thing the format adds
                        over OBJ, so the suite asserts it survives.
    tetra_normals.stl   A tetrahedron whose `facet normal` records are real
                        unit vectors — what most STL writers emit.
    tetra_zeronormals.stl
                        The SAME tetrahedron with `facet normal 0 0 0` — what
                        Blender emits, and what assimp's own STLLoader source
                        calls out ("Blender sometimes writes empty normals ...
                        the RemoveInvalidData helper step should fix that").
                        The pair exists so the suite can state, from measurement
                        rather than from assumption, where a shadeable STL's
                        normals actually come from under the canonical preset.

Neither format carries UVs or materials at all: assimp hands back a single
default aiMaterial ("DefaultMaterial") and no texture coordinates, so these are
also the fixtures for "a model with nothing but geometry still gets a usable
default material" through our import path.

Run:  python3 make_ply_stl_fixtures.py     (writes all three beside this script)
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))


def write(filename, text):
    out = os.path.join(HERE, filename)
    with open(out, "w", newline="\n") as f:
        f.write(text)
    print("wrote %s (%d bytes)" % (out, len(text)))


# ---------------------------------------------------------------------------
# PLY — a unit quad in the z = 0 plane, four vertices, four distinct colours.
# Deliberately NO normals in the file: the canonical preset's GenSmoothNormals
# has to supply them, exactly as it does for a bare OBJ.
PLY_VERTS = [
    (-1.0, -1.0, 0.0, 255, 0, 0),
    (1.0, -1.0, 0.0, 0, 255, 0),
    (1.0, 1.0, 0.0, 0, 0, 255),
    (-1.0, 1.0, 0.0, 255, 255, 0),
]
PLY_FACES = [(0, 1, 2), (0, 2, 3)]

ply = ["ply", "format ascii 1.0",
       "comment jahshaka tests/importer make_ply_stl_fixtures.py",
       "element vertex %d" % len(PLY_VERTS),
       "property float x", "property float y", "property float z",
       "property uchar red", "property uchar green", "property uchar blue",
       "element face %d" % len(PLY_FACES),
       "property list uchar int vertex_indices",
       "end_header"]
for x, y, z, r, g, b in PLY_VERTS:
    ply.append("%g %g %g %d %d %d" % (x, y, z, r, g, b))
for f in PLY_FACES:
    ply.append("3 %d %d %d" % f)
write("colored_quad.ply", "\n".join(ply) + "\n")


# ---------------------------------------------------------------------------
# STL — a regular-ish tetrahedron, 4 facets. Written twice: once with real
# facet normals, once with the zero normals Blender emits.
TETRA = [
    ((0.0, 1.0, 0.0), (-1.0, -1.0, 1.0), (1.0, -1.0, 1.0)),
    ((0.0, 1.0, 0.0), (1.0, -1.0, 1.0), (0.0, -1.0, -1.0)),
    ((0.0, 1.0, 0.0), (0.0, -1.0, -1.0), (-1.0, -1.0, 1.0)),
    ((-1.0, -1.0, 1.0), (0.0, -1.0, -1.0), (1.0, -1.0, 1.0)),
]


def face_normal(tri):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = tri
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = (nx * nx + ny * ny + nz * nz) ** 0.5
    return (nx / length, ny / length, nz / length)


def stl(name, zero_normals):
    lines = ["solid %s" % name]
    for tri in TETRA:
        n = (0.0, 0.0, 0.0) if zero_normals else face_normal(tri)
        lines.append("  facet normal %.6e %.6e %.6e" % n)
        lines.append("    outer loop")
        for v in tri:
            lines.append("      vertex %.6e %.6e %.6e" % v)
        lines.append("    endloop")
        lines.append("  endfacet")
    lines.append("endsolid %s" % name)
    return "\n".join(lines) + "\n"


write("tetra_normals.stl", stl("tetra_normals", False))
write("tetra_zeronormals.stl", stl("tetra_zeronormals", True))
