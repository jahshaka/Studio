// engine.lod_rule_parity — ONE QUALITY CURRENCY, TWO IMPLEMENTATIONS, AND THE
// PROOF THEY ARE THE SAME (SPECS/atom/A4_SUBSTRATE_CULL_DESIGN.md section 3).
//
// THE CURRENCY is `sampleFootprintPerspective` + `allowedWorldError` +
// `lodLevelForWorldError` in jahshaka/engine/Types.h, and its GLSL twin is
// `jahSampleFootprint` + `jahAllowedWorldError` + `jahLevelForAllowed` in
// media/Hlms/Jahshaka/JahLevelRule_piece_cs.any (ONE definition each, inserted by
// the cull's test job and the voxel gather). There are two halves because a
// compute shader cannot include a C++ header. This suite is the reason that is
// SAFE: it drives the REAL shaders — the cull's own job 1, on the device, over the
// real GPU scene table, over 10,000 (bound, distance, tolerance, projection)
// combinations, and the voxel gather's own job over the same table (its cascade
// case, below) — and compares their answers against the header's.
//
// HOW THE 10,000 ARE MADE: 500 instances spread from 1 m to 500 m with varying
// scales (so the distance term and the mesh-units term both move), evaluated
// under 20 parameter sets that sweep the tolerance, proj[1][1] and the viewport
// height. The frustum is made permissive on purpose — every instance must reach
// the level walk, and a frustum rejection would silently shrink the sample.
//
// AND A ROTATED, NON-UNIFORMLY SCALED GROUP (A5b fix round): 100 more instances
// at scale (1, 4, 1) under a 45 degree yaw and a 30 degree pitch. The largest axis
// scale of such a transform is 4 - the longest COLUMN of the row-major 3x4 - while
// every ROW is shorter, so a rule that read rows (the cull did, until this round)
// divides by too small a scale and answers a COARSER level than the tolerance
// permits. The CPU reference takes `worldMaxAxisScale` (Types.h), which is checked
// against the authored 4 first, so the reference is the truth and not a twin.
//
// THE THRESHOLD CASES ARE COUNTED, NOT HIDDEN. The design asks for agreement to
// 1e-4; the shader's output is an integer LEVEL, so the honest statement is
// "every level agrees, and N of the 10,000 evaluations were within 1e-4 of a
// bound" — a disagreement on one of those would be float ordering and not a rule
// difference, and a disagreement away from one would be a real defect. Both
// numbers are printed.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "EnginePrivate.h"
#include "cluster_draw.h"
#include "cluster_fixtures.h"
#include "../support/clustermesh.h"

#include "photon/voxel/PhotonVoxelizer.h"

#include <OgreHlmsCompute.h>
#include <OgreHlmsComputeJob.h>
#include <OgreHlmsManager.h>
#include <OgreRenderSystem.h>
#include <OgreResourceTransition.h>
#include <OgreRoot.h>
#include <Vao/OgreAsyncTicket.h>
#include <Vao/OgreUavBufferPacked.h>
#include <Vao/OgreVaoManager.h>

#include <QCoreApplication>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

/// SEVEN LEVELS over three decades of bound, so a sweep of the tolerance walks
/// the whole chain instead of flipping between two answers. The table holds
/// eight levels per mesh (GpuScene::kLevelsPerMesh), which is the ceiling this
/// deliberately sits just under.
static const std::vector<float> kBounds = { 0.0008f, 0.002f, 0.006f, 0.018f,
                                            0.05f,   0.14f,  0.4f };
/// THE DECOY (ATOM-HOUSE-1, the audit's finding b): a second mesh with OTHER bounds and one
/// level fewer, attached FIRST, so the chained mesh's entry in the GPU scene's tables is not 0
/// and every `meshIndex * stride + level` the shaders compute is a real stride.
static const std::vector<float> kDecoyBounds = { 0.001f, 0.004f, 0.011f, 0.03f, 0.09f, 0.25f };

static MeshData chainedMesh(const std::vector<float> &bounds = kBounds)
{
    const int N = 32;
    MeshData d;
    for (int z = 0; z <= N; ++z)
        for (int x = 0; x <= N; ++x) {
            d.positions.insert(d.positions.end(), { float(x) / float(N) - 0.5f, 0.0f,
                                                    float(z) / float(N) - 0.5f });
            d.normals.insert(d.normals.end(), { 0.0f, 1.0f, 0.0f });
        }
    auto build = [&](int step) {
        std::vector<unsigned> idx;
        for (int z = 0; z < N; z += step)
            for (int x = 0; x < N; x += step) {
                const unsigned a = unsigned(z * (N + 1) + x);
                const unsigned b = unsigned(z * (N + 1) + x + step);
                const unsigned c = unsigned((z + step) * (N + 1) + x + step);
                const unsigned e = unsigned((z + step) * (N + 1) + x);
                idx.insert(idx.end(), { a, b, c, a, c, e });
            }
        return idx;
    };
    d.indices = build(1);
    // Seven extra index lists, one per bound. The geometry of a level is
    // irrelevant here — only its BOUND and the fact that the level exists are —
    // so the coarsest few repeat the coarsest lattice the mesh can express.
    const int steps[7] = { 2, 4, 8, 16, 32, 32, 32 };
    for (size_t i = 0; i < bounds.size() && i < 7u; ++i) d.lodIndices.push_back(build(steps[i]));
    d.lodBounds = bounds;
    d.lodErrors = bounds;
    return d;
}

