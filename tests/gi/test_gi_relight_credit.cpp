// gi.relight_credit (stage 5's read B1; PHOTON-7 stage 4's hitch law, L7) - A LAMP'S RELIGHT AFTER A SHADER WAIT
// SPENDS ONE FRAME'S COUNT, NEVER A BANKED ROUND.
//
// THE MECHANISM (PhotonGi.cpp relightAndFeedback): an owed cascade's relight is paid from its FLOOR CREDIT (half its share
// of the frame's count, GiQualityFacts::lightWorkFrames) plus the frame's pool, in 4-plane slabs. The credit accrues only
// on a frame the cascade can pay and is capped at one slab of its owed box or one floor, the larger: a frame whose delta
// shader is still building (the background compiler's) or whose injection was refused banks nothing.
// THE FIXTURE: an 8 m room at Epic, a lamp of range 12 m at its centre, the field, the gather and the cards off. At rest,
// the test door gi.relightShaderHold holds every cascade's relight as if its delta shader were building; the lamp
// steps; 40 frames pass held; the door opens.
// THE LAWS:
//   (1) THE HELD FRAMES RELIGHT NOTHING (the door engaged): no voxel relit while it holds;
//   (2) NO FRAME RELIGHTS MORE THAN ITS COUNT PLUS THE CREDITS' CAPS: per frame after the door opens, the voxels the
//       spread relit (CascadeStatus::relightVoxels' delta) <= GiStatus::relightCount + the owed cascades' relightSlab -
//       RED with the credit banked over the wait (the first paying frame relights the whole chain);
//   (3) THE ROUND COMPLETES: every owed cascade's relight round ends (relightRounds advanced) within 300 frames.
// The frames' spends printed. A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
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
    const auto cfg = enginetest::testEngineConfig("test-gi-relight-credit-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("relight_credit", 128, 128, Colour(0, 0, 0));
    Scene *scene = e->createScene("relight_credit");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const Colour white(0.6f, 0.6f, 0.6f);
    const float side = 8.0f, wall = 0.2f, h = side * 0.5f + wall * 0.5f, span = side + 2.0f * wall;
    enginetest::leakroom::addSlab(scene, white, Vec3(0, -h + 2.0f, 0), Vec3(span, wall, span));
    enginetest::leakroom::addSlab(scene, white, Vec3(0, h + 2.0f, 0), Vec3(span, wall, span));
    enginetest::leakroom::addSlab(scene, white, Vec3(-h, 2.0f, 0), Vec3(wall, side, span));
    enginetest::leakroom::addSlab(scene, white, Vec3(h, 2.0f, 0), Vec3(wall, side, span));
    enginetest::leakroom::addSlab(scene, white, Vec3(0, 2.0f, -h), Vec3(side, side, wall));
    enginetest::leakroom::addSlab(scene, white, Vec3(0, 2.0f, h), Vec3(side, side, wall));
    const NodeId lamp = scene->createNode();
    scene->setNodeTransform(lamp, Vec3(0.0f, 2.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1.0f, 1.0f, 1.0f);
    l.intensity = 4.0f;
    l.range = 12.0f;
    l.castShadows = true;
    scene->setLight(lamp, l);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 2.0f, -1.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;
    gi.ddgi = GiToggle::Off;
    gi.cards = false;
    gi.bounces = true;
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    int quiet = 0, f0 = 0;
    for (; f0 < 1500 && quiet < 30; ++f0) {
        run(1);
        quiet = scene->published().gi.giAtRest ? quiet + 1 : 0;
    }
    const auto relit = [&](const GiStatus &g) {
        unsigned long long v = 0ull;
        for (const auto &c : g.cascades) v += c.relightVoxels;
        return v;
    };
    const GiStatus g0 = scene->published().gi;
    std::vector<unsigned long long> rounds0;
    for (const auto &c : g0.cascades) rounds0.push_back(c.relightRounds);
    // THE WAIT, THEN THE STEP
    e->setArm("gi.relightShaderHold", 1.0);
    l.intensity = 8.0f;
    scene->setLight(lamp, l);
    run(40);
    const GiStatus gHeld = scene->published().gi;
    const unsigned long long heldRelit = relit(gHeld) - relit(g0);
    e->setArm("gi.relightShaderHold", 0.0);
    std::vector<unsigned long long> prevC;
    for (const auto &c : gHeld.cascades) prevC.push_back(c.relightVoxels);
    std::vector<bool> relitC(prevC.size(), false);
    unsigned long long worst = 0ull, worstBound = 0ull, total = 0ull;
    int worstFrame = -1, over = 0, roundsDone = -1;
    std::printf("   at rest in %d frames; the door held 40 frames (relit %llu voxels meanwhile); then per frame (voxels relit "
                "| the count + the spending cascades' caps):\n", f0, heldRelit);
    for (int f = 1; f <= 300; ++f) {
        run(1);
        const GiStatus g = scene->published().gi;
        if (g.cascades.size() != prevC.size()) break;
        unsigned long long spent = 0ull, caps = 0ull;
        for (size_t c = 0; c < g.cascades.size(); ++c) {
            const unsigned long long d = g.cascades[c].relightVoxels - prevC[c];
            prevC[c] = g.cascades[c].relightVoxels;
            if (!d) continue;
            spent += d;
            caps += g.cascades[c].relightSlab;
            relitC[c] = true;
        }
        const unsigned long long bound = g.relightCount + caps;
        total += spent;
        if (spent && f <= 12) std::printf("     f%-3d %9llu | %9llu\n", f, spent, bound);
        if (spent > worst) { worst = spent; worstBound = bound; worstFrame = f; }
        if (spent > bound) ++over;
        bool done = total > 0ull;
        for (size_t c = 0; c < g.cascades.size() && done; ++c)
            done = !relitC[c] || g.cascades[c].relightRounds > rounds0[c];
        if (done && roundsDone < 0) roundsDone = f;
    }
    std::printf("   the largest frame f%d: %llu voxels relit against its bound %llu; %d frames over; %llu voxels relit in all; "
                "every owed round done at f%d\n", worstFrame, worst, worstBound, over, total, roundsDone);
    CHECK(heldRelit == 0ull, "(1) THE HELD FRAMES RELIGHT NOTHING (the door engaged)");
    CHECK(total > 0ull && over == 0, "(2) NO FRAME RELIGHTS MORE THAN THE COUNT PLUS THE CREDITS' CAPS");
    CHECK(roundsDone > 0, "(3) THE ROUND COMPLETES: every owed cascade's relight round ended");
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
