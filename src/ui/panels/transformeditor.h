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

#include <QWidget>
#include <QSharedPointer>
#include <QVector3D>
#include <QQuaternion>

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

    // transform at scrub start, for the single undo command
    QVector3D scrubStartPos;
    QQuaternion scrubStartRot;
    QVector3D scrubStartScale;
};

#endif // TRANSFORMEDITOR_H
