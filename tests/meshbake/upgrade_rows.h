// THE ATOM-UPGRADE ROWS AND THEIR FIXTURES (SPECS/ATOM_UPGRADE_SPEC.md; test_mesh_bake's `--cut-curve` and the rows
// bake.cut_curve, bake.objective_is_geometry, bake.terms_exact). Public MeshBake API only (buildClusterDag and its
// stats), so the same file measures any tree's bake — the base's "red today" included.
#ifndef TESTS_MESHBAKE_UPGRADE_ROWS_H
#define TESTS_MESHBAKE_UPGRADE_ROWS_H

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <QElapsedTimer>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/import/meshbake.h"
#include "cutcurve_stock.h"

namespace cutcurve
{

using Dag = StockDag;

inline Dag fromMesh(const iris::MeshClusterDag &dag)
{
    Dag d;
    for (const auto &g : dag.groups) { d.groupError.push_back(g.error); d.groupDepth.push_back(g.depth); }
    for (const auto &c : dag.clusters) {
        d.clusterGroup.push_back(c.group);
        d.clusterRefined.push_back(c.refined);
        d.clusterTriangles.push_back(int(c.triangleCount));
    }
    return d;
}

/// The triangles drawn when a group is affordable iff `afford(error)` — the runtime's rule (JahClusterCut.glsl): a
/// cluster is drawn iff its own group is NOT affordable and it is a leaf or its producer IS.
template <typename F> long long cut(const Dag &d, F afford)
{
    long long tris = 0;
    for (size_t c = 0; c < d.clusterGroup.size(); ++c) {
        const int g = d.clusterGroup[c], r = d.clusterRefined[c];
        if (g < 0 || size_t(g) >= d.groupError.size() || afford(d.groupError[size_t(g)])) continue;
        if (r < 0 || (size_t(r) < d.groupError.size() && afford(d.groupError[size_t(r)]))) tris += d.clusterTriangles[c];
    }
    return tris;
}
inline long long cutAt(const Dag &d, float E) { return cut(d, [E](float e) { return e < E; }); }
/// "top": every finite group affordable — 0 when the chain ends in an empty group, else its terminal clusters.
inline long long cutTop(const Dag &d) { return cut(d, [](float e) { return e != FLT_MAX; }); }

inline const float *positionsOf(const iris::MeshPtr &m, size_t *nv, int *comps)
{
    for (const iris::VertexBufferPtr &vb : m->getVertexBuffers()) {
        if (!vb || !vb->data || vb->dataSize <= 0) continue;
        const QList<iris::VertexAttribute> a = vb->vertexLayout.getAttribs();
        if (a.isEmpty() || a.first().usage != iris::VertexAttribUsage::Position) continue;
        if (a.size() != 1 || a.first().count < 3) return nullptr;   // interleaved: not a layout the bake reads
        *comps = a.first().count;
        *nv = size_t(vb->dataSize) / (sizeof(float) * size_t(*comps));
        return reinterpret_cast<const float *>(vb->data);
    }
    return nullptr;
}

inline iris::MeshPtr meshFrom(const std::vector<float> &positions, const std::vector<float> &normals,
                              const std::vector<float> &uvs, const std::vector<unsigned> &indices)
{
    iris::MeshPtr m = iris::Mesh::create();
    const auto add = [&](iris::VertexAttribUsage usage, const std::vector<float> &v, int comps) {
        if (v.empty()) return;
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(v.data()), unsigned(v.size() * sizeof(float)));
        m->addVertexBuffer(vb);
    };
    add(iris::VertexAttribUsage::Position, positions, 3);
    add(iris::VertexAttribUsage::Normal, normals, 3);
    add(iris::VertexAttribUsage::TexCoord0, uvs, 2);
    auto ib = iris::IndexBuffer::create();
    ib->setData(const_cast<unsigned *>(indices.data()), unsigned(indices.size() * sizeof(unsigned)));
    m->setIndexBuffer(ib);
    m->usesIndexBuffer = true;
    m->numVerts = int(positions.size() / 3);
    m->numFaces = int(indices.size() / 3);
    m->primitiveMode = iris::PrimitiveMode::Triangles;
    return m;
}

