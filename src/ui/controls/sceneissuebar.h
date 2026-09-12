/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEISSUEBAR_H
#define SCENEISSUEBAR_H

// SceneIssueBar — the visible half of services/sceneissues.h.
//
// A dismissible error area over the viewport, sitting under the frame-rate
// readout (owner, 2026-09-13: "they should give the user an error toast, like
// where the fps is — we need a user error toast box"). It shows the scene
// problems the user can fix, names the object each one is about, says what to
// do, and lets them select it or wave it away.
//
// A FRAMELESS TOP-LEVEL, like Toast, and for the same reason: the viewport is a
// NATIVE window with WA_PaintOnScreen and a null paint engine, so a Qt child
// widget over it is a second native window and a fight
// (STATS_OVERLAY_SPEC's whole rationale). A top-level parented to the main
// window for ownership only stays above it, moves with it, and paints normally.
//
// The stats readout itself is engine-drawn text with no input; this needs two
// buttons per row, which is exactly why it is Qt and the readout is not.
//
// IT OWNS NO STATE. Everything it shows is SceneIssues::instance(); it is
// rebuilt from the store on `changed()` and hides itself when nothing is
// visible. The store's rules — never repeat, dismiss keeps it live — are
// therefore automatically the bar's rules too.

#include <QFrame>
#include <QPointer>
#include <QVBoxLayout>

class SceneIssueBar : public QFrame
{
    Q_OBJECT

public:
    explicit SceneIssueBar(QWidget *parent = nullptr);

    /// The widget this bar sits over (the viewport). `topInset` is how far
    /// below its top edge the bar starts — enough to clear the engine-drawn
    /// frame-stats rows when they are up.
    void setAnchor(QWidget *widget, int topInset);

    /// Re-reads the store and shows/hides itself. Called on every change; safe
    /// to call at any time.
    void refresh();

    /// The rectangle the bar occupies in its OWNER WINDOW's coordinates —
    /// what a test asserts about. Null when it is not showing.
    QRect geometryInWindow() const;

signals:
    /// The user asked to look at the object an issue is about.
    void selectRequested(const QString &nodeGuid);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reposition();
    QWidget *ownerWindow() const;

    QVBoxLayout *mRows = nullptr;
    QPointer<QWidget> mAnchor;
    int mTopInset = 24;
    int mMargin = 12;
    /// At most this many rows; the rest are counted in a trailing line, because
    /// an error area that can grow without bound is a second problem.
    static constexpr int kMaxRows = 3;
};

#endif // SCENEISSUEBAR_H
