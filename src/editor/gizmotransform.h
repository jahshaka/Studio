/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMOTRANSFORM_H
#define GIZMOTRANSFORM_H

#include <QtMath>
#include <QDebug>
#include <QString>

class GizmoTransform
{
public:

//    QSharedPointer<iris::Scene> POINTER;
//    QSharedPointer<iris::SceneNode> lastSelectedNode;
//    QSharedPointer<iris::SceneNode> currentNode;
//    QVector3D finalHitPoint;
//    QVector3D translatePlaneNormal;
//    float translatePlaneD;

    static GizmoTransform* create() {

    }

    virtual void createHandleShader() {

    }

    virtual void render(QOpenGLFunctions_3_2_Core*, QMatrix4x4&, QMatrix4x4&) {

    }

    virtual bool onHandleSelected(QString name,int x,int y)
    {
        Q_UNUSED(name);
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }

    virtual bool onMousePress(int x,int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }

    virtual bool onMouseMove(int x,int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }

    virtual bool onMouseRelease(int x,int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }

    virtual bool isGizmoHit(int x,int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }
};

#endif // GIZMOTRANSFORM_H
