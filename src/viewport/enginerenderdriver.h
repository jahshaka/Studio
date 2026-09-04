#ifndef ENGINERENDERDRIVER_H
#define ENGINERENDERDRIVER_H

// The ONE render loop for the engine.
//
// Owned wherever the Engine is owned. A single QTimer calls Engine::renderOneFrame(),
// which draws every enabled View. Widgets never own a timer: two widgets with two
// timers would render every window twice per tick (the audit's finding). Hidden
// viewports opt out with View::setEnabled(false), not by stopping this.
//
// Runs on the thread that owns the Engine (its thread-affinity rule).
#include <QObject>
#include "jahshaka/engine/Engine.h"

class QTimer;

class EngineRenderDriver : public QObject
{
    Q_OBJECT
public:
    /// What the loop has actually been doing — the numbers behind `app.frameStats()`
    /// and the frame-pacing gate. Cumulative for the life of the driver.
    struct Stats {
        qulonglong ticks    = 0;   ///< timer fires
        qulonglong rendered = 0;   ///< ticks that called Engine::renderOneFrame
        qulonglong skipped  = 0;   ///< ticks that had no enabled View to draw

        // ---- how long the ticks actually TOOK (STATS_OVERLAY_SPEC §4) -------
        // THE HONEST NUMBER, and the reason this pair exists. An FPS reading on
        // this architecture measures the 16 ms QTimer below, not the renderer:
        // a scene that got twice as expensive but still fits in the budget
        // still reads ~62 fps. What diagnoses anything is how much of each tick
        // the frame ate — which was already measured here and thrown away
        // unless it crossed the 100 ms hitch threshold.
        /// Rolling average of the last kWorkWindow rendered ticks, ms. 0 until
        /// the first one. Deliberately a SHORT window: this is a live readout,
        /// and an all-time average would never move again after a bad open.
        double     workMs   = 0.0;
        /// The worst rendered tick since the driver started, ms.
        double     worstMs  = 0.0;
        /// Rendered ticks that crossed the hitch threshold (100 ms) — the same
        /// ones that log `[open-profile] slow frame` and land in the
        /// LoadTimeline as `frame:slow`. Cumulative, like ticks/rendered.
        qulonglong slowFrames = 0;
    };

    explicit EngineRenderDriver(jahshaka::engine::Engine *engine, QObject *parent = nullptr);

    void start(int intervalMs = 16);
    void stop();
    bool isRunning() const;
    int  intervalMs() const;

    Stats stats() const { return mStats; }

signals:
    /// Emitted before each frame — animate here.
    void beforeFrame();

private:
    /// How many rendered ticks the rolling work-ms average covers. About a
    /// second at the 16 ms interval — long enough to be readable, short enough
    /// that the number still reacts to what the app is doing right now.
    static constexpr int kWorkWindow = 60;

    jahshaka::engine::Engine *mEngine;
    QTimer *mTimer;
    Stats   mStats;
    /// Ring of the last kWorkWindow rendered ticks' durations, ms.
    double  mWork[kWorkWindow] = {};
    int     mWorkNext = 0;
    int     mWorkFilled = 0;
};

#endif // ENGINERENDERDRIVER_H
