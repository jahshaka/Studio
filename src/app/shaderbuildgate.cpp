#include "app/shaderbuildgate.h"

#include "app/versionsplashscreen.h"
#include "bridge/enginehost.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QThread>

using namespace jahshaka::engine;

namespace {

/// How long the compile count has to stand still before we call the burst over.
/// Measured shape of a cold startup on this box: 47 shaders in the first
/// second, 19 in the second, then nothing. The gaps INSIDE a burst are tens of
/// milliseconds, so 250 ms is comfortably outside them — and every millisecond
/// here is paid on every launch, warm or cold, so it is not free.
constexpr int kSettleMs = 250;

/// Hard ceiling. A gate that can hang the launch forever is worse than a launch
/// that shows the window with a few shaders still to build: if we are still
/// compiling after this long, something is wrong (a pathological driver, a
/// machine under extreme load) and the user gets their app.
constexpr int kDeadlineMs = 30000;

/// The warm-up view. Small on purpose: nothing about which SHADERS get built
/// depends on the resolution, and a 32x32 render target costs nothing.
constexpr unsigned kWarmUpSize = 32;

/// Frames to render through the warm-up view. One is enough to force the
/// compositor chain to execute; a few more cost microseconds and cover any pass
/// that only runs on a later frame.
constexpr int kWarmUpFrames = 4;

}  // namespace

unsigned holdSplashForShaderBuild(QApplication &app, VersionSplashScreen &splash)
{
    auto engine = EngineHost::instance().engine();
    if (!engine) return 0;   // headless: no engine, no shaders, no wait

    unsigned compiled = 0, cached = 0, expected = 0;
    engine->shaderBuildProgress(compiled, cached, expected);
    const unsigned entryTotal = compiled + cached;

    QElapsedTimer total;      total.start();
    QElapsedTimer sinceMove;  sinceMove.start();
    unsigned last = entryTotal;
    bool shown = false;

    auto poll = [&]() {
        engine->shaderBuildProgress(compiled, cached, expected);
        const unsigned now = compiled + cached;
        if (now == last) return;
        last = now;
        sinceMove.restart();
        splash.showShaderBuild(int(now), int(expected));
        shown = true;
        splash.repaint();
        app.processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    };

    // ---- Drive the build ---------------------------------------------------
    // MEASURED, and the reason this function is not just a wait loop: at this
    // point in startup NOTHING has compiled — not one shader of the ~66 a
    // session needs. MainWindow's constructor creates the viewport WIDGETS, but
    // EngineViewWidget only creates and enables its engine View on its first
    // showEvent (engineviewwidget.cpp:87), and the Hlms is not even registered
    // until the first View exists. So a hidden window renders nothing, compiles
    // nothing, and the whole burst would land on the first frames after the
    // window appears — exactly where the owner does not want it.
    //
    // So the gate makes the work happen itself: a tiny offscreen View and an
    // empty Scene, a handful of frames, then both destroyed. That is enough to
    // reach the state that actually compiles — Hlms registration, the low-level
    // material scripts, the SSAO/HDR/SMAA helper materials and the compositor
    // chain — using nothing but the public engine API.
    //
    // The shaders survive their view: the Hlms shader cache and the microcode
    // map are process-wide, so the editor's real views reuse everything built
    // here (and everything loaded from disk before it).
    View *warmView = engine->createOffscreenView("startup-warmup", kWarmUpSize, kWarmUpSize,
                                                 Colour(0.0f, 0.0f, 0.0f, 1.0f));
    Scene *warmScene = warmView ? engine->createScene("startup-warmup") : nullptr;
    if (warmScene) warmView->setScene(warmScene);
    // The scene stays EMPTY, deliberately. Measured, both ways:
    //
    //   empty scene           41 of the session's 66 shaders build here (62%)
    //   + a lit shadowed quad 44 of 68 (65%) — because the quad ALSO added two
    //                         Hlms permutations of its own that the app never
    //                         uses, and cached them.
    //
    // What the empty warm-up covers is everything PROCESS-WIDE: Hlms
    // registration, all 41 low-level material scripts (sky, DPSM, depth utils,
    // copy/resolve, ESM, HDR, SSAO, SMAA) and the compositor chain. What it
    // cannot cover is the Hlms permutations, because the Hlms generates a
    // shader per RENDERABLE and the permutation set is a property of the
    // content — guessing at it here would fill the cache with variants nothing
    // draws. Those belong to the per-scene warm-up under the ViewportCover
    // (SHADER_CACHE_SPEC §5), where the actual datablocks exist.
    for (int i = 0; i < kWarmUpFrames && warmView; ++i) {
        engine->renderOneFrame();
        poll();
    }
    if (warmScene) engine->destroyScene(warmScene);
    if (warmView)  engine->destroyView(warmView);

    // ---- Wait for it to settle --------------------------------------------
    // Anything the warm-up kicked off asynchronously, plus whatever the render
    // driver's own frames add, has to stop moving before the window appears.
    for (;;) {
        app.processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
        poll();
        if (total.elapsed() > kDeadlineMs) {
            qWarning("shader build gate: still compiling after %d ms (%u shaders) — "
                     "showing the window anyway", kDeadlineMs, last);
            break;
        }
        if (sinceMove.elapsed() > kSettleMs) break;
    }

    if (shown) splash.showShaderBuild(-1, 0);
    // NOT recorded in LoadTimeline: that ledger belongs to a scene OPEN, and
    // add() is a documented no-op outside begin()/end(). The startup build's
    // numbers live in the Ogre log and, verb-side, in app.shaderCache()'s
    // compiledThisRun / loadedThisRun — which is what the e2e asserts on.
    qInfo("startup shader build: %u shaders (%u compiled, %u from cache) in %lld ms",
          last - entryTotal, compiled, cached, static_cast<long long>(total.elapsed()));
    return last;
}
