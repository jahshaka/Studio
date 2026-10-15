// THE FRAME'S INPUT, DRAINED ONCE (ENGINE-UPGRADE-1 U0 3a UPGRADE-RECORD-1). Two rows, laws in counts:
//
//   engine.frame_input_drain (--drain): one submit then N frames -> exactly N drains, N-1 of them empty, and the
//     record's one changed field applied once; two submits before one frame -> ONE drain, the scene part REPLACED
//     (scenePartsMerged + 1) and the newest value the one in force; a record that changes nothing drains to nothing
//     (fieldsApplied == 0); a view's camera alone moves the per-view count only. Prints the drain's own cost.
//   engine.frame_input_settle (--settle): THE RECORD CHANGES WHEN, NOT WHAT — the one-cascade arm with the gather
//     on (test_engine_gi_scan's cascadeGi): the frames to giAtRest and the GI scans with ONE record then nothing
//     re-submitted EQUAL the frames and scans when the identical record is re-submitted every frame. Both arms in
//     one process, each on its own scene.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

namespace {

GiParams cascadeGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;     // the gather ON: the settle the shot path waits for
    gi.updateBudget = 0;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 6.0f, 64, 0.0f };
    return gi;
}

FrameInput sceneRecord(Scene *s, const FogDesc &fog)
{
    FrameInput in;
    FrameInputScene part;
    part.scene = s;
    part.fog = fog;
    in.scenes.push_back(part);
    return in;
}

int drain(Engine *e)
{
    View *view = e->createOffscreenView("frame_input", 64, 64, Colour(0.0f, 0.0f, 0.0f, 1.0f));
    Scene *scene = view ? e->createScene("frame_input") : nullptr;
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.55f), 3.0f);
    enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.8f);
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(2.5f, 1.8f, 3.0f), Vec3(0, 0.4f, 0)));
    enginetest::frames(e, 3);

    // 1. one submit, then N frames: N drains, N-1 empty, the one changed field applied once
    FogDesc fog;
    fog.enabled = true;
    fog.density = 0.05f;
    const FrameInputStatus s0 = e->published().input;
    const FrameSubmitStatus u0 = e->frameSubmitStatus();
    e->submitFrame(sceneRecord(scene, fog));
    const int N = 6;
    unsigned firstApplied = 0;
    std::vector<double> costs;
    for (int f = 0; f < N; ++f) {
        enginetest::frames(e, 1);
        if (f == 0) firstApplied = e->published().input.fieldsApplied;
        costs.push_back(e->published().input.drainMicros);
    }
    const FrameInputStatus s1 = e->published().input;
    const FrameSubmitStatus u1 = e->frameSubmitStatus();
    CHECK(u1.submitted - u0.submitted == 1, "one submit counted (%llu)", u1.submitted - u0.submitted);
    CHECK(s1.drained - s0.drained == unsigned(N), "one submit then %d frames: %d drains (%llu)", N, N,
          s1.drained - s0.drained);
    CHECK(s1.drainedEmpty - s0.drainedEmpty == unsigned(N - 1), "...%d of them empty (%llu)", N - 1,
          s1.drainedEmpty - s0.drainedEmpty);
    CHECK(firstApplied == 1, "the record's one changed field (the fog) applied at the first drain (%u)", firstApplied);

    // 2. the same record again: it changes nothing, so it drains to nothing
    enginetest::frames(e, 1, sceneRecord(scene, fog));
    CHECK(e->published().input.fieldsApplied == 0, "an unchanged record drains to nothing (fieldsApplied %u)",
          e->published().input.fieldsApplied);

    // 3. two submits before one frame: ONE drain, the scene part REPLACED, the newest in force
    FogDesc fogA = fog, fogB = fog;
    fogA.density = 0.10f;
    fogB.density = 0.02f;
    const FrameInputStatus s2 = e->published().input;
    const FrameSubmitStatus u2 = e->frameSubmitStatus();
    e->submitFrame(sceneRecord(scene, fogA));
    enginetest::frames(e, 1, sceneRecord(scene, fogB));
    const FrameInputStatus s3 = e->published().input;
    const FrameSubmitStatus u3 = e->frameSubmitStatus();
    CHECK(s3.drained - s2.drained == 1 && s3.drainedEmpty == s2.drainedEmpty,
          "two submits then one frame: ONE drain (%llu), not empty", s3.drained - s2.drained);
    CHECK(u3.scenePartsMerged - u2.scenePartsMerged == 1, "...the scene part merged into the pending one, the newer winning (%llu)", u3.scenePartsMerged - u2.scenePartsMerged);
    enginetest::frames(e, 1, sceneRecord(scene, fogB));
    CHECK(e->published().input.fieldsApplied == 0,
          "...and the NEWEST is the one in force (fogB again applies nothing: %u)", e->published().input.fieldsApplied);

    // 4. a camera move is the per-view part only
    {
        FrameInput in;
        FrameInputView v;
        v.view = view;
        v.camera = enginetest::testCameraDescLookAt(Vec3(3.0f, 1.5f, 2.0f), Vec3(0, 0.4f, 0));
        in.views.push_back(v);
        in.scenes.push_back(sceneRecord(scene, fogB).scenes.front());
        enginetest::frames(e, 1, in);
        const FrameInputStatus st = e->published().input;
        CHECK(st.fieldsApplied == 1 && st.viewFieldsApplied == 1,
              "a camera move with a still environment: the per-view part only (%u applied, %u of them per view)",
              st.fieldsApplied, st.viewFieldsApplied);
    }
    // 5. a record naming a scene destroyed before its frame is skipped, not followed
    {
        Scene *gone = e->createScene("gone");
        e->submitFrame(sceneRecord(gone, fogA));
        e->destroyScene(gone);
        enginetest::frames(e, 1);
        CHECK(true, "a record naming a scene destroyed before its frame drains without touching it");
    }
    std::sort(costs.begin(), costs.end());
    std::printf("    the drain's own cost: median %.2f us, max %.2f us over %zu frames (one scene, one view)\n",
                costs[costs.size() / 2], costs.back(), costs.size());
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    return 0;
}

