/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/sceneissuebar.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

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

// Rebuilt from the store, wholesale. The rows are cheap (a handful of labels)
// and the alternative — diffing them — would be a second copy of the store's
// rules, which is the mistake this whole facility exists to avoid.
void SceneIssueBar::refresh()
{
    while (QLayoutItem *item = mRows->takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }

    const QVector<SceneIssue> visible = SceneIssues::instance().issues(false);
    if (visible.isEmpty()) { hide(); return; }

    const int shown = qMin(int(kMaxRows), visible.size());
    for (int i = 0; i < shown; ++i) {
        const SceneIssue &issue = visible[i];
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);

        // WHAT IS WRONG, then WHAT TO DO — in that order, both in the user's
        // words. Nothing here is allowed to be a stack trace or a shader name.
        auto *text = new QLabel(issue.action.isEmpty()
                                    ? issue.message
                                    : issue.message + QLatin1Char(' ') + issue.action,
                                row);
        text->setWordWrap(true);
        text->setMinimumWidth(320);
        layout->addWidget(text, 1);

        // NAMES THE OBJECT, SELECTABLY (the rule's first clause): the button
        // carries the name so the row reads as a sentence about a thing.
        if (!issue.node.isEmpty()) {
            auto *select = new QPushButton(tr("Select %1").arg(issue.nodeName.isEmpty()
                                                                   ? tr("object")
                                                                   : issue.nodeName),
                                           row);
            select->setCursor(Qt::PointingHandCursor);
            const QString guid = issue.node;
            connect(select, &QPushButton::clicked, this,
                    [this, guid]() { emit selectRequested(guid); });
            layout->addWidget(select, 0);
        }

        auto *dismiss = new QPushButton(QStringLiteral("×"), row);
        dismiss->setFixedWidth(28);
        dismiss->setToolTip(tr("Dismiss. It will not come back until the scene is fixed and "
                               "broken again."));
        dismiss->setCursor(Qt::PointingHandCursor);
        const QString id = issue.id;
        connect(dismiss, &QPushButton::clicked, this,
                [id]() { SceneIssues::instance().dismiss(id); });
        layout->addWidget(dismiss, 0);

        mRows->addWidget(row);
    }
    if (visible.size() > shown) {
        auto *more = new QLabel(tr("and %1 more").arg(visible.size() - shown), this);
        mRows->addWidget(more);
    }

    adjustSize();
    reposition();
    show();
    raise();
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
    // push the buttons where nobody can reach them.
    topLeft.setX(qBound(area.left(), topLeft.x(), qMax(area.left(), area.right() - mine.width())));
    topLeft.setY(qBound(area.top(), topLeft.y(), qMax(area.top(), area.bottom() - mine.height())));
    move(topLeft);
    resize(mine);
}
