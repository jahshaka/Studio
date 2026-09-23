// atom.cluster_cut — THE CLUSTER CUT IS ONE CUT (ATOM stage 2, lane
// ATOM-CLUSTER-1; SPECS/atom/B2_CLUSTER_DAG_DESIGN.md §2).
//
// The rule (jahshaka/engine/Types.h `clusterCut`) is only worth anything if,
// over the DAG the bake builds, it selects EXACTLY ONE frontier: every level-0
// triangle stood for by exactly one drawn cluster (no hole = no crack, no
// overlap = no z-fight), at every threshold. This suite asserts that over the
// shipped meshes, document-side — no engine, no display:
//
//   1. THE BAKE'S TWO MONOTONICITIES hold on what the bake stored: every group's
//      measured error >= every child group's, and every group's sphere contains
//      every child group's (the two facts the rule's one-cut proof needs).
//   2. THE PROVENANCE IS A PARTITION: the leaves' regions cover every level-0
//      triangle exactly once (the bake's per-cluster region is what (3) counts).
//   3. EXACTLY ONE CUT at 64 thresholds from "every leaf" (allowed 0) to "the
//      root only" (above every finite error): each level-0 triangle is covered
//      by exactly one selected cluster's region.
//   4. MONOTONE REFINEMENT: raising the threshold never selects a cluster whose
//      parent (a cluster produced by simplifying its group) was selected at a
//      lower threshold.
//   5. THE VIEW-DEPENDENT CUT is one cut too — each group at its own distance,
//      from eyes inside, beside and far from the mesh, at four tolerances. This
//      is where the sphere containment of (1) is load-bearing.
//   6. THE FORMAT round trip: a baked blob reads back to the identical DAG, and
//      a blob whose DAG is not monotone is REFUSED.
#include "cluster_fixtures.h"

#include "irisgl/import/meshbake.h"

#include <QCoreApplication>
#include <QDataStream>

#include <algorithm>
#include <array>
#include <map>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (cond) { std::printf("ok: "); std::printf(__VA_ARGS__); }                       \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); ++failures; }              \
        std::printf("\n");                                                                 \
    } while (0)

/// How many selected clusters' regions cover each level-0 triangle; returns the
/// count of triangles NOT covered exactly once (and the worst over-coverage).
static size_t coverageDefects(const clusterfix::Fixture &f, const std::vector<unsigned> &cut,
                              unsigned *worstOut = nullptr)
{
    std::vector<unsigned> cover(f.triangles, 0u);
    for (unsigned c : cut)
        for (quint32 t : f.stats.clusterRegions.at(int(c)))
            if (t < cover.size()) ++cover[t];
    size_t bad = 0;
    unsigned worst = 0;
    for (unsigned n : cover) {
        if (n != 1u) ++bad;
        worst = std::max(worst, n);
    }
    if (worstOut) *worstOut = worst;
    return bad;
}

/// THE COMBINATORIAL CRACK TEST. Positions are welded exactly (the header's own
/// connectivity: meshopt_generatePositionRemap's equality); an edge of a triangle
/// list is OPEN when it is used by exactly one triangle. A crack is an open edge
/// of the CUT that is not on level 0's own open border — i.e. one whose two ends
/// are not both level-0 border vertices. A watertight level 0 has no border at
/// all, so there every open edge of the cut is a crack.
struct Welder
{
    std::vector<unsigned> canon;               // vertex -> canonical vertex
    std::vector<unsigned char> border;         // canonical vertex on level 0's open border
    explicit Welder(const MeshData &d)
    {
        const size_t nv = d.positions.size() / 3;
        canon.resize(nv);
        std::map<std::array<float, 3>, unsigned> first;
        for (size_t v = 0; v < nv; ++v) {
            const std::array<float, 3> k{ d.positions[v * 3], d.positions[v * 3 + 1], d.positions[v * 3 + 2] };
            canon[v] = first.emplace(k, unsigned(v)).first->second;
        }
        border.assign(nv, 0);
        for (const auto &e : openEdges(d.indices))
            border[e.first] = border[e.second] = 1;
    }
    std::vector<std::pair<unsigned, unsigned>> openEdges(const std::vector<unsigned> &idx) const
    {
        std::map<std::pair<unsigned, unsigned>, int> uses;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            const unsigned a = canon[idx[t]], b = canon[idx[t + 1]], c = canon[idx[t + 2]];
            if (a == b || b == c || a == c) continue;          // a degenerate (pole) triangle
            for (auto e : { std::make_pair(a, b), std::make_pair(b, c), std::make_pair(c, a) })
                ++uses[std::minmax(e.first, e.second)];
        }
        std::vector<std::pair<unsigned, unsigned>> out;
        for (const auto &kv : uses) if (kv.second == 1) out.push_back(kv.first);
        return out;
    }
    size_t cracks(const std::vector<unsigned> &idx) const
    {
        size_t n = 0;
        for (const auto &e : openEdges(idx)) if (!(border[e.first] && border[e.second])) ++n;
        return n;
    }
};

