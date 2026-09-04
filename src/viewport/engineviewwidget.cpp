#include "viewport/engineviewwidget.h"
#include <QEvent>
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
    mViewName = name;
    mBackground = background;
    mCreateError.clear();
    bool handleIsNative = true;
#ifdef Q_OS_MACOS
    // winId() is only an NSView* under the cocoa plugin. The offscreen/minimal
    // plugins (every --headless run) hand out a synthetic WId, and sending an
    // Objective-C message to that integer crashes the process — so on anything
    // but cocoa go straight to the offscreen fallback below.
    handleIsNative = QGuiApplication::platformName() == QLatin1String("cocoa");
#endif
    if (handleIsNative) {
        mView = engine->createView(name.toStdString(),
                                   static_cast<jahshaka::engine::NativeWindowHandle>(winId()),
                                   static_cast<unsigned>(width()),
                                   static_cast<unsigned>(height()), background);
        if (!mView) mCreateError = QString::fromStdString(engine->lastError());
    }
    // Fallback, not the normal path: if the platform has no on-screen backend, or
    // the handle is not one it can present to (an offscreen QPA plugin hands out no
    // NSView), render the view offscreen so the editor, selftest and scripting all
    // still run — the widget area then stays blank.
    //
    // LINUX TAKES IT TOO (deep audit area 7). It used to be #ifndef Q_OS_LINUX, on
    // the reasoning that X11 always has a window backend. But createView can still
    // fail there — a bad handle, a Vulkan surface the driver refuses, an Hlms media
    // directory that never resolved — and the result was the worst possible one: no
    // view, so no rendering, no screenshots, no scripting and no message, forever
    // and silently. Falling back keeps everything except the on-screen pixels
    // working, and mCreateError (shown on the viewport's cover) says why.
    if (!mView)
        mView = engine->createOffscreenView(name.toStdString(),
                                            static_cast<unsigned>(width()),
                                            static_cast<unsigned>(height()), background);
    if (mView) {
#ifdef Q_OS_LINUX
        // From here the engine owns this region entirely; Qt updates would fight it.
        // Only on X11: on cocoa the engine's CAMetalLayer is a sublayer composited
        // ABOVE the QNSView's own content, so Qt repaints underneath are invisible
        // rather than a fight — and disabling updates would also freeze any Qt-drawn
        // child of this widget. And only when the view really presents HERE: an
        // offscreen fallback owns no pixels of this widget, so muting Qt over it
        // would only guarantee stale ones.
        if (!mView->isOffscreen()) setUpdatesEnabled(false);
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

bool EngineViewWidget::event(QEvent *e)
{
    // Qt has given this widget a DIFFERENT native window than the one the View
    // was built on (a reparent, a floatable dock being torn off or re-docked,
    // or any platform that recreates handles). The old one is not ours any more
    // and a Vulkan surface cannot be moved, so rebuild.
    if (e->type() == QEvent::WinIdChange) recreateViewForNewWindow();
    return QWidget::event(e);
}

void EngineViewWidget::recreateViewForNewWindow()
{
    // Nothing bound yet (this fires once for the FIRST native window too, long
    // before createView runs), or the Engine is already gone: nothing to do.
    if (!mView) return;
    auto engine = mEngine.lock();
    if (!engine) return;
    // An offscreen fallback view has no native window and does not care.
    if (mView->isOffscreen()) return;
    // WinIdChange is ALSO how Qt announces the window going away (destroy() sets
    // the id to 0 and posts it). internalWinId(), unlike winId(), never creates
    // one — so this is the difference between "moved to a new window" and "is
    // being torn down", and rebuilding on a null handle would replace a working
    // view with an offscreen fallback on the way out.
    if (!internalWinId()) return;

    const QString name = mViewName;
    const jahshaka::engine::Colour background = mBackground;
    destroyView();
    createView(engine, name, background);
    if (mView) mView->setEnabled(isVisible());
    viewRecreated();
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
