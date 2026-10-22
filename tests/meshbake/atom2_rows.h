// ATOM-2 part A's rows and fixtures (SPECS/ATOM_2_SPEC.md stages 1-5). Included by test_mesh_bake.cpp after its CHECK
// macros, its dagroot:: fixtures and the cutcurve:: helpers; each row is a verb (`--atom2 <row>`), CPU only. The five
// below are the SHIP rows promoted at the merge; the builder's adjacency_by_position and borders_move rows are in
// archive/tests/ATOM-2/ (its locked_positions_kept row was the SHIP row bake.locked_positions_kept's law: lockedPositionsKeptLaw).
//
//   atom2.ladder_never_forced    THE LAW (ladder.h), replayed from the records: every group's measured step is within
//                                the budget it was granted, and every granted budget is at most 2x what its children
//                                carried (stored or granted); a group that cannot reduce is carried, the chain ends
//   atom2.ends_empty             a 100-triangle prop is one leaf + an empty top, vanishing past its extent; every
//                                fixture's root is non-empty before the top and the top cut draws nothing; a chain at
//                                the depth cap closes with the empty top over its whole pool (never "no DAG")
//   atom2.one_step_drift_bound   the cumulative drift from level 0 is at most 2x the stored error at every depth
//   atom2.cards_merge_within_budget  the card soup merges at the level granted its spacing; every set's measured price
//                                within the budget and stored; the quads conserve the cards' area within 10 % (D9)
//   atom2.level_table_is_a_cut   the whole-mesh level table: <= 8 uniform-depth cuts of the DAG, each bound the
//                                largest stored error at its depth, non-decreasing (D10)

#include "import/atomdag/adjacency.h"

