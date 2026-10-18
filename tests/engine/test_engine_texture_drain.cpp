// engine.texture_drain_reset — A THROW INSIDE THE HOST'S TEXTURE WAIT LEAVES THE NEXT WAIT CLEAN (ENGINE-UPGRADE-U5
// decision 22.5). Engine::pumpTextureLoads keeps the bounded drain's state between the host's calls (one step a
// request, the host waiting between them, WAIT-TEXTURE-LOADS-1). A step that throws used to leave that state ACTIVE, so
// the host's next wait stepped the stale drain: its no-progress budget measured from the dead wait's start, expired at
// once, and latched mTextureWaitBroken for the rest of the process - every later frame rendered without waiting for its
// textures. The state is now reset on the way out of a throw.
// THE FAULT IS A DOOR (the arm textures.pumpFault): 1 = the host's drain never agrees the queues are empty (the frame's
// own drain is untouched), 2 = and its step throws. With a 300 ms budget (JAH_TEXTURE_WAIT_MS): a wait begun, its step
// thrown, 450 ms of host time, then a NEW wait - which must step as a fresh drain (still streaming, never timed out),
// and once the door is shut must finish, with no timeout counted anywhere and the frames after it waiting as ever.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

int main()
{
    std::printf("== engine.texture_drain_reset: a thrown step of the host's texture wait, then a clean wait\n");
    setenv("JAH_TEXTURE_WAIT_MS", "300", 1);
    EngineConfig cfg = enginetest::testEngineConfig("test-texture-drain-ogre.log");
    cfg.renderThread = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created on its render thread (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    // a view, so every frame below is a frame (the arms latch at a frame's top)
    View *view = e->createOffscreenView("drain", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("drain");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    enginetest::frames(e, 3);
    const auto pump = [&](bool &threw) {
        int step = -1;
        threw = false;
        try {
            e->request([&](Engine &x) { step = x.pumpTextureLoads(); });
        } catch (const std::exception &ex) {
            threw = true;
            std::printf("   the step threw: %s\n", ex.what());
        }
        return step;
    };
    const auto timeouts = [&]() { unsigned t = 0; e->request([&](Engine &x) { t = x.textureWaitTimeouts(); }); return t; };
    const unsigned before = timeouts();
    bool threw = false;
    // (an arm reaches the engine's live table at the next frame)
    bool armed = false;
    e->request([&](Engine &x) { armed = x.setArm("textures.pumpFault", 2.0); });
    CHECK(armed, "the fault's door (textures.pumpFault 2)");
    enginetest::frames(e, 1);
    const int first = pump(threw);
    CHECK(first == 1 && !threw, "a wait begins under the fault (step %d)", first);
    pump(threw);
    CHECK(threw, "the wait's next step throws (the door)");
    e->request([&](Engine &x) { x.setArm("textures.pumpFault", 1.0); });
    enginetest::frames(e, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));   // past the 300 ms budget of the thrown wait
    const int again = pump(threw);
    const int again2 = pump(threw);
    CHECK(again == 1 && again2 == 1 && !threw,
          "THE NEXT WAIT IS CLEAN: a fresh drain, still streaming (steps %d, %d), not the thrown one's stale budget "
          "expiring at once", again, again2);
    CHECK(timeouts() == before, "no timeout counted (%u, before %u)", timeouts(), before);
    e->request([&](Engine &x) { x.setArm("textures.pumpFault", 0.0); });
    enginetest::frames(e, 1);
    int last = 1, steps = 0;
    for (; steps < 2000 && last == 1; ++steps) {
        last = pump(threw);
        if (last == 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(last == 0, "the door shut, the wait finishes (%d after %d steps)", last, steps);
    enginetest::frames(e, 3);
    CHECK(timeouts() == before, "the frames after it wait as ever: no timeout, nothing latched (%u)", timeouts());
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "engine.texture_drain_reset: FAILED\n" : "engine.texture_drain_reset: PASS\n");
    return failures ? 1 : 0;
}
