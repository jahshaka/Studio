/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/scenetreewidget.h"

#include <QPainter>
#include <QPaintEvent>

namespace {
// Two clearly different accents, because they are two clearly different edits.
// Blue = the hierarchy really changes (reparent). Amber = only metadata does
// (filed into a folder / sent back to the root level).
const QColor kReparent(0x4a, 0x90, 0xd9);
const QColor kFolder(0xd9, 0xa0, 0x3a);
}

void SceneTreeWidget::setDropHint(DropHint hint, QTreeWidgetItem *item)
{
    if (hint == mHint && item == mHintItem) return;
    mHint = hint;
    mHintItem = item;
    viewport()->update();
}

void SceneTreeWidget::paintEvent(QPaintEvent *event)
{
    QTreeWidget::paintEvent(event);
    if (mHint == DropHint::None) return;

    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, false);

    const QColor accent = mHint == DropHint::Reparent ? kReparent : kFolder;
    QPen pen(accent);
    pen.setWidth(2);
    p.setPen(pen);

    if (!mHintItem) {
        // EMPTY SPACE BELOW THE LAST ROW. Both edits can land here since the
        // World row left the tree (RIGHT-TABS-1): amber = filed back to the
        // root LEVEL (folder metadata), blue = really reparented to the scene
        // root, which is the gesture that used to mean "drop onto the World
        // row". The line is drawn at the bottom of the content either way — the
        // target is the list itself, not a row.
        const int y = viewport()->height() - 1;
        p.drawLine(2, y, viewport()->width() - 2, y);
        return;
    }

    if (!mHintItem) return;
    QRect r = visualItemRect(mHintItem);
    if (!r.isValid()) return;
    r.setLeft(0);
    r.setRight(viewport()->width() - 1);

    if (mHint == DropHint::ToRoot) {
        // Between rows: a line under the row the cursor is on.
        p.drawLine(r.left() + 2, r.bottom(), r.right() - 2, r.bottom());
        return;
    }

    // Onto a row: a box around it, filled faintly so it reads at a glance even
    // on the dark palette.
    QColor fill = accent;
    fill.setAlpha(48);
    p.fillRect(r.adjusted(1, 1, -1, -1), fill);
    p.drawRect(r.adjusted(1, 1, -2, -2));
}
