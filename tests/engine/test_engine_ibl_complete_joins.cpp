// sky.ibl_complete_joins — A COMPLETE FRAME's CONVOLUTION BUILDS NO PIPELINE: IT JOINS THE REQUESTS MADE WITH ITS CLONES
// (SHADER-SYSTEM-1 7a, R2 (b)). A sky change convolves the captured sky into the specular cube through a one-shot
// workspace whose ibl_specular pass CLONES a job per mip (fork CompositorPassIblSpecular::setupComputeShaders:
// IblSpecular/Integrate/<id>/mipN), each a compute pipeline of its own even when the code is known. The editor's frames
// are COMPLETE outside a load, where the one async-shader rule (OgreScene::compilesInBackground) makes the convolution
// run in the frame - so the editor's first frames built IblSpecular/Integrate/3/mip0-7 inside a frame
// (spikes/shader-system/ss7/gategap-red2). Now a scene that compiles in the background requests the clones' pipelines on
// the service as a set when the workspace makes them (OgreScene::convolvePendingIbl), and a Complete frame's dispatch
// JOINS that in-flight request (HlmsCompute::dispatch -> waitForAsyncPermutation) instead of building it: counted, as
// AsyncShaderStats::computeJoins (stage 7 brings the frame thread's joins to 0).
// THE SCRIPT: one engine on the rig's shared cache; every frame on the default COMPLETE pace (the editor's). Scene A (an
// atmosphere sky) in an asynchronous offscreen view, frames until its environment has landed and nothing is pending;
// then scene B, the same sky, in a second asynchronous view, until B has landed; then scene C, the same sky, in a
// SYNCHRONOUS view (nothing requested up front: the control), until C has landed.
// THE LAWS:
//  (1) B's frames built 0 compute pipelines inside a frame (the published computePsoInFrame, a process total read before
//      and after). RED before R2 (b): 8 (the clones of B's convolution).
//  CONTROL (the zero-pass rule): C's frames built at least one compute pipeline inside a frame - the convolution's
//      clones, never requested - else (1) proves nothing.
//  PRINTED, NOT A LAW: B's joins and the time they waited (computeJoins / computeJoinMs).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <cstdio>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

/// The hang guard of a scene's wait (never a bar).
static constexpr int kFrameCap = 3000;

static Scene *skyScene(Engine *e, const char *name, bool async)
{
    View *v = e->createOffscreenView(name, 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *s = v ? e->createScene(name) : nullptr;
    if (!s) return nullptr;
    v->setAsyncShaders(async);
    v->setScene(s);
    v->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 1.6f, 3.0f), Vec3(0.0f, 1.6f, 0.0f)));
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    return s->setSky(sky) ? s : nullptr;
}

static bool landed(Engine *e, Scene *s)
{
    return !s->published().environmentPending && s->published().skyAmbientShValid && e->published().shaders.pending == 0u;
}

struct Run { int frames = 0; unsigned long long psoInFrame = 0, joins = 0; double joinMs = 0.0; bool landed = false; };

/// One scene from its creation until its environment has landed: what its frames built and joined.
static Run runScene(Engine *e, Scene *s)
{
    const AsyncShaderStats before = e->published().shaders;
    Run r;
    for (; r.frames < kFrameCap && !landed(e, s); ++r.frames) enginetest::frames(e, 1);
    const AsyncShaderStats after = e->published().shaders;
    r.landed = landed(e, s);
    r.psoInFrame = after.computePsoInFrame - before.computePsoInFrame;
    r.joins = after.computeJoins - before.computeJoins;
    r.joinMs = after.computeJoinMs - before.computeJoinMs;
    return r;
}

int main()
{
    std::printf("== sky.ibl_complete_joins: a Complete frame's convolution joins the requests made with its clones\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-ibl-complete-joins-ogre.log");
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();

    struct Step { const char *name; bool async; Scene *scene = nullptr; Run run; };
    Step steps[] = { { "A (asynchronous)", true }, { "B (asynchronous)", true }, { "C (synchronous: the control)", false } };
    const char *names[] = { "ibl-joins-a", "ibl-joins-b", "ibl-joins-c" };
    for (int i = 0; i < 3; ++i) {
        Step &st = steps[i];
        st.scene = skyScene(e, names[i], st.async);
        CHECK(st.scene != nullptr, "scene %s: an atmosphere sky (%s)", st.name, e->lastError().c_str());
        if (!st.scene) return 1;
        st.run = runScene(e, st.scene);
        std::printf("    scene %s: landed %d after %d frames; compute pipelines built in a frame %llu; joins %llu "
                    "(%.2f ms waited)\n", st.name, st.run.landed ? 1 : 0, st.run.frames, st.run.psoInFrame,
                    st.run.joins, st.run.joinMs);
        CHECK(st.run.landed, "scene %s's environment landed within %d frames", st.name, kFrameCap);
    }
    CHECK(steps[2].run.psoInFrame > 0ull, "CONTROL: C's convolution, never requested, built %llu compute pipeline(s) in "
                                          "a frame (above 0)", steps[2].run.psoInFrame);
    CHECK(steps[1].run.psoInFrame == 0ull, "(1) B's frames built %llu compute pipeline(s) inside a frame (bar 0)",
          steps[1].run.psoInFrame);

    for (const Step &st : steps) enginetest::poseRegistry().erase(st.scene);
    if (failures) std::printf("sky.ibl_complete_joins: FAIL (%d)\n", failures);
    else std::printf("sky.ibl_complete_joins: PASS\n");
    return failures ? 1 : 0;
}
