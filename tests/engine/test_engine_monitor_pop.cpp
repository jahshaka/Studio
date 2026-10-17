// frame.monitor_pop — THE MONITOR'S RECORDS ARE POPPED, NEVER REQUESTED (FRAME-MONITOR-DOOR-1): the hand-back door as
// an ENGINE FIXTURE, the form the TSan arm runs (the app row perf.capture_free_tick cannot boot under either libtsan:
// Qt's recycled pthread_t, TSAN-GCC15-THREAD-REGISTRY). FREE-RUNNING on the render thread with the host's calls as
// records (the app's shape), a capture on: the render thread retires a record into the engine's hand-back every frame
// while this thread pops it after every frame it sees — the push and the pop concurrent, under the queue's one lock.
// Then LOCKSTEP, the same pops after each stepped frame. Asserted, in frames and counts: every record the monitor
// recorded reaches the host exactly once, in frame order, the stop's flushed tail included (one pop after the stop
// takes it, the next returns 0); nothing dropped. AND THE CAPTURE'S SNAPSHOT IS ASKED (stage 3): one Engine::request
// in each mode, while the render thread draws free and while it is stepped — the same shape both times (live, the label,
// the view's workspaces), its `frame` the last frame that closed before the request (no popped record is newer). (That a pop never waits on a frame is the app row's, in frames per
// UI tick: perf.capture_free_tick.) Under TSan, zero reports.
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

namespace {
// THE SNAPSHOT THROUGH THE DOOR: one request, its frame and its shape.
struct Snap {
    bool took = false;
    EngineSnapshot s;
    void ask(Engine *e, const char *label) {
        e->request([&](Engine &x) { took = x.captureSnapshot(s, label, nullptr); });
    }
};
struct Got {
    std::vector<unsigned long long> frames;   // every popped record's MonitorRecord::frame, in pop order
    unsigned events = 0;
    void pop(Engine *e) {
        std::vector<MonitorRecord> recs;
        std::vector<MonitorEvent> evs;
        e->popMonitorRecords(recs);
        events += e->popMonitorEvents(evs);
        for (const MonitorRecord &r : recs) frames.push_back(r.frame);
    }
    // ONCE AND IN ORDER: strictly increasing frame numbers (a duplicate or a reordering breaks it).
    unsigned long long newest() const { return frames.empty() ? 0ull : frames.back(); }
    bool ordered() const {
        for (size_t i = 1; i < frames.size(); ++i) if (frames[i] <= frames[i - 1]) return false;
        return true;
    }
};
}   // namespace

