// frame.drain_budget — A FREE FRAME'S DRAIN IS BOUNDED PER FRAME, THE REST CARRIED IN ORDER (BUGS-7 stage 1).
// FREE-RUNNING (the owner's mode) a frame drained the messages of every host tick queued while the previous frame ran —
// on Bistro's first open (§2604) 823 -> 25,515 messages a frame and frames of 0.8-5 s of render-thread CPU, growing with
// the queue — so a FREE frame applies the structure for at most its drain budget (OgreEngine::freeDrainBudgetMs: the
// part of the display period the frame's own work leaves, never under half a period, never over one) and carries the
// rest, in the one order, to the next frame (RenderThread::carryForward). LOCKSTEP is unchanged: a frame inside the
// caller's call applies its whole record (the six hashes, every test).
//
// THE LAWS, IN COUNTS (frames, never the wall clock): a batch of kNodes document nodes, each created under one created
// EARLIER in the batch (a binary tree, so a create applied before its parent's is refused: "unknown parent") —
//  * LOCKSTEP: one frame applies the whole batch, and no drain is cut;
//  * FREE: the batch is applied over SEVERAL frames — every frame of the backlog cut by its budget, the published
//    serial advancing by one each frame (the carried remainder is a record of its own), the backlog shrinking every
//    frame — each create applied exactly once and none refused (the carried remainder kept the order);
//  * THE BOUND IS THE FRAME'S, NOT THE BACKLOG'S: the longest free drain is under half the lockstep frame's drain of the
//    same batch (an unbounded drain is the whole backlog in one frame).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
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
    std::printf("== frame.drain_budget: a free frame's drain is bounded per frame and carries the rest in order\n");
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
    const double lockstepDrainMs = l1.drainMicros / 1000.0;
    std::printf("    lockstep: %u messages in the frame, %.2f ms of drain, %llu refused, cuts %llu -> %llu\n",
                l1.messages, lockstepDrainMs, l1.messagesRefused - l0.messagesRefused, l0.drainsCut, l1.drainsCut);
    CHECK(l1.messages >= unsigned(kNodes) && l1.messagesCarried == 0u && l1.drainsCut == l0.drainsCut,
          "LOCKSTEP: one frame applied the whole batch of %d creates and no drain was cut", kNodes);
    CHECK(l1.messagesRefused == l0.messagesRefused, "LOCKSTEP: no create was refused");

    // ---- FREE: the batch over several frames, in order, one frame at a time ---------------------------------------
    // A 240 Hz display: a budget of 2.1-4.2 ms a frame against the whole batch's lockstep drain above.
    e->setDisplayPeriod(1.0f / 240.0f);
    e->setFreeRunning(true);
    e->latchSnapshot();
    const FrameInputStatus f0 = e->published().input;
    const unsigned long long frame0 = e->published().frame;
    postTree(scene);
    // THE FRAMES DRAW ON THEIR OWN: every published frame is read (latched until its number moves), bounded in frames.
    struct Seen { unsigned long long frame, serial, drained; unsigned messages, carried; double drainMs; };
    std::vector<Seen> seen;
    unsigned long long lastFrame = frame0;
    for (long spin = 0; spin < 50000000L && seen.size() < 4000u; ++spin) {
        e->latchSnapshot();
        const PublishedEngineFrame &p = e->published();
        if (p.frame == lastFrame) { std::this_thread::yield(); continue; }
        lastFrame = p.frame;
        seen.push_back({ p.frame, p.serial, p.input.drained, p.input.messages, p.input.messagesCarried,
                         p.input.drainMicros / 1000.0 });
        if (p.input.messagesTotal - f0.messagesTotal >= (unsigned long long)kNodes && p.input.messagesCarried == 0u)
            break;
    }
    e->setFreeRunning(false);
    enginetest::frames(e, 1);
    e->latchSnapshot();
    const FrameInputStatus f2 = e->published().input;
    // the frames of the backlog: those that left a remainder, and the one that drained the last of it
    std::vector<Seen> backlog;
    for (const Seen &s : seen) { if (s.messages || s.carried) backlog.push_back(s); }
    bool serialEveryFrame = true, shrinking = true, consecutive = true;
    double maxDrainMs = 0.0;
    for (size_t i = 1; i < backlog.size(); ++i) {
        const Seen &a = backlog[i - 1], &b = backlog[i];
        if (b.drained != a.drained + 1) consecutive = false;           // a frame between them we did not latch
        else {
            if (b.serial != a.serial + 1) serialEveryFrame = false;
            if (a.carried && b.carried >= a.carried) shrinking = false;
        }
    }
    for (const Seen &s : backlog) if (s.carried) maxDrainMs = std::max(maxDrainMs, s.drainMs);
    const unsigned long long cuts = f2.drainsCut - f0.drainsCut;
    std::printf("    free: %llu creates applied over %zu frames seen (%llu drains cut), the longest cut drain %.2f ms, "
                "%llu refused, every frame latched: %s\n", f2.messagesTotal - f0.messagesTotal, backlog.size(), cuts,
                maxDrainMs, f2.messagesRefused - f0.messagesRefused, consecutive ? "yes" : "no");
    for (size_t i = 0; i < backlog.size() && i < 6; ++i)
        std::printf("      frame %llu: %u applied, %u carried, %.2f ms\n", backlog[i].frame, backlog[i].messages,
                    backlog[i].carried, backlog[i].drainMs);
    CHECK(cuts >= 2u, "FREE: the batch was spread over frames (%llu drains cut)", cuts);
    CHECK(f2.messagesTotal - f0.messagesTotal >= (unsigned long long)kNodes,
          "FREE: every create was applied (%llu of %d)", f2.messagesTotal - f0.messagesTotal, kNodes);
    CHECK(f2.messagesRefused == f0.messagesRefused,
          "FREE: no create was refused - every node met its parent, so the carried remainder kept the one order");
    CHECK(serialEveryFrame, "FREE: the published serial advanced by one every frame of the backlog");
    CHECK(shrinking, "FREE: the backlog shrank every frame");
    CHECK(maxDrainMs < 0.5 * lockstepDrainMs,
          "FREE: the frame's drain is bounded, not the backlog's (longest %.2f ms < half the whole batch's %.2f ms)",
          maxDrainMs, lockstepDrainMs);

    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "frame.drain_budget: FAILED\n" : "frame.drain_budget: PASS\n");
    return failures ? 1 : 0;
}
