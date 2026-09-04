#include "viewport/viewportcover.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QWindow>

ViewportCover::ViewportCover(QWidget *parent)
    : QWidget(parent),
      // A deliberate, theme-family grey: just above the editor's panel greys
      // (30-48 on the dark theme, measured), faintly cooler, and clearly not
      // the black of an unpainted window. It cannot match "the viewport" —
      // every world presents its own sky — so it reads as a surface of the app
      // rather than pretending to be the render.
      mBackground(44, 46, 52)
{
    setAttribute(Qt::WA_OpaquePaintEvent);   // we fill every pixel ourselves
    setAutoFillBackground(false);
    // Clicks while a world loads belong to nobody: swallow them rather than let
    // them fall through to a viewport that has no scene to pick in.
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void ViewportCover::setState(State state)
{
    if (state == mState) return;
    mState = state;
    if (state == State::Presenting) {
        hide();                 // unmaps the native window; the engine's shows through
        return;
    }
    ensureAboveViewport();
    show();
    update();
}

void ViewportCover::setSubtitle(const QString &subtitle)
{
    if (subtitle == mSubtitle) return;
    mSubtitle = subtitle;
    if (isVisible()) update();
}

void ViewportCover::showNow(State state)
{
    setState(state);
    if (state == State::Presenting) return;
    // repaint(), not update(): the caller is about to block this thread with a
    // scene load, and a posted paint event would be delivered after it.
    if (isVisible()) repaint();
}

void ViewportCover::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureAboveViewport();
}

void ViewportCover::ensureAboveViewport()
{
    // A native window of our own is what puts these pixels ABOVE the viewport's
    // native window; raise() restacks it there. Both calls are idempotent.
    setAttribute(Qt::WA_NativeWindow);
    raise();
}

void ViewportCover::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), mBackground);

    QString title, subtitle;
    switch (mState) {
    case State::Loading:
        title = tr("Loading world…");
        subtitle = mSubtitle;
        break;
    case State::Failed:
        title = tr("The 3D view could not be created");
        // The engine's own message. Nothing else in the app would show it, and a
        // blank viewport with no explanation is what this state exists to end.
        subtitle = mSubtitle;
        break;
    case State::NoScene:
    case State::Presenting:
        title = tr("No world open");
        subtitle = tr("Open or create a world from the Desktop");
        break;
    }

    QFont titleFont = font();
    titleFont.setPointSizeF(qMax(11.0, font().pointSizeF() * 1.45));
    titleFont.setWeight(QFont::DemiBold);
    QFont subFont = font();
    subFont.setPointSizeF(qMax(8.5, font().pointSizeF() * 0.95));

    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics subMetrics(subFont);
    const int gap = subtitle.isEmpty() ? 0 : subMetrics.height() / 2;
    const int block = titleMetrics.height() + gap +
                      (subtitle.isEmpty() ? 0 : subMetrics.height());
    int y = (height() - block) / 2;

    p.setFont(titleFont);
    p.setPen(QColor(198, 203, 214));
    p.drawText(QRect(0, y, width(), titleMetrics.height()),
               Qt::AlignHCenter | Qt::AlignVCenter, title);
    if (!subtitle.isEmpty()) {
        y += titleMetrics.height() + gap;
        p.setFont(subFont);
        p.setPen(QColor(128, 134, 148));
        p.drawText(QRect(0, y, width(), subMetrics.height()),
                   Qt::AlignHCenter | Qt::AlignVCenter, subtitle);
    }
}
