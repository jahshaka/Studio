#ifndef ENGINEVIEWWIDGET_H
#define ENGINEVIEWWIDGET_H

// A Qt widget that hosts an engine View.
//
// Includes ONLY the engine abstraction — no Ogre, no GL. Qt owns the event loop
// and drives the engine via a timer; the engine renders into this widget's native
// window. Because Qt never creates a GL context, the Wayland/xcb context problem
// that pinned the old renderer does not arise here.
#include <QWidget>
#include <memory>
#include "jahshaka/engine/Engine.h"

class QTimer;

class EngineViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EngineViewWidget(QWidget *parent = nullptr);
    ~EngineViewWidget() override;

    /// Binds this widget to a View on `scene`. Must be called after the widget is shown
    /// so a native window id exists.
    /// Creates the View bound to this widget's native window. Must happen BEFORE
    /// any Scene is created — see Engine.h on ordering. Attach a Scene afterwards
    /// with view()->setScene().
    bool createView(jahshaka::engine::Engine *engine,
                    const QString &name,
                    const jahshaka::engine::Colour &background =
                        jahshaka::engine::Colour(0.10f, 0.11f, 0.14f));

    jahshaka::engine::View *view() const { return mView; }

    /// Qt must not paint here — the engine owns these pixels.
    QPaintEngine *paintEngine() const override { return nullptr; }

protected:
    /// Deliberately empty. Together with WA_OpaquePaintEvent this stops Qt
    /// erasing or repainting the region between the engine's presents, which
    /// otherwise shows as heavy flicker.
    void paintEvent(QPaintEvent *) override {}

public:

    void startRendering(int intervalMs = 16);
    void stopRendering();

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    jahshaka::engine::Engine *mEngine = nullptr;
    jahshaka::engine::View   *mView   = nullptr;
    QTimer                   *mTimer  = nullptr;
};

#endif // ENGINEVIEWWIDGET_H
