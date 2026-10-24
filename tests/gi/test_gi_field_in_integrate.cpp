// gi.field_in_integrate (PHOTON-7 stage 7, 7-2; SPECS/PHOTON_7_SPEC.md stage 7, L4/L5) - THE FIELD'S FALLBACK MOVED INTO
// THE GATHER'S INTEGRATE: the producer moved, the physics did not.
//
// THE MECHANISM: a pixel no screen probe answers takes the irradiance field's cage (and, where the field's confidence
// fades, the environment's diffuse). Before 7-2 the SHADING PASS ran the cage (PhotonIfd_piece_ps.any); since, the
// gather's integrate composes it into its own texture after the temporal mean (rq_probe_integrate.comp), w = 1, and the
// pass's cage declines. Both read the one text, jah_field_read.glsl.
// THE FIXTURE: the leak room (tests/support leakroom: 0.3 m walls, its green lamp inside), High, the rays on, a still
// picture (the view gathers), the field ON, the cards off; the measuring door gather.noAnswer (no probe answers any
// pixel: every pixel's diffuse GI is the fallback alone), the gather never holding (gather.restOff: every frame is
// integrated, so a switched arm is read) - and the scene radiance read in float (hdrReadback).
// THE ARMS, in one process (paired): A the pass's route (the arm gather.fieldInPass), B the integrate's (shipped), each
// at the GI's rest; then B again (the control: the same route twice).
// THE LAWS:
//   (1) THE CONTROL: B against B - every pixel identical (the instrument is exact);
//   (2) THE PRODUCER MOVED, THE PHYSICS DID NOT: every lit pixel of A within the fp16 store's quantum of B's - 2^-10 of
//       the larger, over the store's floor - the share outside it, its mean and worst printed;
//   (3) the fallback is not vacuous: the indirect is present (A's lit pixels against the same picture with the GI off).
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-field-in-integrate-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("field_in_integrate", 256, 160, Colour(0, 0, 0));
    Scene *scene = e->createScene("field_in_integrate");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    view->setOffscreenContract(OffscreenContract::StillPicture);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    enginetest::leakroom::build(scene, view, 0.3f);
    enginetest::testCameraLookAt(view, Vec3(3.4f, 2.6f, 3.4f), Vec3(-2.0f, 1.0f, -3.0f));
    e->setArm("gather.noAnswer", 1.0);
    // THE GATHER NEVER HOLDS (the arm gather.restOff): a held view dispatches no integrate, so an arm switched at rest
    // would read the last composite again - every frame is integrated, and both routes read the same field at rest
    e->setArm("gather.restOff", 1.0);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::On;
    gi.gather = GiToggle::On;
    gi.cards = false;
    gi.bounces = true;
    const auto settle = [&]() {
        int quiet = 0, f = 0;
        for (; f < 1200 && quiet < 30; ++f) {
            run(1);
            quiet = scene->published().gi.giAtRest ? quiet + 1 : 0;
        }
        return quiet >= 30 ? f - quiet : -1;
    };
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    const auto shoot = [&](bool passRoute, ImageF &out) {
        e->setArm("gather.fieldInPass", passRoute ? 1.0 : 0.0);
        run(2);
        const int rest = settle();
        run(2);
        return rest >= 0 && view->readPixelsHdr(out) ? rest : -1;
    };
    ImageF a, b, b2, off;
    const int restB = shoot(false, b);
    const int restA = shoot(true, a);
    const int restB2 = shoot(false, b2);
    // (3)'s reference: the same picture with the GI off (the direct light alone)
    e->setArm("gather.fieldInPass", 0.0);
    GiParams none;
    none.mode = GiMode::Off;
    scene->setGlobalIllumination(none);
    run(4);
    const bool offOk = view->readPixelsHdr(off);
    const bool ok = restA >= 0 && restB >= 0 && restB2 >= 0 && offOk && a.width == b.width && b2.width == b.width &&
                    off.width == b.width && !a.rgba.empty();
    CHECK(ok, "the GI rests in each arm and the scene radiance reads back");
    if (!ok) return 1;

    size_t lit = 0, outside = 0, controlDiff = 0, indirect = 0;
    double sumRel = 0.0, worstRel = 0.0, worstAbs = 0.0;
    const double kFloor = 5.9604645e-6;   // the half store's smallest normal step
    for (size_t i = 0; i + 3 < b.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c)
            controlDiff += b.rgba[i + c] != b2.rgba[i + c] ? 1u : 0u;
        double pixWorst = 0.0;
        bool anyLit = false, anyIndirect = false;
        for (int c = 0; c < 3; ++c) {
            const double va = a.rgba[i + c], vb = b.rgba[i + c];
            const double big = std::max(std::fabs(va), std::fabs(vb));
            if (big <= kFloor) continue;
            anyLit = true;
            anyIndirect = anyIndirect || vb > double(off.rgba[i + c]) * 1.01 + kFloor;
            const double d = std::fabs(va - vb);
            const double quantum = std::ldexp(big, -10) + kFloor;
            pixWorst = std::max(pixWorst, d / quantum);
            worstAbs = std::max(worstAbs, d);
        }
        if (!anyLit) continue;
        ++lit;
        indirect += anyIndirect ? 1u : 0u;
        sumRel += pixWorst;
        worstRel = std::max(worstRel, pixWorst);
        outside += pixWorst > 1.0 ? 1u : 0u;
    }
    std::printf("   at rest in %d (B) / %d (A) / %d (B again) frames | %zu lit pixels (%zu with an indirect over the GI-off "
                "picture) | A against B, in quanta of the fp16 store (2^-10 of the larger): mean %.3f, worst %.2f (%.3e "
                "absolute), %zu outside one (%.2f %%) | B against B: %zu channels differ\n",
                restB, restA, restB2, lit, indirect, lit ? sumRel / double(lit) : 0.0, worstRel, worstAbs, outside,
                lit ? 100.0 * double(outside) / double(lit) : 0.0, controlDiff);
    CHECK(controlDiff == 0u, "(1) THE CONTROL: the integrate's route twice, every pixel identical");
    CHECK(lit > 0u && outside == 0u,
          "(2) THE PRODUCER MOVED, THE PHYSICS DID NOT: every lit pixel of the pass's route within one fp16 quantum of "
          "the integrate's");
    CHECK(indirect * 2u > lit, "(3) the fallback is not vacuous: most lit pixels carry an indirect term");
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
