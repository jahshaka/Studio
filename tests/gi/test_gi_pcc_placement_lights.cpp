// gi.pcc_placement_lights — A PROBE PLACEMENT AFTER THE SCENE GRAPH DRAWS WITH ITS LAMPS (ENGINE-UPGRADE-U4 decision 14).
//
// The engine's GI work runs in the frame's `engine.plan`, AFTER updateSceneGraph (decision 8): the pending flushes —
// the staged build's probe placement among them — run where the world boxes are the frame's. A placement ends in
// ParallaxCorrectedCubemapAuto::updateAllDirtyProbes, whose clearFrameData emptied the scene manager's global light
// list, rebuilt only by updateSceneGraph — already run: the frame drew with ZERO lamps on the tier that places probes
// (VctPccHybrid with rays off: the Mac, ray tracing off). The fork keeps the list as it found it, and a probe a
// placement sets has its node current at once (CubemapProbe::syncInternalProbe), so Forward+ bins it where it was set.
//
// THE GUARD is the arm `gi.pccPlacementLights`: every probe-placement stage (Scout, Finish) records the light count
// before and after it and each placed probe's derived-node offset from the position it was set to. The bar: the
// count after = the count before = the scene's lamps (never 0), and 0 m — on a build staged frame by frame (a
// streaming pace) and on a whole one (a Complete frame), in the same process.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        char m_[512]; std::snprintf(m_, sizeof m_, __VA_ARGS__);                \
        if (cond) std::printf("ok: %s\n", m_);                                  \
        else { std::printf("FAIL: %s\n", m_); ++failures; }                     \
    } while (0)

