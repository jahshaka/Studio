#include "viewport/enginerenderdriver.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>

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
        emit beforeFrame();
        if (mEngine) mEngine->renderOneFrame();
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
