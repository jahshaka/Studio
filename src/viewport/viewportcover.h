#ifndef VIEWPORTCOVER_H
#define VIEWPORTCOVER_H

// ViewportCover — the viewport's standing "nothing is being presented" state.
//
// THE DEFECT IT EXISTS FOR. The engine viewport is a WA_PaintOnScreen native
// window Qt never paints (engineviewwidget.h). Until the engine presents its
// first frame into that window, the X server has nothing of ours to show there
// and leaves whatever pixels were on that part of the screen before — which,
// the instant a scene open switches from the desktop page to the editor, is a
// perfect copy of the desktop page. It stays until the first present, seconds
// later on a heavy world. Clearing the window server-side does not fix it (Qt's
// backing store flushes the outgoing page back over the region), and toggling
// WA_PaintOnScreen at runtime is out of the question — it can recreate the very
// native window the engine's View is bound to.
//
// THE SHAPE. This widget is an ordinary Qt-painted widget that sits in the SAME
// layout cell as the viewport and, while it is up, owns its own native window
// stacked above the viewport's. That is deliberate and is the only arrangement
// that works on X11: a non-native sibling is painted into the top-level's
// backing store, which a native child window covers unconditionally. Being a
// sibling (never a child of the viewport) also keeps it out of the viewport's
// setUpdatesEnabled(false) subtree — a child could never repaint.
//
// It is a FIRST-CLASS STATE, not a load-time shim: whenever no engine frame is
// being presented for the viewport, this is what the viewport looks like. That
// covers the loading window today and the dual-screen layouts (viewport visible
// while a world loads, viewport visible with no world open) later.
//
// The steady state is untouched: once the engine presents, the cover hides, its
// native window unmaps, and the viewport's paint contract (WA_PaintOnScreen +
// null paintEngine + WA_OpaquePaintEvent + empty paintEvent +
// setUpdatesEnabled(false) + vsync) is exactly what it always was.
#include <QColor>
#include <QString>
#include <QWidget>

class ViewportCover : public QWidget
{
    Q_OBJECT
public:
    enum class State {
        Presenting,   ///< the engine owns these pixels — the cover is hidden
        Loading,      ///< a world is being loaded/pushed; no frame presented yet
        NoScene       ///< no world is open in this viewport
    };

    explicit ViewportCover(QWidget *parent = nullptr);

    State state() const { return mState; }
    /// Switches state. Presenting hides the cover; the other two show it.
    /// Idempotent — re-setting the current state does nothing at all, so this
    /// is safe to call every frame.
    void setState(State state);

    /// The world's name, shown under the title while loading. Empty is fine.
    void setSubtitle(const QString &subtitle);

    /// Shows the cover in `state` AND paints it before returning. The editor's
    /// scene-open path runs entirely inside one event-loop turn: without a
    /// synchronous paint the grey would only appear after the load it exists to
    /// cover. Does nothing when the cover is not visible on screen (a hidden
    /// page has nothing to paint).
    void showNow(State state);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    /// Gives the cover its own native window and stacks it above the viewport's.
    /// Deferred to the first show: an editor session that never covers anything
    /// never creates the extra window.
    void ensureAboveViewport();

    State   mState = State::NoScene;
    QString mSubtitle;
    /// The cover's surface colour — a deliberate dark grey in the editor
    /// theme's family (see the constructor for why it does not try to match
    /// the render).
    QColor  mBackground;
};

#endif // VIEWPORTCOVER_H
