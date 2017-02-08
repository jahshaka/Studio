/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMOINSTANCE_H
#define GIZMOINSTANCE_H

#include "gizmohandle.h"

class QOpenGLFunctions_3_2_Core;

class GizmoInstance
{
public:
    QOpenGLFunctions_3_2_Core *gl;

    QSharedPointer<iris::SceneNode> lastSelectedNode;
    QSharedPointer<iris::SceneNode> currentNode;
    QVector3D finalHitPoint;
    QVector3D translatePlaneNormal;
    float translatePlaneD;

    QSharedPointer<iris::Scene> POINTER;

    QSharedPointer<iris::CameraNode> camera;
    QSharedPointer<iris::SceneNode> hitNode;
    QString lastHitAxis;

    QString transformOrientation;

    virtual QSharedPointer<iris::SceneNode> getRootNode() = 0;

    virtual void updateTransforms(const QVector3D& pos) = 0;

    virtual void update(QVector3D, QVector3D) = 0;

    virtual void setPlaneOrientation(const QString&) = 0;

    virtual void createHandleShader() = 0;

    virtual QString getTransformOrientation() = 0;

    virtual void setTransformOrientation(const QString&) = 0;

    virtual void render(QMatrix4x4&, QMatrix4x4&) = 0;

    virtual void isHandleHit() = 0;

    virtual void onMousePress(QVector3D, QVector3D) = 0;

    virtual void onMouseRelease() = 0;

    virtual void isGizmoHit(const iris::CameraNodePtr&, const QPointF&, const QVector3D&) = 0;
};

#endif // GIZMOINSTANCE_H
