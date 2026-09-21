#include "viewport/enginerenderdriver.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

#include "services/engineerrorpump.h"
#include "services/framemonitor.h"
#include "services/jahlog.h"
#include "services/loadtimeline.h"
#include "viewport/devicelossend.h"

namespace {
/// The stage names the slow-frame line uses (OPEN_COVER_SPEC §7). The engine's
/// own words for the five stages of a world's first lighting arm — the point of
/// the line is that a reader can look the stage up, so these are not prettified.
const char *giArmStageName(jahshaka::engine::GiArmStage s)
{
    using S = jahshaka::engine::GiArmStage;
    switch (s) {
    case S::ProbeScout:  return "probe scout";
    case S::ProbeFit:    return "probe fit";
    case S::ProbeFinish: return "probe finish";
    case S::Field:       return "irradiance field";
    case S::None:        break;
    }
    return "cascades";
}
}   // namespace
/// A frame this long is a visible hitch, not a frame.
static const double kSlowFrameMs = 100.0;

EngineRenderDriver::EngineRenderDriver(jahshaka::engine::Engine *engine, QObject *parent)
    : QObject(parent), mEngine(engine), mTimer(new QTimer(this))
{
    // The clock the rolling windows are stamped from. Started before the first
    // tick can fire, so no stamp is ever taken from an invalid timer.
    mSinceStart.start();
    // PRECISE, because the interval now carries meaning. Qt's default coarse
    // timer is allowed to drift by 5% and to coalesce with other timers, which
    // on a 10 ms budget (a 100 Hz panel) is a whole millisecond of slack handed
    // to a loop whose entire job is to beat slightly faster than the display.
    mTimer->setTimerType(Qt::PreciseTimer);
    connect(mTimer, &QTimer::timeout, this, [this] {
        // A frame is UI-THREAD work: whatever it costs, the window is not
        // answering while it runs. The first frame of a freshly opened world is
        // still the expensive one — the Hlms compiles a shader variant per new
        // material/pass combination — but it is no longer expensive for the
        // reason this comment used to give. It claimed "no cache anywhere in
        // the pin (lane-openasync 2026-09-03: no HlmsDiskCache, no microcode
        // cache, no VkPipelineCache persistence is wired)", and shader-cache v2
        // wired all three (irisgl/engine/src/OgreShaderCache.cpp); a warm launch
        // loads them instead of compiling. What is left on a COLD launch, or
        // after an app update, a driver update or a Clear Cache, is real work
        // that has to happen somewhere, and this is where it lands.
        //
        // Since the threading program it is also THREADED work: mode 2 compiles
        // shader variants across the scene's worker pool during the frame
        // (THREADING_ADOPTION_SPEC.md P1), and textures are streamed and
        // collected once at the head of the frame instead of one blocking wait
        // per texture (P2). Slow frames are logged and, while a scene open is
        // being measured, banked in the ledger, so "opening is still slow"
        // always has a number attached to it.
        // A SCRIPT RUN WITH THE Off POLICY OWNS THE LOOP (setScriptRun).
        // Nothing at all happens here for the length of that run: the viewport
        // holds its last picture, no time passes for the document, and a
        // script's editor.frame(n, dt) renders exactly the n frames it asked
        // for. The tick is not even counted — app.frameStats() must read the
        // same before and after, as it did when the run simply blocked this
        // thread.
        if (mScriptRun == ScriptRun::Off) return;
        // A LIVE RUN IS PACED BY TIME (round 2, H1). The worker can only post
        // its next verb after the previous one returns, and an overdue timer
        // always wins that gap — so unpaced, the loop and the script alternate
        // one frame per verb and a trivial verb costs a whole frame (measured:
        // 8.1 ms instead of 30 us). At most one frame per display period while
        // a live run is in flight, measured from the END of the last rendered
        // tick. The period is the DISPLAY's, deliberately, not
        // pacedIntervalMs(): under Unlimited pacing that is 0 and would pace
        // nothing at all.
        if (mScriptRun == ScriptRun::Live && mSinceFrameEnd.isValid()
            && mSinceFrameEnd.elapsed() < scriptPacePeriodMs())
            return;
        QElapsedTimer frame;
        frame.start();
        ++mStats.ticks;
        // THE SESSION'S PACING, RECONCILED (F5). One branch when no session has
        // ever run; see syncVrPacing.
        if (mVrSession) syncVrPacing();
        // THE RENDER-LOOP MONITOR (RENDER_LOOP_MONITOR_SPEC §4.2), and it costs
        // one not-taken branch when no capture is running. The tick's TOP is
        // where the gap since the previous tick is closed and split into idle
        // (blocked in the event loop) and UI (the thread was busy with
        // something that was not a frame) — the difference between "15 fps that
        // feels like 60" and a real stall.
        // The gap is pushed only for ticks that will actually draw. The flag is
        // read HERE, before beforeFrame, rather than reusing `anythingToDraw`
        // below: this call has to happen before the host's own sync stages so
        // the stage list reads in order. The one frame they can disagree on is
        // the first after a viewport is shown by beforeFrame itself, which
        // loses one gap stage and nothing else.
        // SHORT-CIRCUITED ON THE FLAG FIRST: with no capture running this must
        // not even ask the engine (hasEnabledViews is a virtual call, and the
        // tick already makes that call once below — "free when off" means the
        // off path is unchanged, not merely cheap).
        FrameMonitor::instance().noteTickStart(framemonitor::active() && mEngine
                                               && mEngine->hasEnabledViews());
        if (framemonitor::active() && mEngine)
            mEngine->setNextFrameCause(jahshaka::engine::FrameCause::Driver);
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
        // WHAT THIS FRAME IS ABOUT TO SPEND, read before it spends it
        // (OPEN_COVER_SPEC §7, lane OPEN-COVER-2b). Two counters, so a slow
        // frame can name its own cause instead of guessing — see the warning
        // at the bottom of this tick. Free when nothing is arriving: the whole
        // read is a short loop over the live scenes and two atomics.
        jahshaka::engine::StreamingWork owedBefore;
        if (mEngine) owedBefore = mEngine->streamingWork();
        const bool anythingToDraw = mEngine && mEngine->hasEnabledViews();
        // THE LIVE STATE, not a lifetime counter (owner review answer Q3): the
        // tick that draws nothing no longer increments anything at all — it
        // simply says so, and `ticks - rendered` is still there for a caller
        // who wants the total.
        mStats.drawing = anythingToDraw;
        if (anythingToDraw) {
            mEngine->renderOneFrame();
            ++mStats.rendered;
            mDrawn.add(nowMs());
        }

        // THE GPU IS GONE: SAY SO AND END, NEVER FREEZE (lane XID-2, 2026-09-17).
        // The one render loop is the one place that can notice. After a device
        // loss the renderer vetoes every frame (ogre-patch 0072), so without this
        // the window simply stops updating, for ever, with the UI still alive —
        // exactly what the owner saw and reported as "the app froze".
        //
        // AND THE PROCESS ENDS WITHOUT DESTRUCTORS. `vkDestroyDevice` on a device
        // whose channel the driver has not reclaimed does not return (measured:
        // a 100 %-of-a-core spin inside libnvidia-glcore, still spinning ten
        // minutes later), so an orderly quit through ~Engine IS the freeze. The
        // message goes out first, then `_exit`.
        devicelossend::checkAfterFrame(mEngine);
        // The Live pacing clock, restarted from the END of the frame (see the
        // header). Unconditional and two instructions, so the no-script loop
        // reads exactly as it did.
        mSinceFrameEnd.restart();
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
        // The monitor drains the engine's ring here, at the one point in the
        // process where a frame has just finished, and re-arms the gap clock —
        // only for ticks that actually rendered (a skipped tick keeps the clock
        // running, so an absence arrives as one gap instead of none).
        FrameMonitor::instance().noteTickEnd(anythingToDraw);
        if (ms >= kSlowFrameMs) {
            ++mStats.slowFrames;
            // …and into the rolling minute, which is what the readout shows.
            mSlow.add(nowMs());
            LoadTimeline::add(QStringLiteral("frame:slow"), ms);
            // NAME THE CAUSE, DO NOT GUESS IT (OPEN_COVER_SPEC §7). This line
            // used to say "shader/PSO compilation is the usual cause on the
            // first frame of a world" on EVERY slow frame — and OPEN-COVER-2a
            // measured that it is usually wrong: on Grand Showroom 2 the first
            // frame's 1,025 ms was 673 ms of PROBE GRID and 57 of cascades,
            // with the compiles a distant third. The frame knows which it was:
            // a streaming frame spends exactly one stage of the lighting arm,
            // and the shader counter says whether anything was compiled.
            QString cause;
            if (mEngine) {
                const jahshaka::engine::StreamingWork after = mEngine->streamingWork();
                const unsigned compiled = after.shadersCompiled >= owedBefore.shadersCompiled
                                              ? after.shadersCompiled - owedBefore.shadersCompiled
                                              : 0u;
                // The arm's stage is named by what the frame STARTED on: the
                // machine has already advanced by the time we read it again.
                if (after.giStagesLeft != owedBefore.giStagesLeft &&
                    owedBefore.giStage != jahshaka::engine::GiArmStage::None) {
                    cause = QStringLiteral("the world's first lighting arm, stage %1")
                                .arg(giArmStageName(owedBefore.giStage));
                    if (compiled)
                        cause += QStringLiteral(" (and %1 shader compilation(s))").arg(compiled);
                } else if (compiled) {
                    cause = QStringLiteral("%1 shader/PSO compilation(s) in this frame")
                                .arg(compiled);
                }
            }
            if (cause.isEmpty())
                cause = QStringLiteral("no first-time work in this frame — neither the "
                                       "lighting arm nor a shader compilation");
            qWarning("[open-profile] slow frame: %.1f ms (%s)", ms, qUtf8Printable(cause));
        }
    });
}

