// gi.shell_ends (PHOTON-7 stage 1's read F1; SPECS/PHOTON_7_SPEC.md stage 1, THE VISIBILITY MARCH ON FIXED SHELLS) —
// EVERY CASCADE MARCHES THE WORLD CHAIN'S SHELLS.
//
// A voxel's visibility march runs shell j on cascade j's level-0 cells over the world distance [D_{j-1}, D_j), with
// D_j = the sum of 8 cells of every cascade of the chain up to j (Epic / High 0.625 / 1.875 / 5.625 m, Medium 1.25 /
// 3.75 / 7.5, Low 1.25). The ends are the CHAIN's numbers, the same from every cascade: L1 (light is a world
// function) asks that a c1 voxel and a c0 voxel hand their marches to c2 at the same distance. Read by the door
// GiStatus::CascadeStatus::shellEnds (the list the cascade's last injection marched).
//
// THE LAW, at every desktop tier: every cascade's list equals sum_{i <= j} 8 x cell_i over the chain, for j = 0 ..
// n - 2, within a float's rounding. RED ON THE BASE (the read's F1): an inner entry summed THIS cascade's cell, so c1
// read 2.5 / 6.25 m at Epic and c2 11.25.
//
// A fixture (the engine alone, one offscreen view, the four tiers built in turn), a few seconds.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
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
    const auto cfg = enginetest::testEngineConfig("test-gi-shells-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("shells", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("shells");
    view->setScene(scene);
    const NodeId floor = enginetest::addTestCube(scene, Colour(0.6f, 0.6f, 0.6f), 0.0f, 1.0f);
    enginetest::setNodePosition(scene, floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, floor, Vec3(40.0f, 0.1f, 40.0f));
    enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.4f), 1.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 0.0f), Vec3(0.0f, 1.0f, -3.0f));
    const struct { GiQuality q; const char *name; } tiers[] = {
        { GiQuality::Low, "Low" }, { GiQuality::Medium, "Medium" }, { GiQuality::High, "High" },
        { GiQuality::Epic, "Epic" } };
    bool allOk = true;
    int tiersRead = 0;
    for (const auto &t : tiers) {
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = t.q;
        gi.ddgi = GiToggle::Off;
        gi.gather = GiToggle::Off;
        gi.updateBudget = 0;
        gi.numBounces = 1;
        if (!scene->setGlobalIllumination(gi)) { std::printf("   %s: the chain did not build\n", t.name); allOk = false; continue; }
        enginetest::frames(e, 6);
        const GiStatus st = scene->published().gi;
        const size_t n = st.cascades.size();
        std::vector<double> want;
        double d = 0.0;
        for (size_t j = 0; j + 1 < n; ++j) {
            d += 8.0 * double(st.cascades[j].cell);
            want.push_back(d);
        }
        std::printf("   %s (%zu cascades): the chain's ends", t.name, n);
        for (double w : want) std::printf(" %.4f", w);
        std::printf(" m\n");
        bool tierOk = n >= 2u;
        for (size_t c = 0; c < n; ++c) {
            const std::vector<float> &got = st.cascades[c].shellEnds;
            bool same = got.size() == want.size();
            for (size_t j = 0; same && j < want.size(); ++j)
                same = std::fabs(double(got[j]) - want[j]) <= 1e-6 * want[j];
            std::printf("     cascade %zu (cell %.4f m) marched", c, double(st.cascades[c].cell));
            for (float g : got) std::printf(" %.4f", double(g));
            std::printf("%s\n", same ? "" : "  <- not the chain's");
            tierOk = tierOk && same;
        }
        allOk = allOk && tierOk;
        ++tiersRead;
    }
    CHECK(tiersRead == 4, "the four desktop tiers built their chains");
    CHECK(allOk, "EVERY CASCADE MARCHES THE WORLD CHAIN'S SHELLS: its list equals sum 8 x cell_i at every tier");
    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    enginetest::frames(e, 2);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
