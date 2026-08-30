/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DRAWERTREEWIDGET_H
#define DRAWERTREEWIDGET_H

// DrawerTreeWidget — the Assets page's drawers tree (ASSET_DRAWERS_SPEC §1).
//
// A QTreeWidget that turns two kinds of drops into REQUEST signals and never
// mutates anything itself: dragging a drawer within the tree asks for a
// reparent (AssetView runs Database::setCollectionParent — the cycle guard —
// and rebuilds), and dropping an asset tile on a drawer name asks for the
// asset to be filed there. Tile drags carry the assetwidget mime
// ("application/x-qabstractitemmodeldatalist", guid at role 3 — project.h).
//
// Drop targets are computed from row geometry, NOT dropIndicatorPosition()
// (whose state depends on the base view accepting the drag — owner-verified
// fragile). The hovered target row is highlighted by moving the selection,
// restored if the drag leaves or is cancelled.

#include <QTreeWidget>

class DrawerTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit DrawerTreeWidget(QWidget *parent = nullptr);

    /// The drawer id stored on an item (Qt::UserRole); -1 is the virtual root.
    static int drawerId(const QTreeWidgetItem *item);

signals:
    /// A drawer was dragged somewhere new: file `id` under `parentId` (-1 = top level).
    void drawerMoveRequested(int id, int parentId);
    /// An asset tile was dropped on a drawer name: file the asset there.
    void assetMoveRequested(const QString &guid, int drawerId);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    bool isTileDrag(const QMimeData *mime) const;
    /// The drawer a DRAWER drop at `pos` targets: the row's middle band nests
    /// under that row; its top/bottom quarters mean "beside it" (its parent);
    /// empty space is the top level.
    int drawerDropParentId(const QPoint &pos) const;
    /// The drawer a TILE drop at `pos` targets: the row under the cursor
    /// (Uncategorized included), or -2 when there is none / it is the root.
    int tileDropTargetId(const QPoint &pos) const;
    void highlightTarget(const QPoint &pos);

    int mDraggedId = -2;                        // captured when a drawer drag starts
    QTreeWidgetItem *mRestoreItem = nullptr;    // selection before the drag came in
};

#endif // DRAWERTREEWIDGET_H
