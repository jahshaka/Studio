// gi.far_blas — THE FAR FIELD'S GEOMETRY IN THE ONE TLAS (ATOM-FARBLAS-1;
// SPECS/atom/A5b_VOXELISER_FEED_AND_FAR_BLAS_DESIGN.md §4).
//
// Every traced object is written into the top-level structure TWICE: a NEAR copy
// over the level the ray rule chose for it (GpuInstance.ids.w), carrying the
// per-consumer bits and bit 3, and a FAR copy over its mesh's COARSEST level
// carrying bit 4 alone. Every launch names its field with its cull mask; the
// screen-probe gather traces the near copies out to its near length (the outer
// cascade's half extent) and, for a ray that escaped, the far copies from there
// to the far plane.
//
// The fixture makes the two levels tell themselves apart by GEOMETRY: a unit
// cube whose level 1 has lost its +X face. A ray arriving from +X hits the +X
// face at level 0 and, at level 1, passes through the missing face and hits the
// -X face from inside — exactly one cube width further. So a hit DISTANCE names
// the level that answered.
//
// What each case proves:
//   1. THE TABLE: the far copies are the near ones again (farInstances ==
//      instances), one structure per (mesh, level) asked for — a mesh with no
//      chain builds ONE, a chained mesh builds its coarsest beside its near one,
//      and a mesh whose near level IS its coarsest shares it (no second build).
//   2. PAST THE NEAR LENGTH A FAR RAY HITS THE COARSE LEVEL: the hit names the
//      same slot the near ray names, and lies beyond `reach`; within it the near
//      ray hits the fine level.
//   3. THE NEAR COPY FOLLOWS THE RAY RULE: a chained mesh whose bound is below
//      any footprint is traced at its coarsest level by a NEAR ray too.
//   5. THE MASKS KEEP THE COPIES APART, on a fixture where leaking would change
//      the answer: a THREE-level cube whose level 1 has lost its +X face (a tiny
//      bound: the rule's near level) and whose level 2 is the whole cube again
//      (its coarsest: the far copy). From +X, a 0xFF ray answers the near copy's
//      missing face (one cube width further) — it would answer the whole cube's
//      +X face if a far copy leaked into a near launch — and a far-only ray
//      answers the whole cube, which no near copy of it is.
//   6. A STILL SCENE HOLDS THE RULE'S LEVEL (audit F1): a chained mesh added to a
//      scene that then stands still for three frames is in the TLAS at the
//      level the rule chose on the frame it arrived — not at level 0 until some
//      unrelated edit happens to rebuild the structure.
//   4. THE GATHER'S FAR QUERY FIRES, AND ONLY FOR ESCAPING RAYS: with nothing
//      beyond the near length the far query on and off draw the same bytes
//      (frozen frames); with a wall beyond it, the far query darkens the floor
//      the wall hides the sky from — the picture a near-only trace to the far
//      plane draws — where the far query off reads the sky through the wall.
//      WHAT THAT DARKENING IS: a far hit lies outside every cascade and is shaded
//      BLACK until the surface cards reach it (CARDS-2) — today the far query
//      buys OCCLUSION only (the sky stops leaking through distant geometry).
//
// SKIPPED, NOT FAILED, without ray-query hardware.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static void pushRay(std::vector<float> &rays, const Vec3 &o, const Vec3 &d, float tMin, float tMax,
                    unsigned mask)
{
    rays.insert(rays.end(), { o.x, o.y, o.z, tMin, d.x, d.y, d.z, tMax });
    float bits;
    std::memcpy(&bits, &mask, sizeof(bits));   // the mask is BIT-COPIED, never converted
    rays.insert(rays.end(), { bits, 0.0f, 0.0f, 0.0f });
}
struct Hit { float t; int index; bool hit; };
static Hit hitAt(const std::vector<float> &h, size_t i)
{
    return Hit{ h[i * 4 + 0], int(h[i * 4 + 1]), h[i * 4 + 3] > 0.5f };
}

/// The unit cube's triangles minus its +X face.
static std::vector<unsigned> withoutPlusX(const MeshData &d)
{
    std::vector<unsigned> out;
    for (size_t t = 0; t + 2 < d.indices.size(); t += 3) {
        bool plusX = true;
        for (int k = 0; k < 3; ++k)
            if (d.positions[size_t(d.indices[t + size_t(k)]) * 3u] < 0.49f) plusX = false;
        if (plusX) continue;
        out.insert(out.end(), { d.indices[t], d.indices[t + 1], d.indices[t + 2] });
    }
    return out;
}

