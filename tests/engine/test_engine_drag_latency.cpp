// frame.drag_latency — A RECORD IS ON SCREEN THE NEXT FRAME (ENGINE-UPGRADE-U3 stage 5). FREE-RUNNING: the render thread
// draws on its own (Engine::setFreeRunning) at a display period the host supplies while this thread submits a drag's
// records; every record submitted after the render thread's n-th hand-over is applied by the frame of hand-over
// n + 1 (PublishedEngineFrame::handOver) — the gesture is never two frames late. A law in frames: 120 records.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>
#include <thread>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

int main()
{
    std::printf("== frame.drag_latency: free-running, a record submitted at hand-over n is drawn by frame n + 1\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-drag-latency-ogre.log");
    cfg.renderThread = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created on its render thread (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("latency", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("latency");
    if (!view || !scene || !view->setScene(scene)) { std::printf("FAIL: view/scene\n"); return 1; }
    const NodeId node = scene->createDocumentNode(reserveNodeId());
    enginetest::frames(e, 2);
    e->setDisplayPeriod(1.0f / 240.0f);
    e->setFreeRunning(true);
    int onTime = 0, late = 0, measured = 0;
    for (int k = 0; k < 120; ++k) {
        FrameInput in;
        in.stepSerial = (unsigned long long)(k + 2);
        FrameInputScene part;
        part.scene = scene;
        NodeRecord r;
        r.id = node;
        r.fields = NodeRecord::Trs;
        r.position = Vec3(float(k) * 0.01f, 0.0f, 0.0f);
        r.rotation = Quat(0.0f, 0.0f, 0.0f, 1.0f);
        r.scale = Vec3(1.0f, 1.0f, 1.0f);
        part.nodes.push_back(r);
        in.scenes.push_back(part);
        e->submitFrame(in);
        const FrameSubmitStatus st = e->frameSubmitStatus();   // the serial it will be applied as, the hand-overs so far
        const unsigned long long want = st.nextSerial, h0 = st.handOvers;
        // THE FRAMES DRAW ON THEIR OWN: wait (bounded in hand-overs, never in time) for the frame that applied it.
        for (int spin = 0; spin < 2000000; ++spin) {
            e->latchSnapshot();   // the host's latch: the newest snapshot, read whole (stage 5)
            const PublishedEngineFrame &pub = e->published();
            if (pub.serial >= want) {
                ++measured;
                // The frame that applied it took hand-over h0 + 1 — or, where the render thread took the slot between the
                // submit and this read of its status, h0 (the record was already in that frame).
                if (pub.handOver <= h0 + 1) ++onTime; else ++late;
                break;
            }
            if (e->frameSubmitStatus().handOvers > h0 + 3) { ++late; break; }
            std::this_thread::yield();
        }
    }
    e->setFreeRunning(false);
    std::printf("    120 records: %d on the next frame, %d late, %d measured\n", onTime, late, measured);
    CHECK(measured == 120 && late == 0, "every record was drawn by the next frame the render thread began (%d of 120)",
          onTime);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "frame.drag_latency: FAILED\n" : "frame.drag_latency: PASS\n");
    return failures ? 1 : 0;
}
