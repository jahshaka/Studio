// gi.card_walk_cost — THE CARD WALK AT 10k SLOTS, MEASURED (ENGINE-UPGRADE-U3 stage 3 fix round, decision 36).
// PhotonComponent::cardMoverFrame re-walks the card casters' records when the slot count changes (an arrival or a
// departure) — every slot's node looked up, every record matched BY NODE. A `measure` row: 10,000 carded cubes under
// the card row, then twenty arrivals one frame apart, each a full walk at 10k slots; the walk's own time
// (inspect::SceneEcho::cardWalk) is printed — median and min — and the row's only law is that the walks ran at the
// scale asked. Skips cleanly where the device has no ray tier (the cards need it).
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

int main()
{
    std::printf("== gi.card_walk_cost: the card casters' walk at 10k slots\n");
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-card-walk-ogre.log"), err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("cardwalk", 128u, 128u, Colour(0, 0, 0));
    Scene *s = e->createScene("cardwalk");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray-query device here — the cards need the ray tier; the row skips cleanly\n");
        return 0;
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
    PbrParams p;
    p.albedo = Colour(0.6f, 0.5f, 0.4f);
    const MaterialId mat = s->createPbrMaterial(p);
    MeshData md = enginetest::unitCubeMesh();
    md.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(md);
    const int kSide = 100;   // 100 x 100 = 10,000
    for (int i = 0; i < kSide * kSide; ++i) {
        const NodeId n = s->createNode();
        s->attachMesh(n, mesh, mat);
        enginetest::setNodeScale(s, n, Vec3(0.2f, 0.2f, 0.2f));
        enginetest::setNodePosition(s, n, Vec3(float(i % kSide) * 0.3f - 15.0f, 0.1f, float(i / kSide) * 0.3f - 15.0f));
    }
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 16.0f, 128, 0.0f };
    gi.cards = true;
    CHECK(s->setGlobalIllumination(gi), "GI builds with the card row on");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 8.0f, -20.0f), Vec3(0.0f, 0.0f, 0.0f));
    enginetest::frames(e, 8);
    const inspect::SceneEcho *echo = inspect::scene(s);
    CHECK(echo != nullptr, "the scene's probes");
    if (!echo) return 1;
    const unsigned long long walks0 = echo->cardWalk().walks;
    std::vector<double> us;
    unsigned slots = 0;
    for (int k = 0; k < 20; ++k) {
        const NodeId n = s->createNode();   // an arrival: the slot count moves, the records are re-walked
        s->attachMesh(n, mesh, mat);
        enginetest::setNodePosition(s, n, Vec3(float(k) * 0.3f, 2.0f, 16.0f));
        enginetest::frames(e, 1);
        const inspect::SceneEcho::CardWalk w = echo->cardWalk();
        us.push_back(w.micros);
        slots = w.slotCount;
    }
    const unsigned long long walks = echo->cardWalk().walks - walks0;
    std::sort(us.begin(), us.end());
    std::printf("    card walk at %u slots: median %.1f us, min %.1f us, max %.1f us (%llu walks over 20 arrivals)\n",
                slots, us[us.size() / 2], us.front(), us.back(), walks);
    CHECK(walks >= 20 && slots >= 10000u, "every arrival re-walked the records at 10k slots (%llu walks, %u slots)",
          walks, slots);
    std::printf(failures ? "gi.card_walk_cost: FAILED\n" : "gi.card_walk_cost: PASS\n");
    return failures ? 1 : 0;
}
