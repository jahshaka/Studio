#pragma once
// clustermesh — THE ATOM ROWS' HARNESS MESHES (ATOM-TESTS-BACK-1 stage 1; SPECS/ATOM_TESTS_BACK_1_SPEC.md ruling 2): the
// procedural generators the cluster-DAG rows bake, in one header, as plain arrays (enginetest::ShellMesh's named fields:
// positions xyz, normals xyz, uvs uv, 32-bit indices, CCW seen from outside). No engine, no Qt, no sample model: a row
// converts them to an iris::Mesh (clusterfix::meshFromShell) and bakes them through the product's own bake.
//
// CLOSED BY INDEX (DOCS/traps/engine/atom-and-lod.md, PROCEDURAL SEAMS DO NOT WELD): every generator that stands for a
// closed surface wraps its seam and its poles BY INDEX REUSE — one vertex at each pole, the last column the first column's
// own vertices — so the mesh is closed by its index list itself, and every directed edge has exactly one reversed twin
// with no degenerate triangle (the closed-cut law, atom.cut_closed, counts that by weld id; a pole row of duplicated
// vertices would leave degenerate fans whose edges have two twins).
#include "proceduralshell.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace clustermesh {

using Mesh = enginetest::ShellMesh;

constexpr double kPi = 3.14159265358979323846;

/// The point of the unit sphere at ring r of `ring` (r = 0 the north pole) and column s of `seg`, times `radius`. ONE
/// formula for every sphere below, so the split sphere's two halves compute their shared equator bit for bit alike.
inline void spherePoint(int r, int ring, int s, int seg, float radius, float out[3])
{
    const double theta = double(r) / double(ring) * kPi, phi = double(s % seg) / double(seg) * 2.0 * kPi;
    out[0] = float(radius * std::sin(theta) * std::cos(phi));
    out[1] = float(radius * std::cos(theta));
    out[2] = float(radius * std::sin(theta) * std::sin(phi));
}

/// THE CLOSED SPHERE: `seg` columns x `ring` rings, radius `radius`, ONE vertex at each pole and the seam wrapped by index
/// (s = seg is s = 0's vertex). 2 seg (ring - 1) triangles (100 x 100: 19,800). Rings `r0..r1` only (both inclusive, a
/// pole counted as a ring of one vertex): the split sphere's halves; the default is the whole, closed sphere.
inline Mesh closedSphere(int seg = 100, int ring = 100, float radius = 0.5f, int r0 = 0, int r1 = -1)
{
    if (r1 < 0) r1 = ring;
    Mesh m;
    // vertex id of (r, s): a pole is one vertex, every other ring `seg` vertices
    std::vector<uint32_t> rowStart(size_t(ring + 1), 0u);
    uint32_t next = 0;
    for (int r = r0; r <= r1; ++r) {
        rowStart[size_t(r)] = next;
        const int count = (r == 0 || r == ring) ? 1 : seg;
        for (int s = 0; s < count; ++s) {
            float p[3];
            spherePoint(r, ring, s, seg, radius, p);
            m.positions.insert(m.positions.end(), { p[0], p[1], p[2] });
            const float inv = 1.0f / radius;
            m.normals.insert(m.normals.end(), { p[0] * inv, p[1] * inv, p[2] * inv });
            m.uvs.insert(m.uvs.end(), { float(s) / float(seg), float(r) / float(ring) });
        }
        next += uint32_t(count);
    }
    const auto id = [&](int r, int s) {
        return (r == 0 || r == ring) ? rowStart[size_t(r)] : rowStart[size_t(r)] + uint32_t(s % seg);
    };
    for (int r = r0; r < r1; ++r)
        for (int s = 0; s < seg; ++s) {
            // (a, b, c, e) = (r, s), (r, s+1), (r+1, s+1), (r+1, s): (b - a) x (c - a) points out.
            const uint32_t a = id(r, s), b = id(r, s + 1), c = id(r + 1, s + 1), e = id(r + 1, s);
            if (r != 0) m.indices.insert(m.indices.end(), { a, b, c });
            if (r + 1 != ring) m.indices.insert(m.indices.end(), { a, c, e });
        }
    return m;
}