namespace atom2
{

using iris::MeshClusterDag;

/// A closed ring prop: 10 segments around, 5 around the tube = 100 triangles; the last ring and the last segment
/// wrap BY INDEX (ENGINE.md "PROCEDURAL SEAMS DO NOT WELD"). Smooth normals, a UV per vertex.
inline iris::MeshPtr prop100()
{
    std::vector<float> p, n, uv;
    std::vector<unsigned> idx;
    const int segs = 10, sides = 5;
    const float R = 0.5f, r = 0.12f, pi = 3.14159265358979f;
    for (int s = 0; s < segs; ++s)
        for (int t = 0; t < sides; ++t) {
            const float a = 2.0f * pi * float(s) / float(segs), b = 2.0f * pi * float(t) / float(sides);
            const float cx = std::cos(a), cz = std::sin(a);
            const float nx = std::cos(b) * cx, ny = std::sin(b), nz = std::cos(b) * cz;
            p.insert(p.end(), { (R + r * std::cos(b)) * cx, r * std::sin(b), (R + r * std::cos(b)) * cz });
            n.insert(n.end(), { nx, ny, nz });
            uv.insert(uv.end(), { float(s) / float(segs), float(t) / float(sides) });
        }
    const auto id = [&](int s, int t) { return unsigned((s % segs) * sides + (t % sides)); };
    for (int s = 0; s < segs; ++s)
        for (int t = 0; t < sides; ++t)
            idx.insert(idx.end(), { id(s, t), id(s + 1, t), id(s + 1, t + 1), id(s, t), id(s + 1, t + 1), id(s, t + 1) });
    return dagroot::meshFromArrays(p, n, uv, idx);
}

/// The Bistro-units kit-bashed box set (copied from ATOM-UPGRADE's WIP fa369aa45, tests/meshbake/upgrade_rows.h): 30
/// overlapping boxes, 62.5 units a metre, every face an `n` x `n` grid of quads with its own normal and its own chart
/// in [0.05, 0.95] (a hard seam on every box edge). n = 1: 360 triangles; n = 4: 5,760.
inline iris::MeshPtr bistroBoxSet(int n)
{
    std::vector<float> p, nrm, uv;
    std::vector<unsigned> idx;
    unsigned seed = 3030u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    const int faces[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    const float unit = 62.5f;
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
    return dagroot::meshFromArrays(p, nrm, uv, idx);
}

/// A CURVED SHELL (copied from fa369aa45): a quarter sphere of `radius`, a 64 x 48 grid, smooth normals; with
/// `chartsU` x `chartsV` > 1 an ATLAS (a hard UV seam along every chart border, positions welded across it). 6,144 tris.
inline iris::MeshPtr curvedShell(float radius, int chartsU, int chartsV)
{
    std::vector<float> p, nrm, uv;
    std::vector<unsigned> idx;
    const int nu = 64, nv = 48, cw = nu / chartsU, ch = nv / chartsV;
    const float pi = 3.14159265358979f;
    std::map<std::array<int, 4>, unsigned> ids;
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
    return dagroot::meshFromArrays(p, nrm, uv, idx);
}

struct Subject { const char *name; iris::MeshPtr mesh; };

/// The stage's fixture set: the kit-bashed box set (Bistro units, n = 4), the curved shell with a 4 x 3 atlas (10 m in
/// Bistro units), the card soup, the kit-bash, the rough sheet and the 100-triangle prop.
inline std::vector<Subject> fixtures()
{
    return { { "box set (30 boxes, 4x4 faces, Bistro units)", bistroBoxSet(4) },
             { "curved shell (10 m, 4x3 atlas)", curvedShell(625.0f, 4, 3) },
             { "card soup (2,000 cards)", dagroot::cardSoup() },
             { "kit-bash (200 closed boxes)", dagroot::kitBash() },
             { "rough sheet (96 x 96, UV charts)", dagroot::roughSheet() },
             { "prop (a 100-triangle ring)", prop100() } };
}

/// The DAG's own reading: per cluster its birth level and the level its group forms at.
struct Reading
{
    std::vector<int> born, dies;
    int maxDepth = 0;
    explicit Reading(const MeshClusterDag &dag)
    {
        for (const auto &g : dag.groups) maxDepth = std::max(maxDepth, int(g.depth));
        for (const auto &c : dag.clusters) {
            born.push_back(c.refined < 0 ? 0 : int(dag.groups.at(c.refined).depth) + 1);
            dies.push_back(int(dag.groups.at(c.group).depth));
        }
    }
};

/// Weld ids by exact position (±0 folded) — the test's own, not the builder's.
inline std::vector<unsigned> weldOf(const iris::MeshPtr &mesh)
{
    size_t nv = 0; int pc = 3;
    const float *p = cutcurve::positionsOf(mesh, &nv, &pc);
    std::map<std::array<float, 3>, unsigned> first;
    std::vector<unsigned> w(nv);
    for (size_t v = 0; v < nv; ++v) {
        std::array<float, 3> k = { p[v * size_t(pc)], p[v * size_t(pc) + 1], p[v * size_t(pc) + 2] };
        for (float &f : k) if (f == 0.0f) f = 0.0f;
        w[v] = first.emplace(k, unsigned(v)).first->second;
    }
    return w;
}

/// The EXTERNAL positions of DAG group `g`, recomputed from the DAG: the endpoints of its members' edges whose twin
/// lies in a cluster of the level's pool (born at or before the level, not yet merged) outside the group.
inline std::vector<unsigned> externalOf(const MeshClusterDag &dag, const Reading &rd, const std::vector<unsigned> &weld,
                                        int g, std::vector<int> *membersOut = nullptr)
{
    const int L = dag.groups.at(g).depth;
    std::map<std::pair<unsigned, unsigned>, std::vector<int>> edgeOwners;   // directed edge -> clusters holding it
    std::vector<int> members;
    for (int c = 0; c < dag.clusters.size(); ++c) {
        if (rd.born[size_t(c)] > L || rd.dies[size_t(c)] < L) continue;
        const auto &C = dag.clusters.at(c);
        if (C.group == g) members.push_back(c);
        for (int t = 0; t < int(C.triangleCount); ++t)
            for (int k = 0; k < 3; ++k) {
                const unsigned a = weld[dag.vertexOf(C, t, k)], b = weld[dag.vertexOf(C, t, (k + 1) % 3)];
                if (a != b) edgeOwners[{ a, b }].push_back(c);
            }
    }
    std::vector<unsigned> ext;
    for (int c : members) {
        const auto &C = dag.clusters.at(c);
        for (int t = 0; t < int(C.triangleCount); ++t)
            for (int k = 0; k < 3; ++k) {
                const unsigned a = weld[dag.vertexOf(C, t, k)], b = weld[dag.vertexOf(C, t, (k + 1) % 3)];
                const auto it = edgeOwners.find({ b, a });
                if (it == edgeOwners.end()) continue;
                for (int o : it->second)
                    if (dag.clusters.at(o).group != g) { ext.push_back(a); ext.push_back(b); break; }
            }
    }
    std::sort(ext.begin(), ext.end());
    ext.erase(std::unique(ext.begin(), ext.end()), ext.end());
    if (membersOut) *membersOut = members;
    return ext;
}

/// The diagonal of a fixture's level-0 box.
inline float extentOf(const iris::MeshPtr &mesh)
{
    size_t nv = 0; int pc = 3;
    const float *p = cutcurve::positionsOf(mesh, &nv, &pc);
    const iris::IndexBufferPtr ib = mesh->getIndexBuffer();
    if (!p || ib.isNull() || !ib->data) return 0.0f;
    const unsigned *idx = reinterpret_cast<const unsigned *>(ib->data);
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t i = 0; i < size_t(ib->dataSize) / sizeof(unsigned); ++i)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p[size_t(idx[i]) * size_t(pc) + size_t(k)]);
            hi[k] = std::max(hi[k], p[size_t(idx[i]) * size_t(pc) + size_t(k)]);
        }
    return std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
}

