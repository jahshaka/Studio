#include "engineviewwidget.h"
#include <QTimer>
#include <QResizeEvent>

EngineViewWidget::EngineViewWidget(QWidget *parent) : QWidget(parent)
{
    // The engine draws into this widget's own native window.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMinimumSize(160, 120);
}

EngineViewWidget::~EngineViewWidget() { stopRendering(); }

bool EngineViewWidget::attach(jahshaka::engine::Engine *engine,
                              jahshaka::engine::Scene *scene,
                              const QString &name,
                              const jahshaka::engine::Colour &background)
{
    if (!engine || !scene) return false;
    mEngine = engine;
    mView = engine->createView(name.toStdString(), scene,
                               static_cast<jahshaka::engine::NativeWindowHandle>(winId()),
                               static_cast<unsigned>(width()),
                               static_cast<unsigned>(height()), background);
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
