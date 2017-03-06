/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSFORMEDITOR_H
#define TRANSFORMEDITOR_H

#include <QWidget>
#include <QSharedPointer>

namespace iris
{
    class SceneNode;
}

namespace Ui {
class TransformEditor;
}

class TransformEditor : public QWidget
{
    Q_OBJECT

public:
    explicit TransformEditor(QWidget *parent = 0);
    ~TransformEditor();

    /**
     *  sects active scene node
     * @param sceneNode
     */
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

protected slots:
    /**
     * should be triggered when active scene node's properties gets
     * updated externally (from gizmo, scripts, etc)
     */
    // void sceneNodeUpdated();

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

private:
    QSharedPointer<iris::SceneNode> sceneNode;
    QSharedPointer<iris::SceneNode> defaultStateNode;
    Ui::TransformEditor* ui;
};

#endif // TRANSFORMEDITOR_H
