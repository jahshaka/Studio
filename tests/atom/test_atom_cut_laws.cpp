// test_atom_cut_laws — THE CLUSTER CUT'S CPU LAWS (ATOM-TESTS-BACK-1 stage 1; SPECS/ATOM_TESTS_BACK_1_SPEC.md). One verb a
// row (`test_atom_cut_laws <row>`), 0 frames, no engine, no display: the harness meshes (tests/support/clustermesh.h,
// proceduralshell.h) baked through the product's own bake (MeshBake::buildClusterDag) and handed across by the product's
// own SceneMirror::toMeshData, and the cut evaluated by the product's own C++ rule (Types.h clusterCut, the GLSL twin's
// reference: engine.lod_rule_parity holds the device cell-identical to it). No sample model, no scene file.
//
//   atom.cut_partition   t at 64 log-spaced values in [0.25, 64] px x the views {centre, corner, eye corner, ortho}:
//                        every level-0 triangle in exactly one drawn cluster's region, or in none where an affordable
//                        EMPTY group stands over it (the object vanishes there: the empty top's meaning); no drawn
//                        cluster under a drawn one; the cut's triangles non-increasing in t. Negative control: one
//                        drawn region duplicated reads as a partition fault.
//   atom.cut_closed      a CLOSED mesh's cut at every t: every directed edge, by weld id (irisgl/import/atomdag/
//                        adjacency.h EdgeTable: the bake's own twin rule, degenerate edges left out), has exactly one
//                        reversed twin — a crack is an edge with none. Negative control: one interior triangle removed
//                        from a level-0 cut leaves exactly its three neighbours' edges unmatched.
//   atom.cut_closed.multi_mesh  the same law over TWO meshes welded along one seam (the split sphere), ONE view, the 64 t:
//                        every seam edge of one half's cut matched by welded position in the other's. Control: at
//                        level 0 (nothing affordable) 0 unmatched.
//   atom.switch_bracket  every finite group g whose flip t*_g = error_g x s / F_g has no other group's flip within
//                        [t*_g (1 - 1e-3), t*_g (1 + 1e-3)]: the cuts at the bracket's two ends differ by exactly g's
//                        members (below) <-> g's outputs (above), and at t*_g itself (a tolerance whose allowed error
//                        EQUALS g's, found in float) the cut is still the lower one (the rule's strict `<`). Negative
//                        control: a local copy of the verdict with `<=` draws the upper cut there.
//                        THE LAW NEEDS at least one isolated flip in every (fixture, view) - the row's stated minimum,
//                        never a bar on how many - and the fixtures are chosen so each view has them: the closed sphere
//                        all 22 of its 22 groups in each of the three views, the card soup 2 of its 4 (first run,
//                        2026-11-22); a view with none is red (the law would be vacuous there).
#include "cluster_fixtures.h"
#include "../support/clustermesh.h"
#include "../support/lawline.h"
#include "../support/proceduralshell.h"

#include "import/atomdag/adjacency.h"

#include <QCoreApplication>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int gFailures = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

// ---- THE VIEWS -----------------------------------------------------------------------------------------------------
// A view is a LENS (its tangents at the frame's edges, its target size, perspective or orthographic) and an AIM (the NDC
// point of the frame the instance is placed on). The eye sits at the origin looking down -Z.

/// THE HEADSET EYE'S CORNER (the lead's call C2): tangents (1.0, 1.0) at the eye buffer's corner = a 90 x 90 degree eye,
/// corner ratio 1 + tx^2 + ty^2 = 3.0. UNVERIFIED until plan 3.6 measures a headset's eye (its own projection).
constexpr double kEyeCornerTan[2] = { 1.0, 1.0 };

