#pragma once
// enginetest::proceduralShell — THE LARGE-ASSET FIXTURE (lane D1-SCALE-FIXTURES,
// SPECS/briefs/D1-SCALE-FIXTURES.md §4.2): a closed, bumpy, dragon-like shell of
// any triangle count, so the scale suites can push the geometry walls (W3 the
// cluster cut, W4 the eight levels, W5 residency, W11 the bake) with a 1 M, 5 M
// or 10 M triangle asset that no sample ships.
//
// THE SHAPE, and why: a CUBE-SPHERE (six n x n grids projected onto the unit
// sphere — no pole fans, every triangle about the same size) displaced by a sum
// of three incommensurate lobe patterns. The displacement is what makes it a
// fair test of a simplifier: a smooth sphere simplifies almost losslessly, so
// every level of its chain would be free at any distance and the cluster cut
// would have nothing to decide. With real detail at three scales every coarser
// level costs real error everywhere — the "dragon-like" case.
//
// 12 n^2 triangles for n quads a side: n = ceil(sqrt(T / 12)), so the count is
// at least `triangles` and within 0.1 % above it for T >= 1 M. Deterministic:
// the same `triangles` gives the same bytes on every run and every machine.
//
// Plain arrays (positions xyz, normals xyz, uvs uv, 32-bit indices) — the shape
// every consumer converts from: the engine's MeshData, an iris::Mesh, or the
// binary PLY the scale suites write so the asset reaches the PRODUCT's bake
// (MeshBake::buildFromFile — the import door), never a second implementation.
// Header-only, no engine and no Qt, CCW outward winding (Ogre's front face).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace enginetest {

struct ShellMesh
{
    std::vector<float> positions;   ///< xyz
    std::vector<float> normals;     ///< xyz, unit
    std::vector<float> uvs;         ///< uv
    std::vector<uint32_t> indices;  ///< CCW seen from outside
    size_t triangles() const { return indices.size() / 3u; }
};

/// THE SHAPE'S PARAMETERS, in one table the radius reads AND the cache key hashes
/// (proceduralShellKey): three lobe terms, amplitude then six frequency/phase terms.
constexpr double kShellLobes[3][7] = {
    { 0.09, 3.0, 1.3, 2.0, -0.4, 2.5, 0.7 },
    { 0.05, 11.0, 7.0, 9.0, -3.0, 0.0, 0.0 },
    { 0.02, 37.0, -23.0, 5.0, 31.0, 17.0, 0.0 },
};
/// Bumped by hand ONLY when the CONSTRUCTION changes (the mapping, the winding, the
/// lobe formulas); a change to a number in kShellLobes moves the key by itself.
constexpr int kProceduralShellVersion = 1;

/// The radius of the displaced shell along unit direction (x, y, z). Mean radius
/// 1, amplitude ~0.16 across three scales.
inline double shellRadius(double x, double y, double z)
{
    const auto &a = kShellLobes[0], &b = kShellLobes[1], &c = kShellLobes[2];
    return 1.0 + a[0] * std::sin(a[1] * x + a[2]) * std::cos(a[3] * y + a[4]) * std::sin(a[5] * z + a[6]) +
           b[0] * std::sin(b[1] * x + b[2] * y) * std::cos(b[3] * z + b[4] * y) +
           c[0] * std::sin(c[1] * x + c[2] * z + c[3]) * std::sin(c[4] * y + c[5] * x);
}

/// What identifies a shell's BYTES without writing them: the version, every shape
/// parameter and the triangle count — the scale suites' bake-cache key hashes this.
inline std::string proceduralShellKey(size_t triangles)
{
    std::string k = "enginetest::proceduralShell v" + std::to_string(kProceduralShellVersion);
    for (const auto &lobe : kShellLobes)
        for (double v : lobe) k += " " + std::to_string(v);
    return k + " tris " + std::to_string(triangles);
}

