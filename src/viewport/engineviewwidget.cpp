#include "viewport/engineviewwidget.h"
#include <QGuiApplication>
#include <QResizeEvent>

EngineViewWidget::EngineViewWidget(QWidget *parent) : QWidget(parent)
{
    // The engine draws into this widget's own native window.
    setAttribute(Qt::WA_NativeWindow);
#ifdef Q_OS_LINUX
    // Qt documents WA_PaintOnScreen as X11-only, and it is what stops Qt painting
    // over the engine's xcb window. On cocoa QWidget::paintEngine() is already null
    // and the engine composites through a CAMetalLayer sublayer, so setting it there
    // would be misleading noise.
    setAttribute(Qt::WA_PaintOnScreen);
#endif
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
    bool handleIsNative = true;
#ifdef Q_OS_MACOS
    // winId() is only an NSView* under the cocoa plugin. The offscreen/minimal
    // plugins (every --headless run) hand out a synthetic WId, and sending an
    // Objective-C message to that integer crashes the process — so on anything
    // but cocoa go straight to the offscreen fallback below.
    handleIsNative = QGuiApplication::platformName() == QLatin1String("cocoa");
#endif
    if (handleIsNative)
        mView = engine->createView(name.toStdString(),
                                   static_cast<jahshaka::engine::NativeWindowHandle>(winId()),
                                   static_cast<unsigned>(width()),
                                   static_cast<unsigned>(height()), background);
#ifndef Q_OS_LINUX
    // Fallback, not the normal path: if the platform has no on-screen backend, or
    // the handle is not one it can present to (an offscreen QPA plugin hands out no
    // NSView), render the view offscreen so the editor, selftest and scripting all
    // still run — the widget area then stays blank.
    if (!mView)
        mView = engine->createOffscreenView(name.toStdString(),
                                            static_cast<unsigned>(width()),
                                            static_cast<unsigned>(height()), background);
#endif
    if (mView) {
#ifdef Q_OS_LINUX
        // From here the engine owns this region entirely; Qt updates would fight it.
        // Only on X11: on cocoa the engine's CAMetalLayer is a sublayer composited
        // ABOVE the QNSView's own content, so Qt repaints underneath are invisible
        // rather than a fight — and disabling updates would also freeze any Qt-drawn
        // child of this widget.
        setUpdatesEnabled(false);
#endif
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
