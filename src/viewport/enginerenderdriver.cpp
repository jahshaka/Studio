#include "viewport/enginerenderdriver.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

#include "services/engineerrorpump.h"
#include "services/loadtimeline.h"

/// A frame this long is a visible hitch, not a frame.
static const double kSlowFrameMs = 100.0;

EngineRenderDriver::EngineRenderDriver(jahshaka::engine::Engine *engine, QObject *parent)
    : QObject(parent), mEngine(engine), mTimer(new QTimer(this))
{
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
        // present-queue slot, 62 times a second, to produce nothing (deep audit
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
        const bool anythingToDraw = mEngine && mEngine->hasEnabledViews();
        if (anythingToDraw) { mEngine->renderOneFrame(); ++mStats.rendered; }
        else                { ++mStats.skipped; }
        // Whatever the frame (or anything else since the last one — the sink is
        // process-wide) refused to do, said so in the engine's error string and
        // nowhere else. Drain it here, where the one render loop lives, so a
        // silent failure becomes a [warn] instead of a wrong picture
        // (services/engineerrorpump.h). Drained on SKIPPED ticks too: the sink
        // is process-wide, so an off-loop failure (a thumbnail render, a
        // scripted verb) must not sit silent just because no viewport is up.
        EngineErrorPump::instance().drain(mEngine);
        const double ms = double(frame.nsecsElapsed()) / 1.0e6;
        if (ms >= kSlowFrameMs) {
            LoadTimeline::add(QStringLiteral("frame:slow"), ms);
            qWarning("[open-profile] slow frame: %.1f ms (shader/PSO compilation is the "
                     "usual cause on the first frame of a world)", ms);
        }
    });
}

void EngineRenderDriver::start(int intervalMs) { if (mEngine) mTimer->start(intervalMs); }
void EngineRenderDriver::stop()                { mTimer->stop(); }
bool EngineRenderDriver::isRunning() const     { return mTimer->isActive(); }
int  EngineRenderDriver::intervalMs() const    { return mTimer->interval(); }