/// The POSONLY arm's subject: the mesh's positions and indices, nothing else (the simplifier sees no attribute, so
/// clusterlod's estimate is the position quadric).
inline iris::MeshPtr positionsOnly(const iris::MeshPtr &src)
{
    size_t nv = 0;
    int pc = 3;
    const float *p = positionsOf(src, &nv, &pc);
    const iris::IndexBufferPtr sib = src->getIndexBuffer();
    if (!p || sib.isNull() || !sib->data) return iris::MeshPtr();
    std::vector<float> xyz(nv * 3);
    for (size_t v = 0; v < nv; ++v) for (int k = 0; k < 3; ++k) xyz[v * 3 + size_t(k)] = p[v * size_t(pc) + size_t(k)];
    const unsigned *idx = reinterpret_cast<const unsigned *>(sib->data);
    return meshFrom(xyz, {}, {}, std::vector<unsigned>(idx, idx + size_t(sib->dataSize) / sizeof(unsigned)));
}

/// 30 OVERLAPPING BOXES WITH HARD UV SEAMS (the spec's kit-bashed box set), IN BISTRO'S UNITS (1 unit = 0.016 m, the
/// FBX's node scale, so a metre is 62.5 units) with BISTRO'S ATLAS (every face's chart in [0.05, 0.95]: UV range ~1,
/// like the facades' 0.97-34): sizes 0.2-1 m, centres inside a 1.5 m cube so the boxes interpenetrate like kit-bashed
/// facade pieces; every face its own `n` x `n` grid of quads with its own normal and its own chart (a hard seam on every
/// box edge), deterministic. n = 1: 360 triangles (one group at depth 0); n = 4: 5,760 (in-face collapses cost no
/// geometry, edge collapses wreck a box — the ranking's choice). The units matter to the base's metric only (its UV
/// weight was 0.5 x meshExtent / uvRange in group-extent units — the scale squared); the stage-1 tie-break is
/// scale-free.
inline iris::MeshPtr kitBashedBoxSet(int n = 1)
{
    std::vector<float> p, nrm, uv;
    std::vector<unsigned> idx;
    unsigned seed = 3030u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    const int faces[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    const float unit = 62.5f;   // mesh units per metre
    for (int b = 0; b < 30; ++b) {
        const float c[3] = { unit * 1.5f * rnd(), unit * 1.5f * rnd(), unit * 1.5f * rnd() };
        const float h[3] = { unit * (0.1f + 0.4f * rnd()), unit * (0.1f + 0.4f * rnd()), unit * (0.1f + 0.4f * rnd()) };
        for (int f = 0; f < 6; ++f) {
            const int *fa = faces[f];
            const int axis = fa[0] ? 0 : (fa[1] ? 1 : 2);
            const int a = (axis + 1) % 3, bb = (axis + 2) % 3;
            const unsigned base = unsigned(p.size() / 3);
            for (int j = 0; j <= n; ++j)
                for (int i = 0; i <= n; ++i) {
                    const float s = 2.0f * float(i) / float(n) - 1.0f, t = 2.0f * float(j) / float(n) - 1.0f;
                    float v[3] = { c[0], c[1], c[2] };
                    v[axis] += h[axis] * float(fa[axis]);
                    v[a] += h[a] * s;
                    v[bb] += h[bb] * t;
                    p.insert(p.end(), { v[0], v[1], v[2] });
                    nrm.insert(nrm.end(), { float(fa[0]), float(fa[1]), float(fa[2]) });
                    uv.insert(uv.end(), { 0.5f + 0.45f * s, 0.5f + 0.45f * t });
                }
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) {
                    const unsigned q = base + unsigned(j * (n + 1) + i);
                    idx.insert(idx.end(), { q, q + 1, q + unsigned(n) + 2, q, q + unsigned(n) + 2, q + unsigned(n) + 1 });
                }
        }
    }
    return meshFrom(p, nrm, uv, idx);
}

/// A CURVED SHELL: a quarter of a sphere of radius `radius` (90 degrees of longitude, 80 of latitude about the equator),
/// a 64 x 48 grid, smooth normals; with `chartsU` x `chartsV` > 1 the UV is an ATLAS of that many charts, each in
/// [0.05, 0.95] (Bistro's facades: a hard UV seam along every chart border, the positions welded across it), else one
/// continuous chart. Curvature everywhere, no island. 6,144 triangles.
inline iris::MeshPtr curvedShell(float radius = 1.0f, int chartsU = 1, int chartsV = 1)
{
    std::vector<float> p, nrm, uv;
    std::vector<unsigned> idx;
    const int nu = 64, nv = 48, cw = nu / chartsU, ch = nv / chartsV;
    const float pi = 3.14159265358979f;
    std::map<std::array<int, 4>, unsigned> ids;   // (i, j, chart u, chart v) -> vertex: a chart border's twins
    const auto vertex = [&](int i, int j, int ci, int cj) {
        const std::array<int, 4> key = { i, j, ci, cj };
        const auto it = ids.find(key);
        if (it != ids.end()) return it->second;
        const float lon = (float(i) / float(nu) - 0.5f) * 0.5f * pi, lat = (float(j) / float(nv) - 0.5f) * (80.0f / 180.0f) * pi;
        const float x = std::cos(lat) * std::cos(lon), y = std::sin(lat), z = std::cos(lat) * std::sin(lon);
        p.insert(p.end(), { radius * x, radius * y, radius * z });
        nrm.insert(nrm.end(), { x, y, z });
        if (chartsU * chartsV > 1)
            uv.insert(uv.end(), { 0.05f + 0.9f * float(i - ci * cw) / float(cw), 0.05f + 0.9f * float(j - cj * ch) / float(ch) });
        else
            uv.insert(uv.end(), { float(i) / float(nu), float(j) / float(nv) });
        const unsigned id = unsigned(p.size() / 3 - 1);
        ids.emplace(key, id);
        return id;
    };
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const int ci = i / cw, cj = j / ch;
            const unsigned a = vertex(i, j, ci, cj), b = vertex(i + 1, j, ci, cj), c = vertex(i + 1, j + 1, ci, cj),
                           d = vertex(i, j + 1, ci, cj);
            idx.insert(idx.end(), { a, b, c, a, c, d });
        }
    return meshFrom(p, nrm, uv, idx);
}

