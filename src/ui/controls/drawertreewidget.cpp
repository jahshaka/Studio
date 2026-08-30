/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/drawertreewidget.h"

#include <QDataStream>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QIODevice>
#include <QMimeData>

DrawerTreeWidget::DrawerTreeWidget(QWidget *parent) : QTreeWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    // The hovered row is highlighted through the selection instead — the
    // indicator only shows when the BASE view accepts the drag, which it
    // does not reliably do for our custom payload.
    setDropIndicatorShown(false);
}

int DrawerTreeWidget::drawerId(const QTreeWidgetItem *item)
{
    return item ? item->data(0, Qt::UserRole).toInt() : -1;
}

bool DrawerTreeWidget::isTileDrag(const QMimeData *mime) const
{
    return mime && mime->hasFormat(QStringLiteral("application/x-qabstractitemmodeldatalist"));
}

int DrawerTreeWidget::drawerDropParentId(const QPoint &pos) const
{
    QTreeWidgetItem *item = itemAt(pos);
    if (!item) return -1;   // empty space: top level

    const QRect rect = visualItemRect(item);
    const int margin = rect.height() / 4;
    if (pos.y() < rect.top() + margin || pos.y() > rect.bottom() - margin)
        return drawerId(item->parent());   // row edge: beside it, under its parent
    return drawerId(item);                 // row middle: nest under it
}

int DrawerTreeWidget::tileDropTargetId(const QPoint &pos) const
{
    // Tiles land ON a drawer name (Uncategorized included), never the root —
    // anywhere over the row counts, no between-rows dead zones.
    const int id = drawerId(itemAt(pos));
    return id >= 0 ? id : -2;
}

void DrawerTreeWidget::startDrag(Qt::DropActions supportedActions)
{
    // The drop handler must know WHICH drawer is being dragged even while the
    // selection is busy highlighting drop targets — capture it up front.
    mDraggedId = drawerId(currentItem());
    QTreeWidget::startDrag(supportedActions);
    mDraggedId = -2;
}

void DrawerTreeWidget::highlightTarget(const QPoint &pos)
{
    if (QTreeWidgetItem *item = itemAt(pos)) setCurrentItem(item);
}

void DrawerTreeWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this || isTileDrag(event->mimeData())) {
        mRestoreItem = currentItem();
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void DrawerTreeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    const QPoint pos = event->position().toPoint();

    if (event->source() == this) {
        // The root (-1) and Uncategorized (0) never move (their flags block
        // the drag too — this is belt and braces).
        if (mDraggedId <= 0) { event->ignore(); return; }
        highlightTarget(pos);
        event->acceptProposedAction();
    }
    else if (isTileDrag(event->mimeData())) {
        if (tileDropTargetId(pos) < 0) { event->ignore(); return; }
        highlightTarget(pos);
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

void DrawerTreeWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    if (mRestoreItem) setCurrentItem(mRestoreItem);
    mRestoreItem = nullptr;
    QTreeWidget::dragLeaveEvent(event);
}

void DrawerTreeWidget::dropEvent(QDropEvent *event)
{
    const QPoint pos = event->position().toPoint();
    mRestoreItem = nullptr;

    if (event->source() == this) {
        const int id = mDraggedId;
        const int parentId = drawerDropParentId(pos);
        event->setDropAction(Qt::IgnoreAction);   // never let Qt move rows itself
        event->accept();
        if (id > 0 && parentId != id)
            emit drawerMoveRequested(id, parentId);
        return;
    }

    if (isTileDrag(event->mimeData())) {
        const int target = tileDropTargetId(pos);
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (target >= 0) {
            QByteArray mdata = event->mimeData()->data(
                QStringLiteral("application/x-qabstractitemmodeldatalist"));
            QDataStream stream(&mdata, QIODevice::ReadOnly);
            QMap<int, QVariant> roleDataMap;
            stream >> roleDataMap;
            const QString guid = roleDataMap.value(3).toString();   // MODEL_GUID_ROLE slot
            if (!guid.isEmpty()) emit assetMoveRequested(guid, target);
        }
        return;
    }

    event->ignore();
}