struct Lens
{
    const char *name;
    double tanX, tanY;     ///< the tangents of the half-angles at the frame's right and top edges
    double width, height;  ///< the target, pixels (square pixels: width / height = tanX / tanY)
    bool ortho;
    double aimX, aimY;     ///< NDC point the instance is placed on
};
/// The desktop view: 45 degrees vertical, 16:9, 1920 x 1080.
constexpr double kDeskTanY = 0.41421356237309503;   // tan(22.5 deg)
static const Lens kCentre = { "centre", kDeskTanY * 16.0 / 9.0, kDeskTanY, 1920.0, 1080.0, false, 0.0, 0.0 };
static const Lens kCorner = { "corner", kDeskTanY * 16.0 / 9.0, kDeskTanY, 1920.0, 1080.0, false, 1.0, 1.0 };
/// The eye buffer's size is not a law term (every ratio here is size-free); 1080 x 1080 for readable numbers.
static const Lens kEyeCorner = { "eye_corner", kEyeCornerTan[0], kEyeCornerTan[1], 1080.0, 1080.0, false, 1.0, 1.0 };
/// The orthographic view: a window 4 extents tall (projScaleY = 2 / window: the footprint is window / height).
static const Lens kOrtho = { "ortho", 0.0, 0.0, 1920.0, 1080.0, true, 0.0, 0.0 };

/// The unit direction through the lens's aim point.
static void aimDirection(const Lens &l, double d[3])
{
    d[0] = l.aimX * l.tanX;
    d[1] = l.aimY * l.tanY;
    d[2] = -1.0;
    const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int k = 0; k < 3; ++k) d[k] /= n;
}

/// The cut's view of ONE instance: identity rotation, scale 1, translated by `t`; the lens's projection.
static ClusterCutView cutView(const Lens &l, const double t[3], float tolerance, float orthoWindow)
{
    ClusterCutView v;
    for (int r = 0; r < 3; ++r) v.worldRow[r][3] = float(t[r]);
    v.scale = 1.0f;
    v.tolerance = tolerance;
    v.orthographic = l.ortho;
    v.projScaleY = l.ortho ? 2.0f / orthoWindow : float(1.0 / l.tanY);
    v.viewportHeight = float(l.height);
    return v;
}

/// The translation that puts the mesh's box centre `c` at distance `dist` along the lens's aim.
static void placeAt(const Lens &l, const float c[3], double dist, double t[3])
{
    double d[3];
    aimDirection(l, d);
    for (int k = 0; k < 3; ++k) t[k] = d[k] * dist - double(c[k]);
}

static void boxCentre(const MeshData &d, float c[3])
{
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t v = 0; v + 2 < d.positions.size(); v += 3)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], d.positions[v + size_t(k)]);
            hi[k] = std::max(hi[k], d.positions[v + size_t(k)]);
        }
    for (int k = 0; k < 3; ++k) c[k] = 0.5f * (lo[k] + hi[k]);
}

/// The 64 tolerances, log-spaced over [0.25, 64] px.
static std::vector<float> sweep()
{
    std::vector<float> t;
    for (int i = 0; i < 64; ++i) t.push_back(float(0.25 * std::pow(256.0, double(i) / 63.0)));
    return t;
}

// ---- THE FIXTURES --------------------------------------------------------------------------------------------------

static bool bakeShell(clusterfix::Fixture &f, const enginetest::ShellMesh &m, const std::string &name,
                      bool wantRegions = false)
{
    iris::MeshBake::ClusterDagOptions o;
    o.wantRegions = wantRegions;
    const bool ok = clusterfix::bake(f, clusterfix::meshFromShell(m), name, o) && !f.data.clusters.empty();
    std::printf("   fixture %-14s %7zu triangles, %5zu clusters, %4zu groups%s\n", name.c_str(), f.triangles,
                f.data.clusters.size(), f.data.clusterGroups.size(), ok ? "" : "  (NO DAG)");
    check(ok, name + ": bakes to a cluster DAG");
    return ok;
}

/// The cut's triangles as vertex ids, three per triangle.
static std::vector<unsigned> cutTriangles(const MeshData &d, const std::vector<unsigned> &cut)
{
    std::vector<unsigned> tris;
    for (unsigned c : cut) {
        const MeshCluster &cl = d.clusters[c];
        tris.insert(tris.end(), d.clusterIndices.begin() + cl.firstIndex,
                    d.clusterIndices.begin() + cl.firstIndex + cl.indexCount);
    }
    return tris;
}