inline int finish()
{
    if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
    else          std::printf("ALL %d CHECKS PASSED\n", checks);
    return failures ? 1 : 0;
}

inline int ladderNeverForced()
{
    int carriedAll = 0;
    for (const Subject &s : fixtures()) {
        iris::MeshBake::ClusterDagStats st;
        iris::MeshBake::ClusterDagOptions opt;
        opt.wantGroups = true;
        iris::MeshBake::buildClusterDag(s.mesh, &st, opt);
        const MeshClusterDag &dag = s.mesh->clusterDag;
        if (dag.isEmpty()) {
            char m0[200];
            std::snprintf(m0, sizeof(m0), "%s: the DAG was built (depth cap hit: %d)", s.name, int(st.depthCapHit));
            CHECK_LOUD(false, m0);
            continue;
        }
        // THE LAW (ladder.h), REPLAYED from the records in build order: every group (every pass, accepted or carried)
        //   (a) is measured within the budget it was granted — the exact walk's value at most its 5 % bound above E;
        //   (b) was granted at most 2 x max over its children of max(child's stored error, the budget the child was last
        //       granted) — a group of leaves never granted one: 2 x the position quadric's estimate (floored);
        // and an accepted group's stored error is max(its children's stored, a step within E) — read off the blob.
        const float floorLen = st.floorLen;
        std::vector<float> lastGranted(size_t(dag.clusters.size()), 0.0f);
        const auto storedOf = [&](int c) {
            const int r = dag.clusters.at(c).refined;
            return r >= 0 ? dag.groups.at(r).error : 0.0f;
        };
        int reductions = 0, carried = 0, countCarried = 0, overA = 0, overB = 0, overStored = 0;
        float worstA = 0.0f, worstB = 0.0f;
        std::map<int, int> climb;   // reductions by ceil(log2(granted / children's stored)); 0 = leaves
        for (const iris::atomdag::GroupRecord &r : st.records) {
            if (r.kind == 2) continue;   // the empty top: a vanishing priced at the mesh's extent, not a halving
            float childMax = 0.0f, childStored = 0.0f;
            for (int m : r.memberClusters) {
                childStored = std::max(childStored, storedOf(m));
                childMax = std::max(childMax, std::max(storedOf(m), lastGranted[size_t(m)]));
            }
            const float bound = std::max(childMax > 0.0f ? 2.0f * childMax : 2.0f * std::max(r.depth0Estimate, 0.0f), floorLen);
            if (bound > 0.0f) worstB = std::max(worstB, r.budget / bound);
            if (r.budget > bound * (1.0f + 1e-5f)) ++overB;
            if (r.kind == 0) {
                ++reductions;
                if (r.budget > 0.0f) worstA = std::max(worstA, r.measured / r.budget);
                if (r.step > r.budget || r.measured > r.budget * 1.05f) ++overA;
                const float e = dag.groups.at(r.dagGroup).error;
                if (e > std::max(std::max(childStored, r.budget), floorLen) * (1.0f + 1e-6f)) ++overStored;
                ++climb[childStored > 0.0f ? std::max(1, int(std::ceil(std::log2(double(r.budget / childStored)) - 1e-4))) : 0];
            }
            if (r.kind == 1) { ++carried; if (r.countCarry) ++countCarried; }
            for (int m : r.memberClusters) lastGranted[size_t(m)] = r.budget;
        }
        carriedAll += carried;
        const long long top = cutcurve::cutTop(cutcurve::fromMesh(dag));
        std::printf("== %s: %d reductions, %d carried (%d for the cluster count), %d retries, %d levels (%d passes re-run); "
                    "worst measured / granted %.3f, worst granted / 2x children %.3f; top cut %lld\n   reductions by "
                    "granted / children's stored, ceil(log2) (0 = leaves):", s.name, reductions, carried, countCarried,
                    st.retries, st.levels, st.levelsAllCarried, double(worstA), double(worstB), top);
        for (const auto &kv : climb) std::printf(" %d:%d", kv.first, kv.second);
        std::printf("\n");
        char msg[240];
        std::snprintf(msg, sizeof(msg), "%s: (a) every group's measured step within the budget it was granted (%d over)",
                      s.name, overA);
        CHECK_LOUD(overA == 0, msg);
        std::snprintf(msg, sizeof(msg), "%s: (b) every granted budget at most 2x what its children carried (%d over)",
                      s.name, overB);
        CHECK_LOUD(overB == 0, msg);
        std::snprintf(msg, sizeof(msg), "%s: every stored error is max(children's stored, a step within the granted budget) "
                      "(%d over)", s.name, overStored);
        CHECK_LOUD(overStored == 0, msg);
        std::snprintf(msg, sizeof(msg), "%s: the chain ends (the top cut draws nothing: %lld)", s.name, top);
        CHECK_LOUD(!dag.isEmpty() && top == 0, msg);
    }
    std::printf("   carried groups over the set (every pass): %d\n", carriedAll);
    CHECK_LOUD(carriedAll > 0, "some group could not reduce within its budget and was carried (the chains still ended)");
    return finish();
}

