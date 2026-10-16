// frame.runtime_pace — A HEADSET FASTER THAN THE PANEL IS PACED BY ITS RUNTIME, NOT PADDED TO THE PANEL (ENGINE-UPGRADE-U3
// decision 53). FREE-RUNNING on the render thread at the panel's 16.7 ms display period, with a runtime pacing every
// frame at 11 ms (the test door vr.fakeRuntimePace: each free frame waits for its next display time as xrWaitFrame
// would — Monado's null compositor, the rig's runtime, runs at 20 Hz). The render loop must pad nothing after a frame
// the runtime paced: the frames come at the runtime's period (mean interval <= 11.5 ms over 90 frames), and the
// presentation clock — advanced by the period the driver sends, the session's — draws weights strictly between the
// two grid states of a 60 Hz mover (the interpolation is not pinned). The control arm (no runtime) is the panel's.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <thread>
using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

int main()
{
    std::printf("== frame.runtime_pace: a runtime at 11 ms under a 16.7 ms panel paces the free frames\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-runtime-pace-ogre.log");
    cfg.renderThread = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created on its render thread (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("pace", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("pace");
    if (!view || !scene || !view->setScene(scene)) { std::printf("FAIL: view/scene\n"); return 1; }
    const NodeId node = scene->createDocumentNode(reserveNodeId());
    enginetest::frames(e, 2);
    unsigned long long step = 1;
    const auto record = [&]() {
        FrameInput in;
        in.frameDelta = 1.0f / 60.0f;
        in.stepSerial = ++step;
        in.stepPeriod = 1.0f / 60.0f;
        FrameInputScene part;
        part.scene = scene;
        NodeRecord r;
        r.id = node;
        r.fields = NodeRecord::Trs;
        r.position = Vec3(float(step), 0.0f, 0.0f);
        r.rotation = Quat(0.0f, 0.0f, 0.0f, 1.0f);
        r.scale = Vec3(1.0f, 1.0f, 1.0f);
        part.nodes.push_back(r);
        part.changes.moved.push_back(node);
        in.scenes.push_back(part);
        e->submitFrame(in);
    };
    // One arm: `runtimeMs` of runtime pace (0 = none) under the panel's period; the driver's role played here — the
    // period it sends is the session's while one paces, the panel's otherwise (EngineRenderDriver::displayPeriodSeconds).
    const auto arm = [&](double runtimeMs, double sentMs, double &meanMs, size_t &distinct) {
        e->request([&](Engine &x) { x.setArm("vr.fakeRuntimePace", runtimeMs); });
        e->setDisplayPeriod(float(sentMs / 1000.0));
        e->setFreeRunning(true);
        // Warm up: the first frames of the loop (the arm latches at a frame's top).
        e->latchSnapshot();
        const unsigned long long w0 = e->published().frame;
        while (true) { e->latchSnapshot(); if (e->published().frame >= w0 + 10) break; std::this_thread::yield(); }
        e->latchSnapshot();
        const unsigned long long f0 = e->published().frame;
        const auto t0 = std::chrono::steady_clock::now();
        auto nextStep = t0;
        std::set<int> alphas;
        unsigned long long f1 = f0;
        while (f1 < f0 + 90) {   // the game thread: a 60 Hz record; between them, the snapshot read
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextStep) { record(); nextStep += std::chrono::microseconds(16667); }
            e->latchSnapshot();
            const PublishedEngineFrame &p = e->published();
            if (p.presentFree && p.presentAlpha > 1e-3f && p.presentAlpha < 1.0f - 1e-3f)
                alphas.insert(int(p.presentAlpha * 1000.0f + 0.5f));
            f1 = p.frame;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        e->setFreeRunning(false);
        e->request([&](Engine &x) { x.setArm("vr.fakeRuntimePace", 0.0); });
        meanMs = ms / double(f1 - f0);
        distinct = alphas.size();
    };
    double controlMs = 0.0, panelMs = 0.0, sessionMs = 0.0;
    size_t controlAlphas = 0, panelAlphas = 0, sessionAlphas = 0;
    arm(0.0, 1000.0 / 60.0, controlMs, controlAlphas);     // no runtime: the panel's pad paces (the control)
    arm(11.0, 1000.0 / 60.0, panelMs, panelAlphas);        // a runtime at 11 ms, the PANEL's period sent: no pad after it
    arm(11.0, 11.0, sessionMs, sessionAlphas);             // ...and the SESSION's period sent (the driver's rule)
    std::printf("RUNTIME_PACE control_mean_ms=%.2f (no runtime, 16.67 sent) panel_sent_mean_ms=%.2f (runtime 11, 16.67 "
                "sent) session_sent_mean_ms=%.2f (runtime 11, 11 sent) distinct_alphas=%zu (panel sent %zu, control "
                "%zu)\n", controlMs, panelMs, sessionMs, sessionAlphas, panelAlphas, controlAlphas);
    CHECK(controlMs > 15.0, "the control: with no runtime the panel's period paces the free frames (mean %.2f ms)",
          controlMs);
    CHECK(panelMs <= 11.5, "a frame the runtime paced is never padded to the panel's 16.7 ms (mean %.2f ms)", panelMs);
    CHECK(sessionMs <= 11.5, "the runtime's 11 ms paces the frames with the session's period sent (mean %.2f ms)",
          sessionMs);
    CHECK(sessionAlphas >= 2, "the interpolation is not pinned with the session's period: %zu distinct in-between "
          "weights", sessionAlphas);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "frame.runtime_pace: FAILED\n" : "frame.runtime_pace: PASS\n");
    return failures ? 1 : 0;
}