// ---- atom.cut_partition --------------------------------------------------------------------------------------------

struct PartitionFaults
{
    size_t overlap = 0;     ///< drawn clusters whose producing group has a drawn member
    size_t coverage = 0;    ///< level-0 triangles covered other than once (or covered where an empty group vanished)
};

static PartitionFaults partitionFaults(const clusterfix::Fixture &f, const ClusterCutView &v,
                                       const std::vector<unsigned> &cut, const std::vector<unsigned char> &hasOutput)
{
    PartitionFaults p;
    const MeshData &d = f.data;
    std::vector<unsigned char> groupHasDrawnMember(d.clusterGroups.size(), 0);
    for (unsigned c : cut) groupHasDrawnMember[size_t(d.clusters[c].group)] = 1;
    for (unsigned c : cut) {
        const int ref = d.clusters[c].refined;
        if (ref >= 0 && groupHasDrawnMember[size_t(ref)]) ++p.overlap;
    }
    std::vector<unsigned> cover(f.triangles, 0u);
    for (unsigned c : cut)
        for (quint32 t : f.stats.clusterRegions.at(int(c)))
            if (t < cover.size()) ++cover[t];
    // under an AFFORDABLE EMPTY group (no output clusters) nothing is the right cover: the object vanishes there
    std::vector<unsigned char> vanished(f.triangles, 0u);
    for (size_t c = 0; c < d.clusters.size(); ++c) {
        const int g = d.clusters[c].group;
        if (hasOutput[size_t(g)] || !clusterGroupVerdict(d.clusterGroups[size_t(g)], v)) continue;
        for (quint32 t : f.stats.clusterRegions.at(int(c)))
            if (t < vanished.size()) vanished[t] = 1;
    }
    for (size_t t = 0; t < cover.size(); ++t) p.coverage += (vanished[t] ? cover[t] == 0u : cover[t] == 1u) ? 0u : 1u;
    return p;
}

