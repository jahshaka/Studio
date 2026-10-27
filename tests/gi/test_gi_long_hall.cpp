// gi.long_hall (PHOTON-7 stage 10-3, R4; L9 is physics: "ran out of distance" is not a miss) - A SEALED HALL LONGER
// THAN THE CASCADES' REACH IS DARK IN ITS SURFACE CACHE. The cards' gather traced every texel's rays to "the reach" -
// the outermost cascade's guaranteed radius - and read a ray that got that far as the SKY: in a closed building longer
// than that, the rays along its length lit the inside. A miss is an EXIT now: a ray runs to where it leaves the world's
// box (the traced instances' union, jah_world_exit.glsl), only there is it the sky, and a hit past every cascade reads
// its card or is no sample.
// THE FIXTURE: a closed hall 10 x 5 x 240 m inside (0.4 m slabs, albedo 0.6) under a uniform white sky of radiance 1,
// no light inside; the camera inside near one end looking down its length. The ray tier at High (the cards on, the
// field on), run to its cards' rest.
// THE LAW: (1) no resident card texel of the hall's six inner faces holds one 8-bit code of R (R = the albedo x the
// sky: what a sky-lit face of this albedo reads). The exact answer is 0: no path from the sky reaches the inside.
// RED before: 13.2 codes of R, 366,970 texels of the 80 inner-face cards holding light - their rays along the hall's
// length stopped at the reach and read the sky.
// "noray" (gi.long_hall_noray; R4 (a)): a sealed HANGAR 300 m on every side (1 m slabs), wider than the whole voxel
// chain in every direction, at the no-ray tier; the camera inside looking at the floor. (2) No pixel reads one code of
// R. RED before: 215.8 codes, the mean 0.455 - every voxel ray that left the chain inside the world read the sky; an
// escape is the sky only past the world's exit now (jah_voxel_ray.glsl's jahEscapeIsExit).
// A fixture (the engine alone, one 128 x 128 offscreen view; 10 s budget). Counted in frames.
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

