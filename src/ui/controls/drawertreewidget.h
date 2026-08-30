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
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    bool isTileDrag(const QMimeData *mime) const;
    /// The drawer a drop at `pos` targets, honouring the drop indicator:
    /// on an item = that drawer; between items = their parent; empty space =
    /// the top level. `onItemOnly` restricts to direct hits (tile drops).
    int dropTargetId(const QPoint &pos, bool onItemOnly) const;
};

#endif // DRAWERTREEWIDGET_H