inline int endsEmpty()
{
    const iris::MeshPtr prop = prop100();
    iris::MeshBake::ClusterDagStats st;
    iris::MeshBake::buildClusterDag(prop, &st);
    const MeshClusterDag &dag = prop->clusterDag;
    const cutcurve::Dag d = cutcurve::fromMesh(dag);
    const float topError = dag.groups.isEmpty() ? 0.0f : dag.groups.last().error;
    std::printf("== prop: %d clusters, %d groups, top error %.4g (the ring's diagonal %.4g); cut below it %lld, above "
                "it %lld\n", int(dag.clusters.size()), int(dag.groups.size()), double(topError), double(st.rootError),
                cutcurve::cutAt(d, topError * 0.999f), cutcurve::cutAt(d, topError * 1.001f));
    CHECK_LOUD(dag.clusters.size() == 1 && dag.groups.size() == 1, "a 100-triangle prop is one leaf and one group");
    CHECK_LOUD(topError >= extentOf(prop) * (1.0f - 1e-6f), "the prop's top is priced at its own extent");
    CHECK_LOUD(dag.clusters.size() == 1 && dag.clusters.at(0).triangleCount == 100, "the leaf holds the 100 triangles");
    CHECK_LOUD(cutcurve::cutAt(d, topError * 0.999f) == 100, "under its extent the prop draws whole");
    CHECK_LOUD(cutcurve::cutAt(d, topError * 1.001f) == 0, "past its extent it draws nothing (the empty top)");
    for (const Subject &s : fixtures()) {
        iris::MeshBake::ClusterDagStats fs;
        iris::MeshBake::buildClusterDag(s.mesh, &fs);
        const cutcurve::Dag fd = cutcurve::fromMesh(s.mesh->clusterDag);
        std::printf("   %s: root %d triangles before the top (error %.4g), top cut %lld\n", s.name, fs.rootTriangles,
                    double(fs.rootError), cutcurve::cutTop(fd));
        char msg[200];
        std::snprintf(msg, sizeof(msg), "%s: the root is non-empty before the empty top", s.name);
        CHECK_LOUD(fs.rootTriangles > 0, msg);
        std::snprintf(msg, sizeof(msg), "%s: the top cut draws nothing", s.name);
        CHECK_LOUD(cutcurve::cutTop(fd) == 0, msg);
        const float ext = extentOf(s.mesh);
        std::snprintf(msg, sizeof(msg), "%s: the empty top is priced at the mesh's own extent (%.4g >= %.4g)", s.name,
                      double(fs.rootError), double(ext));
        CHECK_LOUD(fs.rootError >= ext * (1.0f - 1e-6f), msg);
    }
    // THE DEPTH CAP NEVER MEANS "NO DAG": the curved shell under a cap of two levels closes with the empty top over
    // every cluster its pool still holds — a DAG whose top cut draws nothing, priced at the shell's extent.
    {
        const iris::MeshPtr shell = curvedShell(625.0f, 4, 3);
        iris::MeshBake::ClusterDagStats cs;
        iris::MeshBake::ClusterDagOptions opt;
        opt.maxLevels = 2;
        iris::MeshBake::buildClusterDag(shell, &cs, opt);
        const MeshClusterDag &cd = shell->clusterDag;
        int topMembers = 0;
        if (!cd.isEmpty())
            for (const auto &c : cd.clusters) if (c.group == cd.groups.size() - 1) ++topMembers;
        const long long top = cd.isEmpty() ? -1 : cutcurve::cutTop(cutcurve::fromMesh(cd));
        std::printf("== the curved shell under a cap of 2 levels: cap hit %d, %d clusters, %d groups, the top over %d "
                    "clusters (%d triangles), error %.4g; top cut %lld\n", int(cs.depthCapHit), int(cd.clusters.size()),
                    int(cd.groups.size()), topMembers, cs.rootTriangles, double(cs.rootError), top);
        CHECK_LOUD(cs.depthCapHit, "the shell meets the cap of two levels");
        CHECK_LOUD(!cd.isEmpty(), "...and still has a DAG");
        CHECK_LOUD(topMembers > 1, "...its empty top closes over every cluster the pool still held");
        CHECK_LOUD(top == 0, "...the top cut draws nothing");
        CHECK_LOUD(cs.rootError >= extentOf(shell) * (1.0f - 1e-6f), "...and the top is priced at the shell's extent");
    }
    return finish();
}