namespace {
const float kAlbedo = 0.6f;

NodeId box(Scene *s, const Vec3 &at, const Vec3 &size)
{
    PbrParams p;
    p.albedo = Colour(kAlbedo, kAlbedo, kAlbedo);
    p.roughness = 0.9f;
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

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "ray";
    if (mode != "ray" && mode != "noray") { std::printf("FAIL: the mode is ray or noray, not '%s'\n", mode.c_str()); return 2; }
    const bool noray = mode == "noray";
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-long-hall-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(mode == "ray");
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("long_hall", 128, 128, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *s = e->createScene("long_hall");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    if (mode == "noray") s->setRayTracing(RayTracingMode::Off);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    float sh[27] = { 0 };
    sh[0] = sh[1] = sh[2] = 1.0f;   // a uniform white sky of radiance 1
    s->setAmbientSh(sh);
    // ray: a hall 10 x 5 x 240 m (longer than the cards' reach); noray: a HANGAR 300 m on every side, 1 m slabs - wider
    // than the whole chain (High's outermost box is 120 m) in every direction, so the voxel rays leave the chain inside
    // the world everywhere
    const float hx = noray ? 150.0f : 5.0f, hz = noray ? 150.0f : 120.0f, hy = noray ? 300.0f : 5.0f,
                t = noray ? 1.0f : 0.4f;
    // the six slabs, every joint overlapped; each one's INNER face by its card bucket (2 x axis + negative)
    struct Face { NodeId node; unsigned bucket; const char *name; };
    std::vector<Face> inner;
    const NodeId floorN = box(s, Vec3(0.0f, -0.5f * t, 0.0f), Vec3(2.0f * hx + 2.0f * t, t, 2.0f * hz + 2.0f * t));
    const NodeId roofN = box(s, Vec3(0.0f, hy + 0.5f * t, 0.0f), Vec3(2.0f * hx + 2.0f * t, t, 2.0f * hz + 2.0f * t));
    const NodeId wallL = box(s, Vec3(-hx - 0.5f * t, 0.5f * hy, 0.0f), Vec3(t, hy + 2.0f * t, 2.0f * hz + 2.0f * t));
    const NodeId wallR = box(s, Vec3(hx + 0.5f * t, 0.5f * hy, 0.0f), Vec3(t, hy + 2.0f * t, 2.0f * hz + 2.0f * t));
    const NodeId endFar = box(s, Vec3(0.0f, 0.5f * hy, -hz - 0.5f * t), Vec3(2.0f * hx + 2.0f * t, hy + 2.0f * t, t));
    const NodeId endNear = box(s, Vec3(0.0f, 0.5f * hy, hz + 0.5f * t), Vec3(2.0f * hx + 2.0f * t, hy + 2.0f * t, t));
    inner = { { floorN, 2u, "the floor" }, { roofN, 3u, "the roof" }, { wallL, 0u, "the left wall" },
              { wallR, 1u, "the right wall" }, { endFar, 4u, "the far end" }, { endNear, 5u, "the near end" } };
    // ray: inside near one end looking down the length (the cards of the whole hall resident); noray: 40 m from the
    // near end looking AT it - every surface in view inside the chain (a start the chain does not hold keeps the sky at
    // the no-ray tier: its estimator limit, R4 (b)), its rays down the hall's length leaving the chain long before the
    // world
    if (mode == "noray")
        enginetest::testCameraLookAt(view, Vec3(0.0f, 6.0f, 0.0f), Vec3(0.0f, 0.0f, -8.0f));
    else
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, hz - 10.0f), Vec3(0.0f, 2.0f, -hz));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::On;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: GI\n"); return 1; }
    int rest = -1;
    unsigned long long relights = ~0ull, admitted = ~0ull;
    for (int f = 0, quiet = 0; f < 500; ++f) {
        run(1);
        const GiStatus g = s->published().gi;
        const bool cardsStill = g.cards.relights == relights && g.cards.admitted == admitted && g.cards.queueLength == 0u;
        relights = g.cards.relights;
        admitted = g.cards.admitted;
        quiet = (!g.cascades.empty() && !g.chainOwed && !g.feedbackOwed && !g.ifdWorking && cardsStill) ? quiet + 1 : 0;
        if (quiet >= 30) { rest = f - quiet; break; }
    }
    if (mode == "noray") {
        // THE NO-RAY TIER (R4 (a)): the picture inside at rest - the voxel rays' escapes along the hall
        ImageF pic;
        double peak = 0.0, sum = 0.0;
        size_t n = 0;
        if (view->readPixelsHdr(pic))
            for (unsigned y = 0; y < pic.height; ++y)
                for (unsigned x = 0; x < pic.width; ++x) {
                    const Colour c = pic.at(x, y);
                    const double v = std::max(c.r, std::max(c.g, c.b));
                    peak = std::max(peak, v);
                    sum += v;
                    ++n;
                }
        const double R = double(kAlbedo), bar = R / 255.0;
        std::printf("   noray: at rest after %d frames; the picture's brightest %.6f (%.3f codes of R), mean %.6f\n", rest,
                    peak, peak / bar, n ? sum / double(n) : 0.0);
        CHECK(rest >= 0, "the hall comes to rest");
        CHECK(peak <= bar, "(2) at the no-ray tier no pixel inside the sealed hall reads one code of R");
        std::printf("\ngi.long_hall (noray): %d frames, %d failure(s)\n", frames, failures);
        return failures ? 1 : 0;
    }
    std::vector<CardRadiance> all;
    unsigned long long cacheFrame = 0ull;
    const bool read = s->readCardRadiance(all, cacheFrame);
    const double R = double(kAlbedo), bar = R / 255.0;
    double worst = 0.0;
    const char *worstAt = "-";
    unsigned cards = 0, texels = 0;
    for (const CardRadiance &c : all) {
        if (c.lastRelit == 0ull) continue;
        double peak = 0.0;
        size_t lit = 0;
        for (size_t i = 0; i + 2 < c.rgb.size(); i += 3) {
            const double v = std::max(c.rgb[i], std::max(c.rgb[i + 1], c.rgb[i + 2]));
            if (v > 0.0) ++lit;
            peak = std::max(peak, v);
        }
        for (const Face &fc : inner)
            if (c.node == fc.node && c.bucket == fc.bucket) {
                ++cards;
                texels += unsigned(lit);
                if (peak > worst) { worst = peak; worstAt = fc.name; }
            }
    }
    std::printf("   at rest after %d frames; %u resident inner-face cards (%u texels holding light); the brightest %.6f "
                "(%.3f codes of R) on %s\n", rest, cards, texels, worst, worst / bar, worstAt);
    CHECK(rest >= 0 && read && cards > 0u, "premise: at rest, the inner faces' cards resident");
    CHECK(worst <= bar, "(1) no inner-face card texel of the sealed hall holds one code of R (the sky enters only at an "
                        "exit from the world)");
    GiParams off;
    off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    run(2);
    view->setScene(nullptr);
    e->destroyView(view);
    e->destroyScene(s);
    run(2);
    std::printf("\ngi.long_hall: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
