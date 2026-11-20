// frame.meter_time — THE HDR METER ADAPTS IN ITS SCENE's TIME (ONE-FRAME-1 stage 1, V2 plan 2.1; ruling R6: exposure is
// a grid quantity).
//
// THE LAW: the time a view hands its meter (JahHdrMeterResolve_cs: mix(new, old, 0.25^dt)) is the steps ITS scene
// advanced this frame x the period — never x particleTimeScale, never Ogre's process-wide frame clock — and 0 on a
// frame nobody stepped the scene (a picture's settle, a free frame between grid steps).
// THE DEFECT (ground read § 2.1 THE METER, [F]): the meter's timeSinceLast was the `frame_time` auto-param = Ogre's
// frame delay = the host's frameDelta = simulated x particleTimeScale (enginesceneviewport.cpp:2724), so slow-motion
// particles slowed the exposure with them. On the base 273b8e638 / irisgl 4576b132 this row reads 0.25 x 3/60 =
// 0.0125 s at scale 0.25 and 0.2 s at scale 4 (DERIVED from the base's code: the probe is this stage's; the card was
// closed while the stage was built — the feature-end pass is its first run).
// A card row: the meter runs only on a view (the NULL render system holds none).
#include <cmath>
#include <cstdio>

#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "enginetesthelpers.h"

using namespace jahshaka::engine;

static int gFailures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

static FrameInput stamp(Scene *s, unsigned long long serial, float particleScale)
{
    FrameInput in;
    FrameInputScene part;
    part.scene = s;
    part.stepSerial = serial;
    part.stepPeriod = 1.0f / 60.0f;
    part.particleTimeScale = particleScale;
    in.scenes.push_back(std::move(part));
    return in;
}

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-frame-meter-ogre.log"), err);
    check(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("meter", 64, 64, Colour(0.2f, 0.2f, 0.2f), OffscreenContract::Live);
    Scene *scene = engine->createScene("meter");
    check(view && scene, "an offscreen view and a scene");
    if (!view || !scene) return 1;
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(0, 1, 4), Vec3(0, 0, 0));
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.hdr = true;   // the auto grade: the meter runs (tonemapFixed false)
    view->setPostFx(fx);
    const inspect::ViewEcho *echo = inspect::view(view);
    check(echo != nullptr, "the view's probes");
    if (!echo) return 1;
    const double period = double(1.0f / 60.0f);

    unsigned long long serial = 100;
    engine->submitFrame(stamp(scene, serial, 1.0f));
    enginetest::frozenFrames(engine, 2);
    for (float scale : { 0.25f, 4.0f }) {
        const unsigned k = 3;
        serial += k;
        engine->submitFrame(stamp(scene, serial, scale));
        enginetest::frozenFrames(engine, 1);
        const float meter = echo->meterSeconds();
        std::printf("      particle time scale %.2f, %u steps: the meter took %.6f s (law %.6f)\n", double(scale), k,
                    double(meter), double(k) * period);
        check(meter == float(double(k) * period), "the meter's time is steps x period, whatever the particle scale");
        enginetest::frozenFrames(engine, 1);
        check(echo->meterSeconds() == 0.0f, "a frame nobody stepped the scene hands the meter 0");
    }
    engine->destroyView(view);
    engine->destroyScene(scene);
    std::printf("frame.meter_time: %s\n", gFailures == 0 ? "all checks passed" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