inline int lockedPositionsKeptLaw(const std::vector<Subject> &subjects)
{
    for (const Subject &s : subjects) {
        iris::MeshBake::buildClusterDag(s.mesh);
        const MeshClusterDag &dag = s.mesh->clusterDag;
        if (dag.isEmpty()) { CHECK_LOUD(false, "the fixture has a DAG"); continue; }
        const Reading rd(dag);
        const std::vector<unsigned> weld = weldOf(s.mesh);
        int groupsChecked = 0, lost = 0, lockedTotal = 0;
        for (int g = 0; g < dag.groups.size(); ++g) {
            const std::vector<unsigned> ext = externalOf(dag, rd, weld, g);
            if (ext.empty()) continue;
            ++groupsChecked;
            lockedTotal += int(ext.size());
            std::set<unsigned> out;
            for (const auto &c : dag.clusters) {
                if (c.refined != g) continue;
                for (int t = 0; t < int(c.triangleCount); ++t) for (int k = 0; k < 3; ++k) out.insert(weld[dag.vertexOf(c, t, k)]);
            }
            for (unsigned w : ext) if (!out.count(w)) ++lost;
        }
        std::printf("== %s: %d groups with a locked border (%d positions); positions their outputs lost: %d\n", s.name,
                    groupsChecked, lockedTotal, lost);
        char msg[200];
        std::snprintf(msg, sizeof(msg), "%s: no group loses a position its level locked (%d)", s.name, lost);
        CHECK_LOUD(lost == 0, msg);
    }
    return finish();
}

inline int oneStepDriftBound()
{
    for (const Subject &s : fixtures()) {
        iris::MeshBake::buildClusterDag(s.mesh);
        const QVector<iris::MeshBake::DagDriftRow> rows = iris::MeshBake::dagDrift(s.mesh);
        int over = 0;
        float worst = 0.0f;
        std::printf("== %s: depth tris drift stored:", s.name);
        for (const auto &r : rows) {
            std::printf(" | d%d %d %.4g %.4g", r.depth, r.triangles, double(r.drift), double(r.stored));
            if (r.depth == 0) continue;   // (an EMPTY level is not skipped: everything gone is a drift too)
            if (r.stored > 0.0f) worst = std::max(worst, r.drift / r.stored);
            if (r.drift > 2.0f * r.stored * (1.0f + 1e-5f)) ++over;
        }
        std::printf("\n   worst drift / stored %.3f\n", double(worst));
        char msg[200];
        std::snprintf(msg, sizeof(msg), "%s: the drift from level 0 is within 2x the stored error at every depth (%d over)",
                      s.name, over);
        CHECK_LOUD(over == 0, msg);
    }
    return finish();
}

