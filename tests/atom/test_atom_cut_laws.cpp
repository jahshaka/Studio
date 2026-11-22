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
//
// STAGE 2 — THE NO-POP ROWS: the owner's law "Atom never pops: the switch error under ONE pixel at every tier", measured as
// a CPU sweep over EVERY flip of EVERY group of the harness meshes (the closed sphere, the round bar, the procedural shell
// at the 4,936-triangle class), judged by THE INDEPENDENT MEASURE (tests/support/surfacedistance.h: never the bake's
// measure.h). For each finite group g and view v the instance is placed so g flips at t: its sphere's centre on the ray
// through the view's aim at D_c = r_g + error_g P11 H / (2 t) (Types.h:314-325, :342-344: t*_g = error_g s / F_g,
// F_g = 2 max(0, D_c - r_g s) / (P11 H); s = 1), and switch_px(g, v) is read over the points that project within the
// WINDOW: a disc of radius kWindowFraction x the half-height about the aim's pixel.
// THE BOUND, DERIVED (the spec's premise): a point at distance D and angle theta off the axis, moved delta across its ray,
// moves (P11 H / 2) delta / (D cos^2 theta) px; the rule spends delta <= error_g at the footprint of the group's NEAREST
// point, D_c - r: switch_px / t <= (D_c - r) / (D cos^2 theta) <= 1 + tan^2 theta (D >= D_c - r). At the frame's centre
// theta ~ 0: switch_px <= t. At the corner of a vertical fov phi, aspect a: 1 + tan^2(phi/2)(1 + a^2) = 1.714 at 45 deg,
// 16:9; at an eye corner of tangents (tx, ty): 1 + tx^2 + ty^2 = 3.0 at kEyeCornerTan. A tier of tolerance t_k flips at
// t_k px, so the switch reaches t_k = 4 / 3 / 2 / 1 px at Low / Medium / High / Epic against the owner's 1 px.
//   atom.no_pop.centre      the centre view, t = 1: switch_px <= t at every flip. Every no-pop row runs the measure's own
//                           checks first (two parallel planes at d read d; the flush shutter without its dropped-island
//                           term reads < 1 px, with it its diagonal) and asserts the literal projected pair never passes
//                           the certified bound.
//   atom.no_pop.measure_agrees  the bake's step beside the measure's own world reading, as the bake's stated contract
//                           (rowMeasureAgrees): the bake misses no term, and is within its own tightness of the measure.
//   atom.no_pop.corner      the same at the desktop's 45-deg 16:9 corner and at the eye corner. RED by design (plan 3.4).
//   atom.no_pop.every_tier  the centre view at each tier's tolerance (worldmodes::row("atomPixelError")->tier[]):
//                           switch_px <= 1 px. RED by design at Low/Medium/High (plan 3.4: its deletion of the tier row
//                           breaks this row's build - the fold into centre/corner is forced there).
#include "cluster_fixtures.h"
#include "../support/clustermesh.h"
#include "../support/lawline.h"
#include "../support/proceduralshell.h"
#include "../support/surfacedistance.h"

#include "import/atomdag/adjacency.h"
#include "irisgl/document/scenegraph/worldmodes.h"

#include <QCoreApplication>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
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

// ---- THE NO-POP ROWS (stage 2) ------------------------------------------------------------------------------------
namespace sd = surfacedistance;

/// The window's radius: this fraction of the frame's half-height about the aim's pixel ("within 10 % of the frame
/// centre", the spec's centre row; the same disc at a corner, its in-frame quarter).
constexpr double kWindowFraction = 0.10;

/// THE MEASURE's PRECISION in pixels: 1 % of the reading, and nothing under a hundredth of a pixel resolved (neither can
/// move a law whose bars are 1 px and its multiples); in world units (the bake's step beside it): 0.1 %, and a millionth of
/// the extent (under the bake's own floor, 1e-5 of its largest axis).
constexpr double kPixelRel = 1e-2, kPixelFloor = 1e-2, kWorldRel = 1e-3, kWorldFloorOfExtent = 1e-6;

/// Group g's members (outputs false) or the clusters it produced (true) as triangles, the mesh translated by `t`.
static std::vector<sd::Tri> groupTris(const MeshData &d, int g, bool outputs, const double t[3])
{
    std::vector<sd::Tri> out;
    for (const MeshCluster &c : d.clusters) {
        if ((outputs ? c.refined : c.group) != g) continue;
        for (unsigned i = 0; i + 2 < c.indexCount; i += 3) {
            sd::Tri tri;
            for (int k = 0; k < 3; ++k) {
                const unsigned v = d.clusterIndices[c.firstIndex + i + unsigned(k)];
                for (int a = 0; a < 3; ++a) tri.p[k][size_t(a)] = double(d.positions[size_t(v) * 3 + size_t(a)]) + t[a];
            }
            out.push_back(tri);
        }
    }
    return out;
}

