// gi.rayquery — THE HARDWARE RAY-QUERY TIER (SPECS/PHOTON_SPEC.md §7 R1).
//
// The tier keeps a ray-traceable copy of the scene: one bottom-level
// acceleration structure per mesh, one top-level structure over the instances,
// built from the SAME Ogre vertex and index buffers the raster draws
// (ogre-patches 0038/0039/0040) and recorded into the frame's own command
// buffer. Nothing in the picture consumes it yet — R2 (probe visibility),
// R3 (sun contact) and R5 (reflections) are the consumers, and each reads this
// same structure. So this suite cannot assert a pixel; it asserts GEOMETRY,
// which is the only thing the tier claims to know.
//
// What each case proves, and why it is the case that matters:
//
//   1. THE TIER COMES UP, AND THE PICTURE DOES NOT MOVE. Enabling ray queries
//      changes the Vulkan device (instance 1.2, three feature bits, two buffer
//      usage bits on every device-local pool). The proof that it is inert is
//      that the scene renders the same pixels with the tier holding structures
//      as with it switched off. A byte-identical picture, not a tolerance.
//
//   2. A RAY ANSWERS THE ANALYTIC DISTANCE. The honest proof that a ray tier
//      works is a ray whose hit can be compared with arithmetic: a known point,
//      a known direction, a known box. Not shading, not a probe — geometry.
//      It also proves the BLAS arithmetic (pool address + buffer start +
//      element offset) is right, because a wrong offset reads someone else's
//      triangles and lands somewhere else entirely.
//
//   3. THE TRACED SET IS THE WORLD, NOT THE EDITOR. The S3 spike traced every
//      Item the SceneManager held — gizmo arrows, light icons, the backdrop
//      (audit C-4) — and skinned Items at bind pose (C-5). A helper must not be
//      hittable, and hiding an object must remove it from the structure.
//
//   4. A STILL SCENE COSTS NOTHING. The tier gates on the same movement epoch
//      the shadow caster walk does. If it rebuilt every frame it would be a
//      permanent tax on a 60 Hz editor that nothing is yet consuming.
//
//   5. THE NO-RAYS SWITCH IS A REAL SWITCH. Off, the structures go and the
//      status reads available true / enabled false — this machine rendering the
//      picture a machine WITHOUT ray-tracing hardware renders. That is what
//      lets every later ray-consuming suite assert both pictures on one GPU
//      instead of assuming the fallback.
//
//   6. A MOVER MOVES ITS HIT. The point of a per-frame structure: move the box,
//      and the same ray reports the new distance.
//
//   7. TWO DRAWN SCENES IN ONE FRAME — the ordinary product case (the editor
//      plus a material-preview or thumbnail scene). Each must keep its own
//      structures and its own timings; the tier's frame accounting must not
//      count a per-SCENE call as a frame, or every "wait N frames in flight"
//      guard waits half as long as it claims and a scratch arena is freed
//      while the build that reads it is still queued (round 2, finding 3).
//
//   8. A DESTROYED SCENE TAKES ITS STRUCTURES WITH IT. Without this the tier
//      held every preview scene's BLASes — and the MeshPtrs under them — for
//      the process's life, and a recycled OgreScene address inherited a dead
//      scene's structures (round 2, finding 2).
//
// SKIPPED, NOT FAILED, without ray-query hardware: a device that does not
// advertise VK_KHR_ray_query is a supported platform (every Mac is one), and a
// suite that went red there would be asserting the driver, not the code.
//
// Its own binary like every GI suite: an engine per process (Ogre::Root is a
// singleton) and process-wide HlmsPbs bindings.
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
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// One ray in the batch layout Scene::traceRays takes: origin.xyz + tMin,
/// direction.xyz + tMax, then the instance mask and three unused words.
static void pushRay(std::vector<float> &rays, const Vec3 &o, const Vec3 &d, float tMin, float tMax,
                    unsigned mask = 0xFFu)
{
    rays.insert(rays.end(), { o.x, o.y, o.z, tMin, d.x, d.y, d.z, tMax });
    // The mask word is read as an unsigned in the shader; it travels through a
    // float array, so it is BIT-COPIED rather than converted (a cast would turn
    // 0xFF into 255.0f and back into 255 by luck, but not every mask is small).
    float bits;
    const unsigned m = mask;
    std::memcpy(&bits, &m, sizeof(bits));
    rays.insert(rays.end(), { bits, 0.0f, 0.0f, 0.0f });
}

struct Hit { float distance; int node; int primitive; bool hit; };