/// atom2.cards_merge_within_budget (stage 4, D9): THE CARD SOUP MERGES WHERE ITS BUDGET REACHES ITS SPACING — 2,000
/// 10 cm cards in a 1 m ball, and the same in a 0.35 m ball (a canopy's overlap): the first level whose card groups are granted at least the cards' spacing (the median
/// nearest-centre distance) merges; every merge's cards lie within its granted budget of their seed; every merged
/// set's MEASURED price (the sources against the quad, both ways) is within the budget and stored in the group's
/// error; the quads conserve the merged cards' area within 10 % (every group at every level, and the whole); the
/// mesh's box is recomputed over the quads; and the chain still obeys the law and ends.
/// A CANOPY-DENSE CARD SOUP (the Linde's class at scale): `count` 10 cm cards of random orientation in a ball of
/// `radius` — at 0.35 m they overlap many deep, where a quad of their summed area reaches past their own box.
inline iris::MeshPtr denseCardSoup(int count, float radius)
{
    std::vector<float> p, n, uv;
    std::vector<unsigned> idx;
    unsigned seed = 4049u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    for (int c = 0; c < count; ++c) {
        float cx, cy, cz;
        do { cx = rnd() * 2.0f - 1.0f; cy = rnd() * 2.0f - 1.0f; cz = rnd() * 2.0f - 1.0f; } while (cx * cx + cy * cy + cz * cz > 1.0f);
        cx *= radius; cy *= radius; cz *= radius;
        float nx = rnd() * 2.0f - 1.0f, ny = rnd() * 2.0f - 1.0f, nz = rnd() * 2.0f - 1.0f;
        float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl < 1e-3f) { nx = 0; ny = 1; nz = 0; nl = 1; }
        nx /= nl; ny /= nl; nz /= nl;
        const float ax = std::fabs(nx) < 0.9f ? 1.0f : 0.0f, ay = ax > 0.5f ? 0.0f : 1.0f, az = 0.0f;
        float ux = ay * nz - az * ny, uy = az * nx - ax * nz, uz = ax * ny - ay * nx;
        const float ul = std::sqrt(ux * ux + uy * uy + uz * uz);
        ux /= ul; uy /= ul; uz /= ul;
        const float vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux;
        const float h = 0.05f;
        const unsigned b = unsigned(p.size() / 3);
        const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const auto &k : corners) {
            p.insert(p.end(), { cx + h * (k[0] * ux + k[1] * vx), cy + h * (k[0] * uy + k[1] * vy), cz + h * (k[0] * uz + k[1] * vz) });
            n.insert(n.end(), { nx, ny, nz });
            uv.insert(uv.end(), { k[0] * 0.5f + 0.5f, k[1] * 0.5f + 0.5f });
        }
        idx.insert(idx.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
    return dagroot::meshFromArrays(p, n, uv, idx);
}