static std::vector<unsigned> cutIndices(const MeshData &d, const std::vector<unsigned> &cut)
{
    std::vector<unsigned> idx;
    for (unsigned c : cut)
        idx.insert(idx.end(), d.clusterIndices.begin() + d.clusters[c].firstIndex,
                   d.clusterIndices.begin() + d.clusters[c].firstIndex + d.clusters[c].indexCount);
    return idx;
}

static void oneMesh(clusterfix::Fixture &f)
{
    const auto &groups = f.data.clusterGroups;
    const auto &clusters = f.data.clusters;
    std::printf("\n-- %s: %zu triangles, %zu clusters, %zu groups, depth %d, %d terminal; "
                "bake fixes: %d monotone, %d sphere; measured under clusterlod's estimate in "
                "%d groups\n",
                f.name.c_str(), f.triangles, clusters.size(), groups.size(), f.stats.depth,
                f.stats.terminalGroups, f.stats.monotoneFixes, f.stats.sphereFixes,
                f.stats.measuredBelowEstimate);

    // The measured error per DAG depth (the evidence for the bake's numbers).
    {
        std::map<int, std::pair<float, float>> byDepth;
        std::map<int, int> count;
        for (const MeshClusterGroup &g : groups) {
            if (g.error >= FLT_MAX) continue;
            auto it = byDepth.find(g.depth);
            if (it == byDepth.end()) byDepth[g.depth] = { g.error, g.error };
            else { it->second.first = std::min(it->second.first, g.error); it->second.second = std::max(it->second.second, g.error); }
            ++count[g.depth];
        }
        std::printf("   measured error by depth (mesh units, extent %.3g):", double(f.extent));
        for (const auto &kv : byDepth)
            std::printf(" d%d[%d] %.3g..%.3g", kv.first, count[kv.first], double(kv.second.first),
                        double(kv.second.second));
        std::printf("\n");
    }

    // ---- 1. the stored monotonicities ------------------------------------
    size_t errorBreaks = 0, sphereBreaks = 0;
    for (const MeshCluster &c : clusters) {
        if (c.refined < 0) continue;
        const MeshClusterGroup &p = groups[size_t(c.group)], &k = groups[size_t(c.refined)];
        if (p.error < k.error) ++errorBreaks;
        const float dx = p.centre[0] - k.centre[0], dy = p.centre[1] - k.centre[1],
                    dz = p.centre[2] - k.centre[2];
        if (p.radius < std::sqrt(dx * dx + dy * dy + dz * dz) + k.radius) ++sphereBreaks;
    }
    CHECK(errorBreaks == 0, "%s: every group's measured error >= every child group's (%zu breaks)",
          f.name.c_str(), errorBreaks);
    CHECK(sphereBreaks == 0, "%s: every group's sphere contains every child group's (%zu breaks)",
          f.name.c_str(), sphereBreaks);

    // ---- 2. the leaves partition level 0 ---------------------------------
    std::vector<unsigned> leaves;
    for (size_t c = 0; c < clusters.size(); ++c) if (clusters[c].refined < 0) leaves.push_back(unsigned(c));
    CHECK(coverageDefects(f, leaves) == 0, "%s: the %zu leaf clusters stand for every level-0 "
          "triangle exactly once", f.name.c_str(), leaves.size());

    // ---- 3 + 4. 64 thresholds -------------------------------------------
    float minErr = FLT_MAX, maxErr = 0.0f;
    for (const MeshClusterGroup &g : groups)
        if (g.error < FLT_MAX) { minErr = std::min(minErr, g.error); maxErr = std::max(maxErr, g.error); }
    std::vector<float> thresholds{ 0.0f };
    if (maxErr > 0.0f) {
        const float lo = minErr * 0.5f, hi = maxErr * 2.0f;
        for (int i = 0; i < 63; ++i)
            thresholds.push_back(lo * std::pow(hi / lo, float(i) / 62.0f));
    }
    std::vector<std::vector<unsigned>> cuts(thresholds.size());
    size_t badThresholds = 0, worstBad = 0;
    std::vector<size_t> tris(thresholds.size());
    for (size_t i = 0; i < thresholds.size(); ++i) {
        tris[i] = clusterCutAtAllowed(groups, clusters, thresholds[i], cuts[i]);
        const size_t bad = coverageDefects(f, cuts[i]);
        if (bad) { ++badThresholds; worstBad = std::max(worstBad, bad); }
    }
    CHECK(thresholds.size() == 64u && badThresholds == 0,
          "%s: EXACTLY ONE CUT at all %zu thresholds (%zu thresholds with a triangle covered "
          "0 or 2+ times; worst %zu triangles)", f.name.c_str(), thresholds.size(), badThresholds,
          worstBad);
    CHECK(cuts.front() == leaves, "%s: allowed 0 selects exactly the leaves (level 0)", f.name.c_str());
    bool rootOnly = true;
    for (unsigned c : cuts.back())
        if (groups[size_t(clusters[c].group)].error < FLT_MAX) rootOnly = false;
    CHECK(rootOnly, "%s: above every finite error only terminal groups' clusters are drawn "
          "(%zu clusters, %zu triangles)", f.name.c_str(), cuts.back().size(), tris.back());
    bool trianglesFall = true;
    for (size_t i = 1; i < tris.size(); ++i) if (tris[i] > tris[i - 1]) trianglesFall = false;
    CHECK(trianglesFall, "%s: the cut's triangle count never rises with the threshold "
          "(%zu -> %zu)", f.name.c_str(), tris.front(), tris.back());

    // THE COMBINATORIAL CRACK TEST over every threshold's cut, and the CHAIN's
    // levels printed beside it (the chain is not this lane's, but it is the same
    // simplifier under the same attribute policy, so its number says whether a
    // seam crack is the DAG's or the policy's).
    {
        const Welder w(f.data);
        size_t worstCut = 0, cutsWithCracks = 0;
        float worstAt = 0.0f;
        for (size_t i = 0; i < thresholds.size(); ++i) {
            const size_t n = w.cracks(cutIndices(f.data, cuts[i]));
            if (n) ++cutsWithCracks;
            if (n > worstCut) { worstCut = n; worstAt = thresholds[i]; }
        }
        std::printf("   level 0: %zu open edges (its own border); the chain's levels' crack edges:",
                    w.openEdges(f.data.indices).size());
        for (const auto &level : f.data.lodIndices) std::printf(" %zu", w.cracks(level));
        std::printf("\n");
        CHECK(worstCut == 0, "%s: NO CRACK EDGE in the cut at any of the %zu thresholds (%zu cuts "
              "with one; worst %zu open edges off level 0's border, at allowed %.4g)", f.name.c_str(),
              thresholds.size(), cutsWithCracks, worstCut, double(worstAt));
    }

    // MONOTONE REFINEMENT. The parents of cluster c are the clusters its group
    // produced (refined == c.group). Once a parent is drawn at threshold i, no
    // threshold j > i may draw c again.
    std::vector<std::vector<unsigned>> parentsOf(clusters.size());
    std::vector<std::vector<unsigned>> producedBy(groups.size());
    for (size_t c = 0; c < clusters.size(); ++c)
        if (clusters[c].refined >= 0) producedBy[size_t(clusters[c].refined)].push_back(unsigned(c));
    for (size_t c = 0; c < clusters.size(); ++c) parentsOf[c] = producedBy[size_t(clusters[c].group)];
    std::vector<int> firstDrawn(clusters.size(), -1), lastDrawn(clusters.size(), -1);
    for (size_t i = 0; i < cuts.size(); ++i)
        for (unsigned c : cuts[i]) {
            if (firstDrawn[c] < 0) firstDrawn[c] = int(i);
            lastDrawn[c] = int(i);
        }
    size_t refinementBreaks = 0;
    for (size_t c = 0; c < clusters.size(); ++c) {
        if (lastDrawn[c] < 0) continue;
        for (unsigned p : parentsOf[c])
            if (firstDrawn[p] >= 0 && firstDrawn[p] < lastDrawn[c]) ++refinementBreaks;
    }
    CHECK(refinementBreaks == 0, "%s: MONOTONE REFINEMENT — no cluster is drawn at a threshold "
          "above one that drew its parent (%zu breaks)", f.name.c_str(), refinementBreaks);

    // ---- 5. the view-dependent cut ------------------------------------------
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t v = 0; v + 2 < f.data.positions.size(); v += 3)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], f.data.positions[v + size_t(k)]);
            hi[k] = std::max(hi[k], f.data.positions[v + size_t(k)]);
        }
    const float c3[3] = { 0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2]) };
    const float ext = f.extent;
    // Eyes at the centre, on the box's faces, beside it and far out, in a few directions.
    const float eyes[][3] = { { 0, 0, 0 },       { 0.5f, 0, 0 },    { 0, 0.5f, 0 },  { 0, 0, -0.5f },
                              { 0.7f, 0.2f, 0 }, { -1.5f, 0.3f, 0.2f }, { 0, 3.0f, 0 }, { 0.4f, 0.4f, 8.0f },
                              { -25.0f, 5.0f, 3.0f }, { 0.1f, 0.0f, 0.55f } };
    const float tols[4] = { 0.25f, 1.0f, 4.0f, 16.0f };
    size_t views = 0, viewBad = 0, distinctCuts = 0;
    size_t minTris = SIZE_MAX, maxTris = 0;
    std::vector<unsigned> cut, previous;
    for (const auto &e : eyes)
        for (float tol : tols) {
            ClusterCutView v;
            for (int k = 0; k < 3; ++k) v.eye[k] = c3[k] + e[k] * ext;
            v.scale = 1.0f;
            v.tolerance = tol;
            v.projScaleY = 1.0f / std::tan(30.0f * 3.14159265f / 180.0f);
            v.viewportHeight = 1080.0f;
            const size_t t = clusterCut(groups, clusters, v, cut);
            minTris = std::min(minTris, t);
            maxTris = std::max(maxTris, t);
            if (cut != previous) ++distinctCuts;
            previous = cut;
            ++views;
            if (coverageDefects(f, cut)) ++viewBad;
        }
    CHECK(viewBad == 0, "%s: the VIEW-DEPENDENT cut is one cut from all %zu (eye, tolerance) "
          "pairs (%zu not; %zu distinct cuts, %zu..%zu triangles)", f.name.c_str(), views, viewBad,
          distinctCuts, minTris, maxTris);
}

