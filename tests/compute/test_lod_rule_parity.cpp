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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
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

int main()
{
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
    for (int i = 0; i < kInstances; ++i) {
        const NodeId n = scene->createNode();
        if (!scene->attachMesh(n, mesh, mat)) { std::printf("FAIL: attach\n"); return 1; }
        // 1 m to 500 m along -Z, and a scale that walks 0.25 .. 8 so the
        // mesh-units term of the currency is exercised over five octaves.
        const float dist = 1.0f + float(i);
        const float s = 0.25f * std::pow(2.0f, float(i % 6));
        enginetest::setNodePosition(scene, n, Vec3(0.0f, 0.0f, -dist));
        enginetest::setNodeScale(scene, n, Vec3(s, s, s));
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

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