static void rowCutPartition()
{
    std::vector<std::pair<std::string, enginetest::ShellMesh>> meshes;
    meshes.push_back({ "closed-sphere", clustermesh::closedSphere() });
    meshes.push_back({ "round-bar", clustermesh::roundBar(4.0f, 0.25f, 64, 48, 0.08f) });
    meshes.push_back({ "card-soup", clustermesh::cardSoup(400) });
    meshes.push_back({ "shell-68220", enginetest::proceduralShell(68220) });
    const std::vector<float> ts = sweep();
    const Lens *lenses[4] = { &kCentre, &kCorner, &kEyeCorner, &kOrtho };
    for (const auto &m : meshes) {
        clusterfix::Fixture f;
        if (!bakeShell(f, m.second, m.first, /*wantRegions=*/true)) continue;
        bool controlRan = false;
        check(f.stats.clusterRegions.size() == int(f.data.clusters.size()),
              m.first + ": the bake's provenance names every cluster's level-0 region");
        if (f.stats.clusterRegions.size() != int(f.data.clusters.size())) continue;
        std::vector<unsigned char> hasOutput(f.data.clusterGroups.size(), 0);
        for (const MeshCluster &c : f.data.clusters)
            if (c.refined >= 0) hasOutput[size_t(c.refined)] = 1;
        float c[3];
        boxCentre(f.data, c);
        for (const Lens *l : lenses) {
            double t[3];
            placeAt(*l, c, 4.0 * double(f.extent), t);
            size_t overlap = 0, coverage = 0, rises = 0, previous = SIZE_MAX;
            std::set<std::vector<unsigned>> distinct;
            for (float tol : ts) {
                const ClusterCutView v = cutView(*l, t, tol, 4.0f * f.extent);
                std::vector<unsigned> cut;
                const size_t tris = clusterCut(f.data.clusterGroups, f.data.clusters, v, cut);
                distinct.insert(cut);
                const PartitionFaults p = partitionFaults(f, v, cut, hasOutput);
                overlap += p.overlap;
                coverage += p.coverage;
                if (previous != SIZE_MAX && tris > previous) ++rises;
                previous = tris;
                // THE NEGATIVE CONTROL, once a fixture: the first non-empty cut with one drawn region duplicated.
                if (!controlRan && !cut.empty()) {
                    std::vector<unsigned> twice = cut;
                    twice.push_back(cut.front());
                    const PartitionFaults bad = partitionFaults(f, v, twice, hasOutput);
                    std::printf("   negative control (%s, %s, t %.3g): one region duplicated reads %zu coverage "
                                "faults\n", m.first.c_str(), l->name, double(tol), bad.coverage);
                    check(bad.coverage > 0, m.first + ": the negative control - a duplicated drawn region is a partition fault");
                    controlRan = true;
                }
            }
            std::printf("   %-14s %-10s %2zu distinct cuts over the 64 t: %zu overlaps, %zu triangle-cuts off, "
                        "%zu rises in t\n", m.first.c_str(), l->name, distinct.size(), overlap, coverage, rises);
            const std::string at = m.first + " " + l->name;
            enginetest::lawLine(enginetest::lawName(at + ":faults").c_str(), double(overlap + coverage + rises), "q",
                                "==", 0.0);
            check(distinct.size() >= 3u, at + ": the sweep reaches at least 3 distinct cuts");
            check(overlap == 0 && coverage == 0,
                  at + ": every level-0 triangle in exactly one drawn region (or none under an affordable empty "
                       "group), no drawn cluster under a drawn one");
            check(rises == 0, at + ": the cut's triangles never rise as t grows");
        }
        check(controlRan, m.first + ": the negative control ran");
    }
}

// ---- atom.cut_closed / atom.cut_closed.multi_mesh ------------------------------------------------------------------

/// The directed edges of `tris` (vertex ids into `positions`) whose reversed twin count is not exactly one.
static size_t unmatchedEdges(const std::vector<float> &positions, const std::vector<unsigned> &tris)
{
    iris::atomdag::MeshView mv;
    mv.positions = positions.data();
    mv.posComps = 3;
    mv.nv = positions.size() / 3;
    const std::vector<unsigned> weld = iris::atomdag::weldByPosition(mv);
    iris::atomdag::EdgeTable table;
    table.build(tris.data(), tris.size() / 3, weld);
    size_t bad = 0;
    for (const auto &e : table.edges) {
        const unsigned wa = unsigned(e.key >> 32), wb = unsigned(e.key & 0xffffffffu);
        int twins = 0;
        table.forTwins(wa, wb, [&](unsigned, unsigned) { ++twins; });
        if (twins != 1) ++bad;
    }
    return bad;
}