// ---- 6. the format ----------------------------------------------------------
static void roundTrip()
{
    std::printf("\n-- the format\n");
    const QString path = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR) + "/app/content/primitives/torus.obj";
    const QString fp = iris::MeshBake::fingerprintFor(
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    const iris::MeshBake::Model model = iris::MeshBake::buildFromFile(path, fp);
    CHECK(model.valid && !model.meshes.isEmpty() && !model.meshes.first()->clusterDag.isEmpty(),
          "the import bake of torus.obj carries a cluster DAG");
    if (!model.valid || model.meshes.isEmpty()) return;
    const QByteArray blob = iris::MeshBake::serialize(model);
    const iris::MeshBake::Model back = iris::MeshBake::deserialize(blob, fp);
    CHECK(back.valid && back.meshes.size() == model.meshes.size(), "the blob reads back");
    if (!back.valid || back.meshes.isEmpty()) return;
    const iris::MeshClusterDag &a = model.meshes.first()->clusterDag, &b = back.meshes.first()->clusterDag;
    bool same = a.clusters.size() == b.clusters.size() && a.groups.size() == b.groups.size() &&
                a.vertices == b.vertices && a.triangles == b.triangles;
    for (int i = 0; same && i < a.clusters.size(); ++i)
        same = std::memcmp(&a.clusters[i], &b.clusters[i], sizeof(iris::MeshClusterDag::Cluster)) == 0;
    for (int i = 0; same && i < a.groups.size(); ++i)
        same = std::memcmp(&a.groups[i], &b.groups[i], sizeof(iris::MeshClusterDag::Group)) == 0;
    CHECK(same, "the DAG survives the blob byte for byte (%d clusters, %d groups)",
          int(a.clusters.size()), int(a.groups.size()));
    CHECK(iris::MeshBake::serialize(back) == blob, "and re-serialises to the same bytes");

    // A NON-MONOTONE DAG IS REFUSED: find a group with a child and write the
    // child an error above its parent's.
    iris::MeshBake::Model bad = back;
    iris::MeshPtr m = bad.meshes.first();
    iris::MeshClusterDag dag = m->clusterDag;
    bool planted = false;
    for (const iris::MeshClusterDag::Cluster &c : dag.clusters)
        if (c.refined >= 0 && dag.groups[c.group].error < FLT_MAX) {
            dag.groups[c.refined].error = dag.groups[c.group].error * 2.0f;
            planted = true;
            break;
        }
    m->clusterDag = dag;
    CHECK(planted, "a child group to plant a non-monotone error in exists");
    const iris::MeshBake::Model refused = iris::MeshBake::deserialize(iris::MeshBake::serialize(bad), fp);
    CHECK(!refused.valid, "a blob whose DAG error FALLS from a child to its parent is refused");
}

