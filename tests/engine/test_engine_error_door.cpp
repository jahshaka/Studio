// engine.error_door — THE HOST'S ERROR DOOR NEVER TOUCHES THE FRAME'S SINK (ENGINE-UPGRADE-U3 decisions 44 + 49): the
// D1 proof as an ENGINE FIXTURE, the row the TSan arm runs (the app rows cannot boot under either libtsan: Qt's
// recycled pthread_t, TSAN-GCC15-THREAD-REGISTRY). FREE-RUNNING on the render thread with the host's calls as records
// (the app's shape): every scene message refused (the arm messages.refuse) while this thread posts one per frame and
// takes its error from the host's door after each — the frame writing the engine's error sink every frame, the host
// reading its own door every frame, concurrently. Asserted, in frames: 300 posts, the refusals reach the host through
// the SNAPSHOT (PublishedEngineFrame::refusals), and the host's door never carries the frame's refusal text — and
// under TSan, zero reports.
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
    std::printf("== engine.error_door: the host's door and the frame's sink, free-running, 300 refused messages\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-error-door-ogre.log");
    cfg.renderThread = true;
    cfg.hostRecords = true;   // the app's shape: the host's calls reach the engine as records and messages
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created on its render thread (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("door", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("door");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    const NodeId node = scene->createDocumentNode(reserveNodeId());
    enginetest::frames(e, 3);
    e->request([&](Engine &x) { x.setArm("messages.refuse", 1.0); });
    e->setDisplayPeriod(1.0f / 240.0f);
    e->setFreeRunning(true);
    e->latchSnapshot();
    const unsigned long long refused0 = e->published().input.messagesRefused;
    int posts = 0, framesSeen = 0, doorCarriedFrame = 0;
    for (int i = 0; i < 300; ++i) {
        scene->setNodeVisible(node, i % 2 == 0);   // one scene message, refused by the frame that drains it
        ++posts;
        e->latchSnapshot();
        const unsigned long long f0 = e->published().frame;
        for (int spin = 0; spin < 2000000; ++spin) {
            e->latchSnapshot();
            if (e->published().frame > f0 + 1) { ++framesSeen; break; }   // the frame after the one that drained it
            std::this_thread::yield();
        }
        // THE HOST'S DOOR, every frame, while the render thread writes its sink: the host's own errors only.
        const std::string door = e->takeLastError();
        if (door.find("messages.refuse") != std::string::npos) ++doorCarriedFrame;
    }
    e->setFreeRunning(false);
    e->request([&](Engine &x) { x.setArm("messages.refuse", 0.0); });
    e->latchSnapshot();
    const PublishedEngineFrame &pub = e->published();
    const unsigned long long refused = pub.input.messagesRefused - refused0;
    bool snapshotSays = false;
    for (const auto &r : pub.refusals) if (r.text.find("messages.refuse") != std::string::npos) snapshotSays = true;
    std::printf("ERROR_DOOR posts=%d frames=%d refused=%llu door_carried_frame=%d snapshot_names_refusal=%d\n", posts,
                framesSeen, refused, doorCarriedFrame, int(snapshotSays));
    CHECK(framesSeen >= 290, "the render thread drew on its own between the posts (%d of %d)", framesSeen, posts);
    CHECK(refused >= 290 && snapshotSays, "every post was refused and the refusals reach the host through the snapshot "
          "(%llu refused)", refused);
    CHECK(doorCarriedFrame == 0, "the host's door never carried the frame's refusal (%d times)", doorCarriedFrame);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "engine.error_door: FAILED\n" : "engine.error_door: PASS\n");
    return failures ? 1 : 0;
}
