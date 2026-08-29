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
    explicit EngineRenderDriver(jahshaka::engine::Engine *engine, QObject *parent = nullptr);

    void start(int intervalMs = 16);
    void stop();
    bool isRunning() const;

signals:
    /// Emitted before each frame — animate here.
    void beforeFrame();

private:
    jahshaka::engine::Engine *mEngine;
    QTimer *mTimer;
};

#endif // ENGINERENDERDRIVER_H
