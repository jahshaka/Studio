// gi.sealed_hall / gi.sealed_hall_noray (PHOTON-7 stage 10-3, R3; L9 is physics: a sky read with no visibility is light
// through a wall, and a light with no visibility gives nothing) - A SEALED HALL WIDER THAN THE FIELD'S WINDOW IS DARK
// INSIDE. A closed hall (24 x 5 x 24 m inside, 0.4 m slabs, albedo 0.6, pillars and blocks for the screen's
// silhouettes) under a uniform white sky of radiance 1, with an area light (4 x 4 m, intensity 20) over its roof shining
// down on it; the camera inside, looking down the hall's length (its far wall 22 m off: past c0's 10 m box, the
// irradiance field's window). Nothing inside emits and nothing outside can reach in: the exact answer is 0 at every
// pixel. THE BAR: one 8-bit code of R (R = the albedo x the sky, what the roof's sky-lit top reads), the readback's
// quantum on that scale. THE TIERS, ONE ROW EACH (argv[1]):
//   "ray"   (gi.sealed_hall), the ray tier at High: (1) AT REST no pixel reads one code of R. RED before: the gather's
//           integrate fell back, for a pixel no probe answered, to the field's cage plus the ENVIRONMENT's diffuse x
//           (1 - confidence) - past the field's window the sky's irradiance, unoccluded (rq_probe_integrate.comp):
//           20.2 codes at rest (the sky alone), 203 with the area light. The share is gathered by the chain's rays
//           now (R3 (b)). DECLARED RED until 10-4's R7 reaches photon-7: 18.9 codes over 9 px at rest - those rays
//           crossing the hall's coarse mixed cells at 22 m (the class of gi.sealed_sky (3)).
//   "noray" (gi.sealed_hall_noray), the no-ray tier at High: (1) NO PIXEL INSIDE READS THE AREA LIGHT UNOBSTRUCTED
//           (its visibility product's diffuse share at 255) at rest. RED before: a pixel no stratum was seen for (U = 0)
//           read "unshadowed", and the light's LTC lit the inside through the roof - 1,038 pixels at 203 codes of R; a
//           light with no visibility gives nothing now (jah_area_vis_law.glsl's resolve), and before the chain exists
//           a stratum is no sample (JahScreenAreaVis_cs.glsl - this fixture's chain is built before its first frame).
// PRINTED, NOT LAWS (measured, the residues' owners named): the ray tier's FIRST FRAME (the area light drawn before its
// first visibility product: 1.74 over 25,484 px - the lead's ruling owed) and its first ~50 frames (the gather's hits
// on the voxel chain before the cards are resident, 17 codes decaying: the coarse cell, 10-4's R7); the no-ray tier at
// rest (the sky 2.1 codes, the area light 15 codes through the roof's coarse cells: the same, 10-4's R7).
// A fixture (the engine alone, one 192 x 192 offscreen view; 10 s budget). Counted in frames.
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
const float kAlbedo = 0.6f;