// ---- THE FOURTH COPY: THE CLUSTER CUT (ATOM stage 2, lane ATOM-CLUSTER-1) --------
//
// The cut's rule is the same currency applied PER GROUP of a mesh's cluster DAG
// (Types.h `clusterGroupAllowed` / `clusterGroupAffordable` / `clusterDrawn`), and
// its GLSL twin is the product piece media/Hlms/Jahshaka/JahClusterCut.glsl — the
// piece the product's cut job includes (the FIFTH copy below drives that job). This
// copy isolates the rule: the device half runs through a TEST job (tests/atom/media) the cluster
// harness dispatches over the DAG's own tables: one thread per (view, cluster),
// each writing whether the rule draws that cluster. It must pick THE SAME SET as
// the C++ `clusterCut`, bit for bit, over the harness meshes (their DAGs baked by
// the product's own bake) at 20 distances x 6 scales x 3 tolerances, with the
// instance ROTATED and scaled NON-UNIFORMLY so the rows of its transform and the
// level rule's column scale are exercised, not just the translation. Answers
// within 1e-4 of a threshold are counted over every evaluation, as the three
// older copies count them, and a disagreement at one is reported apart (float
// ordering between two compilers) but still fails.
// ---- THE EMPTY TOP GROUP (ATOM-6 stage 2c) --------------------------------------------------
// A group the bake's prune emptied (every component removed whole) is a GROUP with its measured error and NO output
// clusters: affordable, the object draws nothing there (a card under its own extent vanishes). The runtime must
// agree on it: the twin and the device draw nothing for an affordable empty group and not its members. Since ATOM-2
// every mesh's DAG ends in an empty top (its error the last cluster's extent), so a far view affords it on every
// fixture and the whole object vanishes; the card soup (tests/support/clustermesh.h) stays as the many-islands case.
// THE HARNESS MESHES (ATOM-TESTS-BACK-1 stage 1): the product's primitives, the closed sphere and the procedural shell
// at the Physics sample's 4,936-triangle class (proceduralShell(4936): 5,292) — no sample model.
static void clusterCutParity(Engine *e)
{
    std::printf("\n== the fourth copy: the CLUSTER CUT, C++ against the GLSL piece ==\n");
    clusterdraw::GpuCut gpu;
    std::string err;
    if (!gpu.init(e, err)) { std::printf("FAIL: the parity job: %s\n", err.c_str()); ++failures; return; }

    const std::string prim = std::string(JAHSHAKA_TEST_SOURCE_DIR) + "/app/content/primitives/";
    std::vector<std::pair<std::string, iris::MeshPtr>> meshes;
    for (const char *name : { "hp_sphere.obj", "torus.obj", "teapot.obj", "capsule.obj" }) {
        const auto ms = clusterfix::loadModel(prim + name);
        if (!ms.empty()) meshes.push_back({ name, ms.front() });
    }
    meshes.push_back({ "shell-4936", clusterfix::meshFromShell(enginetest::proceduralShell(4936)) });
    meshes.push_back({ "closed-sphere-20k", clusterfix::meshFromShell(clustermesh::closedSphere()) });
    meshes.push_back({ "cards", clusterfix::meshFromShell(clustermesh::cardSoup(400)) });   // ATOM-6's empty-top fixture

    // The instance: rotated 30 degrees about Y then 20 about X, scaled NON-UNIFORMLY
    // (1 : 0.6 : 1.3 along its own axes, times the sweep's scale), placed `dist`
    // metres down -Z; the eye at the origin. Non-uniform under a rotation is the
    // case where the longest ROW and the longest COLUMN differ, so both halves must
    // take the scale the level rule takes — `worldMaxAxisScale` here, its twin
    // `jahWorldMaxAxisScale` on the device (JahLevelRule_piece_cs.any), from the
    // same rows.
    const float cy = std::cos(0.5236f), sy = std::sin(0.5236f), cx = std::cos(0.3491f), sx = std::sin(0.3491f);
    const float R[3][3] = { { cy, 0.0f, sy }, { sx * sy, cx, -sx * cy }, { -cx * sy, sx, cx * cy } };
    const float axis[3] = { 1.0f, 0.6f, 1.3f };
    // The tiers' 1 / 2 / 3 / 4 px (the World Mode's atomPixelError row, ATOM-3) and half a pixel.
    const float tols[5] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f };
    const float scales[6] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
    unsigned long long evaluated = 0, mismatched = 0, nearThreshold = 0, nearMismatched = 0, drawnTotal = 0;
    // THE ROOT CUT (ATOM-7 stage 4): the device's jahClusterRootDrawn against Types.h clusterRootDrawn at every
    // (view, cluster). Since ATOM-2 every mesh's DAG ends in an EMPTY TOP (no terminal group exists any more), so on
    // every mesh the root cut is exactly the members of the empty groups — the last non-empty groups' clusters — and
    // never nothing.
    unsigned long long rootEvaluated = 0, rootMismatched = 0, emptyTopRootClusters = 0, emptyTopRootWrong = 0;
    size_t emptyTopMeshes = 0;
    size_t meshesRun = 0;
    for (const auto &m : meshes) {
        clusterfix::Fixture f;
        if (!clusterfix::bake(f, m.second, m.first) || f.data.clusters.empty()) continue;
        ++meshesRun;
        std::vector<ClusterCutView> views;
        for (int di = 0; di < 20; ++di) {
            const float dist = std::pow(500.0f, float(di) / 19.0f);   // 1 m .. 500 m
            for (float sc : scales)
                for (float tol : tols) {
                    ClusterCutView v;
                    for (int r = 0; r < 3; ++r) {
                        for (int c = 0; c < 3; ++c) v.worldRow[r][c] = R[r][c] * axis[c] * sc;
                        v.worldRow[r][3] = r == 2 ? -dist : 0.0f;
                    }
                    v.scale = worldMaxAxisScale(&v.worldRow[0][0]);
                    v.tolerance = tol;
                    v.projScaleY = 1.0f / std::tan(30.0f * 3.14159265f / 180.0f);
                    v.viewportHeight = 1080.0f;
                    views.push_back(v);
                }
        }
        std::vector<unsigned char> drawn, rootDrawn;
        if (!gpu.run(f.data.clusterGroups, f.data.clusters, views, drawn, err, &rootDrawn)) {
            std::printf("FAIL: %s: the device run: %s\n", m.first.c_str(), err.c_str());
            ++failures;
            continue;
        }
        const size_t nc = f.data.clusters.size();
        {
            const std::vector<unsigned> produced = clusterProducedCounts(f.data.clusters, f.data.clusterGroups.size());
            bool emptyGroups = false;
            for (size_t g = 0; g < produced.size(); ++g)
                if (produced[g] == 0u && f.data.clusterGroups[g].error < kClusterTerminal) emptyGroups = true;
            size_t rootCount = 0, meshRoot = 0;
            for (size_t c = 0; c < nc; ++c) {
                const MeshCluster &cl = f.data.clusters[c];
                const bool cpu = clusterRootDrawn(cl, f.data.clusterGroups, produced);
                rootCount += cpu ? 1u : 0u;
                for (size_t vi = 0; vi < views.size(); ++vi) {
                    ++rootEvaluated;
                    if ((rootDrawn[vi * nc + c] != 0) != cpu) ++rootMismatched;
                }
                const bool member = produced[size_t(cl.group)] == 0u &&
                                    f.data.clusterGroups[size_t(cl.group)].error < kClusterTerminal &&
                                    (cl.refined < 0 || f.data.clusterGroups[size_t(cl.refined)].error < kClusterTerminal);
                if (member != cpu) ++emptyTopRootWrong;
                emptyTopRootClusters += cpu ? 1u : 0u;
                meshRoot += cpu ? 1u : 0u;
            }
            if (emptyGroups && meshRoot > 0) ++emptyTopMeshes;
            std::printf("   %-16s the root cut: %zu clusters (%s)\n", m.first.c_str(), rootCount,
                        emptyGroups ? "an empty top" : "NO EMPTY TOP");
        }
        unsigned long long meshBad = 0, meshNear = 0, meshNearBad = 0;
        size_t distinct = 0;
        std::vector<unsigned> cut, previous;
        for (size_t vi = 0; vi < views.size(); ++vi) {
            clusterCut(f.data.clusterGroups, f.data.clusters, views[vi], cut);
            if (cut != previous) ++distinct;
            previous = cut;
            std::vector<unsigned char> cpu(nc, 0);
            for (unsigned c : cut) cpu[c] = 1;
            for (size_t c = 0; c < nc; ++c) {
                ++evaluated;
                drawnTotal += cpu[c];
                // NEAR A THRESHOLD, counted over EVERY evaluation (as the three
                // older copies count it): either of the two groups the answer reads
                // is afforded within 1e-4 of its error.
                bool near = false;
                for (int g : { f.data.clusters[c].group, f.data.clusters[c].refined }) {
                    if (g < 0) continue;
                    const MeshClusterGroup &gr = f.data.clusterGroups[size_t(g)];
                    const float a = clusterGroupAllowed(gr, views[vi]);
                    if (gr.error < FLT_MAX && std::fabs(a - gr.error) <= 1.0e-4f * gr.error) near = true;
                }
                if (near) ++meshNear;
                if (cpu[c] == drawn[vi * nc + c]) continue;
                if (near) ++meshNearBad; else ++meshBad;
            }
        }
        mismatched += meshBad;
        nearThreshold += meshNear;
        nearMismatched += meshNearBad;
        std::printf("   %-16s %4zu clusters %3zu groups, %zu views, %4zu distinct cuts: %llu disagreements "
                    "(+%llu at a threshold); %llu answers within 1e-4 of a threshold\n", m.first.c_str(), nc,
                    f.data.clusterGroups.size(), views.size(), distinct, meshBad, meshNearBad, meshNear);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "%s: the sweep reaches %zu distinct cuts", m.first.c_str(), distinct);
        CHECK(distinct >= 4u, msg);
    }
    char msg[200];
    std::snprintf(msg, sizeof(msg), "the cluster cut ran on %zu harness meshes", meshesRun);
    CHECK(meshesRun >= 5u, msg);
    std::snprintf(msg, sizeof(msg),
                  "THE GLSL CUT AND THE C++ CUT PICK THE SAME CLUSTERS: %llu (view, cluster) answers, "
                  "%llu drawn, %llu disagreements (%llu of them at a threshold); %llu answers within 1e-4 "
                  "of a threshold",
                  evaluated, drawnTotal, mismatched + nearMismatched, nearMismatched, nearThreshold);
    CHECK(mismatched == 0u && nearMismatched == 0u, msg);
    std::snprintf(msg, sizeof(msg), "THE ROOT CUT (ATOM-7): the device's jahClusterRootDrawn = Types.h clusterRootDrawn "
                  "at %llu (view, cluster) answers (%llu differ)", rootEvaluated, rootMismatched);
    CHECK(rootEvaluated > 0u && rootMismatched == 0u, msg);
    std::snprintf(msg, sizeof(msg), "the empty top on every mesh (%zu of %zu): the root cut draws the last non-empty "
                  "groups' %llu clusters (%llu wrong), never nothing", emptyTopMeshes, meshesRun, emptyTopRootClusters,
                  emptyTopRootWrong);
    CHECK(emptyTopMeshes == meshesRun && emptyTopRootClusters > 0u && emptyTopRootWrong == 0u, msg);
}

