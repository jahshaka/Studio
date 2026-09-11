/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TOAST_H
#define TOAST_H

// Toast — the transient message over the app window.
//
// IT POSITIONS ITSELF (owner smoke S8, 2026-09-11: "the Add-to-Project toast is
// not centred; it should sit bottom-centre of the app window"). Every
// positioning line in here was commented out and the two `pos`/`rect`
// parameters callers passed were read by nobody, so a toast appeared wherever
// the window manager first put a frameless top-level — while the two callers
// that DID place one computed the point themselves, in a coordinate space the
// audit flagged as ambiguous (F-D4). One anchor rule, in one place:
//
//   Anchor::WindowBottom   bottom-centre of the owner window (the default)
//   Anchor::WidgetTop      top-centre of a named widget, inset (the viewport
//                          readout: snap size, fly speed)
//   Anchor::WindowCentre   the middle of the owner window (the blocking
//                          "the 3D view is unavailable" message)
//
// A Toast is a FRAMELESS TOP-LEVEL (Qt::Window) parented to a widget for
// ownership only, so its geometry is in SCREEN coordinates — the anchor maths
// maps through the anchor widget, which is what makes it right on a window that
// is not at the screen's origin (the thing the rig cannot see).
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class Toast : public QFrame
{
    Q_OBJECT

public:
    enum class Anchor { WindowBottom, WindowCentre, WidgetTop };

    Toast(QWidget *parent = nullptr);

    /// Shows the message and auto-hides after `holdMs` (0 = the default hold).
    void showToast(const QString &title, const QString &text, int holdMs = 0);

    /// Where this toast sits. `widget` is required for WidgetTop and ignored
    /// otherwise; the anchor is remembered, so a window resize re-places it.
    void setAnchor(Anchor anchor, QWidget *widget = nullptr);

    /// The rectangle the toast occupies, in the OWNER WINDOW's coordinates —
    /// what a test asserts about ("inside the window, centred").
    QRect geometryInWindow() const;

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// The one placement. Called on show, and on every move/resize of the
    /// window the toast is anchored to.
    void reposition();
    QWidget *ownerWindow() const;

    QVBoxLayout *toastLayout;
    /// ONE hold timer, restarted per message: a REUSED toast used to arm a new
    /// single-shot per call, so the previous message's timer hid the new one
    /// early (audit F-D6).
    QTimer mHold;
    QLabel *caption;
    QLabel *info;
    Anchor mAnchor = Anchor::WindowBottom;
    QPointer<QWidget> mAnchorWidget;
    /// Distance from the anchored edge, in pixels.
    int mMargin = 24;
};

#endif // TOAST_H
