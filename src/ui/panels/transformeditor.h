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

namespace iris
{
    class SceneNode;
}

class DragSpinBox;
class QPushButton;

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

    // builds one horizontal row: title label left, X/Y/Z fields side by side
    void addRow(class QGridLayout* grid, int row, const QString& title,
                DragSpinBox*& x, DragSpinBox*& y, DragSpinBox*& z,
                double perPixelStep);
    DragSpinBox* createField(const QString& objectName, double perPixelStep);

    StudioServices *services = nullptr;
    QSharedPointer<iris::SceneNode> sceneNode;
    QSharedPointer<iris::SceneNode> defaultStateNode;

    DragSpinBox* xpos; DragSpinBox* ypos; DragSpinBox* zpos;
    DragSpinBox* xrot; DragSpinBox* yrot; DragSpinBox* zrot;
    DragSpinBox* xscale; DragSpinBox* yscale; DragSpinBox* zscale;
    QPushButton* resetBtn;
    /// FIT TO SIZE: the node subtree's measured world size in metres (read-only).
    class QLabel* sizeLabel = nullptr;

    // transform at scrub start, for the single undo command
    iris::Vec3 scrubStartPos;
    iris::Quat scrubStartRot;
    iris::Vec3 scrubStartScale;
};

#endif // TRANSFORMEDITOR_H