// ---- THE FIFTH COPY: THE PRODUCT'S CUT (ATOM-CLUSTER-CUT) --------------------------
//
// The fourth copy runs the GLSL rule through a TEST job over one mesh's tables. The
// PRODUCT runs it in the cull's cut job (JahCullCut_cs.glsl) over the GPU scene's
// cluster tables, with the instance's rows from the scene's table, the eye and
// footprint from the request, and the drawn set COMPACTED into records — so this
// arm drives the real thing: each harness mesh's baked DAG attached to eight
// instances (inside its bounds out to 500 radii, scaled 0.5-4, one rotated and
// scaled non-uniformly), the cull asked in mode 3 through the public gpuCull door
// at seven parameter sets (tolerance, lens, height; one orthographic), and the drawn
// set read back per instance. It must be CELL-IDENTICAL to Types.h's clusterCut on
// the same rows; disagreements at a threshold (within 1e-4 of a group's error) are
// reported apart and still fail. Nothing may overflow the stream's budget here.
static void productCutParity(Engine *e, View *view)
{
    std::printf("\n== the fifth copy: THE PRODUCT'S CUT (the cull's mode 3) against Types.h clusterCut ==\n");
    const std::string prim = std::string(JAHSHAKA_TEST_SOURCE_DIR) + "/app/content/primitives/";
    std::vector<std::pair<std::string, iris::MeshPtr>> meshes;
    for (const char *name : { "hp_sphere.obj", "torus.obj", "teapot.obj", "capsule.obj", "hemisphere.obj" }) {
        const auto ms = clusterfix::loadModel(prim + name);
        if (!ms.empty()) meshes.push_back({ name, ms.front() });
    }
    meshes.push_back({ "shell-4936", clusterfix::meshFromShell(enginetest::proceduralShell(4936)) });
    meshes.push_back({ "closed-sphere-20k", clusterfix::meshFromShell(clustermesh::closedSphere()) });
    meshes.push_back({ "cards", clusterfix::meshFromShell(clustermesh::cardSoup(400)) });   // ATOM-6's empty-top fixture

    struct Set { float tol, fovDeg, height; bool ortho; };
    // ...and the tiers' 2 and 3 px (ATOM-3 stage 1: High's and Medium's rows).
    const Set sets[7] = { { 0.5f, 45.0f, 1080.0f, false }, { 1.0f, 45.0f, 1080.0f, false },
                          { 4.0f, 90.0f, 720.0f, false },  { 1.0f, 110.0f, 2376.0f, false },
                          { 1.0f, 45.0f, 1080.0f, true },  { 2.0f, 45.0f, 1080.0f, false },
                          { 3.0f, 60.0f, 1080.0f, false } };
    unsigned long long pairs = 0, bad = 0, nearBad = 0, nearCount = 0, drawnTotal = 0, overflow = 0;
    unsigned long long emptyTopVanished = 0;
    unsigned depthHist[16] = {};
    size_t meshesRun = 0;
    for (const auto &m : meshes) {
        clusterfix::Fixture f;
        if (!clusterfix::bake(f, m.second, m.first) || f.data.clusters.empty()) continue;
        Scene *sc = e->createScene("cut-" + m.first);
        view->setScene(sc);
        const MeshId mesh = sc->createMesh(f.data);
        PbrParams p; p.albedo = Colour(0.7f, 0.7f, 0.7f); p.roughness = 0.6f;
        const MaterialId mat = sc->createPbrMaterial(p);
        const float ext = std::max(f.extent, 1e-3f);
        // Eight instances: at 0.3 extents (inside the bounds), then 2 .. 500 extents.
        const float at[8] = { 0.3f, 2.0f, 5.0f, 12.0f, 30.0f, 80.0f, 200.0f, 500.0f };
        const float scl[8] = { 1.0f, 0.5f, 2.0f, 1.0f, 4.0f, 1.0f, 0.5f, 2.0f };
        const Quat tilt(0.2f, 0.3f, -0.1f, 0.93f);
        std::map<NodeId, int> nodeOf;   // node id -> instance index
        for (int i = 0; i < 8; ++i) {
            const NodeId n = sc->createNode();
            sc->attachMesh(n, mesh, mat);
            const Vec3 pos(0.0f, 0.0f, -at[i] * ext);
            if (i == 3) sc->setNodeTransform(n, pos, tilt, Vec3(1.0f, 0.6f, 1.3f));
            else sc->setNodeTransform(n, pos, Quat(), Vec3(scl[i], scl[i], scl[i]));
            nodeOf[unsigned(n)] = i;
        }
        enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f));
        // FRAMES UNTIL THE TABLE HOLDS THEM (a freshly bound scene's first frames can
        // precede its first staged entries), never a fixed count.
        unsigned slots = 0, inTable = 0;
        std::vector<GpuSceneEntry> table;
        for (int frame = 0; frame < 60 && inTable < 8u; ++frame) {
            enginetest::frames(e, 1);
            // The table's dirty scan (engine.atom_draw (archived: archive/tests/engine)'s door): a scene whose frames
            // nothing in the view reads the table in (no GI, no rays) stages nothing.
            static_cast<jahshaka::engine::detail::OgreScene *>(sc)->ensureGpuScene(false);
            slots = sc->published().gpuScene.slotCount;
            table.assign(slots, GpuSceneEntry());
            inTable = 0;
            for (unsigned i = 0; i < slots; ++i)
                if (sc->gpuSceneEntry(i, table[i]) && nodeOf.count(table[i].nodeId)) ++inTable;
        }
        std::printf("   %-16s %u of 8 instances in the GPU scene's table (%u slots)\n", m.first.c_str(), inTable,
                    slots);
        unsigned long long meshBad = 0, meshNearBad = 0, meshNear = 0, meshPairs = 0;
        std::set<std::vector<unsigned>> distinctCuts;
        for (const Set &ps : sets) {
            GpuCullRequest r;
            if (!e->fillCullView(view, r)) { std::printf("FAIL: fillCullView\n"); ++failures; break; }
            for (int i = 0; i < 24; ++i) r.planes[i] = 0.0f;
            for (int i = 0; i < 6; ++i) r.planes[i * 4 + 3] = 1.0e9f;
            r.hzbLevels = 0u;
            r.mode = 3u;
            r.flagsRequired = r.flagsForbidden = 0u;
            r.pixelTolerance = ps.tol;
            r.projScaleY = 1.0f / std::tan(ps.fovDeg * 0.5f * 3.14159265f / 180.0f);
            r.viewportHeight = ps.height;
            r.orthographic = ps.ortho;
            GpuCullResult res;
            if (!e->gpuCull(sc, view, r, /*readBack=*/true, res)) {
                std::printf("FAIL: %s: gpuCull: %s\n", m.first.c_str(), e->takeLastError().c_str());
                ++failures;
                break;
            }
            overflow += res.cutOverflow;
            std::map<unsigned, std::vector<unsigned>> gpuBySlot;
            for (size_t k = 0; k + 2 < res.cutDrawn.size(); k += 3) {
                gpuBySlot[res.cutDrawn[k]].push_back(res.cutDrawn[k + 1]);
                ++depthHist[std::min(res.cutDrawn[k + 2], 15u)];
            }
            for (unsigned slot = 0; slot < slots; ++slot) {
                if (!nodeOf.count(table[slot].nodeId)) continue;
                ClusterCutView v;
                for (int rr = 0; rr < 3; ++rr)
                    for (int c = 0; c < 4; ++c) v.worldRow[rr][c] = table[slot].world[rr * 4 + c];
                v.scale = worldMaxAxisScale(table[slot].world);
                for (int k = 0; k < 3; ++k) v.eye[k] = r.eye[k];
                v.tolerance = r.pixelTolerance;
                v.projScaleY = r.projScaleY;
                v.viewportHeight = r.viewportHeight;
                v.orthographic = r.orthographic;
                std::vector<unsigned> cpu;
                clusterCut(f.data.clusterGroups, f.data.clusters, v, cpu);
                std::vector<unsigned> gpu = gpuBySlot[slot];
                std::sort(gpu.begin(), gpu.end());
                std::sort(cpu.begin(), cpu.end());
                distinctCuts.insert(cpu);
                ++meshPairs;
                drawnTotal += cpu.size();
                // Near a threshold: any group of the mesh afforded within 1e-4 of its error.
                bool near = false;
                for (const MeshClusterGroup &gr : f.data.clusterGroups) {
                    if (gr.error >= FLT_MAX) continue;
                    const float a = clusterGroupAllowed(gr, v);
                    if (std::fabs(a - gr.error) <= 1.0e-4f * gr.error) { near = true; break; }
                }
                meshNear += near ? 1u : 0u;
                // THE CUT'S OWN LAWS (one cut: no drawn cluster under a drawn one, every level-0 triangle covered once)
                // are atom.cut_partition's, on the CPU (ATOM-TESTS-BACK-1): the device's set equal to clusterCut's, below,
                // carries them here.
                if (gpu.empty()) ++emptyTopVanished;   // every mesh ends in an empty top (ATOM-2)
                if (gpu == cpu) continue;
                if (near) ++meshNearBad; else ++meshBad;
                if (meshBad + meshNearBad <= 2u)
                    std::printf("   CUT DISAGREES: %s instance %d set %.1f px/%.0f deg/%.0f%s: GPU %zu clusters, "
                                "CPU %zu\n", m.first.c_str(), nodeOf[table[slot].nodeId], double(ps.tol),
                                double(ps.fovDeg), double(ps.height), ps.ortho ? " ortho" : "", gpu.size(), cpu.size());
            }
        }
        ++meshesRun;
        pairs += meshPairs;
        bad += meshBad;
        nearBad += meshNearBad;
        nearCount += meshNear;
        std::printf("   %-16s %5zu clusters %4zu groups: %llu (instance, view) cuts, %zu distinct, %llu differ "
                    "(+%llu at a threshold)\n", m.first.c_str(), f.data.clusters.size(),
                    f.data.clusterGroups.size(), meshPairs, distinctCuts.size(), meshBad, meshNearBad);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "%s: the product's sweep reaches %zu distinct cuts", m.first.c_str(),
                      distinctCuts.size());
        CHECK(distinctCuts.size() >= 3u, msg);
        view->setScene(nullptr);
        e->destroyScene(sc);
    }
    std::printf("   the drawn clusters' depths: ");
    for (int i = 0; i < 16; ++i) if (depthHist[i]) std::printf("%d:%u ", i, depthHist[i]);
    std::printf("\n");
    char msg[260];
    std::snprintf(msg, sizeof(msg), "the product's cut ran on %zu harness meshes", meshesRun);
    CHECK(meshesRun >= 6u, msg);
    std::snprintf(msg, sizeof(msg), "no instance overflowed the cut's stream (%llu)", overflow);
    CHECK(overflow == 0u, msg);
    std::snprintf(msg, sizeof(msg), "THE EMPTY TOP GROUP (ATOM-6): %llu (instance, view) cuts where the object vanished, "
                  "the device and the twin alike", emptyTopVanished);
    CHECK(emptyTopVanished > 0u, msg);
    std::snprintf(msg, sizeof(msg),
                  "THE PRODUCT'S CUT IS CELL-IDENTICAL TO clusterCut: %llu (instance, view) cuts, %llu clusters "
                  "drawn, %llu differ (%llu of them at a threshold; %llu cuts had a group within 1e-4)",
                  pairs, drawnTotal, bad + nearBad, nearBad, nearCount);
    CHECK(bad == 0u && nearBad == 0u, msg);
}

