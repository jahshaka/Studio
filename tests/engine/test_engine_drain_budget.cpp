// frame.drain_budget — A FREE FRAME'S DRAIN IS BOUNDED, THE REST IS CARRIED IN ORDER (BISTRO-STREAM-IN-1 stage 2).
// FREE-RUNNING (the owner's mode) a frame drained the messages of every host tick queued while the previous frame ran —
// on Bistro's first open 823 -> 21,304 messages a frame, 0.75 -> 3.9 s of drain, the frames growing with the queue — so
// a FREE frame now applies the structure under a budget (OgreEngine::kFreeDrainShare) and carries the rest, in the
// one order, to the next frame (RenderThread::carryForward). LOCKSTEP is unchanged: a frame inside the caller's call
// applies its whole record (the six hashes, every test).
//
// THE LAWS, IN COUNTS: a batch of kNodes document nodes, each created under one created EARLIER in the batch (a binary
// tree, so a create that ran before its parent's is refused: "unknown parent") —
//  * LOCKSTEP: one frame applies the whole batch, and no drain is cut;
//  * FREE: the batch is applied over SEVERAL frames (a drain was cut), every create is applied exactly once, and none
//    is refused (the carried remainder kept the order).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

static constexpr int kNodes = 20000;

static void postTree(Scene *scene) {
    std::vector<NodeId> ids(kNodes);
    for (int i = 0; i < kNodes; ++i) {
        ids[i] = reserveNodeId();
        scene->createDocumentNode(ids[i], i == 0 ? NodeId(0) : ids[(i - 1) / 2]);
    }
}

int main()
{
    std::printf("== frame.drain_budget: a free frame's drain is bounded and carries the rest in order; lockstep drains whole\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-drain-budget-ogre.log");
    cfg.renderThread = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created on its render thread (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("drain", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("drain");
    if (!view || !scene || !view->setScene(scene)) { std::printf("FAIL: view/scene\n"); return 1; }
    enginetest::frames(e, 2);

    // ---- LOCKSTEP: the whole batch in the one frame -------------------------------------------------------------
    e->latchSnapshot();
    const FrameInputStatus l0 = e->published().input;
    postTree(scene);
    enginetest::frames(e, 1);
    e->latchSnapshot();
    const FrameInputStatus l1 = e->published().input;
    std::printf("    lockstep: %u messages in the frame, %llu refused, cuts %llu -> %llu\n", l1.messages,
                l1.messagesRefused - l0.messagesRefused, l0.drainsCut, l1.drainsCut);
    CHECK(l1.messages >= unsigned(kNodes) && l1.messagesCarried == 0u && l1.drainsCut == l0.drainsCut,
          "LOCKSTEP: one frame applied the whole batch of %d creates and no drain was cut", kNodes);
    CHECK(l1.messagesRefused == l0.messagesRefused, "LOCKSTEP: no create was refused");

    // ---- FREE: the batch over several frames, in order --------------------------------------------------------------
    e->setDisplayPeriod(1.0f / 240.0f);
    e->setFreeRunning(true);
    e->latchSnapshot();
    const FrameInputStatus f0 = e->published().input;
    const unsigned long long h0 = e->frameSubmitStatus().handOvers;
    postTree(scene);
    FrameInputStatus f1 = f0;
    unsigned long long frames = 0;
    // THE FRAMES DRAW ON THEIR OWN: wait (bounded in hand-overs, never in time) until every create is applied.
    for (int spin = 0; spin < 20000000; ++spin) {
        e->latchSnapshot();
        f1 = e->published().input;
        frames = e->frameSubmitStatus().handOvers - h0;
        if (f1.messagesTotal - f0.messagesTotal >= (unsigned long long)kNodes && f1.messagesCarried == 0u) break;
        if (frames > 5000) break;
        std::this_thread::yield();
    }
    e->setFreeRunning(false);
    enginetest::frames(e, 1);   // the rest, if any, lockstep (a free frame's carry is the next drain's)
    e->latchSnapshot();
    const FrameInputStatus f2 = e->published().input;
    std::printf("    free: %llu creates applied over %llu hand-overs, %llu drains cut, %llu refused\n",
                f1.messagesTotal - f0.messagesTotal, frames, f1.drainsCut - f0.drainsCut,
                f2.messagesRefused - f0.messagesRefused);
    CHECK(f1.drainsCut > f0.drainsCut, "FREE: the batch was spread over frames (%llu drains cut)", f1.drainsCut - f0.drainsCut);
    CHECK(f1.messagesTotal - f0.messagesTotal >= (unsigned long long)kNodes && f1.messagesCarried == 0u,
          "FREE: every create was applied (%llu of %d) within %llu hand-overs", f1.messagesTotal - f0.messagesTotal, kNodes,
          frames);
    CHECK(f2.messagesRefused == f0.messagesRefused,
          "FREE: no create was refused - every node met its parent, so the carried remainder kept the one order");

    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "frame.drain_budget: FAILED\n" : "frame.drain_budget: PASS\n");
    return failures ? 1 : 0;
}