/// THREE levels: level 1 = the cube minus its +X face with a tiny bound (the
/// rule's near level at any footprint), level 2 = the whole cube again with a
/// half-metre bound (the coarsest: the far copy's level). Case 5's fixture.
static MeshData splitCube()
{
    MeshData d = enginetest::unitCubeMesh();
    d.lodIndices.push_back(withoutPlusX(d));
    d.lodBounds.push_back(1e-7f);
    d.lodIndices.push_back(d.indices);
    d.lodBounds.push_back(0.5f);
    return d;
}

/// A unit cube whose ONE coarser level has lost its +X face (two triangles).
/// `bound` is that level's measured distance from level 0 in mesh units: large
/// keeps the ray rule at level 0 for the near copy, tiny sends it to level 1.
static MeshData chainedCube(float bound)
{
    MeshData d = enginetest::unitCubeMesh();
    const std::vector<unsigned> level1 = withoutPlusX(d);
    d.lodIndices.push_back(level1);
    d.lodBounds.push_back(bound);
    return d;
}

static NodeId place(Scene *s, MeshId mesh, MaterialId mat, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    if (!n || !s->attachMesh(n, mesh, mat)) return 0;
    s->setNodeTransform(n, pos, Quat(), scale);
    return n;
}

struct Delta { unsigned moved = 0u; double meanSigned = 0.0; };
static Delta deltaOf(const Image &a, const Image &b)
{
    Delta d;
    if (a.width != b.width || a.height != b.height) return d;
    double sum = 0.0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        int diff = 0;
        bool moved = false;
        for (int c = 0; c < 3; ++c) {
            const int dc = int(b.rgba[i + c]) - int(a.rgba[i + c]);
            diff += dc;
            if (dc) moved = true;
        }
        if (moved) ++d.moved;
        sum += double(diff) / 3.0;
    }
    d.meanSigned = sum / double(a.width * a.height);
    return d;
}