/// The rows' own tally (the file's CHECK macros live in test_mesh_bake.cpp; this header stands alone).
struct Tally
{
    int checks = 0, failures = 0;
    void check(bool ok, const char *msg)
    {
        ++checks;
        std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", msg);
        if (!ok) ++failures;
    }
    int finish() const
    {
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
};

inline float extentOf(const iris::MeshPtr &mesh)
{
    size_t nv = 0; int pc = 3;
    const float *p = positionsOf(mesh, &nv, &pc);
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t v = 0; p && v < nv; ++v)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p[v * size_t(pc) + size_t(k)]);
            hi[k] = std::max(hi[k], p[v * size_t(pc) + size_t(k)]);
        }
    return std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
}

/// bake.cut_curve (stage 0): THE CUT CURVE IS MONOTONE AND ENDS EMPTY — on the kit-bashed box set the shipped DAG's
/// cut at E = extent x 2^-k (k = 12 .. 0) never grows as E grows, and once every finite group is affordable it draws
/// nothing (the empty top: a thing under a pixel stops drawing — stage 5's law). The curve is printed.
inline int cutCurveLaw()
{
    Tally tally;
    const iris::MeshPtr mesh = kitBashedBoxSet(1);
    const float extent = extentOf(mesh);
    iris::MeshBake::ClusterDagStats st;
    QElapsedTimer t; t.start();
    iris::MeshBake::buildClusterDag(mesh, &st);
    const Dag d = fromMesh(mesh->clusterDag);
    const long long l0 = (long long)(mesh->getIndexBuffer()->dataSize / 12);
    std::printf("== kit-bashed box set: %lld tris, extent %.3f, %zu groups, %zu clusters, %lld ms\n", l0, double(extent),
                d.groupError.size(), d.clusterGroup.size(), qint64(t.elapsed()));
    std::printf("   cut curve (E / extent -> triangles):");
    bool monotone = true;
    long long prev = -1;
    for (int k = 12; k >= 0; --k) {
        const long long c = d.clusterGroup.empty() ? l0 : cutAt(d, extent / float(1 << k));
        std::printf(" 2^-%d %lld", k, c);
        if (prev >= 0 && c > prev) monotone = false;
        prev = c;
    }
    const long long top = d.clusterGroup.empty() ? l0 : cutTop(d);
    if (prev >= 0 && top > prev) monotone = false;
    std::printf(" | top %lld\n", top);
    char msg[200];
    tally.check(!d.clusterGroup.empty(), "the box set has a DAG");
    tally.check(monotone, "the cut curve never grows as E grows");
    std::snprintf(msg, sizeof(msg), "the curve ends at the empty top (top cut %lld triangles == 0)", top);
    tally.check(top == 0, msg);
    return tally.finish();
}

/// Per depth: the largest stored error and the largest clusterlod estimate of a DAG's groups (finite ones).
inline void perDepth(const iris::MeshClusterDag &dag, std::map<int, float> &stored, std::map<int, float> &estimate)
{
    for (const auto &g : dag.groups) {
        if (g.error == FLT_MAX) continue;
        stored[g.depth] = std::max(stored[g.depth], g.error);
        if (g.estimate != FLT_MAX) estimate[g.depth] = std::max(estimate[g.depth], g.estimate);
    }
}

