// frame.meter_time — THE HDR METER ADAPTS IN ITS VIEWER's TIME (ONE-FRAME-1 stage 1; the lead's ruling reversing R6).
//
// THE LAW (OgreEngine::meterSecondsFor):
//  (a) A LIVE view adapts in PRESENTED time: a lockstep frame one grid step (1/60 s), whether or not its scene was stepped
//      (a paused scene still adapts as the camera turns), at any particle time scale (0.25, 4) — never the particles'.
//  (b) A PICTURE (an offscreen StillPicture view: a shot, a thumbnail, a preview tile) has NO HISTORY: its meter takes the
//      frame's own key (an infinite step, 0.25^dt = 0), so its exposure is equal after 2 frames and after 200 — a still
//      picture's brightness never depends on how many settle frames it took (the HISTORY-1 law).
// RED ON THE BASE 273b8e638 / irisgl 4576b132 (derived from its code; this row's probe is the stage's): (a) the meter's
// frame_time was simulated x particleTimeScale (0.0125 s at scale 0.25, 0.2 s at 4, 0 on a stepless frame); (b) a
// picture's meter adapted 1/60 s a frame from its first frame's value, so its exposure moved with its settle's length.
// RED ON irisgl 95a46e7e for both (its rule: the scene's stepped time, 0 for a still scene: a thumbnail kept its first
// frame's brightness for ever). A card row: the meter runs on a view.
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
    const float period = float(double(1.0f / 60.0f));

    // (a) A LIVE VIEW
    unsigned long long serial = 100;
    engine->submitFrame(stamp(scene, serial, 1.0f));
    enginetest::frozenFrames(engine, 2);
    for (float scale : { 0.25f, 4.0f }) {
        serial += 3;
        engine->submitFrame(stamp(scene, serial, scale));
        enginetest::frozenFrames(engine, 1);
        std::printf("      live, particle time scale %.2f, 3 steps: the meter took %.6f s (law %.6f)\n", double(scale),
                    double(echo->meterSeconds()), double(period));
        check(echo->meterSeconds() == period, "a live lockstep frame adapts one presented grid step, whatever the scale");
        enginetest::frozenFrames(engine, 1);
        check(echo->meterSeconds() == period, "...and a frame nobody stepped the scene still adapts (presented time)");
    }

    // (b) A PICTURE: no history
    View *pic = engine->createOffscreenView("meter-picture", 64, 64, Colour(0.2f, 0.2f, 0.2f), OffscreenContract::StillPicture);
    check(pic != nullptr, "a still-picture view");
    if (pic) {
        pic->setScene(scene);
        enginetest::testCameraLookAt(pic, Vec3(0, 1, 4), Vec3(0, 0, 0));
        pic->setPostFx(fx);
        view->setEnabled(false);
        const auto exposure = [&] {
            float m = 0.0f;
            engine->request([&](Engine &) { m = pic->measuredExposureScale(); });
            return m;
        };
        enginetest::frozenFrames(engine, 2);
        const float after2 = exposure();
        enginetest::frozenFrames(engine, 198);
        const float after200 = exposure();
        const float picMeter = inspect::view(pic)->meterSeconds();
        std::printf("      picture: the meter took %.1f s a frame; exposure after 2 frames %.9g, after 200 %.9g\n",
                    double(picMeter), double(after2), double(after200));
        check(picMeter >= 1000.0f, "a picture's meter has no history (an infinite step)");
        check(after2 > 0.0f && after2 == after200, "a picture's exposure is equal after 2 and after 200 frames");
        pic->setScene(nullptr);
        engine->destroyView(pic);
    }
    engine->destroyView(view);
    engine->destroyScene(scene);
    std::printf("frame.meter_time: %s\n", gFailures == 0 ? "all checks passed" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