// ---- 7. the measurement's local soup is EXACT ----------------------------------
// Term 1 of the group measurement queries a LOCAL level-0 soup around the group
// (meshbake.cpp, the fix for the whole-mesh grid's cost on long thin meshes) and
// falls back to the whole-mesh grid past its reach; the claim is that the result is
// the whole-mesh answer. Bake the dragon and the 40 m round bar both ways and hold
// every group's measured error to the reference within 1e-4 (relative).
static void localSoupIsExact(const std::vector<clusterfix::Named> &meshes)
{
    std::printf("\n-- the measurement's local soup against the whole-mesh reference\n");
    for (const clusterfix::Named &n : meshes) {
        if (n.name != "matcaps-dragon" && n.name != "round-bar-40m") continue;
        iris::MeshBake::ClusterDagStats local, reference;
        reference.referenceMeasure = true;
        iris::MeshBake::buildClusterDag(n.mesh, &reference);
        const iris::MeshClusterDag ref = n.mesh->clusterDag;
        iris::MeshBake::buildClusterDag(n.mesh, &local);
        const iris::MeshClusterDag &dag = n.mesh->clusterDag;
        size_t worstGroup = 0, identical = 0;
        double worst = 0.0;
        const bool same = dag.groups.size() == ref.groups.size();
        for (int g = 0; same && g < dag.groups.size(); ++g) {
            const float a = dag.groups[g].error, b = ref.groups[g].error;
            if (a == b) { ++identical; continue; }
            const double rel = std::fabs(double(a) - double(b)) / std::max(double(b), 1e-30);
            if (rel > worst) { worst = rel; worstGroup = size_t(g); }
        }
        std::printf("   %s: measure %.1f ms local (%d fallbacks) vs %.1f ms whole-mesh; %zu of %d group "
                    "errors bit-identical, worst relative difference %.3g (group %zu)\n", n.name.c_str(),
                    local.measureMs, local.localFallbacks, reference.measureMs, identical,
                    int(dag.groups.size()), worst, worstGroup);
        CHECK(same && worst <= 1.0e-4, "%s: every group's measured error equals the whole-mesh reference "
              "within 1e-4 (worst %.3g)", n.name.c_str(), worst);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::vector<clusterfix::Named> meshes =
        clusterfix::shippedMeshes(JAHSHAKA_TEST_SOURCE_DIR, CLUSTER_FIXTURE_DIR);
    meshes.push_back({ "bar-40m", clusterfix::bar(40.0f, 0.5f, 400, 4) });
    meshes.push_back({ "round-bar-40m", clusterfix::roundBar(40.0f, 0.25f, 400, 32) });
    size_t withDag = 0;
    for (const clusterfix::Named &n : meshes) {
        clusterfix::Fixture f;
        if (!clusterfix::bake(f, n.mesh, n.name, iris::MeshBake::ClusterDagVariant::Shipped, true)) {
            CHECK(false, "%s: baked and handed across", n.name.c_str());
            continue;
        }
        if (f.data.clusters.empty()) {
            std::printf("\n-- %s: %zu triangles, NO DAG (under two leaves of triangles)\n", n.name.c_str(),
                        f.triangles);
            continue;
        }
        ++withDag;
        oneMesh(f);
    }
    CHECK(withDag >= 10u, "%zu meshes carry a DAG (the eleven shipped subjects + the two bars)", withDag);
    localSoupIsExact(meshes);
    roundTrip();
    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