/// bake.objective_is_geometry (stage 1): THE RANKING CANNOT WRECK GEOMETRY THE QUADRIC PRICES CHEAP — two subjects in
/// Bistro's units (62.5 per metre) with Bistro's atlas (UV range ~1): the kit-bashed box set with 2 x 2 faces (islands
/// and hard seams: the prune decides most of it) and a curved facade (the curved shell at 10 m radius with an 8 x 6
/// atlas: the collapses decide, and a UV-led order keeps the chart borders and spends the curvature). Each bakes as
/// shipped and positions-only (the POSONLY arm: clusterlod's estimate IS the position quadric of a geometric ranking),
/// and at every depth the shipped DAG's largest stored error is at most kDagLockFactor (2) x the position quadric's.
/// Red at the base on the facade (35x at depth 0: the UV weight, 0.5 x meshExtent / uvRange in group-extent units, made
/// the order a UV order); the box set passes at the base too.
inline int objectiveIsGeometry()
{
    Tally tally;
    struct Subject { const char *name; iris::MeshPtr mesh; };
    const Subject subjects[] = { { "kit-bashed box set 2x2", kitBashedBoxSet(2) },
                                 { "curved facade (10 m shell, 8x6 atlas)", curvedShell(625.0f, 8, 6) } };
    for (const Subject &sj : subjects) {
        const iris::MeshPtr pos = positionsOnly(sj.mesh);
        QElapsedTimer t; t.start();
        iris::MeshBake::ClusterDagStats sa, sb;
        iris::MeshBake::buildClusterDag(sj.mesh, &sa);
        iris::MeshBake::buildClusterDag(pos, &sb);
        std::map<int, float> storedA, estA, storedB, estB;
        perDepth(sj.mesh->clusterDag, storedA, estA);
        perDepth(pos->clusterDag, storedB, estB);
        std::printf("== %s (%d tris): shipped %d groups, positions-only %d groups, %lld ms\n", sj.name,
                    int(sj.mesh->getIndexBuffer()->dataSize / 12), int(sj.mesh->clusterDag.groups.size()),
                    int(pos->clusterDag.groups.size()), qint64(t.elapsed()));
        std::printf("   depth  shipped-stored  posonly-quadric  posonly-stored  ratio\n");
        int over = 0, compared = 0;
        float worst = 0.0f;
        for (const auto &kv : storedA) {
            const auto q = estB.find(kv.first);
            if (q == estB.end() || !(q->second > 0.0f)) continue;
            const float r = kv.second / q->second;
            ++compared;
            worst = std::max(worst, r);
            if (r > 2.0f) ++over;
            std::printf("   %5d  %14.5f  %15.5f  %14.5f  %5.2f%s\n", kv.first, double(kv.second), double(q->second),
                        double(storedB.count(kv.first) ? storedB[kv.first] : -1.0f), double(r), r > 2.0f ? "  OVER" : "");
        }
        char msg[200];
        std::snprintf(msg, sizeof(msg), "%s: every depth's stored error <= 2x the position quadric's (%d of %d over, "
                      "worst %.2fx)", sj.name, over, compared, double(worst));
        tally.check(compared > 0 && over == 0, msg);
    }
    return tally.finish();
}

/// bake.terms_exact (stage 1): THE TERMS ARE EXACT — on the curved shell every group's own measured bound is within
/// 1.1x of the dense reference (8x the samples against the whole level 0, the reference pattern on every facet, no
/// margin). Red at the base: the sampled terms carried the 1.25 margin (1.19-1.22x on Bistro).
inline int termsExact()
{
    Tally tally;
    const iris::MeshPtr mesh = curvedShell();
    QElapsedTimer t; t.start();
    iris::MeshBake::ClusterDagStats st;
    st.denseReference = true;
    iris::MeshBake::buildClusterDag(mesh, &st);
    std::vector<float> r;
    for (const auto &g : st.groupTerms)
        if (g.reference > 0.0f && g.bound != FLT_MAX && g.bound > 0.0f) r.push_back(g.bound / g.reference);
    std::sort(r.begin(), r.end());
    std::printf("== curved shell (%d tris): %d groups, %zu with a reference, %lld ms; bound / dense reference median %.3f, "
                "max %.3f\n", int(mesh->getIndexBuffer()->dataSize / 12), int(mesh->clusterDag.groups.size()), r.size(),
                qint64(t.elapsed()), r.empty() ? 0.0 : double(r[r.size() / 2]), r.empty() ? 0.0 : double(r.back()));
    char msg[200];
    std::snprintf(msg, sizeof(msg), "every group's bound within 1.1x of the dense reference (max %.3f)",
                  r.empty() ? 0.0 : double(r.back()));
    tally.check(!r.empty() && r.back() <= 1.1f, msg);
    return tally.finish();
}

}   // namespace cutcurve

#endif
