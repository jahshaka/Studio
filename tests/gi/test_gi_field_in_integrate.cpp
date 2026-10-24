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
//   (2) THE PRODUCER MOVED, THE PHYSICS DID NOT: every lit channel of A within TWO ULPS of the fp16 value stored (the
//       larger of A's and B's), DERIVED (the lead's ruling, 7-3): the pass's route rounds once, its sum at the end; the
//       integrate's rounds the field term into its rgba16f texture, then its sum at the end - so |A - B| <= 1/2 ulp (A's
//       sum) + 1/2 ulp (B's field term, at its own binade, at most the sum's) + 1/2 ulp (B's sum) <= 1.5 ulps of the
//       result, and 2 bounds it. (Its bar was "2^-10 of the larger" until 7-3 - 1 to 2 ulps by binade, never derived:
//       one pixel of 40,960 read 1.006 of it once the rough specular moved the sum's rounding.) The mean in those old
//       quanta stays printed;
//   (3) THE COMPARISON HAS THE POWER TO SEE A BIAS (the lead's ruling, 7-3): (2) compares at least 1,000 lit pixels
//       whose fallback is NONZERO - the integrate's own composed E/pi (its readback, gather.irradianceReadback) - enough
//       to see a 0.05-quantum bias at 3 sigma at the measured spread (printed, with its standard error). (It counted
//       until 7-3 the lit pixels brighter than the GI-off picture, a majority the cone's specular carried: it never
//       measured the fallback, which answers ~20 % of this view - the field's box ends inside the room, its ambient is
//       black.)
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
    e->setArm("gather.irradianceReadback", 1.0);   // (3)'s reading: the integrate's own composed answer
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
    // (3): the integrate's composed E/pi of the integrate's route (the last retired frame's copy)
    const std::vector<float> irr = scene->published().gi.gather.irradiance;
    size_t irrTexels = irr.size() / 4u, irrLit = 0;
    for (size_t i = 0; i + 3 < irr.size(); i += 4)
        irrLit += std::max(irr[i], std::max(irr[i + 1], irr[i + 2])) > 1e-6f ? 1u : 0u;
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

    const bool irrShaped = irr.size() == b.rgba.size();   // the integrate's readback is the view's own image
    size_t lit = 0, outside = 0, controlDiff = 0, indirect = 0, worstAt = 0, powered = 0;
    double sumRel = 0.0, worstRel = 0.0, worstAbs = 0.0, worstUlps = 0.0, sumP = 0.0, sumP2 = 0.0;
    // ONE fp16 ULP of a stored value (normal: 2^(floor(log2 v) - 10); subnormal: 2^-24)
    const auto ulpOf = [](double v) {
        if (v < 6.103515625e-5) return 5.9604645e-8;
        int e = 0;
        std::frexp(v, &e);   // v = m 2^e, m in [0.5, 1): floor(log2 v) = e - 1
        return std::ldexp(1.0, e - 1 - 10);
    };
    const double kFloor = 5.9604645e-6;   // the half store's smallest normal step
    for (size_t i = 0; i + 3 < b.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c)
            controlDiff += b.rgba[i + c] != b2.rgba[i + c] ? 1u : 0u;
        double pixWorst = 0.0, pixUlps = 0.0;
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
            pixUlps = std::max(pixUlps, d / ulpOf(big));
            worstAbs = std::max(worstAbs, d);
        }
        if (!anyLit) continue;
        ++lit;
        indirect += anyIndirect ? 1u : 0u;
        sumRel += pixWorst;
        if (pixUlps > worstUlps) worstAt = i;
        worstRel = std::max(worstRel, pixWorst);
        worstUlps = std::max(worstUlps, pixUlps);
        outside += pixUlps > 2.0 ? 1u : 0u;
        // (3)'s power: the lit pixels whose fallback is nonzero, and their spread in the old quanta
        if (irrShaped && std::max(irr[i], std::max(irr[i + 1], irr[i + 2])) > 1e-6f) {
            ++powered;
            sumP += pixWorst;
            sumP2 += pixWorst * pixWorst;
        }
    }
    const double meanP = powered ? sumP / double(powered) : 0.0;
    const double sdP = powered > 1u ? std::sqrt(std::max(0.0, (sumP2 - double(powered) * meanP * meanP) /
                                                                   double(powered - 1u)))
                                    : 0.0;
    std::printf("   at rest in %d (B) / %d (A) / %d (B again) frames | %zu lit pixels (%zu over the GI-off picture) | the "
                "integrate's composed E/pi nonzero at %zu of %zu texels | A against B: worst %.2f ulps of the stored "
                "value (%.3e absolute), %zu outside two; in the old quanta (2^-10 of the larger) mean %.3f, worst %.2f | "
                "B against B: %zu channels differ\n",
                restB, restA, restB2, lit, indirect, irrLit, irrTexels, worstUlps, worstAbs, outside,
                lit ? sumRel / double(lit) : 0.0, worstRel, controlDiff);
    std::printf("   (3)'s power: %zu lit pixels with a nonzero fallback; their per-pixel spread %.3f quanta, the mean's "
                "standard error %.4f (a 0.05-quantum bias at %.1f sigma)\n",
                powered, sdP, powered ? sdP / std::sqrt(double(powered)) : 0.0,
                powered && sdP > 0.0 ? 0.05 / (sdP / std::sqrt(double(powered))) : 0.0);
    std::printf("   the worst pixel (%zu, %zu): A %.6g %.6g %.6g | B %.6g %.6g %.6g | GI off %.6g %.6g %.6g\n",
                (worstAt / 4) % size_t(b.width), (worstAt / 4) / size_t(b.width), a.rgba[worstAt], a.rgba[worstAt + 1],
                a.rgba[worstAt + 2], b.rgba[worstAt], b.rgba[worstAt + 1], b.rgba[worstAt + 2], off.rgba[worstAt],
                off.rgba[worstAt + 1], off.rgba[worstAt + 2]);
    CHECK(controlDiff == 0u, "(1) THE CONTROL: the integrate's route twice, every pixel identical");
    CHECK(lit > 0u && outside == 0u,
          "(2) THE PRODUCER MOVED, THE PHYSICS DID NOT: every lit channel of the pass's route within two ulps of the "
          "stored value (derived: three half-ulp roundings)");
    CHECK(irrShaped && powered >= 1000u,
          "(3) THE COMPARISON HAS THE POWER: at least 1,000 lit pixels whose fallback is nonzero (the integrate's own "
          "readback)");
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