// ---- THE SIXTH COPY'S USER: THE VOXEL GATHER'S WALK (ATOM-HOUSE-1) ------------------
//
// The walk is ONE piece (`JahLevelRuleWalk`, media/Hlms/Jahshaka/JahLevelRule_piece_cs.any)
// with two users on the device: the cull's test job, which the 10,000 evaluations above
// drive, and the VOXEL GATHER (Jahshaka/VoxelGatherWrite, JahVoxelGather_cs.glsl), which
// spends the currency's CASCADE case — one sample is a cell, no distance term:
// allowed = allowedWorldError(tolerance, cell, scale), the scale the instance's longest
// column. Until this arm nothing ran the gather's copy against the header.
//
// THIS ARM RUNS THE PRODUCT'S GATHER JOB ITSELF over the same 660 instances (two meshes): the GPU
// scene's own mesh, level and partition tables; one octant over everything; one material
// bucket; and the instance table read back with the two words the Photon store would
// write — the GI-visible flag, and a material word whose slot half is the instance's own
// slot — so the job's predicates admit every instance, its walk alone picks the level,
// and every record it writes names its instance (the record's slot lane) and its level
// (its geometry row). For each instance the records must be EXACTLY the partitions of
// the level `lodLevelForWorldError` picks, one per partition, over cells that walk the
// whole chain at the product's tolerance (a 256th of a cell, OgreGi.cpp's
// kCascadeLodCellFraction), and AT EVERY BOUND: a cell of exactly 256 x a bound affords a
// scale-1 instance that bound itself, where the walk's STRICT `<` keeps the finer level
// (a `<=` would red here and nowhere else).
namespace voxelcase {
struct Record {   // JahVoxelGather_cs.glsl's Record (PhotonVoxelizer::kInstanceRecordBytes)
    float worldRow[3][4];
    float aabb0[4];
    float aabb1[4];   // w = the material SLOT, bit-cast
    uint32_t meshData[4];   // geometry row, first index, index count, flags
};
static_assert(sizeof(Record) == Ogre::PhotonVoxelizer::kInstanceRecordBytes, "the gather's record");

Ogre::DescriptorSetUav::BufferSlot slotOf(Ogre::UavBufferPacked *b, Ogre::ResourceAccess::ResourceAccess a)
{
    Ogre::DescriptorSetUav::BufferSlot s = Ogre::DescriptorSetUav::BufferSlot::makeEmpty();
    s.buffer = b;
    s.offset = 0;
    s.sizeBytes = 0;
    s.access = a;
    return s;
}
}   // namespace voxelcase

