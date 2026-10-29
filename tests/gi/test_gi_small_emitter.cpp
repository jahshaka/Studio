// gi.small_emitter (PHOTON-7 stage 10-3; L9 is physics: the renderer follows the ray) - A SMALL OBJECT IS GEOMETRY AT
// EVERY CASCADE. A voxel ray reads cascade j over its shell (c0 to 0.625 m, c1 to 1.875 m, c2 to 5.6 m, c3 beyond), and
// the cascades' gather declined every instance under half its cell (RULE 2: 0.234 m at High's c2, 0.94 m at c3), so a
// ray past 1.9 m passed through a lamp's bulb, a post or a shade: the 0.20 m emitter of the K-scene court was in no
// reflection and its c2 store read 0 (spikes/photon-7/stage10/10-3/r2/emitters). Rule 2 is deleted: a small object
// enters every cascade by its coverage.
// THE FIXTURE: an open ground (no wall near the emitter: its coarse cells hold no other face), a smooth metal mirror
// (roughness 0.05: the K = 1 mirror ray) facing +z, the camera 0.6 m in front of it looking straight in, and a 0.20 m
// cube emitting E = 20 behind the camera on the mirror's axis - two arms: NEAR, 1.2 m from the mirror (the mirror ray
// reads it in c1, whose 0.156 m cells are finer than the cube), and FAR, 3.0 m from it (read in c2, 0.469 m cells).
// The no-ray tier as it ships (High, the field on: at roughness 0.05 the rays take the whole lobe). Rest is counted in
// frames.
// THE LAWS (the far arm):
//   (1) THE STORE HOLDS THE EMITTER AT EVERY CASCADE, BY ITS COVERAGE: cascades 0-2's emissive volumes peak at E within
//       the half-float store's step (2^-10 of E) and the merge's (2^-18) - no other face shares the cube's cells there -
//       and cascade 3's holds it (its 1.875 m cell shares the ground's faces: the coverage-weighted mean, in (0, E]);
//   (2) THE MIRROR SHOWS IT: the far picture's brightest pixel equals the near arm's within one 8-bit code of it (the
//       emitter's face is the same radiance at any distance, and its reflection that radiance times the metal's).
// RED before the deletion: the far arm's c2 and c3 stores 0, its picture's peak 0.43 against the near arm's 18.1.
// NOT LAWS HERE, printed (the coarse cell's two findings, measured on this fixture: PHOTON-7 10-3 F2): the image's
// CENTRE in the far arm reads 0 - the cube's +z and -z faces share one c2 cell, whose mixed plane is no front to the
// ray, which escapes; and the image's FLUX - a hit reads a cell's un-premultiplied radiance over the whole cell, so a
// small emitter's image is the cell's size (far / near flux 1.35 measured, (1.8 / 3.6)^2 = 0.25 exact).
// A fixture (the engine alone, one 128 x 128 offscreen view; 10 s budget).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
const float kE = 20.0f;

NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, const PbrParams &p)
{
    const NodeId n = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    s->attachMesh(n, s->createMesh(d), s->createPbrMaterial(p));
    enginetest::poseRegistry()[s][n] = enginetest::NodePose{};
    enginetest::setNodePosition(s, n, at);
    enginetest::setNodeScale(s, n, size);
    return n;
}
}   // namespace

