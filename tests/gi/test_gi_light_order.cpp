// gi.light_order (PHOTON-7 stage 1's read F3 and F4) — THE SCENE'S LIGHT ORDER IS THE WORLD'S, AND ITS SLOTS ARE COUNTED.
//
//   TIES (F3): the scene's one light list orders lights by peak intensity x range^2 and breaks an exact tie BY THE WORLD
//     (position, type, peak, range, colour, direction, then the document's id) - never by Ogre's movable id, which is
//     creation order. Twenty lamps of one key, more than a 128^3 voxel's 16 lights, so the tie decides WHICH sixteen a
//     voxel takes: created in one order, then removed and created in the reverse order, cascade 0's direct term is the
//     same, bit for bit. RED ON THE BASE (the id tie): the reverse creation picks another sixteen.
//   SLOTS (F4): a box holds 128 light slots, and a 64^3 voxel's budget IS the slots - past 128 lights in range of a box
//     a coarse voxel takes the box's first 128, not its own first K (L1 broken there). THE BOX OVER ITS SLOTS IS
//     COUNTED (CascadeStatus::lightsInRangeMax, injectionsOverSlots): 132 lamps in range of both boxes count it on
//     each cascade; the twenty of TIES count nothing.
// A fixture (the engine alone, one offscreen view, a two-cascade chain: 128 cells at 5 m, 64 at 20 m).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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
    const auto cfg = enginetest::testEngineConfig("test-gi-light-order-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("order", 64, 64, Colour(0, 0, 0), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("order");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    const NodeId floor = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, floor, Vec3(30.0f, 0.1f, 30.0f));
    const NodeId box = enginetest::addTestCube(scene, Colour(0.3f, 0.6f, 0.9f), 0.0f, 0.7f);
    enginetest::setNodePosition(scene, box, Vec3(0.6f, 0.6f, -0.8f));
    enginetest::setNodeScale(scene, box, Vec3(1.2f, 1.2f, 1.2f));
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 2.0f), Vec3(0.0f, 0.8f, -1.0f));

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    gi.bounces = true;
    gi.cascadeCount = 2;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 5.0f, 128, 0.0f };
    gi.cascadeSet[1] = GiParams::GiCascadeDesc{ 20.0f, 64, 0.0f };

    // TWENTY LAMPS OF ONE KEY (the same intensity and range): a ring around the scene's middle, each reaching every voxel
    // of cascade 0's box (range 12 m) - so the per-voxel sixteen is the order's tie.
    const int kLamps = 20;
    const auto lampPos = [](int k) {
        const float a = 6.2831853f * float(k) / float(kLamps);
        return Vec3(2.2f * std::cos(a), 1.2f + 0.05f * float(k % 3), 2.2f * std::sin(a));
    };
    const auto addLamps = [&](bool reverse, std::vector<NodeId> &out) {
        out.clear();
        for (int i = 0; i < kLamps; ++i) {
            const int k = reverse ? kLamps - 1 - i : i;
            const NodeId n = scene->createNode();
            enginetest::setNodePosition(scene, n, lampPos(k));
            LightDesc l;
            l.type = LightType::Point;
            l.colour = Colour(1.0f, 0.9f, 0.8f);
            l.intensity = 0.4f;
            l.range = 12.0f;
            scene->setLight(n, l);
            out.push_back(n);
        }
    };
    const auto build = [&](GiVoxelVolume &v, GiStatus &st) {
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
        const bool ok = scene->setGlobalIllumination(gi);
        run(10);
        st = scene->published().gi;
        return ok && scene->giVoxelVolume(0, v);
    };

    std::printf("== TIES: twenty lamps of one key, created in two orders ==\n");
    {
        std::vector<NodeId> lamps;
        addLamps(false, lamps);
        GiVoxelVolume a, b;
        GiStatus sa, sb;
        CHECK(build(a, sa), "the chain builds with the lamps created in one order");
        for (NodeId n : lamps) scene->removeNode(n);
        run(2);
        addLamps(true, lamps);
        CHECK(build(b, sb), "...and with them removed and created in the reverse order");
        size_t diff = 0, lit = 0;
        for (size_t i = 0; i < a.direct.size() && i < b.direct.size(); ++i) {
            diff += std::memcmp(&a.direct[i], &b.direct[i], 4) != 0 ? 1u : 0u;
            lit += a.direct[i] != 0.0f ? 1u : 0u;
        }
        std::printf("   cascade 0: %d lights in range, %d a voxel takes; the direct term differs in %zu of %zu floats (%zu "
                    "non-zero)\n", sa.cascades[0].lightsInRange, sa.cascades[0].lightCapacity, diff, a.direct.size(), lit);
        CHECK(sa.cascades[0].lightsInRange > sa.cascades[0].lightCapacity && sa.cascades[0].lightCapacity > 0,
              "more lamps reach the voxels than a voxel takes (the tie decides which)");
        CHECK(!a.direct.empty() && a.direct.size() == b.direct.size() && lit > 0 && diff == 0,
              "THE TIE IS THE WORLD'S: the direct term is the same, bit for bit, whatever the creation order");
        CHECK(sb.cascades[0].injectionsOverSlots == 0u && sb.cascades[1].injectionsOverSlots == 0u &&
                  sb.cascades[1].lightsInRangeMax == kLamps,
              "twenty lamps: no box over its slots (the most in range: twenty)");
        for (NodeId n : lamps) scene->removeNode(n);
        run(2);
    }

    std::printf("\n== SLOTS: 132 lamps in range of both boxes ==\n");
    {
        std::vector<NodeId> many;
        for (int k = 0; k < 132; ++k) {
            const NodeId n = scene->createNode();
            const float a = 6.2831853f * float(k) / 132.0f;
            enginetest::setNodePosition(scene, n, Vec3(3.0f * std::cos(a), 1.0f + 0.01f * float(k), 3.0f * std::sin(a)));
            LightDesc l;
            l.type = LightType::Point;
            l.intensity = 0.02f;
            l.range = 30.0f;
            scene->setLight(n, l);
            many.push_back(n);
        }
        GiVoxelVolume v;
        GiStatus st;
        CHECK(build(v, st), "the chain builds with 132 lamps");
        for (size_t i = 0; i < st.cascades.size(); ++i)
            std::printf("   cascade %zu: %d in range now, the most %d; %llu injection(s) over the 128 slots; a voxel takes %d\n",
                        i, st.cascades[i].lightsInRange, st.cascades[i].lightsInRangeMax,
                        st.cascades[i].injectionsOverSlots, st.cascades[i].lightCapacity);
        CHECK(st.cascades.size() == 2u && st.cascades[0].lightsInRangeMax == 132 && st.cascades[1].lightsInRangeMax == 132 &&
                  st.cascades[0].injectionsOverSlots > 0u && st.cascades[1].injectionsOverSlots > 0u,
              "THE BOX OVER ITS LIGHT SLOTS IS COUNTED: 132 in range of each box, every cascade's injections counted");
        for (NodeId n : many) scene->removeNode(n);
        run(2);
    }

    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