static sd::View pixelView(const Lens &l)
{
    sd::View v;
    v.tanX = l.tanX;
    v.tanY = l.tanY;
    v.width = l.width;
    v.height = l.height;
    v.winX = (l.aimX * 0.5 + 0.5) * l.width;
    v.winY = (l.aimY * 0.5 + 0.5) * l.height;
    v.winR = kWindowFraction * 0.5 * l.height;
    return v;
}

/// ONE FLIP of one group in one view, read by the independent measure.
struct Flip
{
    int group = -1, depth = 0;
    double t = 0.0;          ///< the flip's tolerance by the product's own footprint (clusterGroupFootprint), px
    double px = 0.0;         ///< switch_px: the attained reading (surface term, dropped islands), px
    double upper = 0.0;      ///< its certified upper bound, px
    double surface = 0.0, islands = 0.0;
    double literal = 0.0;    ///< the largest literal |proj(p) - proj(q)| over every point sampled, px
    bool counted = false;    ///< some point of the switch projected in the window
};

static std::vector<Flip> flipsAt(const clusterfix::Fixture &f, const Lens &l, double tol)
{
    std::vector<Flip> out;
    const MeshData &d = f.data;
    const sd::View view = pixelView(l);
    double dir[3];
    aimDirection(l, dir);
    for (size_t g = 0; g < d.clusterGroups.size(); ++g) {
        const MeshClusterGroup &gr = d.clusterGroups[g];
        if (!(gr.error > 0.0f) || gr.error >= kClusterTerminal) continue;
        const double dc = double(gr.radius) + double(gr.error) * (1.0 / l.tanY) * l.height / (2.0 * tol);
        double tr[3];
        for (int k = 0; k < 3; ++k) tr[k] = dir[k] * dc - double(gr.centre[k]);
        const ClusterCutView cv = cutView(l, tr, float(tol), 0.0f);
        const float F = clusterGroupFootprint(gr, cv);
        Flip fl;
        fl.group = int(g);
        fl.depth = gr.depth;
        fl.t = F > 0.0f ? double(gr.error) * double(cv.scale) / double(F) : 0.0;
        const sd::Switch s = sd::measure(groupTris(d, int(g), false, tr), groupTris(d, int(g), true, tr), &view, kPixelRel,
                                         kPixelFloor);
        fl.surface = s.surface();
        fl.islands = s.islands;
        fl.px = s.total();
        fl.upper = s.upper();
        fl.counted = s.aToB.any || s.bToA.any || s.islands > 0.0;
        fl.literal = std::max(s.aToB.literal, s.bToA.literal);
        out.push_back(fl);
    }
    return out;
}

static std::vector<std::pair<std::string, enginetest::ShellMesh>> noPopMeshes()
{
    std::vector<std::pair<std::string, enginetest::ShellMesh>> m;
    m.push_back({ "closed-sphere", clustermesh::closedSphere() });
    m.push_back({ "round-bar", clustermesh::roundBar(4.0f, 0.25f, 64, 48, 0.08f) });
    m.push_back({ "shell-4936", enginetest::proceduralShell(4936) });
    return m;
}

/// The sweep of one (fixture, lens, tolerance): every flip judged against `bar` px (0: against its own t). Prints the
/// worst flip; returns the number over the bar.
static size_t judge(const clusterfix::Fixture &f, const Lens &l, double tol, double bar, const std::string &tag)
{
    const std::vector<Flip> fl = flipsAt(f, l, tol);
    size_t counted = 0, over = 0, literalPast = 0;
    const Flip *worst = nullptr;
    double worstRatio = 0.0;
    for (const Flip &x : fl) {
        if (!x.counted || !(x.t > 0.0)) continue;
        ++counted;
        if (x.literal > x.upper * (1.0 + 1e-9)) ++literalPast;
        const double b = bar > 0.0 ? bar : x.t;
        if (x.px > b * (1.0 + 1e-6)) ++over;
        const double ratio = x.px / b;
        if (!worst || ratio > worstRatio) { worst = &x; worstRatio = ratio; }
    }
    if (worst)
        std::printf("   %-13s %-10s t %4.1f: %3zu flips read, %3zu over %s; the worst group %d (depth %d): switch %.4f px "
                    "(upper %.4f; surface %.4f, islands %.4f; the largest literal projected pair %.4f) at t %.4f px = "
                    "%.4f x the bar\n", f.name.c_str(), l.name, tol, counted, over, bar > 0.0 ? "1 px" : "its t", worst->group,
                    worst->depth, worst->px, worst->upper, worst->surface, worst->islands, worst->literal, worst->t,
                    worstRatio);
    enginetest::lawLine(enginetest::lawName(tag + ":worst_ratio").c_str(), worstRatio, "x", "<=", 1.0);
    check(counted > 0, tag + ": the sweep reads at least one flip in the window");
    check(literalPast == 0, tag + ": the literal projected pair never passes the certified bound (" +
                                std::to_string(literalPast) + " flips past it)");
    return over;
}