int main()
{
    std::printf("== frame.monitor_pop: the monitor's hand-back popped against a free-running render thread, then lockstep\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-monitor-pop-ogre.log");
    cfg.renderThread = true;
    cfg.hostRecords = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created on its render thread (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("pop", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("pop");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    const NodeId node = scene->createDocumentNode(reserveNodeId());
    enginetest::frames(e, 3);

    // ---- FREE-RUNNING: the capture on (a message, applied by the request), the host popping after every frame ----
    e->setFrameMonitor(MonitorLevel::Review);
    e->request([](Engine &) {});
    e->setDisplayPeriod(1.0f / 240.0f);
    e->setFreeRunning(true);
    Got free;
    int framesSeen = 0;
    const int kFree = 240;
    for (int i = 0; i < kFree; ++i) {
        scene->setNodeVisible(node, i % 2 == 0);   // the host's record changes every frame, as an editor's would
        e->latchSnapshot();
        const unsigned long long f0 = e->published().frame;
        for (int spin = 0; spin < 2000000; ++spin) {
            e->latchSnapshot();
            if (e->published().frame > f0) { ++framesSeen; break; }
            std::this_thread::yield();
        }
        free.pop(e);   // THE DOOR, per frame, while the render thread pushes
    }
    // THE SNAPSHOT, ASKED WHILE THE RENDER THREAD DRAWS FREE (stage 3): between two frames, never beside one.
    free.pop(e);
    const unsigned long long beforeFreeSnap = free.newest();   // every record popped before the request
    Snap freeSnap;
    freeSnap.ask(e, "free");
    e->setFreeRunning(false);
    e->request([](Engine &) {});   // the loop idle: no frame in flight, the snapshot republished
    e->latchSnapshot();
    const MonitorStatus onFree = e->published().monitor;
    e->setFrameMonitor(MonitorLevel::Off);
    e->request([](Engine &) {});   // the stop applied: its flushed tail is in the hand-back
    const size_t beforeTail = free.frames.size();
    free.pop(e);
    const size_t tail = free.frames.size() - beforeTail;
    std::vector<MonitorRecord> none;
    const unsigned after = e->popMonitorRecords(none);
    std::printf("MONITOR_POP free: frames_seen=%d recorded=%llu popped=%zu tail=%zu events=%u dropped=%llu/%llu\n",
                framesSeen, onFree.framesRecorded, free.frames.size(), tail, free.events, onFree.framesDropped,
                onFree.eventsDropped);
    CHECK(framesSeen >= kFree - 10, "the render thread drew on its own between the pops (%d of %d)", framesSeen, kFree);
    CHECK(onFree.framesRecorded >= (unsigned long long)kFree &&
          free.frames.size() == size_t(onFree.framesRecorded),
          "every record the monitor recorded reached the host (%zu popped of %llu recorded)", free.frames.size(),
          onFree.framesRecorded);
    CHECK(free.ordered(), "once each and in frame order (%zu records)", free.frames.size());
    CHECK(tail > 0 && after == 0u, "the stop's flushed tail came in ONE pop (%zu records), the next pop is empty (%u)",
          tail, after);
    CHECK(onFree.framesDropped == 0 && onFree.eventsDropped == 0, "nothing dropped (%llu records, %llu events)",
          onFree.framesDropped, onFree.eventsDropped);

    // ---- LOCKSTEP: the same door after each stepped frame ----
    e->setFrameMonitor(MonitorLevel::Review);
    e->request([](Engine &) {});
    Got step;
    const int kStep = 30;
    for (int i = 0; i < kStep; ++i) {
        scene->setNodeVisible(node, i % 2 == 0);
        enginetest::frames(e, 1);
        step.pop(e);
    }
    // (Printed: the records a pop had after the last stepped frame — they wait for their GPU samples, so a window that
    // counted POPPED records would not close on exactly N stepped frames; the stop's flush hands back the rest.)
    const size_t poppedBeforeStop = step.frames.size();
    Snap stepSnap;
    stepSnap.ask(e, "lockstep");   // the capture's "end" shape: the monitor still on, between two stepped frames
    e->latchSnapshot();
    const MonitorStatus onStep = e->published().monitor;
    e->setFrameMonitor(MonitorLevel::Off);
    e->request([](Engine &) {});
    step.pop(e);
    std::printf("MONITOR_POP snapshot: free took=%d frame=%llu workspaces=%zu (newest popped %llu); lockstep took=%d "
                "frame=%llu workspaces=%zu (newest popped %llu)\n", int(freeSnap.took), freeSnap.s.frame,
                freeSnap.s.workspaces.size(), beforeFreeSnap, int(stepSnap.took), stepSnap.s.frame,
                stepSnap.s.workspaces.size(), step.newest());
    CHECK(freeSnap.took && freeSnap.s.live && freeSnap.s.label == "free" && !freeSnap.s.workspaces.empty(),
          "free-running: the snapshot answered through ONE request (live, its label, %zu workspaces)",
          freeSnap.s.workspaces.size());
    CHECK(freeSnap.s.frame > 0 && beforeFreeSnap <= freeSnap.s.frame,
          "free-running: its frame (%llu) is the last that closed before the request: no record popped before it is "
          "newer (%llu)", freeSnap.s.frame, beforeFreeSnap);
    CHECK(stepSnap.took && stepSnap.s.live && stepSnap.s.label == "lockstep" &&
          stepSnap.s.workspaces.size() == freeSnap.s.workspaces.size(),
          "lockstep: the same shape (%zu workspaces against %zu)", stepSnap.s.workspaces.size(),
          freeSnap.s.workspaces.size());
    CHECK(stepSnap.s.frame == step.newest() && stepSnap.s.frame > freeSnap.s.frame,
          "lockstep: its frame (%llu) IS the last stepped frame's record (%llu), after the free one's",
          stepSnap.s.frame, step.newest());
    std::printf("MONITOR_POP lockstep: recorded=%llu popped=%zu (popped before the stop: %zu)\n", onStep.framesRecorded,
                step.frames.size(), poppedBeforeStop);
    CHECK(onStep.framesRecorded == (unsigned long long)kStep && step.frames.size() == size_t(kStep),
          "lockstep: one record per stepped frame, all popped (%zu of %llu, %d frames)", step.frames.size(),
          onStep.framesRecorded, kStep);
    CHECK(step.ordered() && (step.frames.empty() || step.frames.front() > (free.frames.empty() ? 0ull : free.frames.back())),
          "lockstep: in frame order, none of the previous capture's");

    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "frame.monitor_pop: FAILED\n" : "frame.monitor_pop: PASS\n");
    return failures ? 1 : 0;
}
