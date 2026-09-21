/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSFORMEDITOR_H
#define TRANSFORMEDITOR_H

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QWidget>
#include <QSharedPointer>

#include <functional>

namespace iris
{
    class SceneNode;
}

class DragSpinBox;
class QPushButton;
class QToolButton;

struct StudioServices;

class TransformEditor : public QWidget
{
    Q_OBJECT

public:
    explicit TransformEditor(QWidget *parent = 0);

    /**
     *  sets active scene node
     * @param sceneNode
     */
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);
    /// Undo pushes go through the services (Phase 4: was UiManager's statics).
    void setServices(StudioServices *s) { services = s; }

    void refreshUi();

protected slots:
    void xPosChanged(double value);
    void yPosChanged(double value);
    void zPosChanged(double value);

    void xRotChanged(double value);
    void yRotChanged(double value);
    void zRotChanged(double value);

    void xScaleChanged(double value);
    void yScaleChanged(double value);
    void zScaleChanged(double value);

    /// The lock icon beside the Scale label (SCALE-LOCK-1).
    void onScaleLockToggled(bool locked);

    void onResetBtnClicked();

    // a scrub (click-drag on a field) becomes ONE undoable change:
    // capture the transform when the drag starts, push a single
    // TransformSceneNodeCommand when it ends (same pattern as the gizmo)
    void onScrubStarted();
    void onScrubFinished(bool cancelled);

private:
    /// The document's rotation IS what the three rotation fields read (lane
    /// SPACE-2): a quaternion cannot say which euler triple built it, so
    /// re-deriving one per edit sent the user's drag somewhere else at gimbal
    /// lock. See the note in transformeditor.cpp.
    void applyRotationFromFields();
    /// The exact quaternion the rotation row last wrote, read back off the
    /// node. THE ROW IS ALLOWED TO SHOW A TRIPLE OTHER THAN THE CANONICAL
    /// DECOMPOSITION FOR EXACTLY AS LONG AS THE DOCUMENT HOLDS THIS VALUE —
    /// bit for bit. The moment anything else moves the rotation (a script, the
    /// gizmo, an animation, an undo) refreshUi puts the document's own triple
    /// back; and a new selection clears the memo, so a freshly selected node
    /// always shows its canonical one.
    iris::Quat rotationMemo;
    bool rotationMemoValid = false;

    // builds one horizontal row: title label left, X/Y/Z fields side by side.
    // `withLock` puts the scale-ratio lock button INSIDE the title cell, which
    // is why the cell is a fixed-width widget rather than a bare label: the
    // label gives up the width the icon needs and the three fields keep the
    // exact geometry they have on every other row (SCALE-LOCK-1 — "no box
    // narrows").
    void addRow(class QGridLayout* grid, int row, const QString& title,
                DragSpinBox*& x, DragSpinBox*& y, DragSpinBox*& z,
                double perPixelStep, bool withLock = false);
    DragSpinBox* createField(const QString& objectName, double perPixelStep);

    /// EVERY WRITE THIS PANEL MAKES, and the undo step a TYPED one earns.
    /// `box` is the field the value came from: while it is scrubbing nothing is
    /// recorded (onScrubFinished records the whole gesture as one step);
    /// otherwise the transform is snapshotted, `write` runs, and if the
    /// document moved the step is pushed — the same TransformSceneNodeCommand,
    /// pushed the same way (rewind, then push) so it captures the pre-edit
    /// SCENE_STATIC classification.
    void writeTransform(DragSpinBox *box,
                        const std::function<void(const QSharedPointer<iris::SceneNode> &)> &write);

    /// ONE SCALE CHANNEL, written the one way the whole app writes one
    /// (iris::scalelock::apply): the node's lock, or Shift held during THIS
    /// scrub, makes it uniform. `axis` is 0/1/2 = x/y/z.
    void scaleChannelChanged(int axis, DragSpinBox* box, double value);
    /// Puts the node's scale back into the three fields without writing back
    /// (the other two channels move when a locked edit scales them).
    void refreshScaleFields();
    /// Puts the lock icon's checked state back in step with the document.
    void refreshLockButton();

    StudioServices *services = nullptr;
    QSharedPointer<iris::SceneNode> sceneNode;
    /// The node the value slots may WRITE — `sceneNode`, or nothing while a
    /// script run owns the document (services/editgate.h).
    QSharedPointer<iris::SceneNode> editableNode() const;
    QSharedPointer<iris::SceneNode> defaultStateNode;

    DragSpinBox* xpos = nullptr; DragSpinBox* ypos = nullptr; DragSpinBox* zpos = nullptr;
    DragSpinBox* xrot = nullptr; DragSpinBox* yrot = nullptr; DragSpinBox* zrot = nullptr;
    DragSpinBox* xscale = nullptr; DragSpinBox* yscale = nullptr; DragSpinBox* zscale = nullptr;
    QPushButton* resetBtn;
    /// The preserve-ratio lock, to the right of the "Scale" label and before
    /// the three fields (SCALE-LOCK-1).
    QToolButton* scaleLockBtn = nullptr;
    /// True while refreshLockButton is driving the button, so the toggled
    /// signal it emits is not read as a user's click.
    bool refreshingLock = false;
    /// FIT TO SIZE: the node subtree's measured world size in metres (read-only).
    class QLabel* sizeLabel = nullptr;

    // transform at scrub start, for the single undo command
    iris::Vec3 scrubStartPos;
    iris::Quat scrubStartRot;
    iris::Vec3 scrubStartScale;
};

#endif // TRANSFORMEDITOR_H
