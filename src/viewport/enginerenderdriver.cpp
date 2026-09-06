#include "viewport/enginerenderdriver.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

#include "services/engineerrorpump.h"
#include "services/jahlog.h"
#include "services/loadtimeline.h"

/// A frame this long is a visible hitch, not a frame.
static const double kSlowFrameMs = 100.0;

EngineRenderDriver::EngineRenderDriver(jahshaka::engine::Engine *engine, QObject *parent)
    : QObject(parent), mEngine(engine), mTimer(new QTimer(this))
{
    // PRECISE, because the interval now carries meaning. Qt's default coarse
    // timer is allowed to drift by 5% and to coalesce with other timers, which
    // on a 10 ms budget (a 100 Hz panel) is a whole millisecond of slack handed
    // to a loop whose entire job is to beat slightly faster than the display.
    mTimer->setTimerType(Qt::PreciseTimer);
    connect(mTimer, &QTimer::timeout, this, [this] {
        // A frame is UI-THREAD work: whatever it costs, the window is not
        // answering while it runs. The first frame of a freshly opened world
        // is the expensive one — the Hlms compiles a shader variant per new
        // material/pass combination, with no cache anywhere in the pin
        // (lane-openasync 2026-09-03: no HlmsDiskCache, no microcode cache,
        // no VkPipelineCache persistence is wired). Slow frames are logged
        // and, while a scene open is being measured, banked in the ledger, so
        // "opening is still slow" always has a number attached to it.
        QElapsedTimer frame;
        frame.start();
        ++mStats.ticks;
        emit beforeFrame();
        // Nothing is showing anywhere — every viewport widget is hidden, so
        // every View is disabled (EngineViewWidget's show/hideEvent). Drawing
        // is then pure waste: renderOneFrame would still walk every scene, run
        // the refraction/globals interlocks, submit a command buffer and burn a
        // present-queue slot, every tick, to produce nothing (deep audit
        // area 7 F8 — measured while sitting on the Desktop page).
        //
        // The TIMER KEEPS RUNNING. Skipping is per tick, not a stop(), which is
        // what makes the wake-up free: the first tick after a widget's showEvent
        // re-enables its View sees `true` here and renders, exactly as if the
        // loop had never idled. Stopping the timer instead would have needed a
        // signal from every host to restart it, and would have stopped the
        // error-pump drain below with it.
        //
        // beforeFrame is emitted either way, deliberately: its subscribers pull
        // a WALL-CLOCK delta (EngineSceneViewport::syncFrame, EnginePlayerView::
        // syncFrame both call mFrameTimer.restart()), so skipping it would bank
        // the whole idle period into the first resumed frame and jump physics,
        // animation and the camera controller.
        //
        // TWO PACERS, AND THE SLOWER ONE WINS. renderOneFrame BLOCKS inside the
        // swapchain acquire while vsync is on: the frame is not "submitted and
        // forgotten", the tick literally waits for the display to hand back an
        // image. Beating that with a timer is why the presented rate QUANTIZES
        // to refresh/n instead of sliding — a tick that misses its vblank by a
        // microsecond waits a whole refresh, so the loop settles on an integer
        // divisor of the panel (100 Hz: 50, 33, 25 — the owner's "60 -> 24 fps"
        // episode was one step of that divisor, with the frame work unchanged).
        // The cure is to make the timer the FASTER pacer (framepacing.h derives
        // the interval from the panel) or to take the blocking out of the loop
        // altogether (Unlimited = vsync off).
        const bool anythingToDraw = mEngine && mEngine->hasEnabledViews();
        if (anythingToDraw) { mEngine->renderOneFrame(); ++mStats.rendered; }
        else                { ++mStats.skipped; }
        // The session log's frame column (SESSION_LOG_SPEC §3.7) — what turns
        // "these three warnings" into "these three warnings IN THE SAME FRAME".
        // ONE relaxed atomic store, no log call: the discipline is zero LOG
        // calls on the frame path, and pushing the number is what lets the
        // logging threads read it without racing this loop's Stats struct.
        JahLog::setFrameCounter(mStats.rendered);
        // Whatever the frame (or anything else since the last one — the sink is
        // process-wide) refused to do, said so in the engine's error string and
        // nowhere else. Drain it here, where the one render loop lives, so a
        // silent failure becomes a [warn] instead of a wrong picture
        // (services/engineerrorpump.h). Drained on SKIPPED ticks too: the sink
        // is process-wide, so an off-loop failure (a thumbnail render, a
        // scripted verb) must not sit silent just because no viewport is up.
        EngineErrorPump::instance().drain(mEngine);
        const double ms = double(frame.nsecsElapsed()) / 1.0e6;
        // KEEP the number, do not just threshold it (STATS_OVERLAY_SPEC §4).
        // This measurement used to be discarded below 100 ms, which threw away
        // the ONE honest performance figure this architecture has: an FPS
        // reading here is a measurement of the timer, not of the renderer.
        // Only RENDERED ticks are banked — a skipped tick costs ~nothing and
        // would drag the average toward zero while the user sits on a page
        // with no viewport.
        if (anythingToDraw) {
            mWork[mWorkNext] = ms;
            mWorkNext = (mWorkNext + 1) % kWorkWindow;
            if (mWorkFilled < kWorkWindow) ++mWorkFilled;
            double sum = 0.0;
            for (int i = 0; i < mWorkFilled; ++i) sum += mWork[i];
            mStats.workMs = sum / mWorkFilled;
            if (ms > mStats.worstMs) mStats.worstMs = ms;
        }
        if (ms >= kSlowFrameMs) {
            ++mStats.slowFrames;
            LoadTimeline::add(QStringLiteral("frame:slow"), ms);
            qWarning("[open-profile] slow frame: %.1f ms (shader/PSO compilation is the "
                     "usual cause on the first frame of a world)", ms);
        }
    });
}

void EngineRenderDriver::start()
{
    if (mEngine) mTimer->start(pacedIntervalMs());
}
void EngineRenderDriver::stop()                { mTimer->stop(); }
bool EngineRenderDriver::isRunning() const     { return mTimer->isActive(); }
int  EngineRenderDriver::intervalMs() const    { return mTimer->interval(); }

void EngineRenderDriver::setPacingMode(framepacing::Mode m)
{
    if (m == mMode) return;
    mMode = m;
    // VSYNC IS THE OTHER HALF, and it is not the timer's to fake: with the
    // display still gating the swapchain acquire, a 0 ms timer buys nothing but
    // a busier CPU. Off is what makes "unlimited" mean anything — and it costs
    // one swapchain rebuild per on-screen window, which is why this only ever
    // happens on a deliberate change (Engine::setVsync documents the price).
    if (mEngine) mEngine->setVsync(framepacing::vsyncFor(m));
    applyPacing();
}

void EngineRenderDriver::setRefreshHz(double hz)
{
    if (!(hz > 0.0)) hz = 0.0;                  // NaN-safe: anything unusable is "unknown"
    if (qFuzzyCompare(hz + 1.0, mRefreshHz + 1.0)) return;
    mRefreshHz = hz;
    applyPacing();
}

void EngineRenderDriver::applyPacing()
{
    const int want = pacedIntervalMs();
    if (mTimer->isActive() && mTimer->interval() != want) mTimer->start(want);
    else mTimer->setInterval(want);
    emit pacingChanged();
}