void EngineRenderDriver::resetWorst()
{
    mStats.worstMs = 0.0;
    mStats.slowFrames = 0;
}

EngineRenderDriver::Stats EngineRenderDriver::stats() const
{
    Stats out = mStats;
    // THE TWO ROLLING NUMBERS ARE EVALUATED HERE, at the read, and that is the
    // whole point of a window: a loop that stopped drawing a minute ago reports
    // 0 fps drawn the moment somebody asks, without a tick having to come along
    // to zero a counter.
    const double now = nowMs();
    out.fpsDrawn = mDrawn.perSecond(now);
    out.slowFramesLastMinute = mSlow.count(now);
    return out;
}

void EngineRenderDriver::noteExternalFrame()
{
    mSinceFrameEnd.restart();
    mDrawn.add(nowMs());
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

void EngineRenderDriver::syncVrPacing()
{
    if (!mEngine) return;
    const jahshaka::engine::VrStatus st = mEngine->vrStatus();
    if (!st.active) {
        // THE ENGINE ENDED IT WITHOUT US. A runtime that went away or a device
        // that was lost ends the session inside the frame (OgreEngine::
        // renderOneFrame), and nothing tells the host; left alone this loop
        // would keep a zero interval and vsync off against a pump that no
        // longer exists. `setVrSessionActive(false)` is the SAME call the host
        // makes on `vr.end()`, so the restore happens in exactly one place.
        setVrSessionActive(false);
        return;
    }
    // A LIVE SESSION ONLY BLOCKS BETWEEN READY AND FOCUSED. Outside that —
    // before the runtime is ready, while it is stopping, while it is lost —
    // xrWaitFrame returns at once and a zero interval becomes a spin at
    // whatever the CPU can manage. The user's own pacing mode answers for
    // those frames; vsync is deliberately left off for the whole session
    // (flipping it is a swapchain rebuild per transition, which is worse than
    // the transition).
    const bool pumping = st.state == jahshaka::engine::VrState::Ready ||
                         st.state == jahshaka::engine::VrState::Synchronized ||
                         st.state == jahshaka::engine::VrState::Visible ||
                         st.state == jahshaka::engine::VrState::Focused;
    if (pumping == mVrPumping) return;
    mVrPumping = pumping;
    applyPacing();
}

void EngineRenderDriver::setVrSessionActive(bool on)
{
    if (mVrSession == on) return;
    mVrSession = on;
    // A session begins in Idle and reaches Ready a frame or two later; assuming
    // it pumps from the first tick is what lets the very first xrWaitFrame be
    // the clock instead of racing a timer.
    mVrPumping = on;
    // The same price the Unlimited mode pays, for the same reason and with the
    // same one-swapchain-rebuild cost (Engine::setVsync): with the display
    // still gating the acquire, a zero interval buys nothing but a busier CPU.
    // On the way out the user's own pacing mode decides again.
    if (mEngine) mEngine->setVsync(on ? false : framepacing::vsyncFor(mMode));
    applyPacing();
}

void EngineRenderDriver::setRefreshHz(double hz)
{
    if (!(hz > 0.0)) hz = 0.0;                  // NaN-safe: anything unusable is "unknown"
    if (qFuzzyCompare(hz + 1.0, mRefreshHz + 1.0)) return;
    mRefreshHz = hz;
    applyPacing();
}

double EngineRenderDriver::scriptPacePeriodMs() const
{
    // The DISPLAY's period, not the loop's interval: pacedIntervalMs() is 0
    // under Unlimited pacing (vsync off, beat as fast as you can), and a live
    // script paced to 0 ms is not paced.
    return mRefreshHz > 0.0 ? 1000.0 / mRefreshHz : 16.7;
}

void EngineRenderDriver::applyPacing()
{
    const int want = pacedIntervalMs();
    if (mTimer->isActive() && mTimer->interval() != want) mTimer->start(want);
    else mTimer->setInterval(want);
    emit pacingChanged();
}
