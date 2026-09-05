#include "app/shaderbuildgate.h"

#include "app/versionsplashscreen.h"
#include "bridge/enginehost.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
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
    // THE WARM VIEW MUST HAVE THE EDITOR'S PASS SHAPE (audit F1b).
    //
    // This used to be a 32x32 view over an EMPTY scene with no lights, no
    // shadows and 1x MSAA, and the recorded set was replayed into it. That is
    // the wrong world twice over: Hlms permutations are a function of the PASS
    // as much as of the renderable — shadows change the shader, and the sample
    // count is literally a shader property (HlmsBaseProp::MsaaSamples, set from
    // the target's sample description at OgreHlms.cpp:3771, consumed at :2919).
    // So the replay compiled zero-light / no-shadow / 1x variants the editor
    // never draws, cached them, and counted them into expectedShaders — the
    // splash denominator — as shaders somebody wanted.
    //
    // EngineHost::warmUpShape() is what the last session's editor view actually
    // used (recorded on the open path beside the warm-up set itself), so this
    // is measured rather than guessed. A first-ever launch gets the defaults
    // and is no worse off than the old code.
    const EngineHost::WarmUpShape shape = EngineHost::warmUpShape();
    View *warmView = engine->createOffscreenView("startup-warmup", kWarmUpSize, kWarmUpSize,
                                                 Colour(0.0f, 0.0f, 0.0f, 1.0f));
    Scene *warmScene = warmView ? engine->createScene("startup-warmup") : nullptr;
    if (warmScene) {
        warmView->setScene(warmScene);
        warmView->setShadows(shape.shadows);
        if (shape.samples > 1) warmView->setSampleCount(shape.samples);
        // A REPRESENTATIVE LIGHT RIG, not a lit scene: one shadow-casting
        // directional plus ambient is the smallest thing that makes the pass
        // hash look like the editor's (it is also exactly what
        // tests/shadercache/test_warm_up.cpp builds for the same reason). No
        // geometry — the recorded set brings its own degenerate renderables,
        // and anything else here would compile permutations nothing draws,
        // which is the defect being fixed.
        warmScene->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.1f, 0.1f, 0.12f));
        const NodeId sun = warmScene->createNode();
        if (sun) {
            // A light points down its node's -Y (the engine's convention), and
            // the identity orientation already aims it straight down — which is
            // all this needs. No transform, no rotation maths.
            LightDesc l;
            l.type = LightType::Directional;
            l.colour = Colour(1.0f, 1.0f, 1.0f);
            l.intensity = 1.0f;
            l.castShadows = shape.shadows;
            warmScene->setLight(sun, l);
        }
    }
    // What this covers is everything PROCESS-WIDE: Hlms registration, all the
    // low-level material scripts (sky, DPSM, depth utils, copy/resolve, ESM,
    // HDR, SSAO, SMAA) and the compositor chain. What it cannot cover on its
    // own is the Hlms permutations — the Hlms generates a shader per RENDERABLE
    // and the permutation set is a property of the CONTENT. Those come from the
    // recorded set below, replayed into this correctly-shaped pass.
    for (int i = 0; i < kWarmUpFrames && warmView; ++i) {
        engine->renderOneFrame();
        poll();
    }

    // THE RECORDED SET (SHADER_CACHE_SPEC §2.7b). The previous session wrote
    // down which permutations it actually used — a list of vertex formats,
    // render queues and one representative material each, not shaders. Applying
    // it here builds every one of them against degenerate 4-vertex buffers, so
    // this session's Hlms permutations exist before the window does, without
    // loading a single mesh, skeleton or texture.
    //
    // This is what the process-wide warm-up above cannot do on its own: Hlms
    // shaders are per RENDERABLE, so guessing at them from an empty scene is
    // impossible — but REMEMBERING them from last time is not.
    //
    // GATED ON THE CACHE SETTING (audit F12): the set lives in the cache
    // directory and is derived data of exactly the same kind, so "keep compiled
    // shaders between launches: off" has to mean this too.
    if (warmScene && EngineHost::shaderCacheEnabled()) {
        const QString setPath = EngineHost::warmUpSetPath();
        if (!setPath.isEmpty() && QFileInfo::exists(setPath)) {
            const unsigned built =
                engine->applyWarmUpSet(setPath.toStdString(), warmScene);
            poll();
            qInfo("startup: replayed the recorded warm-up set (%u shader(s) built)", built);
        }
    }
    // The SCENE goes; the VIEW stays, disabled, for the life of the process.
    //
    // That asymmetry is not tidiness, it is a DEFECT WORKAROUND, narrowed by
    // bisection against scripting.e2e.particles:
    //
    //   destroy scene + view : a LATER editor.screenshot() reads back a
    //                          completely black image — not the clear colour,
    //                          zeroes — and the test that photographs a particle
    //                          plume sees nothing at all.
    //   destroy the view only: same failure.
    //   destroy the scene only, keep the view: correct.
    //   destroy nothing:                       correct.
    //
    // So destroying this offscreen View — at the one moment in the process when
    // it is the ONLY view, since the editor's views do not exist until the
    // window is shown — leaves engine state behind that a later offscreen
    // readback trips over. Note EngineSceneViewport::takeScreenshot creates and
    // destroys offscreen views constantly and is fine, so it is specifically
    // "the last view in the process goes away". Adding frames after the destroy
    // does not help, so it is not a pending-command flush.
    //
    // That is an engine defect and it is not this lane's to fix. The price of
    // routing around it is one disabled 32x32 view (a 4 KB render target, a
    // camera and a workspace) held for the session, which renderOneFrame skips.
    if (warmScene) engine->destroyScene(warmScene);
    if (warmView)  warmView->setEnabled(false);

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