inline ShellMesh proceduralShell(size_t triangles)
{
    ShellMesh m;
    const size_t n = std::max<size_t>(1, size_t(std::ceil(std::sqrt(double(triangles) / 12.0))));
    const size_t side = n + 1;
    // Face frames: centre direction, u axis, v axis (u x v = the outward centre).
    static const double F[6][3][3] = {
        { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },   // +X
        { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },   // -X
        { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },   // +Y
        { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },   // -Y
        { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },   // +Z
        { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },  // -Z
    };
    const size_t verts = 6 * side * side;
    m.positions.resize(verts * 3);
    m.normals.assign(verts * 3, 0.0f);
    m.uvs.resize(verts * 2);
    m.indices.reserve(6 * n * n * 6);
    for (size_t f = 0; f < 6; ++f) {
        const size_t base = f * side * side;
        for (size_t j = 0; j < side; ++j)
            for (size_t i = 0; i < side; ++i) {
                // equal-angle cube-sphere mapping: evenly sized triangles
                const double a = (double(i) / double(n) * 2.0 - 1.0) * (M_PI / 4.0);
                const double b = (double(j) / double(n) * 2.0 - 1.0) * (M_PI / 4.0);
                const double tu = std::tan(a), tv = std::tan(b);
                double d[3];
                for (int k = 0; k < 3; ++k) d[k] = F[f][0][k] + tu * F[f][1][k] + tv * F[f][2][k];
                const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                for (double &c : d) c /= len;
                const double r = shellRadius(d[0], d[1], d[2]);
                const size_t v = base + j * side + i;
                for (int k = 0; k < 3; ++k) m.positions[v * 3 + size_t(k)] = float(d[k] * r);
                m.uvs[v * 2 + 0] = float(double(i) / double(n));
                m.uvs[v * 2 + 1] = float(double(j) / double(n));
            }
        for (size_t j = 0; j < n; ++j)
            for (size_t i = 0; i < n; ++i) {
                const uint32_t p0 = uint32_t(base + j * side + i), p1 = p0 + 1,
                               p3 = uint32_t(p0 + side), p2 = p3 + 1;
                // u x v points out, so (p0, p1, p2) is CCW seen from outside
                m.indices.insert(m.indices.end(), { p0, p1, p2, p0, p2, p3 });
            }
    }
    // Area-weighted vertex normals (the seams between faces carry duplicate
    // vertices, exactly like an unwelded export; the importer welds them).
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const uint32_t ia = m.indices[t], ib = m.indices[t + 1], ic = m.indices[t + 2];
        const float *A = &m.positions[ia * 3], *B = &m.positions[ib * 3], *C = &m.positions[ic * 3];
        const float e1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
        const float e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
        const float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2],
                    nz = e1[0] * e2[1] - e1[1] * e2[0];
        for (uint32_t v : { ia, ib, ic }) {
            m.normals[v * 3 + 0] += nx;
            m.normals[v * 3 + 1] += ny;
            m.normals[v * 3 + 2] += nz;
        }
    }
    for (size_t v = 0; v < verts; ++v) {
        float *nv = &m.normals[v * 3];
        const float l = std::sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
        if (l > 0.0f) { nv[0] /= l; nv[1] /= l; nv[2] /= l; }
    }
    return m;
}

/// Write `m` as a BINARY little-endian PLY (float xyz, nx ny nz, s t; uchar-count
/// uint32 faces) — the smallest format the import's assimp reads with normals and
/// uvs, so a 10 M-triangle asset is ~290 MB rather than a ~700 MB OBJ.
inline bool writeBinaryPly(const ShellMesh &m, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t verts = m.positions.size() / 3, faces = m.indices.size() / 3;
    std::fprintf(f,
                 "ply\nformat binary_little_endian 1.0\ncomment enginetest::proceduralShell\n"
                 "element vertex %zu\nproperty float x\nproperty float y\nproperty float z\n"
                 "property float nx\nproperty float ny\nproperty float nz\n"
                 "property float s\nproperty float t\n"
                 "element face %zu\nproperty list uchar uint vertex_indices\nend_header\n",
                 verts, faces);
    std::vector<float> row(8);
    bool ok = true;
    for (size_t v = 0; v < verts && ok; ++v) {
        for (int k = 0; k < 3; ++k) row[size_t(k)] = m.positions[v * 3 + size_t(k)];
        for (int k = 0; k < 3; ++k) row[size_t(3 + k)] = m.normals[v * 3 + size_t(k)];
        row[6] = m.uvs[v * 2];
        row[7] = m.uvs[v * 2 + 1];
        ok = std::fwrite(row.data(), sizeof(float), 8, f) == 8;
    }
    unsigned char rec[13];
    rec[0] = 3;
    for (size_t t = 0; t < faces && ok; ++t) {
        for (int k = 0; k < 3; ++k) {
            const uint32_t i = m.indices[t * 3 + size_t(k)];
            std::memcpy(rec + 1 + 4 * k, &i, 4);
        }
        ok = std::fwrite(rec, 1, 13, f) == 13;
    }
    return std::fclose(f) == 0 && ok;
}

}   // namespace enginetest
