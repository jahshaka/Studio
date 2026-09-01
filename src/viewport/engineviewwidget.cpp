#include "viewport/engineviewwidget.h"
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

EngineViewWidget::~EngineViewWidget() { destroyView(); }

bool EngineViewWidget::createView(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                                  const QString &name,
                                  const jahshaka::engine::Colour &background)
{
    if (!engine || mView) return false;
    mEngine = engine;
    mView = engine->createView(name.toStdString(),
                               static_cast<jahshaka::engine::NativeWindowHandle>(winId()),
                               static_cast<unsigned>(width()),
                               static_cast<unsigned>(height()), background);
#ifndef Q_OS_LINUX
    // No native on-screen window backend on this platform yet (DOCS/HANDOFF.md
    // §7): render the view offscreen so the editor, selftest and scripting all
    // still run — the widget area itself stays blank until a real presenter
    // (macOS: CAMetalLayer + VK_EXT_metal_surface) exists.
    if (!mView)
        mView = engine->createOffscreenView(name.toStdString(),
                                            static_cast<unsigned>(width()),
                                            static_cast<unsigned>(height()), background);
#endif
    if (mView) {
        // From here the engine owns this region entirely; Qt updates would fight it.
        setUpdatesEnabled(false);
        mView->setEnabled(isVisible());
    }
    return mView != nullptr;
}

void EngineViewWidget::destroyView()
{
    if (!mView) return;
    // If the Engine is already gone it took the View with it: nothing to release.
    if (auto engine = mEngine.lock()) engine->destroyView(mView);
    mView = nullptr;
    mEngine.reset();
}

void EngineViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (mView) mView->resize(static_cast<unsigned>(width()), static_cast<unsigned>(height()));
}

void EngineViewWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (mView) mView->setEnabled(true);
}

void EngineViewWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (mView) mView->setEnabled(false);
}