static void rowCutClosed()
{
    std::vector<std::pair<std::string, enginetest::ShellMesh>> meshes;
    meshes.push_back({ "closed-sphere", clustermesh::closedSphere() });
    meshes.push_back({ "round-bar", clustermesh::roundBar(4.0f, 0.25f, 64, 48, 0.08f) });
    const std::vector<float> ts = sweep();
    const Lens *lenses[4] = { &kCentre, &kCorner, &kEyeCorner, &kOrtho };
    for (const auto &m : meshes) {
        clusterfix::Fixture f;
        if (!bakeShell(f, m.second, m.first)) continue;
        // THE NEGATIVE CONTROL: level 0 (the input itself) closed, then one interior triangle removed — its three
        // neighbours' edges lose their twins, exactly three.
        {
            const std::vector<unsigned> level0(f.data.indices.begin(), f.data.indices.end());
            const size_t closed = unmatchedEdges(f.data.positions, level0);
            std::vector<unsigned> holed = level0;
            const size_t mid = (holed.size() / 3 / 2) * 3;
            holed.erase(holed.begin() + long(mid), holed.begin() + long(mid) + 3);
            const size_t open = unmatchedEdges(f.data.positions, holed);
            std::printf("   %-14s level 0: %zu unmatched edges; one interior triangle removed: %zu\n", m.first.c_str(),
                        closed, open);
            check(closed == 0, m.first + ": level 0 is closed by weld id (every directed edge has one reversed twin)");
            check(open == 3, m.first + ": the negative control - one triangle removed leaves exactly 3 unmatched edges");
        }
        float c[3];
        boxCentre(f.data, c);
        for (const Lens *l : lenses) {
            double t[3];
            placeAt(*l, c, 4.0 * double(f.extent), t);
            size_t worst = 0, cuts = 0;
            float worstT = 0.0f;
            std::set<std::vector<unsigned>> distinct;
            for (float tol : ts) {
                std::vector<unsigned> cut;
                clusterCut(f.data.clusterGroups, f.data.clusters, cutView(*l, t, tol, 4.0f * f.extent), cut);
                if (cut.empty() || !distinct.insert(cut).second) continue;   // the vanished top; a cut already read
                ++cuts;
                const size_t u = unmatchedEdges(f.data.positions, cutTriangles(f.data, cut));
                if (u > worst) { worst = u; worstT = tol; }
            }
            std::printf("   %-14s %-10s %2zu distinct non-empty cuts: the worst %zu unmatched edges (t %.3g)\n",
                        m.first.c_str(), l->name, cuts, worst, double(worstT));
            const std::string at = m.first + " " + l->name;
            enginetest::lawLine(enginetest::lawName(at + ":unmatched").c_str(), double(worst), "q", "==", 0.0);
            check(cuts >= 3u, at + ": the sweep reaches at least 3 distinct cuts");
            check(worst == 0, at + ": every cut is closed (every directed edge by weld id has one reversed twin)");
        }
    }
}

static void rowCutClosedMultiMesh()
{
    const clustermesh::SplitSphere split = clustermesh::splitSphere();
    clusterfix::Fixture north, south;
    if (!bakeShell(north, split.north, "north-half") || !bakeShell(south, split.south, "south-half")) return;
    // ONE vertex space for the two halves (the second's ids offset by the first's count), welded by position: the
    // seam's ring is in both at bit-identical positions.
    std::vector<float> positions = north.data.positions;
    positions.insert(positions.end(), south.data.positions.begin(), south.data.positions.end());
    const unsigned offset = unsigned(north.data.positions.size() / 3);
    float c[3] = { 0.0f, 0.0f, 0.0f };   // the split sphere is centred on the origin
    const float extent = std::max(north.extent, south.extent);
    double t[3];
    placeAt(kCentre, c, 4.0 * double(extent), t);
    size_t worst = 0, atLevel0 = SIZE_MAX, redT = 0;
    float worstT = 0.0f;
    for (float tol : sweep()) {
        const ClusterCutView v = cutView(kCentre, t, tol, 4.0f * extent);
        std::vector<unsigned> cn, cs;
        clusterCut(north.data.clusterGroups, north.data.clusters, v, cn);
        clusterCut(south.data.clusterGroups, south.data.clusters, v, cs);
        std::vector<unsigned> tris = cutTriangles(north.data, cn);
        for (unsigned i : cutTriangles(south.data, cs)) tris.push_back(i + offset);
        const size_t u = tris.empty() ? 0 : unmatchedEdges(positions, tris);
        std::printf("   t %8.4f px: north %4zu clusters, south %4zu: %zu unmatched edges\n", double(tol), cn.size(),
                    cs.size(), u);
        enginetest::lawLine(enginetest::lawName("t" + std::to_string(tol) + ":unmatched").c_str(), double(u), "q",
                            "==", 0.0);
        if (u > 0) ++redT;
        if (u > worst) { worst = u; worstT = tol; }
    }
    // THE CONTROL: level 0 (no group affordable: tolerance 0) is the split sphere itself, closed along its seam.
    {
        const ClusterCutView v0 = cutView(kCentre, t, 0.0f, 4.0f * extent);
        std::vector<unsigned> cn, cs;
        clusterCut(north.data.clusterGroups, north.data.clusters, v0, cn);
        clusterCut(south.data.clusterGroups, south.data.clusters, v0, cs);
        std::vector<unsigned> tris = cutTriangles(north.data, cn);
        for (unsigned i : cutTriangles(south.data, cs)) tris.push_back(i + offset);
        atLevel0 = unmatchedEdges(positions, tris);
    }
    std::printf("   the control, level 0 (t = 0): %zu unmatched edges; the sweep: %zu of 64 t with a crack, the worst %zu "
                "(t %.3g px)\n", atLevel0, redT, worst, double(worstT));
    check(atLevel0 == 0, "the control: the two halves at level 0 close along their seam");
    check(worst == 0, "every t: every seam edge of one half's cut is matched by welded position in the other's");
}

