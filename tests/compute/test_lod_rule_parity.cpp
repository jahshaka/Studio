// engine.lod_rule_parity — ONE QUALITY CURRENCY, TWO IMPLEMENTATIONS, AND THE
// PROOF THEY ARE THE SAME (SPECS/atom/A4_SUBSTRATE_CULL_DESIGN.md section 3).
//
// THE CURRENCY is `sampleFootprintPerspective` + `allowedWorldError` +
// `lodLevelForWorldError` in jahshaka/engine/Types.h, and its GLSL twin is
// `jahSampleFootprint` + `jahAllowedWorldError` + `jahLevelForAllowed` in
// media/Hlms/Jahshaka/JahCullTest_cs.glsl. There are two copies because a
// compute shader cannot include a C++ header. This suite is the reason that is
// SAFE: it drives the REAL shader — the cull's own job 1, on the device, over the
// real GPU scene table — and compares its answer against the header's over
// 10,000 (bound, distance, tolerance, projection) combinations.
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

static MeshData chainedMesh()
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
    for (int i = 0; i < 7; ++i) d.lodIndices.push_back(build(steps[i]));
    d.lodBounds = kBounds;
    d.lodErrors = kBounds;
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
// the C++ `clusterCut`, bit for bit, over the shipped meshes (their DAGs baked by
// the product's own bake) at 20 distances x 6 scales x 3 tolerances, with the
// instance ROTATED and scaled NON-UNIFORMLY so the rows of its transform and the
// level rule's column scale are exercised, not just the translation. Answers
// within 1e-4 of a threshold are counted over every evaluation, as the three
// older copies count them, and a disagreement at one is reported apart (float
// ordering between two compilers) but still fails.
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
    const auto phys = clusterfix::loadModel(std::string(CLUSTER_FIXTURE_DIR) + "/physics_model.obj");
    if (!phys.empty()) meshes.push_back({ "physics-model", phys.front() });
    meshes.push_back({ "uv-sphere-20k", clusterfix::uvSphere() });

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
    const float tols[3] = { 0.5f, 1.0f, 4.0f };
    const float scales[6] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
    unsigned long long evaluated = 0, mismatched = 0, nearThreshold = 0, nearMismatched = 0, drawnTotal = 0;
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
        std::vector<unsigned char> drawn;
        if (!gpu.run(f.data.clusterGroups, f.data.clusters, views, drawn, err)) {
            std::printf("FAIL: %s: the device run: %s\n", m.first.c_str(), err.c_str());
            ++failures;
            continue;
        }
        const size_t nc = f.data.clusters.size();
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
    std::snprintf(msg, sizeof(msg), "the cluster cut ran on %zu shipped meshes", meshesRun);
    CHECK(meshesRun >= 5u, msg);
    std::snprintf(msg, sizeof(msg),
                  "THE GLSL CUT AND THE C++ CUT PICK THE SAME CLUSTERS: %llu (view, cluster) answers, "
                  "%llu drawn, %llu disagreements (%llu of them at a threshold); %llu answers within 1e-4 "
                  "of a threshold",
                  evaluated, drawnTotal, mismatched + nearMismatched, nearMismatched, nearThreshold);
    CHECK(mismatched == 0u && nearMismatched == 0u, msg);
}

