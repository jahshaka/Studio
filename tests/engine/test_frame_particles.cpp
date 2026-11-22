// THE PARTICLES RUN ON THEIR SCENE's TIME, AND ONLY IN ITS FRAME (ONE-FRAME-1 stage 1, the stage-1 audit's test gap 1 and
// worth-a-look 2), through PFX2 itself — one binary, one row per proof; card rows (particles live on a drawn scene).
//
//   --per-scene  frame.particles_per_scene: two scenes, each an emitter at 120 particles/s (ttl 10 s), stamped in ONE
//                hand-over per frame with +1 and +3 steps: after 20 frames scene A holds 120 x 20/60 = 40 particles and
//                scene B 120 x 60/60 = 120 (+-1, PFX2's emission rounding). THE FORK's HOOK IS WHAT MAKES IT TRUE: with
//                ParticleSystemManager2::prepareForUpdate's override line reverted (fork 0b9e1e5c8), both scenes advance
//                by Ogre's one process-wide frame time (the first drawn view's scene: A's), and B holds ~40 — RED. That
//                revert is run once at the feature end and its output kept under spikes/one-frame-1/.
//   --resolve    frame.particles_once_a_frame: after a frame, a pose resolve (Engine::updateScene: the scene graph updated
//                again between frames — the avatar preview's, the sky export's) advances the particles NOTHING: the
//                live count is unchanged across it. RED ON irisgl 84455f7b (and on the base: the persisting delay), by
//                their code: the particles' time outlived its frame, so the resolve emitted another frame's worth.
#include <cmath>
#include <cstdio>
#include <string>

#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/FrameInput.h"
#include "jahshaka/engine/Published.h"
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

static ParticleSystemDesc steadyEmitter()
{
    ParticleSystemDesc d;
    d.quota = 4096;
    d.additive = true;
    ParticleEmitterDesc e;
    e.shape = ParticleEmitterShape::Point;
    e.direction = Vec3(0, 1, 0);
    e.rate = 120.0f;
    e.velocityMin = e.velocityMax = 0.1f;
    e.ttlMin = e.ttlMax = 10.0f;   // nothing dies inside the run
    e.sizeWidth = e.sizeHeight = 0.05f;
    d.emitters.push_back(e);
    return d;
}

static void stamp(FrameInput &in, Scene *s, unsigned long long serial)
{
    FrameInputScene part;
    part.scene = s;
    part.stepSerial = serial;
    part.stepPeriod = 1.0f / 60.0f;
    in.scenes.push_back(std::move(part));
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "--per-scene" && mode != "--resolve") {
        std::printf("usage: test_frame_particles --per-scene | --resolve\n");
        return 2;
    }
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-frame-particles-ogre.log"), err);
    check(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *va = engine->createOffscreenView("pa", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    View *vb = engine->createOffscreenView("pb", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *a = engine->createScene("pa");
    Scene *b = engine->createScene("pb");
    check(va && vb && a && b, "two offscreen views, two scenes");
    if (!va || !vb || !a || !b) return 1;
    va->setScene(a);
    vb->setScene(b);
    const NodeId ea = a->createNode(), eb = b->createNode();
    check(a->setParticleSystem(ea, steadyEmitter()) && b->setParticleSystem(eb, steadyEmitter()),
          "an emitter in each scene (Plugin_ParticleFX2 loaded)");
    {
        FrameInput in;
        stamp(in, a, 0);
        stamp(in, b, 0);
        engine->submitFrame(in);
    }
    enginetest::frozenFrames(engine, 1);   // primed: nothing emitted yet
    if (mode == "--per-scene") {
        const int k = 20;
        for (int f = 1; f <= k; ++f) {
            FrameInput in;
            stamp(in, a, (unsigned long long)f);
            stamp(in, b, (unsigned long long)(3 * f));
            engine->submitFrame(in);
            enginetest::frozenFrames(engine, 1);
        }
        const unsigned na = a->published().particlesOf(ea), nb = b->published().particlesOf(eb);
        std::printf("      %d frames, +1 and +3 steps a hand-over: scene A %u particles (law 40), scene B %u (law 120)\n",
                    k, na, nb);
        check(std::abs(int(na) - 40) <= 1, "scene A's particles aged by its own steps (120/s x 20/60 s)");
        check(std::abs(int(nb) - 120) <= 1, "scene B's particles aged by ITS steps in the same hand-overs (120/s x 60/60 s)");
    } else {
        for (int f = 1; f <= 10; ++f) {
            FrameInput in;
            stamp(in, a, (unsigned long long)f);
            engine->submitFrame(in);
            enginetest::frozenFrames(engine, 1);
        }
        const unsigned before = a->published().particlesOf(ea);
        const bool resolved = engine->updateScene(a);
        const unsigned after = a->published().particlesOf(ea);
        std::printf("      10 frames: %u particles; after a pose resolve (Engine::updateScene): %u\n", before, after);
        check(resolved, "the resolve ran");
        check(before > 0 && after == before, "a scene update between frames advances the particles nothing");
    }
    va->setScene(nullptr);
    vb->setScene(nullptr);
    engine->destroyView(va);
    engine->destroyView(vb);
    engine->destroyScene(a);
    engine->destroyScene(b);
    const char *row = mode == "--per-scene" ? "frame.particles_per_scene" : "frame.particles_once_a_frame";
    std::printf("%s: %s\n", row, gFailures == 0 ? "all checks passed" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