inline void cardsMergeCase(const char *name, const iris::MeshPtr &soup)
{
    // the spacing: the median distance from a card's centre to its nearest card's
    std::vector<iris::Vec3> centres;
    {
        size_t nv = 0;
        int pc = 3;
        const float *p = cutcurve::positionsOf(soup, &nv, &pc);
        for (size_t c = 0; c + 3 < nv; c += 4) {
            iris::Vec3 m(0, 0, 0);
            for (size_t k = 0; k < 4; ++k) m += iris::Vec3(p[(c + k) * size_t(pc)], p[(c + k) * size_t(pc) + 1], p[(c + k) * size_t(pc) + 2]);
            centres.push_back(m * 0.25f);
        }
    }
    std::vector<float> nearest;
    for (size_t i = 0; i < centres.size(); ++i) {
        float best = FLT_MAX;
        for (size_t j = 0; j < centres.size(); ++j) if (j != i) best = std::min(best, (centres[i] - centres[j]).length());
        nearest.push_back(best);
    }
    std::sort(nearest.begin(), nearest.end());
    const float spacing = nearest[nearest.size() / 2];
    const float l0Area = [&] {
        double a = 0.0;
        size_t nv = 0;
        int pc = 3;
        const float *p = cutcurve::positionsOf(soup, &nv, &pc);
        const iris::IndexBufferPtr ib = soup->getIndexBuffer();
        const unsigned *idx = reinterpret_cast<const unsigned *>(ib->data);
        for (size_t t = 0; t < size_t(ib->dataSize) / 12u; ++t) {
            const auto P = [&](unsigned v) { return iris::Vec3(p[v * size_t(pc)], p[v * size_t(pc) + 1], p[v * size_t(pc) + 2]); };
            a += 0.5 * double(iris::Vec3::crossProduct(P(idx[t * 3 + 1]) - P(idx[t * 3]), P(idx[t * 3 + 2]) - P(idx[t * 3])).length());
        }
        return float(a);
    }();
    iris::MeshBake::ClusterDagStats st;
    iris::MeshBake::ClusterDagOptions opt;
    opt.wantGroups = true;
    iris::MeshBake::buildClusterDag(soup, &st, opt);
    const MeshClusterDag &dag = soup->clusterDag;
    CHECK_LOUD(!dag.isEmpty(), "the card soup gets a DAG");
    if (dag.isEmpty()) return;
    int firstReach = -1, firstMerge = -1, farMerges = 0, overPrice = 0, unstored = 0, overCoverage = 0, overA = 0;
    double src = 0.0, quad = 0.0;
    for (const iris::atomdag::GroupRecord &r : st.records) {
        if (!r.cards) continue;
        if (firstReach < 0 && r.budget >= spacing) firstReach = r.level;
        if (r.kind == 0 && r.cardSets > 0) {
            if (firstMerge < 0) firstMerge = r.level;
            if (r.cardReach > r.budget) ++farMerges;
            if (r.cardPrice > r.budget) ++overPrice;
            if (dag.groups.at(r.dagGroup).error < r.cardPrice) ++unstored;
            if (r.step > r.budget || r.measured > r.budget * 1.05f) ++overA;
            const float q = r.cardSourceArea > 0.0f ? r.cardQuadArea / r.cardSourceArea : 0.0f;
            if (q < 0.9f || q > 1.1f) ++overCoverage;
            src += double(r.cardSourceArea);
            quad += double(r.cardQuadArea);
        }
        std::printf("   level %d pass %d kind %d granted %.4g step %.4g sets %d cards %d price %.4g reach %.4g area %.4g -> %.4g\n",
                    r.level, r.pass, r.kind, double(r.budget), double(r.step), r.cardSets, r.cardsMerged, double(r.cardPrice),
                    double(r.cardReach), double(r.cardSourceArea), double(r.cardQuadArea));
    }
    const long long top = cutcurve::cutTop(cutcurve::fromMesh(dag));
    // THE BOX IS RECOMPUTED: every vertex the DAG names lies inside the mesh's (grown) box
    int outside = 0;
    {
        size_t nvAll = 0;
        int pcAll = 3;
        const float *pa = cutcurve::positionsOf(soup, &nvAll, &pcAll);
        for (size_t v = 0; v < nvAll; ++v)
            for (int k = 0; k < 3; ++k)
                if (pa[v * size_t(pcAll) + size_t(k)] < soup->aabb.getMin()[k] ||
                    pa[v * size_t(pcAll) + size_t(k)] > soup->aabb.getMax()[k]) { ++outside; break; }
    }
    std::printf("== %s: spacing %.4g (level 0's area %.4g); the first level granted the spacing %d, the first merge at "
                "level %d; %d quads for %d cards, %zu vertices appended; coverage %.4f; vertices outside the box %d; top "
                "cut %lld\n", name, double(spacing), double(l0Area), firstReach, firstMerge, st.cardSets, st.cardsMerged,
                st.appendedVertices, src > 0.0 ? quad / src : 0.0, outside, top);
    CHECK_LOUD(firstReach >= 0 && firstMerge == firstReach,
               "the first level whose card groups are granted the cards' spacing merges (and none merges before it)");
    CHECK_LOUD(st.cardSets > 0 && st.appendedVertices == size_t(4 * st.cardSets), "each quad is four appended vertices");
    CHECK_LOUD(farMerges == 0, "every merged card lies within its group's granted budget of its seed");
    CHECK_LOUD(overPrice == 0 && overA == 0, "every merged set's measured price is within the granted budget");
    CHECK_LOUD(unstored == 0, "every merged group's stored error carries its sets' measured price");
    CHECK_LOUD(overCoverage == 0 && src > 0.0 && std::fabs(quad / src - 1.0) <= 0.1,
               "the quads conserve the merged cards' area within 10 % (every group, and the whole)");
    CHECK_LOUD(outside == 0, "the mesh's box is recomputed over the quads' corners");
    CHECK_LOUD(top == 0, "the chain ends (the top cut draws nothing)");
}

inline int cardsMergeWithinBudget()
{
    cardsMergeCase("card soup (2,000 cards in a 1 m ball)", dagroot::cardSoup());
    cardsMergeCase("dense canopy (2,000 cards in a 0.35 m ball)", denseCardSoup(2000, 0.35f));
    return finish();
}