static void voxelGatherParity(Scene *scene, unsigned slots,
                              const std::map<unsigned, const std::vector<float> *> &boundsOfMesh)
{
    using namespace jahshaka::engine::detail;
    std::printf("\n== the voxel gather's walk (the one piece's second user) against Types.h ==\n");
    OgreScene *os = static_cast<OgreScene *>(scene);
    GpuScene &gs = os->gpuScene();
    Ogre::Root &root = Ogre::Root::getSingleton();
    Ogre::RenderSystem *rs = root.getRenderSystem();
    Ogre::HlmsCompute *hc = root.getHlmsManager()->getComputeHlms();
    Ogre::VaoManager *vm = rs->getVaoManager();
    Ogre::HlmsComputeJob *job = hc ? hc->findComputeJobNoThrow("Jahshaka/VoxelGatherWrite") : nullptr;
    if (!job || !gs.live()) { std::printf("FAIL: no gather job or no GPU scene\n"); ++failures; return; }
    // THE PRODUCT'S BRACKET: the partitions' AABB job is a Photon dispatch, made inside a Graphics GI region
    // (PhotonComponent::prepareGiInputs); the gather's dispatches below sit in one too, as runVoxelGather's do.
    PhotonComponent::GiRegion region(PhotonComponent::RegionKind::Graphics);
    gs.ensurePartitions(root.getHlmsManager(), rs, Ogre::PhotonVoxelizer::kIndicesPerPartition);
    if (!gs.partitionAabbBuffer() || !gs.partitionCount()) {
        std::printf("FAIL: the GPU scene built no partitions\n");
        ++failures;
        return;
    }

    // THE INSTANCE TABLE, as the device holds it, with the Photon store's two words.
    std::vector<GpuInstance> inst(slots);
    {
        Ogre::AsyncTicketPtr t = gs.instanceBuffer()->readRequest(0, slots);
        std::memcpy(inst.data(), t->map(), size_t(slots) * sizeof(GpuInstance));
        t->unmap();
    }
    uint32_t finestParts = 0;
    std::vector<uint32_t> meshOf(slots, 0xFFFFFFFFu);
    bool known = true;
    for (unsigned s = 0; s < slots; ++s) {
        uint32_t flags;
        std::memcpy(&meshOf[s], &inst[s].boundsMin[3], 4);
        std::memcpy(&flags, &inst[s].boundsMax[3], 4);
        known = known && boundsOfMesh.count(meshOf[s]);
        flags |= kGpuGiVisible;
        std::memcpy(&inst[s].boundsMax[3], &flags, 4);
        inst[s].ids[1] = s;   // pool 0 (the one bucket), slot = the instance's own slot
    }
    CHECK(known && boundsOfMesh.size() == 2u, "the voxel arm's instances name the two meshes' entries");
    if (!known) return;
    // geometry row -> (mesh entry, level); every level of both meshes has its own row
    std::map<uint32_t, std::pair<uint32_t, unsigned>> levelOfRow;
    size_t levelsExpected = 0;
    for (const auto &mb : boundsOfMesh) {
        levelsExpected += mb.second->size() + 1u;
        for (uint32_t l = 0; l < GpuScene::kLevelsPerMesh; ++l) {
            const GpuMeshLevel &lv = gs.levelAt(mb.first, l);
            if (lv.geomRow != GpuScene::kNoGeomRow) levelOfRow[lv.geomRow] = { mb.first, l };
            finestParts = std::max(finestParts, lv.partCount);
        }
    }
    char msg[260];
    std::snprintf(msg, sizeof(msg), "the two meshes' levels each have their own geometry row (%zu of %zu)",
                  levelOfRow.size(), levelsExpected);
    CHECK(levelOfRow.size() == levelsExpected, msg);
    const uint32_t capacity = std::max(finestParts * slots, 1u);

    Ogre::UavBufferPacked *bParams = vm->createUavBuffer(1u, sizeof(VoxelGatherParams), 0, nullptr, false);
    Ogre::UavBufferPacked *bInst = vm->createUavBuffer(slots, sizeof(GpuInstance), 0, inst.data(), false);
    Ogre::UavBufferPacked *bWork = vm->createUavBuffer(2u, sizeof(uint32_t), 0, nullptr, false);
    const uint32_t range[2] = { 0u, capacity };   // the one (octant, bucket) range: all of the records
    Ogre::UavBufferPacked *bRanges = vm->createUavBuffer(1u, sizeof(range), 0, const_cast<uint32_t *>(range), false);
    Ogre::UavBufferPacked *bRecords = vm->createUavBuffer(capacity, sizeof(voxelcase::Record), 0, nullptr, false);
    Ogre::UavBufferPacked *bReadout = vm->createUavBuffer(kReadWords, sizeof(uint32_t), 0, nullptr, false);
    Ogre::UavBufferPacked *bMask = vm->createUavBuffer(1u, sizeof(uint32_t), 0, nullptr, false);

    // The product's tolerance (OgreGi.cpp kCascadeLodCellFraction: a 256th of a cell) over cells that take the
    // chain from level 0 (the finest instance at 5 cm cells) to past its last bound (the coarsest at 1 km).
    const float tolerance = 1.0f / 256.0f;
    std::vector<float> cells = { 0.05f, 0.25f, 1.0f, 4.0f, 16.0f, 64.0f, 256.0f, 1024.0f };
    for (float b : kBounds) cells.push_back(b * 256.0f);   // exact: a power-of-two scale of the bound's float
    unsigned evaluated = 0, mismatched = 0, missing = 0, wrongCount = 0, mixed = 0, nearThreshold = 0, atBound = 0;
    unsigned overflow = 0, histogram[8] = {};
    bool ran = true;
    for (float cell : cells) {
        VoxelGatherParams p;
        p.counts[0] = slots;
        p.counts[1] = 1u;   // one octant
        p.counts[2] = 1u;   // one material bucket
        p.counts[3] = kGatherLod;
        p.caps[0] = capacity;
        p.caps[1] = 1u;
        p.caps[2] = Ogre::PhotonVoxelizer::kIndicesPerPartition;
        p.lod[0] = cell;
        p.lod[1] = tolerance;
        for (int i = 0; i < 3; ++i) { p.octMin[0][i] = -1.0e9f; p.octMax[0][i] = 1.0e9f; }
        bParams->upload(&p, 0, 1u);
        const uint32_t zeros[kReadWords] = {};
        bWork->upload(zeros, 0, 2u);
        bReadout->upload(zeros, 0, kReadWords);
        std::vector<voxelcase::Record> rec;
        uint32_t readout[kReadWords] = {};
        try {
            GpuScene::setTableStrideProperties(job);
            job->_setUavBuffer(0u, voxelcase::slotOf(bParams, Ogre::ResourceAccess::Read));
            job->_setUavBuffer(1u, voxelcase::slotOf(bInst, Ogre::ResourceAccess::Read));
            job->_setUavBuffer(2u, voxelcase::slotOf(gs.meshBuffer(), Ogre::ResourceAccess::Read));
            job->_setUavBuffer(3u, voxelcase::slotOf(gs.levelBuffer(), Ogre::ResourceAccess::Read));
            job->_setUavBuffer(4u, voxelcase::slotOf(gs.partitionAabbBuffer(), Ogre::ResourceAccess::Read));
            job->_setUavBuffer(5u, voxelcase::slotOf(bWork, Ogre::ResourceAccess::ReadWrite));
            job->_setUavBuffer(6u, voxelcase::slotOf(bRanges, Ogre::ResourceAccess::Read));
            job->_setUavBuffer(7u, voxelcase::slotOf(bRecords, Ogre::ResourceAccess::Write));
            job->_setUavBuffer(8u, voxelcase::slotOf(bReadout, Ogre::ResourceAccess::ReadWrite));
            job->_setUavBuffer(9u, voxelcase::slotOf(bMask, Ogre::ResourceAccess::Read));
            job->setNumThreadGroups((slots + kGatherThreadsPerGroup - 1u) / kGatherThreadsPerGroup, 1u, 1u);
            Ogre::ResourceTransitionArray &rt = rs->getBarrierSolver().getNewResourceTransitionsArrayTmp();
            job->analyzeBarriers(rt);
            rs->executeResourceTransition(rt);
            hc->dispatch(job, 0, 0);
            Ogre::AsyncTicketPtr tr = bReadout->readRequest(0, kReadWords);
            std::memcpy(readout, tr->map(), sizeof(readout));
            tr->unmap();
            const uint32_t written = std::min(readout[kReadRecords], capacity);
            rec.resize(written);
            if (written) {
                Ogre::AsyncTicketPtr t = bRecords->readRequest(0, written);
                std::memcpy(rec.data(), t->map(), size_t(written) * sizeof(voxelcase::Record));
                t->unmap();
            }
        } catch (Ogre::Exception &ex) {
            std::printf("FAIL: the gather job: %s\n", ex.getFullDescription().c_str());
            ++failures;
            ran = false;
        }
        job->clearUavBuffers();
        if (!ran) break;
        overflow += readout[kReadOverflow];
        // Per instance: the levels its records name and how many there are.
        std::vector<unsigned> count(slots, 0u), level(slots, 0xFFFFFFFFu);
        std::vector<unsigned char> mixedAt(slots, 0u);
        for (const voxelcase::Record &r : rec) {
            uint32_t s;
            std::memcpy(&s, &r.aabb1[3], 4);
            const auto it = levelOfRow.find(r.meshData[0]);
            if (s >= slots || it == levelOfRow.end() || it->second.first != meshOf[s]) { ++wrongCount; continue; }
            if (level[s] != 0xFFFFFFFFu && level[s] != it->second.second) mixedAt[s] = 1u;
            level[s] = it->second.second;
            ++count[s];
        }
        for (unsigned s = 0; s < slots; ++s) {
            const float scale = worldMaxAxisScale(inst[s].world);
            const float allowed = allowedWorldError(tolerance, cell, scale);
            const std::vector<float> &bounds = *boundsOfMesh.at(meshOf[s]);
            const unsigned cpu = unsigned(lodLevelForWorldError(bounds, allowed, bounds.size() + 1u));
            ++evaluated;
            for (float b : bounds) {
                if (b > 0.0f && std::fabs(allowed - b) / b < 1.0e-4f) ++nearThreshold;
                if (allowed == b) ++atBound;
            }
            if (level[s] == 0xFFFFFFFFu) { ++missing; continue; }
            mixed += mixedAt[s];
            if (level[s] < 8u) ++histogram[level[s]];
            if (count[s] != gs.levelAt(meshOf[s], level[s]).partCount) ++wrongCount;
            if (level[s] != cpu) {
                if (++mismatched <= 3u)
                    std::printf("   GATHER DISAGREES: slot %u (scale %.3f) at a %.2f m cell: device level %u, "
                                "Types.h %u (allowed %.6g)\n", s, double(scale), double(cell), level[s], cpu, allowed);
            }
        }
    }
    vm->destroyUavBuffer(bMask);
    vm->destroyUavBuffer(bReadout);
    vm->destroyUavBuffer(bRecords);
    vm->destroyUavBuffer(bRanges);
    vm->destroyUavBuffer(bWork);
    vm->destroyUavBuffer(bInst);
    vm->destroyUavBuffer(bParams);
    if (!ran) return;

    std::printf("   %u evaluations (%u instances x %zu cells at a 1/256 cell tolerance)\n   levels taken: ", evaluated,
                slots, cells.size());
    for (int i = 0; i < 8; ++i) std::printf("%u:%u ", i, histogram[i]);
    std::printf("\n   %u evaluations within 1e-4 of a level boundary, %u EXACTLY at one\n", nearThreshold, atBound);
    std::snprintf(msg, sizeof(msg), "every instance wrote records at every cell (%u missing) and none overflowed (%u)",
                  missing, overflow);
    CHECK(missing == 0u && overflow == 0u, msg);
    std::snprintf(msg, sizeof(msg), "each instance's records are exactly its level's partitions, one level an instance "
                  "(%u wrong, %u mixed)", wrongCount, mixed);
    CHECK(wrongCount == 0u && mixed == 0u, msg);
    unsigned distinct = 0;
    for (int i = 0; i < 8; ++i) if (histogram[i]) ++distinct;
    std::snprintf(msg, sizeof(msg), "the gather's sweep reached %u distinct levels of the eight", distinct);
    CHECK(distinct >= 5u, msg);
    std::snprintf(msg, sizeof(msg), "THE VOXEL GATHER'S WALK AGREES WITH THE C++ CURRENCY on all %u, two meshes (%u disagree)",
                  evaluated, mismatched);
    CHECK(evaluated == slots * unsigned(cells.size()) && mismatched == 0u, msg);
    std::snprintf(msg, sizeof(msg), "THE STRICT `<` IS REACHED: %u evaluations afforded exactly a bound (agreeing above)",
                  atBound);
    CHECK(atBound > 0u, msg);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-engine-lod-parity-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();

    View *view = e->createOffscreenView("parity", 320u, 240u, Colour(0, 0, 0), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("parity");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    // THE STOCK ROUTE (SHADOW-LOD-1): the per-object chain is what stock-PBR items and
    // every stock caster draw; an Atom-routed item is drawn by the cluster cut and no
    // view pass walks its level any more (the fork's LOD walk skips the queue the id
    // pass draws), so the chain is measured with the split off.
    scene->setAtomDrawEnabled(false);
    scene->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.2f, 0.2f, 0.2f));

    PbrParams p; p.albedo = Colour(0.7f, 0.7f, 0.7f); p.roughness = 0.6f;
    const MaterialId mat = scene->createPbrMaterial(p);
    // WHAT EACH SLOT HOLDS is read by its NODE, never by its slot's number (the decoys come first).
    enum Kind { kStraight, kTurned, kDecoy };
    std::unordered_map<unsigned long long, int> kindOf;
    const MeshId decoy = scene->createMesh(chainedMesh(kDecoyBounds));
    const int kDecoys = 60;
    for (int i = 0; i < kDecoys; ++i) {
        const NodeId n = scene->createNode();
        if (!scene->attachMesh(n, decoy, mat)) { std::printf("FAIL: attach (decoy)\n"); return 1; }
        const float s = 0.5f * std::pow(2.0f, float(i % 4));
        enginetest::setNodePosition(scene, n, Vec3(2.0f, 0.0f, -(3.0f + 7.0f * float(i))));
        enginetest::setNodeScale(scene, n, Vec3(s, s, s));
        kindOf[(unsigned long long)n] = kDecoy;
    }
    const MeshId mesh = scene->createMesh(chainedMesh());
    const int kInstances = 500;
    // WHAT EACH NODE IS, so the DRAW PATH's answer can be checked against the
    // rule without going through the GPU table (the strategy arm below reads
    // `objectLods()`, which is keyed by NodeId).
    struct Placed { float dist, scale; };
    std::unordered_map<unsigned long long, Placed> placed;
    for (int i = 0; i < kInstances; ++i) {
        const NodeId n = scene->createNode();
        if (!scene->attachMesh(n, mesh, mat)) { std::printf("FAIL: attach\n"); return 1; }
        // 1 m to 500 m along -Z, and a scale that walks 0.25 .. 8 so the
        // mesh-units term of the currency is exercised over five octaves.
        const float dist = 1.0f + float(i);
        const float s = 0.25f * std::pow(2.0f, float(i % 6));
        enginetest::setNodePosition(scene, n, Vec3(0.0f, 0.0f, -dist));
        enginetest::setNodeScale(scene, n, Vec3(s, s, s));
        placed[(unsigned long long)n] = { dist, s };
        kindOf[(unsigned long long)n] = kStraight;
    }
    const int kRotated = 100;
    const float c1 = std::cos(0.5f * 45.0f * 3.14159265f / 180.0f);
    const float s1 = std::sin(0.5f * 45.0f * 3.14159265f / 180.0f);
    const float c2 = std::cos(0.5f * 30.0f * 3.14159265f / 180.0f);
    const float s2 = std::sin(0.5f * 30.0f * 3.14159265f / 180.0f);
    const Quat yawPitch(c1 * s2, c2 * s1, -s1 * s2, c1 * c2);   // yaw 45 then pitch 30
    for (int i = 0; i < kRotated; ++i) {
        const NodeId n = scene->createNode();
        if (!scene->attachMesh(n, mesh, mat)) { std::printf("FAIL: attach\n"); return 1; }
        const float dist = 2.0f + 3.0f * float(i);
        scene->setNodeTransform(n, Vec3(0.0f, 0.0f, -dist), yawPitch, Vec3(1.0f, 4.0f, 1.0f));
        kindOf[(unsigned long long)n] = kTurned;
    }
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f));
    enginetest::frames(e, 3);

    const unsigned slots = scene->published().gpuScene.slotCount;
    CHECK(slots == unsigned(kInstances + kRotated + kDecoys), "every instance is in the table");
    std::vector<GpuSceneEntry> table;
    for (unsigned i = 0; i < slots; ++i) {
        GpuSceneEntry en;
        if (scene->gpuSceneEntry(i, en)) table.push_back(en);
    }
    CHECK(table.size() == slots, "the table reads back");
    // Per slot: its kind and its mesh's bounds; per mesh entry: its bounds (the voxel arm's).
    std::vector<int> kindAt(slots, -1);
    std::map<unsigned, const std::vector<float> *> boundsOfMesh;
    unsigned chainedEntry = 0xFFFFFFFFu, decoyEntry = 0xFFFFFFFFu;
    for (unsigned i = 0; i < slots && i < table.size(); ++i) {
        const auto k = kindOf.find((unsigned long long)table[i].nodeId);
        kindAt[i] = k == kindOf.end() ? -1 : k->second;
        if (kindAt[i] == kDecoy) decoyEntry = table[i].meshIndex;
        else if (kindAt[i] >= 0) chainedEntry = table[i].meshIndex;
    }
    const auto boundsAt = [&](unsigned slot) -> const std::vector<float> & {
        return kindAt[slot] == kDecoy ? kDecoyBounds : kBounds;
    };
    boundsOfMesh[chainedEntry] = &kBounds;
    boundsOfMesh[decoyEntry] = &kDecoyBounds;
    {
        bool one = true;
        for (unsigned i = 0; i < slots && i < table.size(); ++i)
            one = one && kindAt[i] >= 0 && table[i].meshIndex == (kindAt[i] == kDecoy ? decoyEntry : chainedEntry);
        char m[200];
        std::snprintf(m, sizeof(m), "THE STRIDE IS REAL: the chained mesh's entry is %u, the decoy's %u, every slot "
                      "names its own", chainedEntry, decoyEntry);
        CHECK(one && chainedEntry != decoyEntry && chainedEntry != 0u && chainedEntry != 0xFFFFFFFFu, m);
    }
    {
        bool truth = table.size() == slots;
        float rowMax = 0.0f;
        for (unsigned i = 0; truth && i < slots; ++i) {
            if (kindAt[i] != kTurned) continue;
            truth = std::fabs(worldMaxAxisScale(table[i].world) - 4.0f) < 1.0e-4f;
            for (int row = 0; row < 3; ++row) {
                const float *w = &table[i].world[row * 4];
                rowMax = std::max(rowMax, std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]));
            }
        }
        char m[200];
        std::snprintf(m, sizeof(m), "the rotated group's largest axis scale is the authored 4 by "
                                    "COLUMN (the longest ROW reads %.4f)", rowMax);
        CHECK(truth && rowMax < 3.99f, m);
    }

    // THE 20 PARAMETER SETS. proj[1][1] of a 20 to 110 degree vertical lens, the
    // heights a desktop, a 4K and a VR eye render at, and tolerances from a
    // quarter of a pixel (finer than any tier) to four (coarser than Low).
    struct Params { float tol, projScaleY, height; };
    std::vector<Params> sets;
    const float fovs[5] = { 20.0f, 45.0f, 60.0f, 90.0f, 110.0f };
    const float heights[4] = { 720.0f, 1080.0f, 2160.0f, 2376.0f };
    const float tols[4] = { 0.25f, 0.5f, 1.0f, 4.0f };
    for (int i = 0; i < 20; ++i) {
        const float fov = fovs[i % 5];
        const float p11 = 1.0f / std::tan(fov * 0.5f * 3.14159265f / 180.0f);
        sets.push_back({ tols[i % 4], p11, heights[i % 4] });
    }

    unsigned mismatchedRotated = 0;
    unsigned evaluated = 0, mismatched = 0, nearThreshold = 0, histogram[8] = {};
    unsigned firstBadSlot = 0xFFFFFFFFu, firstBadGpu = 0, firstBadCpu = 0;
    for (const Params &ps : sets) {
        GpuCullRequest r;
        if (!e->fillCullView(view, r)) { std::printf("FAIL: fillCullView\n"); return 1; }
        // EVERY INSTANCE MUST REACH THE LEVEL WALK: a plane that nothing can be
        // outside of. (0, 0, 0, d) with d > 0 puts every point inside, whatever
        // the AABB — the shader's positive-vertex test reduces to `d >= 0`.
        for (int i = 0; i < 24; ++i) r.planes[i] = 0.0f;
        for (int i = 0; i < 6; ++i) r.planes[i * 4 + 3] = 1.0e9f;
        r.hzbLevels = 0u;
        r.mode = 1u;
        r.pixelTolerance = ps.tol;
        r.projScaleY = ps.projScaleY;
        r.viewportHeight = ps.height;
        GpuCullResult res;
        if (!e->gpuCull(scene, view, r, /*readBack=*/true, res)) {
            std::printf("FAIL: gpuCull: %s\n", e->takeLastError().c_str());
            ++failures;
            break;
        }
        if (res.survivors != slots) {
            std::printf("FAIL: %u of %u instances survived the permissive frustum\n",
                        res.survivors, slots);
            ++failures;
            break;
        }
        for (unsigned slot = 0; slot < slots && slot < res.levels.size(); ++slot) {
            const GpuSceneEntry &en = table[slot];
            const float scale = worldMaxAxisScale(en.world);
            const float cx = 0.5f * (en.boundsMin[0] + en.boundsMax[0]);
            const float cy = 0.5f * (en.boundsMin[1] + en.boundsMax[1]);
            const float cz = 0.5f * (en.boundsMin[2] + en.boundsMax[2]);
            const float dx = cx - r.eye[0], dy = cy - r.eye[1], dz = cz - r.eye[2];
            // The local radius is the mesh AABB's half-diagonal, which is what
            // `Mesh::_setBoundingSphereRadius(aabb.getRadius())` stores and what
            // the shader derives from the mesh table's own local bounds. The
            // fixture's mesh is a unit square in xz.
            const float radius = 0.5f * std::sqrt(2.0f) * scale;
            const float dist = std::max(0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - radius);
            const float footprint = sampleFootprintPerspective(dist, ps.projScaleY, ps.height);
            const float allowed = allowedWorldError(ps.tol, footprint, scale);
            const std::vector<float> &bounds = boundsAt(slot);
            const unsigned cpu =
                unsigned(lodLevelForWorldError(bounds, allowed, bounds.size() + 1u));
            const unsigned gpu = res.levels[slot];
            ++evaluated;
            if (gpu < 8u) ++histogram[gpu];
            for (float b : bounds)
                if (b > 0.0f && std::fabs(allowed - b) / b < 1.0e-4f) ++nearThreshold;
            if (gpu != cpu) {
                ++mismatched;
                if (kindAt[slot] == kTurned) ++mismatchedRotated;
                if (firstBadSlot == 0xFFFFFFFFu) {
                    firstBadSlot = slot;
                    firstBadGpu = gpu;
                    firstBadCpu = cpu;
                }
            }
        }
    }

    // ---- MODE 2: THE DRAW COMMANDS (JahCullDraws_cs; ATOM-HOUSE-1, the audit's finding a) ----------
    // The cull's job 3 runs only in mode 2, which no frame of the product's asks for but a measuring arm
    // (shadow.casterPbsProbe): this request is the one place it is compiled and run. Each command must
    // draw its slot's level exactly as the level table holds it - `levels[meshIndex * stride + level]`
    // over two mesh entries, the decoy's first - one instance, its slot as firstInstance.
    {
        GpuCullRequest r;
        if (!e->fillCullView(view, r)) { std::printf("FAIL: fillCullView (mode 2)\n"); return 1; }
        for (int i = 0; i < 24; ++i) r.planes[i] = 0.0f;
        for (int i = 0; i < 6; ++i) r.planes[i * 4 + 3] = 1.0e9f;
        r.hzbLevels = 0u;
        r.mode = 2u;
        r.pixelTolerance = 1.0f;
        r.projScaleY = 1.0f / std::tan(45.0f * 0.5f * 3.14159265f / 180.0f);
        r.viewportHeight = 1080.0f;
        GpuCullResult res;
        unsigned commands = 0, wrong = 0;
        std::set<unsigned> levelsDrawn, seen;
        if (!e->gpuCull(scene, view, r, /*readBack=*/true, res)) {
            std::printf("FAIL: gpuCull (mode 2): %s\n", e->takeLastError().c_str());
            ++failures;
        } else {
            const auto &gs = static_cast<jahshaka::engine::detail::OgreScene *>(scene)->gpuScene();
            for (size_t k = 0; k + 4 < res.drawCommands.size(); k += 5) {
                const unsigned *c = &res.drawCommands[k];
                const unsigned slot = c[4];
                ++commands;
                if (slot >= slots || slot >= res.levels.size() || !seen.insert(slot).second) { ++wrong; continue; }
                const unsigned level = res.levels[slot];
                const auto &L = gs.levelAt(table[slot].meshIndex, level);
                levelsDrawn.insert(level);
                if (c[0] != L.indexCount || c[1] != 1u || c[2] != L.firstIndex || c[3] != 0u) ++wrong;
            }
        }
        char m[220];
        std::snprintf(m, sizeof(m), "MODE 2: %u draw commands for %u slots (%u survivors), each drawing its slot's level "
                      "as the level table holds it (%u wrong; %zu distinct levels)", commands, slots, res.survivors,
                      wrong, levelsDrawn.size());
        CHECK(commands == slots && res.survivors == slots && wrong == 0u && levelsDrawn.size() >= 3u, m);
    }

    // ---- THE THIRD COPY: THE DRAW PATH'S OWN STRATEGY -----------------------
    //
    // The two copies above are the ones a compute shader forces on us. The
    // STRATEGY (`JahWorldErrorLodStrategy`, irisgl/engine/src/OgreMesh.cpp) is a
    // third evaluation of the same rule, in Ogre's own four-wide SoA loop, and
    // it is the one that decides which VAO is DRAWN — so a drift there is a
    // drift in the picture, not in a report. It was measurably NOT the same rule
    // until ATOM-RESUMES-1 item 1: it carried no `meshToWorldScale`, so a
    // 10x-scaled instance took a level whose real deviation was ten times what
    // it asked for, and nothing compared it with anything.
    //
    // ITS PARAMETERS ARE THE VIEW'S, not a sweep's: the strategy reads the
    // pass's own projection and render-target height, which is exactly what
    // `fillCullView` reports for this view, and its tolerance is the VIEW's
    // pixel-error threshold (Scene::setPixelTolerance, the World Mode's
    // `atomPixelError` row — ATOM-3 stage 1), which reaches the strategy through
    // the view camera's own LOD factor (OgreView::syncLodTolerance). So this arm
    // holds the SAME (distance x scale) fixture to the same rule at Epic's 1 px
    // AND at Medium's 3 px — the second is the wiring's proof: a camera that did
    // not carry the row would draw the 1 px levels there — read through
    // `objectLods()`, the byte the render queue indexes the VAO list with.
    for (const float stratTol : { kLodBudgetPixels, 3.0f }) {
        // The level is written by the LOD walk of a rendered frame; the cull
        // sweep above renders none, so the reading is taken from a fresh one.
        scene->setPixelTolerance(stratTol);
        enginetest::frames(e, 2);
        GpuCullRequest vr;
        if (!e->fillCullView(view, vr)) { std::printf("FAIL: fillCullView (view)\n"); return 1; }
        std::vector<ObjectLodDesc> drawn;
        scene->objectLods(drawn);
        // The rotated group is attached and drawn too (A5b's fix round added it
        // after this block was written on another lane — the phase A merge
        // joined them): every instance of BOTH groups reports a level.
        CHECK(drawn.size() == size_t(kInstances + kRotated + kDecoys), "every instance reports a drawn level");

        unsigned sEvaluated = 0, sMismatched = 0, sHist[8] = {}, sNearThreshold = 0;
        // Per SCALE, because the defect this arm exists for is a scale defect:
        // the table prints one row per octave so a disagreement names the scale
        // it happens at instead of a slot number.
        struct Row { unsigned n, bad, levelMin, levelMax; };
        std::unordered_map<int, Row> byScale;
        for (const ObjectLodDesc &d : drawn) {
            auto it = placed.find((unsigned long long)d.node);
            if (it == placed.end()) continue;
            const float s = it->second.scale;
            // Ogre's own quantity: the distance to the bounding SPHERE, whose
            // radius is the mesh's (the unit square's half-diagonal) times the
            // largest axis scale — `MovableObject::updateAllBounds`.
            const float radius = 0.5f * std::sqrt(2.0f) * s;
            const float dist = std::max(0.0f, it->second.dist - radius);
            const float footprint = sampleFootprintPerspective(dist, vr.projScaleY,
                                                               vr.viewportHeight);
            const float allowed = allowedWorldError(stratTol, footprint, s);
            const unsigned rule =
                unsigned(lodLevelForWorldError(kBounds, allowed, kBounds.size() + 1u));
            ++sEvaluated;
            if (d.level < 8u) ++sHist[d.level];
            for (float b : kBounds)
                if (b > 0.0f && std::fabs(allowed - b) / b < 1.0e-4f) ++sNearThreshold;
            const int octave = int(std::lround(std::log2(double(s))));
            Row &row = byScale[octave];
            if (!row.n) { row.levelMin = 8u; row.levelMax = 0u; }
            ++row.n;
            row.levelMin = std::min(row.levelMin, d.level);
            row.levelMax = std::max(row.levelMax, d.level);
            if (d.level != rule) {
                ++sMismatched;
                ++row.bad;
                if (sMismatched <= 3u)
                    std::printf("   STRATEGY DISAGREES: scale %.2f at %.1f m, drawn %u, rule %u "
                                "(allowed %.6g)\n", s, it->second.dist, d.level, rule, allowed);
            }
        }
        std::printf("\n== the strategy (the DRAWN level), at this view's own "
                    "%.0f px / proj11 %.4f / %.1f px tolerance ==\n",
                    vr.viewportHeight, vr.projScaleY, double(stratTol));
        std::printf("   %-8s %-6s %-14s %s\n", "scale", "count", "levels drawn", "disagreements");
        std::vector<int> octaves;
        for (const auto &kv : byScale) octaves.push_back(kv.first);
        std::sort(octaves.begin(), octaves.end());
        for (int o : octaves) {
            const Row &row = byScale[o];
            std::printf("   %-8.2f %-6u %u..%-12u %u\n", std::pow(2.0, double(o)), row.n,
                        row.levelMin, row.levelMax, row.bad);
        }
        std::printf("   levels taken: ");
        for (int i = 0; i < 8; ++i) std::printf("%u:%u ", i, sHist[i]);
        std::printf("\n   %u evaluations, %u within 1e-4 of a level boundary\n", sEvaluated,
                    sNearThreshold);
        char smsg[192];
        std::snprintf(smsg, sizeof(smsg),
                      "the STRATEGY agrees with the currency on all %u drawn instances at %.0f px",
                      sEvaluated, double(stratTol));
        CHECK(sMismatched == 0u, smsg);
        // THE FIXTURE MUST EXERCISE THE SCALE TERM, or this is an equality over
        // one octave: five octaves of scale, each of which must reach the walk.
        CHECK(byScale.size() >= 5u, "the drawn set spans at least five octaves of scale");
        // ...AND THE CHAIN MUST BE WALKED, exactly as in the GPU arm.
        unsigned sDistinct = 0;
        for (int i = 0; i < 8; ++i) if (sHist[i]) ++sDistinct;
        std::snprintf(smsg, sizeof(smsg), "the drawn levels span %u of the eight", sDistinct);
        CHECK(sDistinct >= 3u, smsg);

        // NO SCALE-INVARIANCE ARM LIVES HERE (deleted in ATOM-RESUMES-1's fix
        // round, deep-auditor NOTE 2b). It evaluated `lodLevelForWorldError` on
        // `allowedWorldError` for k = 1 and k = s and compared the two — the C++
        // formula against itself, which is an algebraic identity
        // (`allowedWorldError(t, footprint(s*d), s)` IS `footprint(d)`) that can
        // only fail on a float boundary and never touches the strategy. What
        // states that physics against the RENDERER is the 500-instance block
        // above (six octaves of scale, the drawn byte) and atom.dolly_gate (archived: archive/tests/atom)'s 10x
        // arm (the same 152 poses at scale 10 and ten times the distance must
        // draw the same level); and a change that dropped the divisor from
        // `allowedWorldError` itself would red the GPU-vs-C++ sweep below, since
        // the GLSL copy divides by the instance's scale on the device.
    }

    std::printf("\n== the parity ==\n   %u evaluations (%u instances x %zu parameter sets)\n",
                evaluated, slots, sets.size());
    std::printf("   levels taken: ");
    for (int i = 0; i < 8; ++i) std::printf("%u:%u ", i, histogram[i]);
    std::printf("\n   %u evaluations within 1e-4 of a level boundary\n", nearThreshold);
    if (mismatched)
        std::printf("   FIRST DISAGREEMENT: slot %u, GPU level %u, CPU level %u\n", firstBadSlot,
                    firstBadGpu, firstBadCpu);
    char msg[160];
    std::snprintf(msg, sizeof(msg), "10,000 evaluations asked for, %u made", evaluated);
    CHECK(evaluated >= 10000u, msg);
    std::snprintf(msg, sizeof(msg), "the ROTATED NON-UNIFORM group agrees too (%u disagreements "
                                    "of %u)", mismatchedRotated, unsigned(kRotated) * unsigned(sets.size()));
    CHECK(mismatchedRotated == 0u, msg);
    CHECK(mismatched == 0u, "the GLSL currency and the C++ currency agree on every one");
    // The chain must actually be WALKED: a sweep that only ever answers 0 (or
    // only ever the coarsest) would pass an equality and prove nothing.
    unsigned distinct = 0;
    for (int i = 0; i < 8; ++i) if (histogram[i]) ++distinct;
    std::snprintf(msg, sizeof(msg), "the sweep reached %u distinct levels of the eight", distinct);
    CHECK(distinct >= 5u, msg);

    voxelGatherParity(scene, slots, boundsOfMesh);
    clusterCutParity(e);
    productCutParity(e, view);

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