void box(Scene *s, const Vec3 &at, const Vec3 &size)
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
}
}   // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "ray";
    if (mode != "ray" && mode != "noray") { std::printf("FAIL: the mode is ray or noray, not '%s'\n", mode.c_str()); return 2; }
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-sealed-hall-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(mode == "ray");
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("sealed_hall", 192, 192, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *s = e->createScene("sealed_hall");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    if (mode == "noray") s->setRayTracing(RayTracingMode::Off);
    float sh[27] = { 0 };
    sh[0] = sh[1] = sh[2] = 1.0f;   // a uniform white sky of radiance 1
    s->setAmbientSh(sh);
    // the hall: floor, roof, four walls (0.4 m), every joint overlapped
    const float hx = 12.0f, hz = 12.0f, hy = 5.0f, t = 0.4f;
    box(s, Vec3(0.0f, -0.5f * t, 0.0f), Vec3(2.0f * hx + 2.0f * t, t, 2.0f * hz + 2.0f * t));
    box(s, Vec3(0.0f, hy + 0.5f * t, 0.0f), Vec3(2.0f * hx + 2.0f * t, t, 2.0f * hz + 2.0f * t));
    box(s, Vec3(-hx - 0.5f * t, 0.5f * hy, 0.0f), Vec3(t, hy + 2.0f * t, 2.0f * hz + 2.0f * t));
    box(s, Vec3(hx + 0.5f * t, 0.5f * hy, 0.0f), Vec3(t, hy + 2.0f * t, 2.0f * hz + 2.0f * t));
    box(s, Vec3(0.0f, 0.5f * hy, -hz - 0.5f * t), Vec3(2.0f * hx + 2.0f * t, hy + 2.0f * t, t));
    box(s, Vec3(0.0f, 0.5f * hy, hz + 0.5f * t), Vec3(2.0f * hx + 2.0f * t, hy + 2.0f * t, t));
    // pillars and blocks: the screen's silhouettes, where a probe's plane weights fail
    for (const float x : { -4.0f, 4.0f })
        for (const float z : { -8.0f, -2.0f, 4.0f }) box(s, Vec3(x, 0.5f * hy, z), Vec3(0.6f, hy, 0.6f));
    box(s, Vec3(1.5f, 0.5f, -5.0f), Vec3(1.0f, 1.0f, 1.0f));
    box(s, Vec3(-2.0f, 0.75f, -10.0f), Vec3(1.5f, 1.5f, 1.5f));
    // the area light over the roof, facing down onto it
    {
        const NodeId lamp = s->createNode();
        LightDesc l;
        l.type = LightType::Area;
        l.rectWidth = 4.0f;
        l.rectHeight = 4.0f;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 20.0f;
        l.range = 40.0f;
        s->setNodeTransform(lamp, Vec3(0.0f, hy + t + 1.0f, -4.0f), Quat(), Vec3(1, 1, 1));
        s->setLight(lamp, l);
    }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 10.0f), Vec3(0.0f, 1.2f, -12.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::On;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: GI\n"); return 1; }
    if (mode == "noray") e->setArm("area.visReadback", 1.0);
    const double R = double(kAlbedo), bar = R / 255.0;
    // EVERY FRAME from the build to rest (shader compiles included: a frame drawn while a permutation builds is a frame
    // the user sees): the picture's brightest pixel and the count over the bar
    double worst = 0.0, last = 0.0;
    int worstFrame = -1, rest = -1;
    unsigned worstOver = 0, lastOver = 0;
    unsigned long long relights = ~0ull, admitted = ~0ull;
    for (int f = 0, quiet = 0; f < 420; ++f) {
        run(1);
        ImageF pic;
        double peak = 0.0;
        unsigned over = 0;
        if (view->readPixelsHdr(pic))
            for (unsigned y = 0; y < pic.height; ++y)
                for (unsigned x = 0; x < pic.width; ++x) {
                    const Colour c = pic.at(x, y);
                    const double v = std::max(c.r, std::max(c.g, c.b));
                    peak = std::max(peak, v);
                    if (v > bar) ++over;
                }
        if (peak > worst) { worst = peak; worstFrame = f; worstOver = over; }
        last = peak;
        lastOver = over;
        const GiStatus g = s->published().gi;
        const bool cardsStill = g.cards.relights == relights && g.cards.admitted == admitted && g.cards.queueLength == 0u;
        relights = g.cards.relights;
        admitted = g.cards.admitted;
        const bool areaHeld = mode == "ray" || s->published().areaVis.held;
        quiet = (!g.cascades.empty() && !g.chainOwed && !g.feedbackOwed && !g.ifdWorking && cardsStill && areaHeld)
                    ? quiet + 1 : 0;
        if (quiet >= 12) { rest = f - quiet; break; }
    }
    std::printf("   %s: at rest after %d frames; the brightest pixel of any frame %.6f (%.3f codes of R) at frame %d, %u px "
                "over one code; at rest %.6f (%.3f codes), %u px over\n", mode.c_str(), rest, worst, worst / bar,
                worstFrame, worstOver, last, last / bar, lastOver);
    CHECK(rest >= 0, "the hall comes to rest");
    if (mode == "ray") {
        if (last > bar)
            std::printf("   ray: the residue at rest - %u px over one code, the brightest %.3f codes: the chain's rays "
                        "through the coarse mixed cells (10-4's R7)\n", lastOver, last / bar);
        CHECK(last <= bar, "(1) at rest no pixel inside the sealed hall reads one code of R");
    } else {
        const AreaVisStatus a = s->published().areaVis;
        unsigned open = 0, read = 0;
        for (size_t i = 0; i + 7 < a.vis.size(); i += 8u) {
            ++read;
            if (a.vis[i] >= 255u) ++open;   // light 0's diffuse share
        }
        std::printf("   noray: the area light's visibility at rest - %u of %u pixels read unobstructed\n", open, read);
        CHECK(read > 0u && open == 0u, "(1) no pixel inside the sealed hall reads the area light unobstructed");
    }
    if (mode == "noray") e->setArm("area.visReadback", 0.0);
    GiParams off;
    off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    run(2);
    view->setScene(nullptr);
    e->destroyView(view);
    e->destroyScene(s);
    run(2);
    std::printf("\ngi.sealed_hall (%s): %d frames, %d failure(s)\n", mode.c_str(), frames, failures);
    return failures ? 1 : 0;
}
