/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMOHANDLE_H
#define GIZMOHANDLE_H

#include <QtMath>

#include "../irisgl/src/irisgl.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/core/irisutils.h"
#include "../irisgl/src/scenegraph/meshnode.h"

enum class AxisHandle
{
    X = 0,
    Y,
    Z
};

enum class GizmoTransformMode
{
    Translate,
    Rotate,
    Scale
};

enum class GizmoTransformSpace
{
    Local,
    Global
};

enum class GizmoTransformAxis
{
    NONE,
    X,
    Y,
    Z,
    XY,
    XZ,
    YZ
};

enum class GizmoPivot
{
    CENTER,
    OBJECT_PIVOT
};

class GizmoHandle
{
private:

    QColor      handleColor;
    QString     handleName;
    QVector3D   handlePosition;
    QVector3D   handleScale;
    QQuaternion handleRotation;

public:

    GizmoHandle(const QString& objPath, const QString& name) {
        gizmoHandle = iris::MeshNode::create();
        gizmoHandle->setMesh(IrisUtils::getAbsoluteAssetPath(objPath));
        gizmoHandle->setName(name);
    }

    void setHandleColor(const QColor& color) {
        this->handleColor = color;
    }

    QColor getHandleColor() const {
        return this->handleColor;
    }

    void setHandlePosition(const QVector3D& position) {
         gizmoHandle->pos = this->handlePosition = position;
    }

    QVector3D getHandlePosition() const {
       return this->handlePosition;
    }

    void setHandleScale(const QVector3D& scale) {
        this->handleScale = scale;
    }

    QVector3D getHandleScale() const {
        return this->handleScale;
    }

    void setHandleRotation(const QVector3D& rotation) {
        gizmoHandle->rot = this->handleRotation = QQuaternion::fromEulerAngles(rotation);
    }

    QQuaternion getHandleRotation() const {
        return this->handleRotation;
    }

    void setHandleName(const QString& name) {
        this->handleName = name;
    }

    QString getHandleName() const {
        return this->handleName;
    }

    QSharedPointer<iris::MeshNode> gizmoHandle;

//    QMatrix4x4 localTransform;
//    QMatrix4x4 globalTransform;
};

#endif // GIZMOHANDLE_H