struct SettleArm { int frames = -1; unsigned long long scans = 0; };

SettleArm settleArm(Engine *e, const char *name, bool resubmitEveryFrame)
{
    SettleArm arm;
    View *view = e->createOffscreenView(name, 96, 96, Colour(0.05f, 0.06f, 0.08f, 1.0f));
    Scene *scene = view ? e->createScene(name) : nullptr;
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return arm; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.25f, 0.25f, 0.25f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(40.0f, 0.1f, 40.0f));
    const NodeId cube = enginetest::addTestCube(scene, Colour(0.85f, 0.2f, 0.15f), 0.0f, 0.5f);
    enginetest::setNodePosition(scene, cube, Vec3(0.0f, 0.5f, 0.0f));
    FrameInput in;
    FrameInputScene part;
    part.scene = scene;
    part.gi = cascadeGi();
    in.scenes.push_back(part);
    FrameInputView v;
    v.view = view;
    v.camera = enginetest::testCameraDescLookAt(Vec3(2.5f, 1.8f, 3.0f), Vec3(0, 0.4f, 0));
    in.views.push_back(v);
    e->submitFrame(in);
    const unsigned long long scans0 = scene->published().gi.giScans;
    for (int f = 0; f < 600; ++f) {
        if (resubmitEveryFrame && f > 0) e->submitFrame(in);
        enginetest::frames(e, 1);
        if (f > 2 && scene->published().gi.giAtRest) { arm.frames = f + 1; break; }
    }
    arm.scans = scene->published().gi.giScans - scans0;
    std::printf("    [%s] frames to giAtRest %d, GI scans %llu\n", name, arm.frames, arm.scans);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    return arm;
}

int settle(Engine *e)
{
    const SettleArm once = settleArm(e, "settle_once", false);
    const SettleArm every = settleArm(e, "settle_every", true);
    CHECK(once.frames > 0 && every.frames > 0, "both arms reach giAtRest (%d / %d frames)", once.frames, every.frames);
    CHECK(once.frames == every.frames,
          "the record changes WHEN, not WHAT: frames to giAtRest equal with one record and with one per frame (%d / %d)",
          once.frames, every.frames);
    CHECK(once.scans == every.scans, "...and the GI scans equal (%llu / %llu)", once.scans, every.scans);
    return 0;
}

}   // namespace

int main(int argc, char **argv)
{
    const bool settleMode = argc > 1 && std::strcmp(argv[1], "--settle") == 0;
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig(settleMode ? "test-frame-input-settle-ogre.log"
                                                                         : "test-frame-input-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const int rc = settleMode ? settle(engine.get()) : drain(engine.get());
    std::printf("%s: %s\n", settleMode ? "engine.frame_input_settle" : "engine.frame_input_drain",
                rc || failures ? "FAILED" : "PASSED");
    return rc || failures ? 1 : 0;
}
