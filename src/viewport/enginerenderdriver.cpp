#include "viewport/enginerenderdriver.h"
#include <QTimer>

EngineRenderDriver::EngineRenderDriver(jahshaka::engine::Engine *engine, QObject *parent)
    : QObject(parent), mEngine(engine), mTimer(new QTimer(this))
{
    connect(mTimer, &QTimer::timeout, this, [this] {
        emit beforeFrame();
        if (mEngine) mEngine->renderOneFrame();
    });
}

void EngineRenderDriver::start(int intervalMs) { if (mEngine) mTimer->start(intervalMs); }
void EngineRenderDriver::stop()                { mTimer->stop(); }
bool EngineRenderDriver::isRunning() const     { return mTimer->isActive(); }