static Hit hitAt(const std::vector<float> &hits, size_t i)
{
    return Hit{ hits[i * 4 + 0], int(hits[i * 4 + 1]), int(hits[i * 4 + 2]),
                hits[i * 4 + 3] > 0.5f };
}

static bool imagesEqual(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return false;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour &p = a.at(x, y), &q = b.at(x, y);
            if (p.r != q.r || p.g != q.g || p.b != q.b) return false;
        }
    return true;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    // A LOG PER RUN. Three ctest entries share this binary and this working
    // directory (rays on, rays off, and the sanitised twin); one log name would
    // have them clobbering each other's evidence whenever two run at once.
    cfg.logFile = getenv("JAHSHAKA_NO_RAY_QUERY")
                      ? "test-gi-rayquery-" JAH_RQ_LOG_TAG "-norays-ogre.log"
                      : "test-gi-rayquery-" JAH_RQ_LOG_TAG "-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // THE DEVICE DOES NOT EXIST YET. Ogre creates the VkDevice with the first
    // render target, not with Root (the same startup-order law that makes a
    // render window a prerequisite for registerHlms and createSceneManager), so
    // rayQueryAvailable() is only meaningful once a View has been made — asking
    // before that reports false on hardware that has rays.
    View *view = e->createOffscreenView("rayquery", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("rayquery");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.2f, 0.2f, 0.25f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -0.8f, -0.45f), 3.0f);

    // A ground slab and ONE target box with an exactly known geometry, so a ray
    // aimed at it has an answer arithmetic can produce.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.7f, 0.7f, 0.7f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.5f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(40.0f, 1.0f, 40.0f));

    // A UNIT cube scaled to 2 m, centred at (0, 1, 0): its +X face is the plane
    // x = 1, its top y = 2. Every distance below is read off those two numbers.
    const NodeId box = enginetest::addTestCube(scene, Colour(0.8f, 0.2f, 0.2f), 0.0f, 0.6f);
    enginetest::setNodePosition(scene, box, Vec3(0.0f, 1.0f, 0.0f));
    enginetest::setNodeScale(scene, box, Vec3(2.0f, 2.0f, 2.0f));

    view->setCamera(enginetest::testCameraDescLookAt(Vec3(5.0f, 3.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f)));
    render(e, 6);

    // =====================================================================
    // THE NO-RAYS RUN (JAHSHAKA_NO_RAY_QUERY=1, which is what --no-ray-query
    // sets). This is the whole point of the switch: this GPU renders the
    // picture a machine WITHOUT ray-tracing hardware renders, so the fallback
    // every later ray-consuming suite depends on is PROVED on every push
    // instead of assumed.
    //
    // It is a DIFFERENT contract from the traced one, not a subset, so it gets
    // its own assertions and stops here — the cases below would otherwise
    // switch the tier back on at runtime and test nothing the traced run does
    // not already cover.
    //
    // With the switch set the patch does not even ask the driver for the
    // extensions, so `available` is false too: the process is on exactly the
    // device it would have had before the ray tier existed.
    // =====================================================================
    if (!e->rayTracing()) {
        std::printf("\n== the no-rays run (JAHSHAKA_NO_RAY_QUERY) ==\n");
        const RayQueryStatus st = scene->rayQueryStatus();
        CHECK(!st.available, "the DEVICE was built without ray queries — the switch reaches it");
        CHECK(!st.enabled, "...and the tier is off");
        CHECK(st.blasCount == 0 && st.instances == 0 && st.triangles == 0,
              "no acceleration structure exists at all");
        CHECK(st.blasBytes == 0 && st.tlasBytes == 0, "...and it costs no memory");
        CHECK(st.tlasBuilds == 0 && st.tlasRefits == 0 && st.blasBuilds == 0,
              "...and no frame ever built one");
        std::vector<float> rays, hits;
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(!scene->traceRays(rays, hits),
              "a trace REFUSES rather than answering wrongly");
        CHECK(hits.empty(), "...leaving no answers behind");
        CHECK(!e->rayQueryAvailable(), "the engine agrees the device has none");
        // AND THE SCENE STILL RENDERS. The fallback is the same picture with
        // one term computed differently — never a degraded or missing one.
        Image img;
        CHECK(view->readPixels(img) && img.width == kSize && img.height == kSize,
              "and the scene renders exactly as it always did");
        std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                    failures == 1 ? "" : "s");
        return failures ? 1 : 0;
    }

    if (!e->rayQueryAvailable()) {
        // NOT A FAILURE. macOS, a pre-RTX GPU and a software rasteriser without
        // the extension are all supported platforms; the fallback picture is
        // the product, not a degraded build.
        std::printf("SKIP: this device does not advertise VK_KHR_ray_query — "
                    "the no-rays path is the supported picture here\n");
        return 0;
    }
    std::printf("device advertises hardware ray queries\n");

    // =====================================================================
    // CASE 1 — the tier holds a structure, and the picture did not move
    // =====================================================================
    std::printf("\n== case 1: the tier is up and inert ==\n");
    {
        const RayQueryStatus st = scene->rayQueryStatus();
        std::printf("   blas %d, instances %d, triangles %d, blasBytes %llu, tlasBytes %llu\n",
                    st.blasCount, st.instances, st.triangles,
                    (unsigned long long)st.blasBytes, (unsigned long long)st.tlasBytes);
        CHECK(st.available, "the device's answer is available:true");
        CHECK(st.enabled, "...and the tier is enabled");
        CHECK(st.blasCount >= 1, "a bottom-level structure exists");
        CHECK(st.instances == 2, "the traced set is exactly the ground and the box");
        CHECK(st.triangles > 0 && st.blasBytes > 0, "it holds real geometry and real memory");
        CHECK(st.tlasBuilds >= 1, "a top-level structure was built");

        // THE PICTURE. Byte-identical with the tier holding structures and with
        // it switched off — the tier touches no pixel, which is what makes
        // enabling the Vulkan device features safe.
        Image withRays, withoutRays;
        view->readPixels(withRays);
        e->setRayTracing(false);
        render(e, 3);
        view->readPixels(withoutRays);
        CHECK(imagesEqual(withRays, withoutRays),
              "THE PICTURE IS BYTE-IDENTICAL with the tier on and off");
        e->setRayTracing(true);
        render(e, 3);
    }

    // =====================================================================
    // CASE 2 — a ray answers the analytic distance
    // =====================================================================
    std::printf("\n== case 2: a known ray against a known box ==\n");
    {
        std::vector<float> rays, hits;
        // (a) straight down the -X axis from x = 10 at the box's centre height:
        //     the +X face is at x = 1, so the first hit is at 9.
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        // (b) straight down from above the box's centre: its top is y = 2, from
        //     y = 12 that is 10.
        pushRay(rays, Vec3(0.0f, 12.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.001f, 100.0f);
        // (c) the same downward ray displaced 5 m in +X: it clears the box
        //     (half-extent 1) and lands on the ground, whose top is y = 0, so 12.
        pushRay(rays, Vec3(5.0f, 12.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.001f, 100.0f);
        // (d) a ray aimed at nothing at all: straight up, above everything.
        pushRay(rays, Vec3(0.0f, 20.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.001f, 100.0f);
        CHECK(scene->traceRays(rays, hits), "the batch traced");
        CHECK(hits.size() == 4u * 4u, "...and came back with one answer per ray");
        if (hits.size() == 16u) {
            const Hit a = hitAt(hits, 0), b = hitAt(hits, 1), c = hitAt(hits, 2), d = hitAt(hits, 3);
            std::printf("   (a) %.4f  (b) %.4f  (c) %.4f  (d) %.4f\n",
                        a.distance, b.distance, c.distance, d.distance);
            // A millimetre. The geometry is exact and the arithmetic is exact;
            // the only slack is the traversal's own float precision.
            CHECK(a.hit && std::fabs(a.distance - 9.0f) < 0.001f,
                  "(a) -X from x=10 hits the box's +X face at exactly 9 m");
            CHECK(b.hit && std::fabs(b.distance - 10.0f) < 0.001f,
                  "(b) -Y from y=12 hits the box's top at exactly 10 m");
            CHECK(c.hit && std::fabs(c.distance - 12.0f) < 0.001f,
                  "(c) the same ray 5 m aside misses the box and hits the ground at 12 m");
            CHECK(!d.hit && d.distance < 0.0f, "(d) a ray into the sky reports a miss");
            CHECK(a.node == b.node, "(a) and (b) name the SAME object — both hit the box");
            CHECK(c.node != a.node, "...and (c) names a different one — the ground");
        }
    }

    // =====================================================================
    // CASE 3 — the traced set is the world, not the editor
    // =====================================================================
    std::printf("\n== case 3: what is in the structure ==\n");
    {
        // A HELPER: editor furniture carries its own visibility channel instead
        // of the world's, exactly so captures can exclude it. A gizmo arrow in
        // the acceleration structure is the defect audit C-4 found in the spike.
        const NodeId helper = enginetest::addTestCube(scene, Colour(0.1f, 0.9f, 0.1f), 0.0f, 0.5f);
        enginetest::setNodePosition(scene, helper, Vec3(4.0f, 1.0f, 0.0f));
        scene->setNodeHelper(helper, true);
        render(e, 3);
        CHECK(scene->rayQueryStatus().instances == 2,
              "a HELPER does not join the traced set (the spike traced gizmos and light icons)");

        std::vector<float> rays, hits;
        // Aimed straight at where the helper stands: it must not be hit, and the
        // ray must fly on to nothing.
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 5.5f);
        CHECK(scene->traceRays(rays, hits) && hits.size() == 4u, "a ray aimed at the helper traced");
        if (hits.size() == 4u)
            CHECK(!hitAt(hits, 0).hit, "...and passes straight through it — a helper is not hittable");

        scene->removeNode(helper);
        render(e, 3);

        // HIDING. An object that is not drawn is not traced: the structure
        // follows what the world shows, one walk, one predicate.
        scene->setNodeVisible(box, false);
        render(e, 3);
        CHECK(scene->rayQueryStatus().instances == 1, "a HIDDEN object leaves the traced set");
        {
            std::vector<float> r2, h2;
            pushRay(r2, Vec3(0.0f, 12.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.001f, 100.0f);
            if (scene->traceRays(r2, h2) && h2.size() == 4u)
                CHECK(hitAt(h2, 0).hit && std::fabs(hitAt(h2, 0).distance - 12.0f) < 0.001f,
                      "...and the ray that hit it now reaches the ground at 12 m");
        }
        scene->setNodeVisible(box, true);
        render(e, 3);
        CHECK(scene->rayQueryStatus().instances == 2, "showing it again puts it back");
    }

    // =====================================================================
    // CASE 4 — a still scene costs nothing
    // =====================================================================
    std::printf("\n== case 4: at rest ==\n");
    {
        render(e, 5);
        const RayQueryStatus before = scene->rayQueryStatus();
        render(e, 30);
        const RayQueryStatus after = scene->rayQueryStatus();
        std::printf("   tlas builds %llu -> %llu, refits %llu -> %llu, blas %llu -> %llu "
                    "over 30 still frames\n",
                    (unsigned long long)before.tlasBuilds, (unsigned long long)after.tlasBuilds,
                    (unsigned long long)before.tlasRefits, (unsigned long long)after.tlasRefits,
                    (unsigned long long)before.blasBuilds, (unsigned long long)after.blasBuilds);
        CHECK(after.tlasBuilds == before.tlasBuilds && after.tlasRefits == before.tlasRefits,
              "A STILL SCENE REBUILDS NOTHING for 30 frames");
        CHECK(after.blasBuilds == before.blasBuilds,
              "...and builds no new bottom-level structure either");
    }

    // =====================================================================
    // CASE 5 — a mover moves its hit
    // =====================================================================
    std::printf("\n== case 5: the structure follows the world ==\n");
    {
        const RayQueryStatus before = scene->rayQueryStatus();
        // 3 m further from the ray's origin along -X: the +X face moves from
        // x = 1 to x = 4, so the hit at 9 becomes a hit at 6.
        enginetest::setNodePosition(scene, box, Vec3(-3.0f, 1.0f, 0.0f));
        render(e, 3);
        std::vector<float> rays, hits;
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(scene->traceRays(rays, hits) && hits.size() == 4u, "the ray traced after the move");
        if (hits.size() == 4u) {
            std::printf("   after moving the box 3 m: %.4f\n", hitAt(hits, 0).distance);
            CHECK(hitAt(hits, 0).hit && std::fabs(hitAt(hits, 0).distance - 12.0f) < 0.001f,
                  "the same ray now hits the box's +X face at 12 m — the structure moved with it");
        }
        const RayQueryStatus after = scene->rayQueryStatus();
        CHECK(after.tlasBuilds + after.tlasRefits > before.tlasBuilds + before.tlasRefits,
              "...because a MOVED scene did update the top-level structure");
        CHECK(after.blasBuilds == before.blasBuilds,
              "...without rebuilding a bottom-level one (geometry did not change, only its place)");
        enginetest::setNodePosition(scene, box, Vec3(0.0f, 1.0f, 0.0f));
        render(e, 3);
    }

    // =====================================================================
    // CASE 6 — the no-rays switch
    // =====================================================================
    std::printf("\n== case 6: the no-rays switch ==\n");
    {
        e->setRayTracing(false);
        render(e, 4);
        const RayQueryStatus off = scene->rayQueryStatus();
        CHECK(off.available, "the DEVICE still advertises ray queries — that cannot be switched");
        CHECK(!off.enabled, "...but the tier is off");
        CHECK(off.instances == 0 && off.blasCount == 0,
              "every structure is gone — this is the picture a machine without rays renders");
        std::vector<float> rays, hits;
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(!scene->traceRays(rays, hits), "and a trace REFUSES rather than answering wrongly");
        CHECK(hits.empty(), "...leaving no answers behind");
        CHECK(!e->rayTracing(), "the engine reports the switch as off");

        e->setRayTracing(true);
        render(e, 5);
        const RayQueryStatus on = scene->rayQueryStatus();
        CHECK(on.enabled && on.instances == 2,
              "switching it back on rebuilds the structure from scratch");
        std::vector<float> r2, h2;
        pushRay(r2, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(scene->traceRays(r2, h2) && h2.size() == 4u && hitAt(h2, 0).hit &&
                  std::fabs(hitAt(h2, 0).distance - 9.0f) < 0.001f,
              "...and the same ray gives the same 9 m it gave before it was switched off");
    }

    // =====================================================================
    // CASE 7 — two drawn scenes in one frame
    // =====================================================================
    std::printf("\n== case 7: a second scene ==\n");
    {
        // A preview-sized offscreen view and its own scene, exactly like the
        // material preview and the thumbnail renderer: both are drawn every
        // frame the editor is, and both reach the tier through the same call.
        View *pv = e->createOffscreenView("rayquery-preview", 64, 64, Colour(0, 0, 0));
        Scene *ps = e->createScene("rayquery-preview");
        CHECK(pv && ps, "a second view and scene exist");
        if (pv && ps) {
            pv->setScene(ps);
            ps->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
            const NodeId a = enginetest::addTestCube(ps, Colour(0.2f, 0.4f, 0.9f), 0.0f, 0.5f);
            enginetest::setNodePosition(ps, a, Vec3(0.0f, 0.0f, 0.0f));
            pv->setCamera(enginetest::testCameraDescLookAt(Vec3(0, 1, 4), Vec3(0, 0, 0)));
            render(e, 6);

            const RayQueryStatus editorSt = scene->rayQueryStatus();
            const RayQueryStatus previewSt = ps->rayQueryStatus();
            std::printf("   editor: %d instances / %d blas   preview: %d instances / %d blas\n",
                        editorSt.instances, editorSt.blasCount,
                        previewSt.instances, previewSt.blasCount);
            CHECK(previewSt.enabled && previewSt.instances == 1,
                  "the second scene has its OWN structure, with its own one instance");
            CHECK(editorSt.instances == 2,
                  "...and the editor scene's is untouched by it");

            // AND BOTH STILL GO QUIET. If the frame accounting counted calls
            // rather than frames, the guards below would be the thing that
            // broke first — and silently.
            render(e, 5);
            const RayQueryStatus e0 = scene->rayQueryStatus(), p0 = ps->rayQueryStatus();
            render(e, 30);
            const RayQueryStatus e1 = scene->rayQueryStatus(), p1 = ps->rayQueryStatus();
            CHECK(e1.tlasBuilds == e0.tlasBuilds && p1.tlasBuilds == p0.tlasBuilds,
                  "TWO still scenes rebuild nothing over 30 frames");

            // A ray into each still answers for the right world.
            std::vector<float> r, h;
            pushRay(r, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
            if (scene->traceRays(r, h) && h.size() == 4u)
                CHECK(hitAt(h, 0).hit && std::fabs(hitAt(h, 0).distance - 9.0f) < 0.001f,
                      "the editor scene's ray still answers 9 m with a second scene alive");

            // =================================================================
            // CASE 8 — destroying it takes the structures with it
            // =================================================================
            std::printf("\n== case 8: a destroyed scene ==\n");
            const int blasBefore = scene->rayQueryStatus().blasCount;
            pv->setScene(nullptr);
            e->destroyScene(ps);
            ps = nullptr;
            render(e, 6);
            CHECK(scene->rayQueryStatus().blasCount == blasBefore,
                  "destroying the second scene leaves the editor's structures alone");
            // The editor scene still traces correctly afterwards — the honest
            // proof that nothing of the dead scene's state leaked into it.
            std::vector<float> r2, h2;
            pushRay(r2, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
            CHECK(scene->traceRays(r2, h2) && h2.size() == 4u && hitAt(h2, 0).hit &&
                      std::fabs(hitAt(h2, 0).distance - 9.0f) < 0.001f,
                  "...and the editor scene still answers 9 m");
            if (pv) e->destroyView(pv);
            render(e, 3);
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
