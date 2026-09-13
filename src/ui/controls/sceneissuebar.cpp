/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/sceneissuebar.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QLabel>

#include "services/sceneissues.h"
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"

SceneIssueBar::SceneIssueBar(QWidget *parent) : QFrame(parent)
{
    setObjectName("SceneIssueBar");
    mRows = new QVBoxLayout(this);
    mRows->setContentsMargins(12, 10, 10, 10);
    mRows->setSpacing(8);

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus |
                   Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet(StyleSheet::ToastPanel());
    // WARNING, not Panel: this is the one surface in the editor that means
    // "something in your scene is wrong", and it has to read as that at a
    // glance from across the viewport.
    ThemeRoles::setSurface(this, ThemeRoles::Surface::Warning);
    ThemeRoles::setFrame(this, QFrame::StyledPanel);
    setLineWidth(1);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    connect(&SceneIssues::instance(), &SceneIssues::changed, this, &SceneIssueBar::refresh);
    if (QWidget *owner = ownerWindow()) owner->installEventFilter(this);
    hide();
}

void SceneIssueBar::setAnchor(QWidget *widget, int topInset)
{
    mAnchor = widget;
    mTopInset = topInset;
    if (isVisible()) reposition();
}

QWidget *SceneIssueBar::ownerWindow() const
{
    if (QWidget *p = parentWidget()) return p->window();
    return QApplication::activeWindow();
}

void SceneIssueBar::setEditorActive(bool active)
{
    if (mEditorActive == active) return;
    mEditorActive = active;
    if (!active) hide();
    else refresh();
}

// Rebuilt from the store, wholesale. The rows are cheap (a handful of labels)
// and the alternative — diffing them — would be a second copy of the store's
// rules, which is the mistake this whole facility exists to avoid.
void SceneIssueBar::refresh()
{
    while (QLayoutItem *item = mRows->takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }

    // NOT THE EDITOR'S PAGE, NOT ON SCREEN — checked here and not only at the
    // caller, because `changed()` from the store reaches refresh() directly.
    if (!mEditorActive) { hide(); return; }

    // EVERY ISSUE, LINE BY LINE, in the store's stable order (by kind, then by
    // the object) — never a count, never a "1 of 3". The store sorts; this just
    // prints. One QLabel per issue and nothing else in the row: no Select, no
    // dismiss, no layout to hold them (owner, 2026-09-13).
    const QVector<SceneIssue> visible = SceneIssues::instance().issues();
    if (visible.isEmpty()) { hide(); return; }

    const int shown = qMin(int(kMaxRows), visible.size());
    for (int i = 0; i < shown; ++i) {
        const SceneIssue &issue = visible[i];
        // WHAT IS WRONG, then WHAT TO DO — in that order, both in the user's
        // words. Nothing here is allowed to be a stack trace or a shader name.
        // The message names the object itself ("Moon" and "Sun" are quoted in
        // it), which is why no button is needed to point at one.
        auto *text = new QLabel(issue.action.isEmpty()
                                    ? issue.message
                                    : issue.message + QLatin1Char(' ') + issue.action,
                                this);
        text->setObjectName(QStringLiteral("SceneIssueLine"));
        text->setTextInteractionFlags(Qt::NoTextInteraction);
        text->setWordWrap(true);
        text->setMinimumWidth(360);
        mRows->addWidget(text);
    }
    if (visible.size() > shown) {
        auto *more = new QLabel(tr("and %1 more").arg(visible.size() - shown), this);
        more->setObjectName(QStringLiteral("SceneIssueLine"));
        mRows->addWidget(more);
    }

    adjustSize();
    reposition();
    show();
    raise();
}

int SceneIssueBar::lineCount() const
{
    return mRows ? mRows->count() : 0;
}

// ZERO BY CONSTRUCTION — and asserted, because "the bar has no buttons" is an
// owner decision and not a detail of how refresh() happens to build rows today.
int SceneIssueBar::buttonCount() const
{
    return int(findChildren<QAbstractButton *>().size());
}

bool SceneIssueBar::eventFilter(QObject *watched, QEvent *event)
{
    if (isVisible() && watched == ownerWindow() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        reposition();
    return QFrame::eventFilter(watched, event);
}

QRect SceneIssueBar::geometryInWindow() const
{
    QWidget *owner = ownerWindow();
    if (!owner || !isVisible()) return QRect();
    return QRect(owner->mapFromGlobal(geometry().topLeft()), size());
}

// TOP-LEFT OF THE VIEWPORT, under the frame-rate readout. A frameless top-level
// moves in SCREEN coordinates, so the anchor is computed through mapToGlobal on
// the widget rather than by adding a window origin to a local point (the
// mistake Toast's own audit found, F-D4).
void SceneIssueBar::reposition()
{
    QWidget *anchor = mAnchor ? mAnchor.data() : ownerWindow();
    if (!anchor) return;
    const QRect area(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QSize mine = sizeHint().expandedTo(size());
    QPoint topLeft(area.left() + mMargin, area.top() + mTopInset);
    // Never off the viewport: a long message on a small window would otherwise
    // push the text where nobody can read it.
    topLeft.setX(qBound(area.left(), topLeft.x(), qMax(area.left(), area.right() - mine.width())));
    topLeft.setY(qBound(area.top(), topLeft.y(), qMax(area.top(), area.bottom() - mine.height())));
    move(topLeft);
    resize(mine);
}