int main(int, char **)
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-small-emitter-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    const float kMirrorZ = 0.0f, kCamZ = 0.6f, kY = 1.0f;
    const float arms[2] = { 1.2f, 3.0f };   // the emitter's face distance from the mirror: near (c1), far (c2)
    double centre[2] = { -1.0, -1.0 }, peaks[2] = { -1.0, -1.0 }, flux[2] = { 0.0, 0.0 };
    for (int k = 0; k < 2; ++k) {
        View *view = e->createOffscreenView("small_emitter", 128, 128, Colour(0, 0, 0), OffscreenContract::StillPicture);
        Scene *s = e->createScene("small_emitter");
        if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
        view->setScene(s);
        {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = 0;   // the camera stands between the mirror and the emitter: the screen holds no emitter to trace
            fx.hdrReadback = true;
            view->setPostFx(fx);
        }
        s->setRayTracing(RayTracingMode::Off);
        float sh[27] = {};
        s->setAmbientSh(sh);   // a black sky: the emitter is the only light
        PbrParams ground;
        ground.albedo = Colour(0.5f, 0.5f, 0.5f);
        ground.roughness = 0.9f;
        box(s, Vec3(0.0f, -0.2f, 0.0f), Vec3(20.0f, 0.4f, 20.0f), ground);
        PbrParams metal;
        metal.albedo = Colour(0.9f, 0.9f, 0.9f);
        metal.metalness = 1.0f;
        metal.roughness = 0.05f;
        box(s, Vec3(0.0f, kY, kMirrorZ - 0.02f), Vec3(1.6f, 1.6f, 0.04f), metal);
        PbrParams em;
        em.albedo = Colour(0.1f, 0.1f, 0.1f);
        em.emissive = Colour(kE, kE, kE);
        box(s, Vec3(0.0f, kY, kMirrorZ + arms[k] + 0.1f), Vec3(0.2f, 0.2f, 0.2f), em);
        enginetest::testCameraLookAt(view, Vec3(0.0f, kY, kCamZ), Vec3(0.0f, kY, kMirrorZ - 1.0f));
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::On;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: GI\n"); return 1; }
        e->waitForAsyncShaders();
        int f = 0, quiet = 0;
        for (; f < 600 && quiet < 8; ++f) {
            run(1);
            const GiStatus g = s->published().gi;
            quiet = (!g.cascades.empty() && !g.chainOwed && !g.feedbackOwed && !g.ifdWorking) ? quiet + 1 : 0;
        }
        run(4);
        if (k == 0) {   // MEASURING ARM (REDS-1 (b), reverted after its one run): the near cube's c1 cells through the voxel door
            GiVoxelVolume v, l;
            if (s->giVoxelVolume(1, v) && v.available && s->giVoxelLight(1, l) && l.available) {
                const float cell = v.cell[0];
                std::printf("   ARM c1 cell %.4f m; the cube x [-0.1, 0.1] y [0.9, 1.1] z [1.2, 1.4]\n", cell);
                for (int z = 0; z < v.depth; ++z) for (int y = 0; y < v.height; ++y) for (int x = 0; x < v.width; ++x) {
                    const float c0[3] = { v.origin[0] + cell * x, v.origin[1] + cell * y, v.origin[2] + cell * z };
                    if (c0[0] + cell < -0.1f || c0[0] > 0.1f || c0[1] + cell < 0.9f || c0[1] > 1.1f || c0[2] + cell < 1.2f || c0[2] > 1.4f) continue;
                    const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                    const float c = l.light[i + 3];
                    if (!(c > 0.0f)) continue;
                    std::printf("     cell [%.3f..%.3f, %.3f..%.3f, %.3f..%.3f] c %.3f covP (%.2f %.2f %.2f) covN (%.2f %.2f %.2f) n (%.2f %.2f %.2f) two %d "
                                "emissive %.3f emitP (%.3f %.3f %.3f) emitN (%.3f %.3f %.3f) albedo %.3f | front %.4f back %.4f (un-premultiplied)\n",
                                c0[0], c0[0] + cell, c0[1], c0[1] + cell, c0[2], c0[2] + cell, c,
                                v.coverageP[i], v.coverageP[i + 1], v.coverageP[i + 2], v.coverageN[i], v.coverageN[i + 1], v.coverageN[i + 2],
                                2 * l.normal[i] - 1, 2 * l.normal[i + 1] - 1, 2 * l.normal[i + 2] - 1, int(l.normal[i + 3] > 0.5f),
                                v.emissive.size() > i ? v.emissive[i + 1] : -1.0f,
                                v.emitP.size() > i ? v.emitP[i] : -1.0f, v.emitP.size() > i ? v.emitP[i + 1] : -1.0f, v.emitP.size() > i ? v.emitP[i + 2] : -1.0f,
                                v.emitN.size() > i ? v.emitN[i] : -1.0f, v.emitN.size() > i ? v.emitN[i + 1] : -1.0f, v.emitN.size() > i ? v.emitN[i + 2] : -1.0f,
                                v.albedo[i + 1], l.lightFront[i + 1] / c, l.lightBack[i + 1] / c);
                }
            }
        }
        ImageF pic;
        double sum = 0.0, peak = 0.0, bright = 0.0;
        unsigned n = 0, over = 0;
        if (view->readPixelsHdr(pic) && pic.width >= 64u) {
            const unsigned cx = pic.width / 2u, cy = pic.height / 2u;
            for (unsigned y = cy - 2u; y <= cy + 2u; ++y)
                for (unsigned x = cx - 2u; x <= cx + 2u; ++x) { sum += pic.at(x, y).g; ++n; }
            for (unsigned y = 0; y < pic.height; ++y)
                for (unsigned x = 0; x < pic.width; ++x) {
                    const double v = pic.at(x, y).g;
                    peak = std::max(peak, v);
                    if (v > 0.25 * kE) { ++over; bright += v; }
                }
        }
        centre[k] = n ? sum / double(n) : -1.0;
        peaks[k] = peak;
        flux[k] = bright;
        std::printf("   %s (the emitter %.1f m from the mirror): at rest after %d frames; the image's centre %.4f, the "
                    "picture's peak %.4f, %u px over E/4 (their sum %.1f)\n", k ? "FAR" : "NEAR", arms[k],
                    quiet >= 8 ? f - quiet : -1, centre[k], peak, over, bright);
        if (k == 1) {
            bool storeOk = quiet >= 8;
            for (int c = 0; c < 4; ++c) {
                const GiVoxelStats vs = s->giVoxelStats(c);
                std::printf("   FAR: cascade %d's emissive store (%s) peaks at %.4f (%lld above 1)\n", c,
                            vs.emissiveFormat.c_str(), double(vs.peakEmissive), vs.emissiveAboveOne);
                storeOk = storeOk && (c < 3 ? std::fabs(double(vs.peakEmissive) - kE) <= kE * (1.0 / 1024.0 + 1.0 / 262144.0)
                                            : vs.peakEmissive > 0.0f && vs.peakEmissive <= kE);
            }
            CHECK(storeOk, "(1) the far arm's cascades 0-2 hold the 0.20 m emitter's E within the store's step, and "
                           "cascade 3 holds it by its coverage");
        }
        GiParams off;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        run(2);
        e->destroyScene(s);
        e->destroyView(view);
        run(2);
    }
    {
        char msg[200];
        std::printf("   (not laws: F2) the image's centre far %.4f / near %.4f; its flux far / near %.3f (exact 0.25)\n",
                    centre[1], centre[0], flux[0] > 0.0 ? flux[1] / flux[0] : -1.0);
        std::snprintf(msg, sizeof(msg), "(2) the mirror shows the emitter far as near: the far picture's peak %.4f against "
                                        "the near's %.4f within one code of it", peaks[1], peaks[0]);
        CHECK(peaks[0] > 0.0 && std::fabs(peaks[1] - peaks[0]) <= peaks[0] / 255.0, msg);
    }
    std::printf("\ngi.small_emitter: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