// ---- atom.switch_bracket -------------------------------------------------------------------------------------------

/// The cut at per-group answers `aff` (the product's render test over them).
static std::vector<unsigned> cutFrom(const MeshData &d, const std::vector<unsigned char> &aff)
{
    std::vector<unsigned> out;
    clusterCutFromAffordable(d.clusters, aff, out);
    return out;
}

static void rowSwitchBracket()
{
    std::vector<std::pair<std::string, enginetest::ShellMesh>> meshes;
    meshes.push_back({ "closed-sphere", clustermesh::closedSphere() });
    meshes.push_back({ "card-soup", clustermesh::cardSoup(400) });
    const Lens *lenses[3] = { &kCentre, &kCorner, &kEyeCorner };
    for (const auto &m : meshes) {
        clusterfix::Fixture f;
        if (!bakeShell(f, m.second, m.first)) continue;
        const MeshData &d = f.data;
        float c[3];
        boxCentre(d, c);
        for (const Lens *l : lenses) {
            double t[3];
            placeAt(*l, c, 4.0 * double(f.extent), t);
            const ClusterCutView base = cutView(*l, t, 1.0f, 4.0f * f.extent);
            // every finite group's flip in this view: t*_g = error_g x s / F_g (Types.h clusterGroupFootprint)
            std::vector<float> flip(d.clusterGroups.size(), -1.0f);
            for (size_t g = 0; g < d.clusterGroups.size(); ++g) {
                const MeshClusterGroup &gr = d.clusterGroups[g];
                const float F = clusterGroupFootprint(gr, base);
                if (gr.error < kClusterTerminal && F > 0.0f && gr.error > 0.0f) flip[g] = gr.error * base.scale / F;
            }
            size_t tested = 0, bad = 0, exact = 0, strictBad = 0, looseSame = 0;
            for (size_t g = 0; g < flip.size(); ++g) {
                if (flip[g] <= 0.0f) continue;
                const double lo = double(flip[g]) * (1.0 - 1e-3), hi = double(flip[g]) * (1.0 + 1e-3);
                bool isolated = true;
                for (size_t h = 0; h < flip.size() && isolated; ++h)
                    if (h != g && flip[h] > 0.0f && double(flip[h]) >= lo && double(flip[h]) <= hi) isolated = false;
                if (!isolated) continue;
                ++tested;
                ClusterCutView vl = base, vh = base;
                vl.tolerance = float(lo);
                vh.tolerance = float(hi);
                std::vector<unsigned> cl, ch;
                clusterCut(d.clusterGroups, d.clusters, vl, cl);
                clusterCut(d.clusterGroups, d.clusters, vh, ch);
                std::vector<unsigned> gone, came, members, outputs;
                std::set_difference(cl.begin(), cl.end(), ch.begin(), ch.end(), std::back_inserter(gone));
                std::set_difference(ch.begin(), ch.end(), cl.begin(), cl.end(), std::back_inserter(came));
                for (size_t k = 0; k < d.clusters.size(); ++k) {
                    if (d.clusters[k].group == int(g)) members.push_back(unsigned(k));
                    if (d.clusters[k].refined == int(g)) outputs.push_back(unsigned(k));
                }
                if (gone != members || came != outputs) {
                    if (bad < 3)
                        std::printf("   BRACKET DIFFERS: %s %s group %zu (t* %.6g): %zu gone / %zu members, %zu came / "
                                    "%zu outputs\n", m.first.c_str(), l->name, g, double(flip[g]), gone.size(),
                                    members.size(), came.size(), outputs.size());
                    ++bad;
                }
                // AT t* ITSELF: a tolerance whose allowed error at g EQUALS g's error, in float (a few ulps around the
                // quotient) — the rule's `<` keeps g's members; a `<=` copy draws its outputs.
                const float F = clusterGroupFootprint(d.clusterGroups[g], base);
                float at = flip[g];
                for (int k = 0; k < 8 && allowedWorldError(at, F, base.scale) < d.clusterGroups[g].error; ++k)
                    at = std::nextafter(at, FLT_MAX);
                for (int k = 0; k < 8 && allowedWorldError(at, F, base.scale) > d.clusterGroups[g].error; ++k)
                    at = std::nextafter(at, 0.0f);
                ClusterCutView ve = base;
                ve.tolerance = at;
                if (allowedWorldError(at, F, base.scale) != d.clusterGroups[g].error) continue;
                ++exact;
                std::vector<unsigned char> strict(d.clusterGroups.size(), 0), loose(d.clusterGroups.size(), 0);
                for (size_t h = 0; h < d.clusterGroups.size(); ++h) {
                    const float allowed = allowedWorldError(ve.tolerance, clusterGroupFootprint(d.clusterGroups[h], ve),
                                                            ve.scale);
                    strict[h] = clusterGroupVerdict(d.clusterGroups[h], ve) ? 1 : 0;
                    loose[h] = (allowed > 0.0f && d.clusterGroups[h].error <= allowed) ? 1 : 0;   // the `<=` copy
                }
                if (cutFrom(d, strict) != cl) ++strictBad;
                if (cutFrom(d, loose) == cl) ++looseSame;
            }
            std::printf("   %-14s %-10s %4zu isolated flips: %zu brackets differ from members <-> outputs; %zu exact t*: "
                        "the rule's cut there differs from the lower %zu times, the `<=` copy's equals it %zu times\n",
                        m.first.c_str(), l->name, tested, bad, exact, strictBad, looseSame);
            const std::string at = m.first + " " + l->name;
            enginetest::lawLine(enginetest::lawName(at + ":bracket_faults").c_str(), double(bad + strictBad), "q", "==",
                                0.0);
            check(tested > 0u, at + ": the view holds an isolated flip (the law is not vacuous)");
            check(bad == 0, at + ": every isolated flip swaps exactly the group's members for its outputs");
            check(exact > 0 && strictBad == 0, at + ": at t* exactly the rule keeps the lower cut (strict <)");
            check(exact > 0 && looseSame == 0, at + ": the negative control - a `<=` verdict draws the upper cut at t*");
        }
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const std::string row = argc > 1 ? argv[1] : "";
    std::printf("== %s ==\n", row.c_str());
    if (row == "atom.cut_partition") rowCutPartition();
    else if (row == "atom.cut_closed") rowCutClosed();
    else if (row == "atom.cut_closed.multi_mesh") rowCutClosedMultiMesh();
    else if (row == "atom.switch_bracket") rowSwitchBracket();
    else {
        std::printf("usage: test_atom_cut_laws <atom.cut_partition | atom.cut_closed | atom.cut_closed.multi_mesh | "
                    "atom.switch_bracket>\n");
        return 2;
    }
    if (gFailures == 0) std::printf("%s: PASSED\n", row.c_str());
    else std::printf("%s: FAILED (%d)\n", row.c_str(), gFailures);
    return gFailures == 0 ? 0 : 1;
}