/// THE MEASURE's OWN CHECKS (closed forms, no bake): two parallel planes at d read d; the flush shutter dropped onto its
/// plate reads its thickness without the island term and its diagonal with it.
static void measureChecks()
{
    // two 1 m squares (4 x 4 cells each) at y = 0 and y = d: every point of either is exactly d from the other
    const double dPlanes = 0.0125;
    std::vector<sd::Tri> a, b;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            for (int h = 0; h < 2; ++h) {
                const double x0 = -0.5 + 0.25 * i, z0 = -0.5 + 0.25 * j, x1 = x0 + 0.25, z1 = z0 + 0.25;
                sd::Tri t = h == 0 ? sd::Tri { { { x0, 0, z0 }, { x0, 0, z1 }, { x1, 0, z0 } } }
                                   : sd::Tri { { { x1, 0, z0 }, { x0, 0, z1 }, { x1, 0, z1 } } };
                a.push_back(t);
                for (auto &p : t.p) p[1] = dPlanes;
                b.push_back(t);
            }
    const sd::Switch planes = sd::measure(a, b, nullptr, 1e-9, 1e-12);
    std::printf("   the measure's checks: parallel planes at d = %.4f read %.9f (upper %.9f; the sheet's island: %d dropped, %d "
                "replaced by the new sheet)\n", dPlanes, planes.total(), planes.upper(), planes.dropped, planes.replaced);
    check(std::fabs(planes.total() - dPlanes) <= 1e-9 && planes.upper() <= dPlanes + 2e-9 && planes.dropped == 1 &&
              planes.replaced == 1,
          "the measure: two parallel planes at d read d (each side against the other, every facet; the replaced sheet "
          "charged its distance to the new one, never its diagonal)");

    // THE FLUSH SHUTTER: the plate + the box against the plate alone, turned to face the eye (+Y -> +Z) and placed at the
    // centre where the box's diagonal projects to 1 px (D = diagonal x P11 H / 2): without the island term the switch is
    // the thickness (~0.007 px) - under 1 px though the whole box vanished; with it, the diagonal.
    const clustermesh::FlushShutter fs = clustermesh::flushShutter();
    const double D = double(fs.diagonal) / kCentre.tanY * kCentre.height / 2.0;
    std::vector<sd::Tri> in, plateOnly;
    for (size_t t = 0; t < fs.mesh.indices.size() / 3; ++t) {
        sd::Tri tri;
        for (int k = 0; k < 3; ++k) {
            const float *p = &fs.mesh.positions[size_t(fs.mesh.indices[t * 3 + size_t(k)]) * 3];
            tri.p[k] = { double(p[0]), -double(p[2]), double(p[1]) - D };   // a quarter turn about X, then -D along Z
        }
        in.push_back(tri);
        if (t < fs.plateTriangles) plateOnly.push_back(tri);
    }
    const sd::Switch w = sd::measure(in, plateOnly, nullptr, 1e-9, 1e-12);
    const sd::View view = pixelView(kCentre);
    const sd::Switch px = sd::measure(in, plateOnly, &view, kPixelRel, 1e-6);
    std::printf("   the flush shutter (box %.3f m diagonal, %.4f m thick) dropped: world surface %.6f, island %.6f; at the "
                "centre: without the island term %.4f px, with it %.4f px\n", double(fs.diagonal), double(fs.thickness),
                w.surface(), w.islands, px.surface(), px.total());
    check(std::fabs(w.surface() - double(fs.thickness)) <= 1e-7 && std::fabs(w.islands - double(fs.diagonal)) <= 1e-7 &&
              w.dropped == 1 && w.replaced == 0,
          "the measure: the shutter's surface term is its thickness, its dropped island its diagonal (closed forms)");
    check(px.surface() < 1.0 && px.total() - px.surface() > 0.5,
          "the measure's negative control: without the dropped-island term the vanished shutter reads under 1 px; with "
          "it, its diagonal (the difference over half a pixel)");
}

