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
    jahshaka::engine::Engine *mEngine;
    QTimer *mTimer;
    Stats   mStats;
};

#endif // ENGINERENDERDRIVER_H