/// atom2.level_table_is_a_cut (stage 5, D10): THE WHOLE-MESH LEVEL TABLE IS UNIFORM-DEPTH CUTS OF THE DAG — on every
/// fixture (the card soup's merged quads included) the table holds at most eight levels; every level is EXACTLY the triangles of the
/// clusters alive at one depth (born at or before it, their own group forming at or after it: the builder's pool
/// there, so its borders are the locked ones — atom2.locked_positions_kept); its bound is the largest stored error at
/// that depth (never more than the DAG stores there); the bounds are positive and non-decreasing and the levels
/// strictly fewer triangles each; every index names a vertex of the mesh.
inline int levelTableIsACut()
{
    for (const Subject &s : fixtures()) {
        iris::MeshBake::buildClusterDag(s.mesh);
        iris::MeshBake::buildLevelTable(s.mesh);
        const MeshClusterDag &dag = s.mesh->clusterDag;
        if (dag.isEmpty()) { CHECK_LOUD(false, "the fixture has a DAG"); continue; }
        const Reading rd(dag);
        const auto sorted = [](std::vector<std::array<quint32, 3>> v) { std::sort(v.begin(), v.end()); return v; };
        // every depth's cut, its triangles (each sorted by corner, the list sorted) and its largest stored error
        std::vector<std::vector<std::array<quint32, 3>>> cuts(size_t(rd.maxDepth) + 1);
        std::vector<float> storedAt(size_t(rd.maxDepth) + 1, 0.0f);
        for (int c = 0; c < dag.clusters.size(); ++c) {
            const auto &cl = dag.clusters.at(c);
            for (int d = rd.born[size_t(c)]; d <= rd.dies[size_t(c)]; ++d) {
                if (cl.refined >= 0) storedAt[size_t(d)] = std::max(storedAt[size_t(d)], dag.groups.at(cl.refined).error);
                for (int t = 0; t < int(cl.triangleCount); ++t)
                    cuts[size_t(d)].push_back({ dag.vertexOf(cl, t, 0), dag.vertexOf(cl, t, 1), dag.vertexOf(cl, t, 2) });
            }
        }
        for (auto &cut : cuts) cut = sorted(cut);
        size_t nv = 0;
        int pc = 3;
        cutcurve::positionsOf(s.mesh, &nv, &pc);
        int notACut = 0, overStored = 0, badBounds = 0, badCounts = 0, badIndex = 0;
        float prevBound = 0.0f;
        int prevTris = -1, lastDepth = 0;
        std::printf("== %s: DAG depth %d, %d levels:", s.name, rd.maxDepth, int(s.mesh->lodIndices.size()));
        for (int i = 0; i < s.mesh->lodIndices.size(); ++i) {
            const QVector<quint32> &idx = s.mesh->lodIndices.at(i);
            std::vector<std::array<quint32, 3>> tris;
            for (int t = 0; t + 2 < idx.size(); t += 3) tris.push_back({ idx[t], idx[t + 1], idx[t + 2] });
            for (quint32 v : idx) if (size_t(v) >= nv) ++badIndex;
            tris = sorted(tris);
            int depth = -1;
            for (int d = lastDepth + 1; d <= rd.maxDepth && depth < 0; ++d) if (cuts[size_t(d)] == tris) depth = d;
            const float bound = s.mesh->lodBounds.at(i);
            if (depth < 0) ++notACut;
            else {
                lastDepth = depth;
                if (bound > storedAt[size_t(depth)] * (1.0f + 1e-6f) || bound < storedAt[size_t(depth)] * (1.0f - 1e-6f))
                    ++overStored;
            }
            if (!(bound > 0.0f) || bound < prevBound) ++badBounds;
            if (prevTris >= 0 && int(tris.size()) >= prevTris) ++badCounts;
            prevBound = bound;
            prevTris = int(tris.size());
            std::printf(" [d%d %zu tris, bound %.4g]", depth, tris.size(), double(bound));
        }
        std::printf("\n");
        char msg[240];
        std::snprintf(msg, sizeof(msg), "%s: at most %d levels, at least one when the DAG has a level", s.name,
                      iris::MeshBake::kLevelTableMax);
        CHECK_LOUD(s.mesh->lodIndices.size() <= iris::MeshBake::kLevelTableMax &&
                       (rd.maxDepth < 2 || !s.mesh->lodIndices.isEmpty()), msg);
        std::snprintf(msg, sizeof(msg), "%s: every level is a uniform-depth cut of the DAG (%d not)", s.name, notACut);
        CHECK_LOUD(notACut == 0, msg);
        std::snprintf(msg, sizeof(msg), "%s: every bound is the largest stored error at its depth (%d not)", s.name, overStored);
        CHECK_LOUD(overStored == 0, msg);
        std::snprintf(msg, sizeof(msg), "%s: the bounds positive and non-decreasing, the levels fewer triangles each, "
                      "every index a vertex (%d / %d / %d)", s.name, badBounds, badCounts, badIndex);
        CHECK_LOUD(badBounds == 0 && badCounts == 0 && badIndex == 0, msg);
    }
    return finish();
}

inline int run(const QString &row)
{
    if (row == QStringLiteral("ladder_never_forced")) return ladderNeverForced();
    if (row == QStringLiteral("ends_empty")) return endsEmpty();
    if (row == QStringLiteral("one_step_drift_bound")) return oneStepDriftBound();
    if (row == QStringLiteral("cards_merge_within_budget")) return cardsMergeWithinBudget();
    if (row == QStringLiteral("level_table_is_a_cut")) return levelTableIsACut();
    std::printf("FAIL: --atom2 %s: no such row\n", qUtf8Printable(row));
    return 1;
}

}   // namespace atom2
