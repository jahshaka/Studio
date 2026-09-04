#ifndef ENGINEVIEWWIDGET_H
#define ENGINEVIEWWIDGET_H

// A Qt widget that hosts an engine View.
//
// Includes ONLY the engine abstraction — no Ogre, no GL. The engine renders into
// this widget's native window; the frame loop lives in EngineRenderDriver, never
// here. Because Qt never creates a GL context, the Wayland/xcb context problem
// that pinned the old renderer does not arise here.
//
// LIFETIME CONTRACT. The View belongs to the Engine and dies with it. The widget
// holds the Engine through a weak_ptr so it can never touch a dead Engine: if the
// Engine is still alive when the widget goes, the widget destroys its View; if the
// Engine went first, the View is already gone and nothing is touched. Owners
// should still destroy their EngineViewWidgets BEFORE the Engine (see
// OgrePreviewDialog) so teardown is deterministic rather than merely safe.
#include <QWidget>
#include <memory>
#include "jahshaka/engine/Engine.h"

class EngineViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EngineViewWidget(QWidget *parent = nullptr);
    ~EngineViewWidget() override;

    /// Creates the View bound to this widget's native window. Must be called after
    /// the widget is shown so a native window id exists, and BEFORE any Scene is
    /// created — see Engine.h on ordering. Attach a Scene with view()->setScene().
    bool createView(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                    const QString &name,
                    const jahshaka::engine::Colour &background =
                        jahshaka::engine::Colour(0.10f, 0.11f, 0.14f));
    /// Releases the View now (idempotent). The destructor does this too.
    void destroyView();

    jahshaka::engine::View *view() const { return mView; }

    /// Empty unless the ON-SCREEN view could not be created and this widget fell
    /// back to an offscreen one: then it carries the engine's own reason. The
    /// widget area is blank in that state and the engine renders into a texture,
    /// so nothing else would ever tell anybody — hosts show this (the editor
    /// viewport puts it on its cover).
    QString viewCreationError() const { return mCreateError; }

    /// Qt must not paint here — the engine owns these pixels.
    QPaintEngine *paintEngine() const override { return nullptr; }

protected:
    /// Deliberately empty. Together with WA_OpaquePaintEvent this stops Qt
    /// erasing or repainting the region between the engine's presents, which
    /// otherwise shows as heavy flicker.
    void paintEvent(QPaintEvent *) override {}
    void resizeEvent(QResizeEvent *) override;
    /// Hidden viewports stop rendering (View::setEnabled) instead of burning a frame.
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    /// QEvent::WinIdChange only — see recreateViewForNewWindow().
    bool event(QEvent *) override;

    /// Called right after the View has been rebuilt on a NEW native window.
    /// The base class can only restore what it created (size, enabled state);
    /// anything a subclass attached — the Scene above all — is gone with the old
    /// View and has to be re-attached here. Default: nothing.
    virtual void viewRecreated() {}

    /// Destroys the View and builds a new one on this widget's current native
    /// window, then calls viewRecreated().
    ///
    /// WHY IT EXISTS. The View is bound to the native window handle that
    /// existed when createView() ran. Qt is entitled to destroy and re-create
    /// that window under us — reparenting a native widget does it, and the
    /// Materials Display dock is floatable, so a live engine view really can be
    /// torn off and re-docked. On X11 the old handle is then an invalid drawable
    /// (the swapchain presents into nothing); on Windows the HWND genuinely
    /// changes. Qt tells us with QEvent::WinIdChange and this is the only honest
    /// answer, because a Vulkan surface cannot be re-pointed at another window.
    void recreateViewForNewWindow();

private:
    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View                 *mView = nullptr;
    /// What createView() was called with, so a WinIdChange can repeat it.
    QString                                 mViewName;
    jahshaka::engine::Colour                mBackground{0.10f, 0.11f, 0.14f};
    QString                                 mCreateError;
};

#endif // ENGINEVIEWWIDGET_H
