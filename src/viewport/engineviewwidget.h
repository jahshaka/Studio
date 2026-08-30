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

private:
    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View                 *mView = nullptr;
};

#endif // ENGINEVIEWWIDGET_H