static void rowNoPopCentre()
{
    measureChecks();
    size_t over = 0;
    for (const auto &m : noPopMeshes()) {
        clusterfix::Fixture f;
        if (!bakeShell(f, m.second, m.first)) continue;
        over += judge(f, kCentre, 1.0, 0.0, m.first + " centre");
    }
    check(over == 0, "THE NO-POP LAW AT THE CENTRE: every flip's switch at most its t (1 px)");
}

/// THE BAKE's STATED TIGHTNESS, READ FROM ITS CODE (never retyped): facetMax closes a piece when its bound is within
/// c x max(stop, best) of its centre's distance (irisgl/import/atomdag/measure.cpp, the line below - an inline literal,
/// no named constant: a finding for plan 3.5). With stop <= the stored step S, every closed piece's bound is <= T + c S
/// (T the true maximum), so S <= T / (1 - c): K = 1 / (1 - c) (the comment's "TIGHT to 1.05x" is the first-order 1 + c).
/// 0 when the line is not there (the row then fails, naming it).
static double bakeTightness(std::string &line)
{
    FILE *fp = std::fopen(JAHSHAKA_TEST_SOURCE_DIR "/irisgl/import/atomdag/measure.cpp", "r");
    if (!fp) return 0.0;
    char buf[512];
    double c = 0.0;
    while (std::fgets(buf, sizeof(buf), fp)) {
        const char *at = std::strstr(buf, "const float tol = std::max(floorTol, ");
        if (!at || !std::strstr(buf, "* std::max(stop, best));")) continue;
        c = std::atof(at + std::strlen("const float tol = std::max(floorTol, "));
        line = buf;
        while (!line.empty() && (line.back() == '\n' || line.back() == ' ')) line.pop_back();
        break;
    }
    std::fclose(fp);
    return c > 0.0 && c < 1.0 ? 1.0 / (1.0 - c) : 0.0;
}

/// atom.no_pop.measure_agrees.<fixture> (closed_sphere, round_bar, shell_4936: split by fixture, each under the CPU row's
/// 10 s; every group of each) - the bake's step (its group record) beside the independent measure's own world
/// reading, as the bake's stated contract: bake_step >= mine (the bake misses no term: a group below is a red of the BAKE,
/// printed) and bake_step <= K x mine_upper + the bake's floor (K its tightness, read from its code), where both are
/// defined: a reduction (record kind 0), no card merge, a non-empty output, the bake's step above its floor.
static void rowMeasureAgrees(const std::string &only)
{
    std::string line;
    const double K = bakeTightness(line);
    std::printf("   the bake's tightness: K = %.6f from measure.cpp's line \"%s\"\n", K, line.c_str());
    check(K > 1.0, "the bake's facetMax tolerance line is found in irisgl/import/atomdag/measure.cpp");
    if (!(K > 1.0)) return;
    size_t compared = 0, below = 0, above = 0;
    double worstAbove = 0.0, worstBelow = 0.0;
    for (const auto &m : noPopMeshes()) {
        std::string key = m.first;   // the row's suffix spells the fixture with '_' for '-'
        std::replace(key.begin(), key.end(), '-', '_');
        if (key != only) continue;
        clusterfix::Fixture f;
        iris::MeshBake::ClusterDagOptions o;
        o.wantGroups = true;
        const bool ok = clusterfix::bake(f, clusterfix::meshFromShell(m.second), m.first, o) && !f.data.clusters.empty();
        check(ok, m.first + ": bakes to a cluster DAG");
        if (!ok) continue;
        const double zero[3] = { 0.0, 0.0, 0.0 };
        // Each group's reading is independent and deterministic: measured four at a time (the leaf groups' walks are
        // seconds each), printed in group order.
        std::vector<const iris::atomdag::GroupRecord *> todo;
        for (const auto &rec : f.stats.records)
            if (!(rec.dagGroup < 0 || rec.kind != 0 || rec.cards || rec.outTris == 0 || !(rec.step > f.stats.floorLen)))
                todo.push_back(&rec);
        std::vector<sd::Switch> readings(todo.size());
        for (size_t i = 0; i < todo.size(); i += 4) {
            std::vector<std::future<sd::Switch>> jobs;
            for (size_t j = i; j < std::min(todo.size(), i + 4); ++j)
                jobs.push_back(std::async(std::launch::async, [&f, &zero, rec = todo[j]] {
                    return sd::measure(groupTris(f.data, rec->dagGroup, false, zero),
                                       groupTris(f.data, rec->dagGroup, true, zero), nullptr, kWorldRel,
                                       kWorldFloorOfExtent * double(f.extent));
                }));
            for (size_t j = i; j < std::min(todo.size(), i + 4); ++j) readings[j] = jobs[j - i].get();
        }
        for (size_t i = 0; i < todo.size(); ++i) {
            const auto &rec = *todo[i];
            const sd::Switch &s = readings[i];
            ++compared;
            const double step = double(rec.step), lo = s.total(), hi = K * s.upper() + double(f.stats.floorLen);
            const bool isBelow = step < lo, isAbove = step > hi;
            below += isBelow ? 1u : 0u;
            above += isAbove ? 1u : 0u;
            worstBelow = std::max(worstBelow, lo / step);
            worstAbove = std::max(worstAbove, step / s.upper());
            std::printf("   %-13s group %3d (depth %d): the bake's step %.7g (removed %.3g lost %.3g added %.3g island %.3g "
                        "locked %.3g), the measure's %.7g .. %.7g (surface %.7g, islands %.7g): step / upper %.4f%s\n",
                        m.first.c_str(), rec.dagGroup, rec.level, step, double(rec.removedVertex), double(rec.lostFacet),
                        double(rec.addedFacet), double(rec.droppedIsland), double(rec.lostLocked), lo, s.upper(),
                        s.surface(), s.islands, step / s.upper(),
                        isBelow ? "  THE BAKE BELOW THE MEASURE (a missing term)" : (isAbove ? "  PAST K x upper" : ""));
        }
    }
    std::printf("   %zu groups compared: %zu with the bake's step below the measure (worst mine / step %.4f), %zu past K x "
                "the measure's upper bound (worst step / upper %.4f, K %.4f)\n", compared, below, worstBelow, above,
                worstAbove, K);
    enginetest::lawLine("step_over_upper", worstAbove, "x", "<=", K);
    check(compared > 0, "at least one group where both are defined");
    check(below == 0, "the bake's step is never below the independent measure (the bake misses no term)");
    check(above == 0, "the bake's step is within its own stated tightness K of the independent measure's upper bound");
}

