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
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
}

int DrawerTreeWidget::drawerId(const QTreeWidgetItem *item)
{
    return item ? item->data(0, Qt::UserRole).toInt() : -1;
}

bool DrawerTreeWidget::isTileDrag(const QMimeData *mime) const
{
    return mime && mime->hasFormat(QStringLiteral("application/x-qabstractitemmodeldatalist"));
}

int DrawerTreeWidget::dropTargetId(const QPoint &pos, bool onItemOnly) const
{
    QTreeWidgetItem *item = itemAt(pos);
    if (!item) return onItemOnly ? -2 : -1;   // empty space: top level (drawer moves only)

    switch (dropIndicatorPosition()) {
    case QAbstractItemView::OnItem:
        return drawerId(item);
    case QAbstractItemView::AboveItem:
    case QAbstractItemView::BelowItem:
        if (onItemOnly) return -2;
        return drawerId(item->parent());
    default:
        return onItemOnly ? -2 : -1;
    }
}

void DrawerTreeWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this || isTileDrag(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void DrawerTreeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    // Let the base track the hover highlight/indicator, then veto bad targets.
    QTreeWidget::dragMoveEvent(event);

    const QPoint pos = event->position().toPoint();
    if (event->source() == this) {
        QTreeWidgetItem *dragged = currentItem();
        // The root (-1) and Uncategorized (0) never move (flags block the drag
        // too — this is belt and braces), and nothing nests under itself.
        if (drawerId(dragged) <= 0) { event->ignore(); return; }
        event->acceptProposedAction();
    }
    else if (isTileDrag(event->mimeData())) {
        // Tiles land ON a drawer name (Uncategorized included), never the root.
        if (dropTargetId(pos, true) < 0) { event->ignore(); return; }
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

void DrawerTreeWidget::dropEvent(QDropEvent *event)
{
    const QPoint pos = event->position().toPoint();

    if (event->source() == this) {
        QTreeWidgetItem *dragged = currentItem();
        const int id = drawerId(dragged);
        const int parentId = dropTargetId(pos, false);
        event->setDropAction(Qt::IgnoreAction);   // never let Qt move rows itself
        event->accept();
        if (id > 0 && parentId != -2 && parentId != id)
            emit drawerMoveRequested(id, parentId);
        return;
    }

    if (isTileDrag(event->mimeData())) {
        const int target = dropTargetId(pos, true);
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