static void tune(Scene *s, bool farOff)
{
    GatherTuning t;
    t.freezeFrameIndex = true;   // every A/B freezes the frame index (GATE_AND_RIG)
    t.farQueryOff = farOff;
    s->setGatherTuning(t);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-far-blas-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("farblas", 256u, 256u, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: view: %s\n", e->lastError().c_str()); return 1; }
    view->setShadows(true);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;   // the gather's prepass rides the SSR row (test_gi_gather's armChain)
    view->setPostFx(fx);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.far_blas skips cleanly\n");
        return 0;
    }

    Scene *s = e->createScene("farblas");
    view->setScene(s);
    PbrParams pp; pp.albedo = Colour(0.5f, 0.5f, 0.5f); pp.metalness = 0.0f; pp.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(pp);
    const MeshId floorMesh = s->createMesh(enginetest::unitCubeMesh());   // no chain
    const MeshId wallMesh = s->createMesh(chainedCube(0.25f));            // near level 0
    const MeshId cheapMesh = s->createMesh(chainedCube(1e-7f));           // near level 1
    CHECK_MSG(mat && floorMesh && wallMesh && cheapMesh, "the fixture's meshes exist (%s)",
              e->lastError().c_str());
    const NodeId floorNode = place(s, floorMesh, mat, Vec3{ 0.0f, -0.1f, 0.0f }, Vec3{ 20.0f, 0.2f, 20.0f });
    // THE CHEAP CUBE: 1 m, centred at (0, 0.5, -6), inside the near length.
    const NodeId cheapNode = place(s, cheapMesh, mat, Vec3{ 0.0f, 0.5f, -6.0f }, Vec3{ 1, 1, 1 });
    enginetest::addDirectionalLight(s, Vec3{ -0.4f, -1.0f, -0.55f }, 4.0f);
    SkyDesc sky; sky.mode = SkyMode::Atmosphere;
    s->setSky(sky);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.03f, 0.03f, 0.035f));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3{ 4.0f, 2.2f, 6.0f }, Vec3{ 0.0f, 0.6f, 0.0f }));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.cascades = true;
    gi.ddgi = GiToggle::Off;
    gi.numBounces = 1;
    gi.gather = GiToggle::On;
    s->setGlobalIllumination(gi);
    tune(s, false);
    render(e, 90);

    const GiStatus gs = s->giStatus();
    CHECK_MSG(gs.gather.on && gs.gather.running && !gs.cascades.empty(),
              "the gather runs over a cascade chain (on %d running %d, %zu cascades)",
              int(gs.gather.on), int(gs.gather.running), gs.cascades.size());
    if (gs.cascades.empty()) { std::printf("\nFAILED (%d failures)\n", failures + 1); return 1; }
    // THE NEAR LENGTH, the engine's own derivation (OgreScreenProbeGather.cpp `reach`).
    const float reach = gs.cascades.back().halfSize;
    std::printf("   near length (outer half extent) %.2f m\n", double(reach));

    // ---- 4a. nothing beyond the near length: the far query changes no byte.
    Image farOnEmpty, farOffEmpty;
    tune(s, false); render(e, 4); view->readPixels(farOnEmpty);
    tune(s, true);  render(e, 4); view->readPixels(farOffEmpty);
    const Delta empty = deltaOf(farOffEmpty, farOnEmpty);
    CHECK_MSG(empty.moved == 0u,
              "with nothing beyond the near length the far query on and off draw the same bytes "
              "(%u px moved)", empty.moved);

    // THE WALL: 2 m thick, 120 m tall, 400 m wide, its +X face at 1.5 x reach - 1
    // from the origin along -X — beyond the near length, inside the far plane.
    const float wallX = -1.5f * reach;
    const NodeId wallNode = place(s, wallMesh, mat, Vec3{ wallX, 0.0f, 0.0f }, Vec3{ 2.0f, 120.0f, 400.0f });
    CHECK_MSG(floorNode && cheapNode && wallNode, "every node attached");
    tune(s, false);
    render(e, 60);

    // =====================================================================
    // 1. THE TABLE
    // =====================================================================
    {
        const RayQueryStatus st = s->rayQueryStatus();
        std::printf("   instances %d far %d, blas %d (level>0: %d, %llu bytes of %llu), tlas %llu B\n",
                    st.instances, st.farInstances, st.blasCount, st.levelBlasCount,
                    (unsigned long long)st.levelBlasBytes, (unsigned long long)st.blasBytes,
                    (unsigned long long)st.tlasBytes);
        CHECK_MSG(st.instances == 3 && st.farInstances == 3,
                  "every traced object is written twice: 3 near + 3 far copies (%d + %d)",
                  st.instances, st.farInstances);
        // THE COARSE STRUCTURES: the wall's level 1 (its far copy) and the cheap
        // cube's level 1 (its near AND far copy — the rule's level IS the
        // coarsest, so the two share one structure). Exactly two.
        CHECK_MSG(st.levelBlasCount == 2,
                  "two coarse structures: the wall's far level, and ONE the cheap cube's near "
                  "and far copies share (%d)", st.levelBlasCount);
        // THE LEVEL-0 STRUCTURES: the floor's (no chain — its near and far copies
        // are one structure, no second build) and the wall's near one. EXACTLY
        // two: the cheap cube's level 0 is never asked for, because the writer
        // reads the rule's answer the frame the rule gives it (audit F1 — the
        // old one-frame lag built it on the first frame and held it ~10 s).
        const int level0 = st.blasCount - st.levelBlasCount;
        CHECK_MSG(level0 == 2,
                  "level-0 structures: the floor's one (shared by its two copies) and the "
                  "wall's near one, and no other (%d)", level0);
    }

    // =====================================================================
    // 2 + 3. THE RAYS
    // =====================================================================
    {
        const float kFar = 1000.0f;
        const Vec3 o{ 0.0f, 5.0f, 0.0f };
        std::vector<float> rays, hits;
        pushRay(rays, o, Vec3{ -1, 0, 0 }, 0.001f, kFar, kRayMaskNearField);     // 0 near -> wall
        pushRay(rays, o, Vec3{ -1, 0, 0 }, reach, kFar, kRayMaskFar);            // 1 far  -> wall
        pushRay(rays, o, Vec3{ 0, -1, 0 }, 0.001f, kFar, kRayMaskNearField);     // 2 near -> floor
        pushRay(rays, Vec3{ 10.0f, 0.5f, -6.0f }, Vec3{ -1, 0, 0 }, 0.001f, 50.0f,
                kRayMaskNearField);                                              // 3 near -> cheap
        CHECK_MSG(s->traceRays(rays, hits) && hits.size() == 4u * 4u, "the batch traced");
        if (hits.size() == 16u) {
            const Hit nearWall = hitAt(hits, 0), farWall = hitAt(hits, 1),
                      nearFloor = hitAt(hits, 2), cheap = hitAt(hits, 3);
            const float fine = -wallX - 1.0f, coarse = -wallX + 1.0f;
            std::printf("   near wall %.4f (fine %.4f)  far wall %.4f (coarse %.4f)  "
                        "near floor %.4f  cheap %.4f\n",
                        double(nearWall.t), double(fine), double(farWall.t), double(coarse),
                        double(nearFloor.t), double(cheap.t));
            CHECK_MSG(nearWall.hit && std::fabs(nearWall.t - fine) < 0.01f,
                      "a NEAR ray hits the wall's FINE level: its +X face at %.3f m (%.3f)",
                      double(fine), double(nearWall.t));
            CHECK_MSG(farWall.hit && std::fabs(farWall.t - coarse) < 0.01f && farWall.t > reach,
                      "a FAR ray past the near length hits the COARSE level: through the missing "
                      "face to %.3f m, beyond reach %.2f (%.3f)",
                      double(coarse), double(reach), double(farWall.t));
            CHECK_MSG(farWall.index == nearWall.index,
                      "...and names the SAME slot the near ray names (%d, %d)", farWall.index,
                      nearWall.index);
            CHECK_MSG(nearFloor.hit && std::fabs(nearFloor.t - 5.0f) < 0.01f &&
                          nearFloor.index != nearWall.index,
                      "within the near length a near ray hits the floor at 5 m (%.4f)",
                      double(nearFloor.t));
            CHECK_MSG(cheap.hit && std::fabs(cheap.t - 10.5f) < 0.01f,
                      "THE NEAR COPY FOLLOWS THE RAY RULE: a bound below every footprint traces "
                      "the coarsest level, through the missing face to 10.5 m (%.4f)",
                      double(cheap.t));
        }
    }

    // =====================================================================
    // 4b. THE GATHER'S FAR QUERY: the wall hides the sky from the floor
    // =====================================================================
    {
        Image farOn, farOff;
        tune(s, false); render(e, 4); view->readPixels(farOn);
        tune(s, true);  render(e, 4); view->readPixels(farOff);
        const Delta d = deltaOf(farOff, farOn);
        std::printf("   wall beyond reach: far on vs off %u px moved, mean %.3f/255 signed\n",
                    d.moved, d.meanSigned);
        CHECK_MSG(d.moved > 1000u && d.meanSigned < 0.0,
                  "with a wall beyond the near length the far query DARKENS the picture — the "
                  "escaping rays hit it instead of reading the sky (%u px, %.3f/255)",
                  d.moved, d.meanSigned);
    }

    // =====================================================================
    // 5 + 6. THE MASKS ON A DISCRIMINATING FIXTURE, AND A STILL SCENE
    // =====================================================================
    {
        const MeshId splitMesh = s->createMesh(splitCube());
        // Case 5's cube at (0, 0.5, 12); case 6's cheap cube at (0, 0.5, -12).
        const NodeId splitNode = place(s, splitMesh, mat, Vec3{ 0.0f, 0.5f, 12.0f }, Vec3{ 1, 1, 1 });
        const NodeId stillNode = place(s, cheapMesh, mat, Vec3{ 0.0f, 0.5f, -12.0f }, Vec3{ 1, 1, 1 });
        CHECK_MSG(splitMesh && splitNode && stillNode, "the three-level cube and the still cube exist");
        // THE SCENE STANDS STILL FROM HERE: the attach is the last edit; three
        // frames with no transform, no camera move, nothing.
        render(e, 3);
        std::vector<float> rays, hits;
        pushRay(rays, Vec3{ 10.0f, 0.5f, 12.0f }, Vec3{ -1, 0, 0 }, 0.001f, 50.0f, 0xFFu);  // 0
        pushRay(rays, Vec3{ 10.0f, 0.5f, 12.0f }, Vec3{ -1, 0, 0 }, 0.001f, 50.0f,
                kRayMaskFar);                                                                // 1
        pushRay(rays, Vec3{ 10.0f, 0.5f, -12.0f }, Vec3{ -1, 0, 0 }, 0.001f, 50.0f,
                kRayMaskNearField);                                                          // 2
        CHECK_MSG(s->traceRays(rays, hits) && hits.size() == 3u * 4u, "the batch traced");
        if (hits.size() == 12u) {
            const Hit all = hitAt(hits, 0), far = hitAt(hits, 1), still = hitAt(hits, 2);
            std::printf("   split cube: 0xFF %.4f, far-only %.4f; still cube near %.4f\n",
                        double(all.t), double(far.t), double(still.t));
            CHECK_MSG(all.hit && std::fabs(all.t - 10.5f) < 0.01f,
                      "0xFF traces the NEAR copies only: the rule's level (no +X face) answers "
                      "10.5 m — a leaked far copy (the whole cube) would answer 9.5 (%.4f)",
                      double(all.t));
            CHECK_MSG(far.hit && std::fabs(far.t - 9.5f) < 0.01f && far.index == all.index,
                      "a far-only ray answers the COARSEST level (the whole cube, 9.5 m), which "
                      "no near copy is, naming the same slot (%.4f)", double(far.t));
            CHECK_MSG(still.hit && std::fabs(still.t - 10.5f) < 0.01f,
                      "A STILL SCENE HOLDS THE RULE'S LEVEL: three frames after its attach the "
                      "cube is traced at level 1 (10.5 m), not level 0 (9.5) (%.4f)",
                      double(still.t));
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