/// THE SPLIT SPHERE (atom.cut_closed.multi_mesh): the closed sphere cut at its equator into TWO MESHES, each its own
/// vertex buffer and its own DAG; the equator ring is in both, at bit-identical positions (spherePoint), so the halves
/// weld by position along one seam. One mesh's DAG never locks its open border (irisgl/import/atomdag/groups.h:21-23):
/// the seam is free to move in each half on its own.
struct SplitSphere
{
    Mesh north;   ///< rings 0 .. ring/2
    Mesh south;   ///< rings ring/2 .. ring
};
inline SplitSphere splitSphere(int seg = 64, int ring = 64, float radius = 0.5f)
{
    return { closedSphere(seg, ring, radius, 0, ring / 2), closedSphere(seg, ring, radius, ring / 2, ring) };
}

/// A ROUND BAR: a closed cylinder `length` long along -Z from the origin, `radius` round, `segments` rings along its length
/// and `around` quads around — a bar whose simplification has real error around its section (a flat-faced box simplifies
/// losslessly). `bump` > 0 displaces the radius by that fraction in a 0.5 m x three-lobe pattern (zero at the two end
/// rings), so the bar has real detail along its length too. Closed by index: the seam wraps, and each cap is a fan from a
/// centre vertex over the END RING's own vertices.
inline Mesh roundBar(float length, float radius, int segments, int around, float bump = 0.0f)
{
    Mesh m;
    const double twoPi = 2.0 * kPi;
    const uint32_t row = uint32_t(around);
    for (int s = 0; s <= segments; ++s)
        for (int a = 0; a < around; ++a) {
            const double t = double(a) / double(around) * twoPi;
            const double z = -double(length) * double(s) / double(segments);
            const bool end = s == 0 || s == segments;
            const double rr = radius * (1.0 + (end ? 0.0 : bump * std::sin(z * twoPi / 0.5) * std::cos(3.0 * t)));
            m.positions.insert(m.positions.end(), { float(rr * std::cos(t)), float(rr * std::sin(t)), float(z) });
            m.normals.insert(m.normals.end(), { float(std::cos(t)), float(std::sin(t)), 0.0f });
            m.uvs.insert(m.uvs.end(), { float(a) / float(around), float(s) / float(segments) });
        }
    for (int s = 0; s < segments; ++s)
        for (int a = 0; a < around; ++a) {
            const uint32_t p0 = uint32_t(s) * row + uint32_t(a), p1 = uint32_t(s) * row + uint32_t((a + 1) % around);
            const uint32_t p3 = p0 + row, p2 = p1 + row;
            // (p3 - p0) runs along -Z and (p1 - p0) around (+theta): (p3 - p0) x (p1 - p0) points OUT.
            m.indices.insert(m.indices.end(), { p0, p3, p1, p1, p3, p2 });
        }
    for (int end = 0; end < 2; ++end) {
        const float z = end == 0 ? 0.0f : -length;
        const uint32_t centre = uint32_t(m.positions.size() / 3), ringStart = end == 0 ? 0u : uint32_t(segments) * row;
        m.positions.insert(m.positions.end(), { 0.0f, 0.0f, z });
        m.normals.insert(m.normals.end(), { 0.0f, 0.0f, end == 0 ? 1.0f : -1.0f });
        m.uvs.insert(m.uvs.end(), { 0.5f, 0.5f });
        for (int a = 0; a < around; ++a) {
            const uint32_t q0 = ringStart + uint32_t(a), q1 = ringStart + uint32_t((a + 1) % around);
            if (end == 0) m.indices.insert(m.indices.end(), { centre, q0, q1 });
            else          m.indices.insert(m.indices.end(), { centre, q1, q0 });
        }
    }
    return m;
}