static void rowNoPopCorner()
{
    measureChecks();
    size_t over = 0;
    for (const auto &m : noPopMeshes()) {
        clusterfix::Fixture f;
        if (!bakeShell(f, m.second, m.first)) continue;
        over += judge(f, kCorner, 1.0, 0.0, m.first + " corner");
        over += judge(f, kEyeCorner, 1.0, 0.0, m.first + " eye_corner");
    }
    check(over == 0, "THE NO-POP LAW AT THE CORNERS (desktop 45 deg 16:9, the eye's): every flip's switch at most its t");
}

static void rowNoPopEveryTier()
{
    measureChecks();
    const worldmodes::Row *row = worldmodes::row(QStringLiteral("atomPixelError"));
    check(row != nullptr, "the tier row atomPixelError exists");
    if (!row) return;
    const char *names[4] = { "Low", "Medium", "High", "Epic" };
    size_t over = 0;
    for (const auto &m : noPopMeshes()) {
        clusterfix::Fixture f;
        if (!bakeShell(f, m.second, m.first)) continue;
        for (int k = 0; k < 4; ++k)
            over += judge(f, kCentre, double(row->tier[k]), 1.0, m.first + " " + names[k]);
    }
    check(over == 0, "THE OWNER'S LAW AT EVERY TIER: every flip's switch at most 1 px at the tier's tolerance");
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
    else if (row == "atom.no_pop.centre") rowNoPopCentre();
    else if (row == "atom.no_pop.corner") rowNoPopCorner();
    else if (row == "atom.no_pop.every_tier") rowNoPopEveryTier();
    else if (row.rfind("atom.no_pop.measure_agrees", 0) == 0)
        rowMeasureAgrees(row.size() > 27 ? row.substr(27) : std::string("(a fixture)"));
    else {
        std::printf("usage: test_atom_cut_laws <atom.cut_partition | atom.cut_closed | atom.cut_closed.multi_mesh | "
                    "atom.switch_bracket | atom.no_pop.centre | atom.no_pop.corner | atom.no_pop.every_tier | "
                    "atom.no_pop.measure_agrees.<closed_sphere | round_bar | shell_4936>>\n");
        return 2;
    }
    if (gFailures == 0) std::printf("%s: PASSED\n", row.c_str());
    else std::printf("%s: FAILED (%d)\n", row.c_str(), gFailures);
    return gFailures == 0 ? 0 : 1;
}
