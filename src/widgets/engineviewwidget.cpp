#include "engineviewwidget.h"
#include <QTimer>
#include <QResizeEvent>

EngineViewWidget::EngineViewWidget(QWidget *parent) : QWidget(parent)
{
    // The engine draws into this widget's own native window.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    // Tell Qt this widget paints every pixel itself, so it neither erases the
    // background nor composites anything underneath into it.
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setMinimumSize(160, 120);
}

EngineViewWidget::~EngineViewWidget() { stopRendering(); }

bool EngineViewWidget::createView(jahshaka::engine::Engine *engine,
                                  const QString &name,
                                  const jahshaka::engine::Colour &background)
{
    if (!engine) return false;
    mEngine = engine;
    mView = engine->createView(name.toStdString(),
                               static_cast<jahshaka::engine::NativeWindowHandle>(winId()),
                               static_cast<unsigned>(width()),
                               static_cast<unsigned>(height()), background);
    if (mView) {
        // From here the engine owns this region entirely; Qt updates would fight it.
        setUpdatesEnabled(false);
    }
    return mView != nullptr;
}

void EngineViewWidget::startRendering(int intervalMs)
{
    if (!mEngine) return;
    if (!mTimer) {
        mTimer = new QTimer(this);
        connect(mTimer, &QTimer::timeout, this, [this] { mEngine->renderOneFrame(); });
    }
    mTimer->start(intervalMs);
}

void EngineViewWidget::stopRendering()
{
    if (mTimer) mTimer->stop();
}

void EngineViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (mView) mView->resize(static_cast<unsigned>(width()), static_cast<unsigned>(height()));
}