/// A CARD SOUP: `cards` two-triangle cards (0.1 m squares, a fixed LCG's centres in a 2 m box, random yaw), every card its
/// own island — the many-islands case (ATOM-6's empty-top fixture; the DAG's card merge).
inline Mesh cardSoup(int cards)
{
    Mesh m;
    unsigned seed = 77u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    for (int c = 0; c < cards; ++c) {
        const float cx = rnd() * 2.0f - 1.0f, cy = rnd() * 2.0f - 1.0f, cz = rnd() * 2.0f - 1.0f, h = 0.05f;
        const float tilt = rnd() * 3.14159265f;
        const float ux = std::cos(tilt), uz = std::sin(tilt);
        const uint32_t b = uint32_t(m.positions.size() / 3);
        const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const auto &k : corners) {
            m.positions.insert(m.positions.end(), { cx + h * k[0] * ux, cy + h * k[1], cz + h * k[0] * uz });
            m.normals.insert(m.normals.end(), { -uz, 0.0f, ux });
        }
        m.indices.insert(m.indices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
    return m;
}

/// THE FLUSH SHUTTER (the independent measure's dropped-island term, ATOM_TESTS_BACK_1 stage 2): a square PLATE of
/// `cells` x `cells` quads, `plate` metres a side in the y = 0 plane facing +Y, and a thin closed BOX `size` x `size` x
/// `thickness` lying FLUSH on it at the plate's centre — its own island (its own eight vertices, closed by index; 12
/// triangles). A step that drops the box wholly moves no point of it more than `thickness` from the plate, so a
/// closest-point distance alone reads ~thickness; the true change is the whole box gone: its diagonal.
struct FlushShutter
{
    Mesh mesh;
    size_t plateTriangles = 0;   ///< the first plateTriangles triangles are the plate, the rest the box
    float diagonal = 0.0f;       ///< the box's diagonal, sqrt(2 size^2 + thickness^2)
    float thickness = 0.0f;
};
inline FlushShutter flushShutter(int cells = 8, float plate = 1.0f, float size = 0.2f, float thickness = 0.002f)
{
    FlushShutter f;
    Mesh &m = f.mesh;
    const float h = plate * 0.5f;
    for (int j = 0; j <= cells; ++j)
        for (int i = 0; i <= cells; ++i) {
            m.positions.insert(m.positions.end(), { -h + plate * float(i) / float(cells), 0.0f,
                                                    -h + plate * float(j) / float(cells) });
            m.normals.insert(m.normals.end(), { 0.0f, 1.0f, 0.0f });
            m.uvs.insert(m.uvs.end(), { float(i) / float(cells), float(j) / float(cells) });
        }
    const uint32_t row = uint32_t(cells + 1);
    for (int j = 0; j < cells; ++j)
        for (int i = 0; i < cells; ++i) {
            const uint32_t a = uint32_t(j) * row + uint32_t(i), b = a + 1, c = a + row, d = c + 1;
            // (c - a) runs +Z, (b - a) +X: (c - a) x (b - a) = +Z x +X = +Y, the plate's face.
            m.indices.insert(m.indices.end(), { a, c, b, b, c, d });
        }
    f.plateTriangles = m.indices.size() / 3;
    const uint32_t base = uint32_t(m.positions.size() / 3);
    const float s = size * 0.5f;
    for (int k = 0; k < 8; ++k) {   // bit 0: x, bit 1: z, bit 2: y (0 = on the plate, 1 = its top)
        const float x = (k & 1) ? s : -s, z = (k & 2) ? s : -s, y = (k & 4) ? thickness : 0.0f;
        m.positions.insert(m.positions.end(), { x, y, z });
        m.normals.insert(m.normals.end(), { 0.0f, (k & 4) ? 1.0f : -1.0f, 0.0f });
        m.uvs.insert(m.uvs.end(), { (k & 1) ? 1.0f : 0.0f, (k & 2) ? 1.0f : 0.0f });
    }
    // Six faces, each two triangles wound outward (CCW seen from outside).
    const uint32_t faces[6][4] = { { 4, 6, 7, 5 },    // top (+Y)
                                   { 0, 1, 3, 2 },    // bottom (-Y)
                                   { 1, 5, 7, 3 },    // +X
                                   { 0, 2, 6, 4 },    // -X
                                   { 2, 3, 7, 6 },    // +Z
                                   { 0, 4, 5, 1 } };  // -Z
    for (const auto &q : faces)
        m.indices.insert(m.indices.end(), { base + q[0], base + q[1], base + q[2], base + q[0], base + q[2], base + q[3] });
    f.diagonal = std::sqrt(2.0f * size * size + thickness * thickness);
    f.thickness = thickness;
    return f;
}

}   // namespace clustermesh