static NodeId box(Scene *s, const Vec3 &pos, const Vec3 &scale, const Colour &albedo, float metal, float rough)
{
    const NodeId n = s->createNode();
    PbrParams p;
    p.albedo = albedo;
    p.metalness = metal;
    p.roughness = rough;
    const MaterialId mat = s->createPbrMaterial(p);
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    if (!n || !mat || !mesh || !s->attachMesh(n, mesh, mat)) return 0;
    s->setNodeTransform(n, pos, Quat(), scale);
    return n;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-pcc-placement-lights-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    View *view = e->createOffscreenView("pcc_lights", 128, 128, Colour(0, 0, 0));
    Scene *s = e->createScene("pcc_lights");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    s->setRayTracing(RayTracingMode::Off);   // the tier that places probes
    s->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.01f, 0.01f, 0.01f));
    // A CLOSED WHITE ROOM with a chrome cube in it (gi.mobility's): the placement keeps its candidates.
    const Colour white(0.85f, 0.85f, 0.85f);
    box(s, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f), white, 0.0f, 0.9f);
    box(s, Vec3(0.0f, 5.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f), white, 0.0f, 0.9f);
    box(s, Vec3(0.0f, 2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f), white, 0.0f, 0.9f);
    box(s, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f), white, 0.0f, 0.9f);
    box(s, Vec3(4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f), white, 0.0f, 0.9f);
    box(s, Vec3(0.0f, 2.5f, 4.2f), Vec3(8.8f, 5.0f, 0.4f), Colour(1.0f, 0.02f, 0.02f), 0.0f, 0.9f);
    box(s, Vec3(0.0f, 2.0f, 0.0f), Vec3(1.6f, 1.6f, 1.6f), Colour(1, 1, 1), 1.0f, 0.0f);   // chrome: probes matter
    // THE SCENE'S LAMPS: a sun and a point lamp (a Forward+ light).
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.2f), 4.0f);
    const NodeId lamp = s->createNode();
    s->setNodeTransform(lamp, Vec3(1.5f, 3.0f, 1.5f), Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1.0f, 0.9f, 0.8f);
    l.intensity = 20.0f;
    CHECK(s->setLight(lamp, l), "the point lamp");
    const unsigned lamps = 2u;
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 3.5f), Vec3(0.0f, 2.0f, 0.0f)));

    const inspect::SceneEcho *echo = inspect::scene(s);
    CHECK(echo && e->setArm("gi.pccPlacementLights", 1.0), "the placement guard's arm");
    if (!echo) return 1;

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    gi.updateBudget = 1;
    gi.testProbeRegionMin = Vec3(-4.6f, -0.6f, -4.6f);
    gi.testProbeRegionMax = Vec3(4.6f, 5.6f, 4.6f);
    const auto frame = [&](FramePace pace) {
        e->setFramePace(pace);   // the driver's property, in force until set again
        FrameInputScene part;
        part.scene = s;
        part.gi = gi;
        FrameInput in;
        in.scenes.push_back(part);
        enginetest::frames(e, 1, in);
    };

    // ARM 1: THE BUILD STAGED FRAME BY FRAME. The world "arrives" (Scene::setLoading) so nothing is built in the
    // configuration's push; then streaming frames build the chain and spend one probe stage each — in the frame's
    // pending flush, after its updateSceneGraph — and a Complete frame finishes the rest.
    frame(FramePace::Complete);
    frame(FramePace::Complete);
    s->setLoading(true);
    for (int f = 0; f < 3; ++f) frame(FramePace::Streaming);
    s->setLoading(false);
    for (int f = 0; f < 8; ++f) frame(FramePace::Streaming);
    for (int f = 0; f < 3; ++f) frame(FramePace::Complete);
    const inspect::SceneEcho::GiPlan a = echo->giPlan();
    const GiStatus st = s->published().gi;
    std::printf("   staged: %llu placement stages checked (%llu inside a frame), %llu changed the light count (last %u "
                "-> %u), %u probes checked, worst node offset %.6f m; probes %d bound %d\n", a.pccStages,
                a.pccStagesLit, a.pccLightChanges, a.pccLightsBefore, a.pccLightsAfter, a.pccProbes,
                double(a.pccProbeOffsetMax), st.probeCount, int(st.pccBound));
    CHECK(st.probeCount == 4 && st.pccBound, "the 2x1x2 probe grid built and bound (%d probes)", st.probeCount);
    CHECK(a.pccStagesLit >= 2u, "the Scout and the Finish ran after the frame's scene graph, under the guard "
          "(%llu of %llu stages inside a frame)", a.pccStagesLit, a.pccStages);
    CHECK(a.pccLightChanges == 0u && a.pccLightsAfter == lamps,
          "a placement after the scene graph leaves the frame's light list as it found it: the scene's %u lamps "
          "(%llu stages changed it; last %u -> %u)", lamps, a.pccLightChanges, a.pccLightsBefore, a.pccLightsAfter);
    CHECK(a.pccProbes > 0u && a.pccProbeOffsetMax == 0.0f,
          "every placed probe's node is where it was set in the frame (%u probes, worst %.6f m)", a.pccProbes,
          double(a.pccProbeOffsetMax));

    // ARM 2: A WHOLE BUILD IN ONE COMPLETE FRAME (every stage back to back in the frame's flush, after its scene
    // graph): a new grid shape arrives while the world is "loading", then one Complete frame builds the arm whole.
    gi.pccProbesX = 3;
    s->setLoading(true);
    frame(FramePace::Complete);
    s->setLoading(false);
    frame(FramePace::Complete);
    frame(FramePace::Complete);
    const inspect::SceneEcho::GiPlan b = echo->giPlan();
    const GiStatus st2 = s->published().gi;
    std::printf("   whole: %llu more stages (%llu inside a frame), %llu changed the light count (last %u -> %u), worst "
                "node offset %.6f m; probes %d\n", b.pccStages - a.pccStages, b.pccStagesLit - a.pccStagesLit,
                b.pccLightChanges - a.pccLightChanges, b.pccLightsBefore, b.pccLightsAfter,
                double(b.pccProbeOffsetMax), st2.probeCount);
    CHECK(st2.probeCount == 6, "the 3x1x2 grid re-placed (%d probes)", st2.probeCount);
    CHECK(b.pccStagesLit >= a.pccStagesLit + 2u, "...in the frame's flush, after its scene graph (%llu stages)",
          b.pccStagesLit - a.pccStagesLit);
    CHECK(b.pccLightChanges == 0u && b.pccLightsAfter == lamps,
          "...and its placement left the light list as it found it (%u lamps)", b.pccLightsAfter);
    CHECK(b.pccProbeOffsetMax == 0.0f, "...and its probes' nodes where they were set (%.6f m)",
          double(b.pccProbeOffsetMax));

    // ARM 3: A BUDGETED RECAPTURE SEES A LAMP THE VIEWER DOES NOT (decision 16.1). A short-range point lamp BEHIND
    // the camera (its sphere entirely behind the near plane: the main camera's light culling drops it) lighting the
    // red wall the chrome cube reflects back at the viewer. The lamp stales the grid; the budget (1 probe a frame)
    // recaptures each probe through its own light-culling camera, after the frame's scene graph — the fork rebuilds
    // the frame's light list once the collection queued those cameras, so the list holds every lamp, and what the
    // probes captured holds the lamp: the chrome pixel with the lamp differs from the one without.
    const auto chrome = [&]() { Image img; view->readPixels(img); return img.at(img.width / 2, img.height / 2); };
    for (int f = 0; f < 12; ++f) frame(FramePace::Complete);   // the 3x1x2 grid's catch-up spent
    const Colour without = chrome();
    const NodeId behind = s->createNode();
    s->setNodeTransform(behind, Vec3(0.0f, 2.0f, 3.95f), Quat(), Vec3(1, 1, 1));
    LightDesc bl;
    bl.type = LightType::Point;
    bl.colour = Colour(1.0f, 1.0f, 1.0f);
    bl.intensity = 40.0f;
    bl.range = 0.4f;
    CHECK(s->setLight(behind, bl), "the lamp behind the viewer (range 0.4 m, at z 3.95; the camera at z 3.5)");
    unsigned recaptureFrames = 0u, short_ = 0u, worst = lamps + 1u;
    for (int f = 0; f < 12; ++f) {
        frame(FramePace::Complete);
        const GiStatus gs = s->published().gi;
        const inspect::SceneEcho::GiPlan p = echo->giPlan();
        if (gs.probeCapturesLastFrame > 0) {
            ++recaptureFrames;
            if (p.pccCollectLights != lamps + 1u) ++short_;
            worst = std::min(worst, p.pccCollectLights);
        }
    }
    for (int f = 0; f < 4; ++f) frame(FramePace::Complete);
    const Colour with = chrome();
    std::printf("   recapture: %u frames recaptured, %u of them with a short light list (fewest %u of %u lamps); chrome "
                "%.4f/%.4f/%.4f without the lamp, %.4f/%.4f/%.4f with it\n", recaptureFrames, short_, worst, lamps + 1u,
                without.r, without.g, without.b, with.r, with.g, with.b);
    CHECK(recaptureFrames > 0u, "the lamp staled the grid and the budget recaptured (%u frames)", recaptureFrames);
    CHECK(short_ == 0u, "every recapture frame's light list after the collection holds every lamp (%u short; fewest "
          "%u of %u)", short_, worst, lamps + 1u);
    const float change = std::fabs(with.r - without.r) + std::fabs(with.g - without.g) + std::fabs(with.b - without.b);
    CHECK(change > 0.002f, "what the probes captured holds the lamp behind the viewer (chrome %.4f/%.4f/%.4f -> "
          "%.4f/%.4f/%.4f)", without.r, without.g, without.b, with.r, with.g, with.b);

    e->setFramePace(FramePace::Complete);
    e->setArm("gi.pccPlacementLights", 0.0);
    view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "gi.pcc_placement_lights: FAILED (%d)\n" : "gi.pcc_placement_lights: PASS\n", failures);
    return failures ? 1 : 0;
}
