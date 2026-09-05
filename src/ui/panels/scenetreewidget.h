/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENETREEWIDGET_H
#define SCENETREEWIDGET_H

// The hierarchy panel's tree. It exists for ONE reason: to draw a drop
// indicator that says which of the two different drops is about to happen
// (SCENEGRAPH_SPEC §6b — "drop indicator states must make the distinction
// visible").
//
// The panel's drag logic stays where it already was and where it is already
// verified — SceneHierarchyWidget's viewport event filter, which owns the
// reparent guard, the cycle check and the asset-bin -> decal case. This widget
// is told what that filter decided and paints it; it makes no decisions.
//
// Qt's own indicator is switched off (setDropIndicatorShown(false), as before)
// because it only knows about "on / above / below an item" and cannot tell a
// REPARENT (a real hierarchy change, drawn as a box around the target row) from
// a FOLDER FILE (metadata only, drawn as a box in the folder accent) from a
// MOVE TO ROOT (drawn as a line at the level the row would land on).

#include <QTreeWidget>

class SceneTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    /// What the drop under the cursor would do.
    enum class DropHint {
        None,        ///< no drag, or a drop that is refused
        Reparent,    ///< onto a NODE row: the existing ReparentSceneNodeCommand
        IntoFolder,  ///< onto a FOLDER row: a folderPath metadata edit
        ToRoot       ///< between root entries / empty space: back to the root level
    };

    using QTreeWidget::QTreeWidget;

    /// Sets what to draw. `item` is the row the hint refers to (null for
    /// ToRoot-on-empty-space). Repaints only when something actually changed,
    /// so this is safe to call from every dragMoveEvent.
    void setDropHint(DropHint hint, QTreeWidgetItem *item);
    void clearDropHint() { setDropHint(DropHint::None, nullptr); }

    DropHint dropHint() const { return mHint; }
    QTreeWidgetItem *dropHintItem() const { return mHintItem; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    DropHint mHint = DropHint::None;
    QTreeWidgetItem *mHintItem = nullptr;
};

#endif   // SCENETREEWIDGET_H