// ---- THE FIFTH COPY: THE PRODUCT'S CUT (ATOM-CLUSTER-CUT) --------------------------
//
// The fourth copy runs the GLSL rule through a TEST job over one mesh's tables. The
// PRODUCT runs it in the cull's cut job (JahCullCut_cs.glsl) over the GPU scene's
// cluster tables, with the instance's rows from the scene's table, the eye and
// footprint from the request, and the drawn set COMPACTED into records — so this
// arm drives the real thing: each shipped mesh's baked DAG attached to eight
// instances (inside its bounds out to 500 radii, scaled 0.5-4, one rotated and
// scaled non-uniformly), the cull asked in mode 3 through the public gpuCull door
// at five parameter sets (tolerance, lens, height; one orthographic), and the drawn
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
    const auto phys = clusterfix::loadModel(std::string(CLUSTER_FIXTURE_DIR) + "/physics_model.obj");
    if (!phys.empty()) meshes.push_back({ "physics-model", phys.front() });
    meshes.push_back({ "uv-sphere-20k", clusterfix::uvSphere() });

    struct Set { float tol, fovDeg, height; bool ortho; };
    const Set sets[5] = { { 0.5f, 45.0f, 1080.0f, false }, { 1.0f, 45.0f, 1080.0f, false },
                          { 4.0f, 90.0f, 720.0f, false },  { 1.0f, 110.0f, 2376.0f, false },
                          { 1.0f, 45.0f, 1080.0f, true } };
    unsigned long long pairs = 0, bad = 0, nearBad = 0, nearCount = 0, drawnTotal = 0, overflow = 0;
    unsigned long long coverageBad = 0, overlapBad = 0;
    unsigned depthHist[16] = {};
    size_t meshesRun = 0;
    for (const auto &m : meshes) {
        clusterfix::Fixture f;
        if (!clusterfix::bake(f, m.second, m.first, iris::MeshBake::ClusterDagVariant::Shipped, /*wantRegions=*/true) || f.data.clusters.empty()) continue;
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
        std::map<unsigned, int> nodeOf;   // node id -> instance index
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
            e->renderOneFrame();
            // The table's dirty scan (engine.atom_draw's door): a scene whose frames
            // nothing in the view reads the table in (no GI, no rays) stages nothing.
            static_cast<jahshaka::engine::detail::OgreScene *>(sc)->ensureGpuScene(false);
            slots = sc->gpuSceneStatus().slotCount;
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
                // THE DEVICE'S SET IS ONE CUT ON ITS OWN (the fix round's F2): no drawn
                // cluster sits under another drawn one (a drawn cluster produced by a
                // group whose member is drawn), and the drawn clusters' regions cover
                // every level-0 triangle exactly once (the bake's provenance partition,
                // atom.cluster_cut's own count).
                {
                    std::vector<unsigned char> isDrawn(f.data.clusters.size(), 0);
                    for (unsigned c : gpu) if (c < isDrawn.size()) isDrawn[c] = 1;
                    std::vector<unsigned char> groupHasDrawnMember(f.data.clusterGroups.size(), 0);
                    for (unsigned c : gpu) groupHasDrawnMember[size_t(f.data.clusters[c].group)] = 1;
                    for (unsigned c : gpu) {
                        const int ref = f.data.clusters[c].refined;
                        if (ref >= 0 && groupHasDrawnMember[size_t(ref)]) ++overlapBad;
                    }
                    std::vector<unsigned> cover(f.triangles, 0u);
                    for (unsigned c : gpu)
                        if (int(c) < f.stats.clusterRegions.size())
                            for (quint32 t : f.stats.clusterRegions.at(int(c)))
                                if (t < cover.size()) ++cover[t];
                    for (unsigned n : cover) coverageBad += n != 1u ? 1u : 0u;
                }
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
    std::snprintf(msg, sizeof(msg), "the product's cut ran on %zu shipped meshes", meshesRun);
    CHECK(meshesRun >= 6u, msg);
    std::snprintf(msg, sizeof(msg), "no instance overflowed the cut's stream (%llu)", overflow);
    CHECK(overflow == 0u, msg);
    std::snprintf(msg, sizeof(msg), "THE DEVICE'S SETS ARE CUTS: no drawn cluster under a drawn one (%llu) and every "
                  "level-0 triangle covered exactly once by the drawn regions (%llu triangle-cuts off)",
                  overlapBad, coverageBad);
    CHECK(overlapBad == 0u && coverageBad == 0u, msg);
    std::snprintf(msg, sizeof(msg),
                  "THE PRODUCT'S CUT IS CELL-IDENTICAL TO clusterCut: %llu (instance, view) cuts, %llu clusters "
                  "drawn, %llu differ (%llu of them at a threshold; %llu cuts had a group within 1e-4)",
                  pairs, drawnTotal, bad + nearBad, nearBad, nearCount);
    CHECK(bad == 0u && nearBad == 0u, msg);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-lod-parity-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("parity", 320u, 240u, Colour(0, 0, 0));
    Scene *scene = e->createScene("parity");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.2f, 0.2f, 0.2f));

    const MeshId mesh = scene->createMesh(chainedMesh());
    PbrParams p; p.albedo = Colour(0.7f, 0.7f, 0.7f); p.roughness = 0.6f;
    const MaterialId mat = scene->createPbrMaterial(p);
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
    }
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f));
    for (int i = 0; i < 3; ++i) e->renderOneFrame();

    const unsigned slots = scene->gpuSceneStatus().slotCount;
    CHECK(slots == unsigned(kInstances + kRotated), "every instance is in the table");
    std::vector<GpuSceneEntry> table;
    for (unsigned i = 0; i < slots; ++i) {
        GpuSceneEntry en;
        if (scene->gpuSceneEntry(i, en)) table.push_back(en);
    }
    CHECK(table.size() == slots, "the table reads back");
    {
        bool truth = table.size() == slots;
        float rowMax = 0.0f;
        for (unsigned i = unsigned(kInstances); truth && i < slots; ++i) {
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
            const unsigned cpu =
                unsigned(lodLevelForWorldError(kBounds, allowed, kBounds.size() + 1u));
            const unsigned gpu = res.levels[slot];
            ++evaluated;
            if (gpu < 8u) ++histogram[gpu];
            for (float b : kBounds)
                if (b > 0.0f && std::fabs(allowed - b) / b < 1.0e-4f) ++nearThreshold;
            if (gpu != cpu) {
                ++mismatched;
                if (slot >= unsigned(kInstances)) ++mismatchedRotated;
                if (firstBadSlot == 0xFFFFFFFFu) {
                    firstBadSlot = slot;
                    firstBadGpu = gpu;
                    firstBadCpu = cpu;
                }
            }
        }
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
    // `fillCullView` reports for this view, and its tolerance is the shipped
    // `kLodBudgetPixels` (the tier column is not wired into the draw path —
    // GiQualityFacts::pixelTolerance, scripting.e2e.tier_table_atom). So this
    // arm holds the SAME (distance x scale) fixture to the same rule at one
    // parameter set, read through `objectLods()` — the byte the render queue
    // indexes the VAO list with.
    {
        // The level is written by the LOD walk of a rendered frame; the cull
        // sweep above renders none, so the reading is taken from a fresh one.
        for (int i = 0; i < 2; ++i) e->renderOneFrame();
        GpuCullRequest vr;
        if (!e->fillCullView(view, vr)) { std::printf("FAIL: fillCullView (view)\n"); return 1; }
        std::vector<ObjectLodDesc> drawn;
        scene->objectLods(drawn);
        // The rotated group is attached and drawn too (A5b's fix round added it
        // after this block was written on another lane — the phase A merge
        // joined them): every instance of BOTH groups reports a level.
        CHECK(drawn.size() == size_t(kInstances + kRotated), "every instance reports a drawn level");

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
            const float allowed = allowedWorldError(kLodBudgetPixels, footprint, s);
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
                    vr.viewportHeight, vr.projScaleY, kLodBudgetPixels);
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
                      "the STRATEGY agrees with the currency on all %u drawn instances", sEvaluated);
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
        // above (six octaves of scale, the drawn byte) and atom.dolly_gate's 10x
        // arm (the same 152 poses at scale 10 and ten times the distance must
        // draw the same level); and a change that dropped the divisor from
        // `allowedWorldError` itself would red the GPU-vs-C++ sweep below, since
        // the GLSL copy divides by the instance's scale on the device.
    }

    std::printf("\n== the parity ==\n   %u evaluations (%d instances x %zu parameter sets)\n",
                evaluated, kInstances, sets.size());
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

    clusterCutParity(e);
    productCutParity(e, view);

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
