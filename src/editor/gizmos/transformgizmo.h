/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSFORMGIZMO_H
#define TRANSFORMGIZMO_H

#include <QEntity>
#include <Qt3DExtras/QCuboidMesh>
#include <Qt3DRender/QMesh>
#include <Qt3DCore/QTransform>
#include "../../materials/materials.h"
#include "../../scenegraph/scenenodes.h"

using namespace Qt3DCore;
using namespace Qt3DRender;
using namespace Qt3DExtras;

class TranslationGizmo;
class RotationGizmo;
class ScaleGizmo;

class GizmoHandle
{
public:
    QEntity* entity;
    QMesh* mesh;
    QCuboidMesh* cube;
    Qt3DCore::QTransform* transform;
    GizmoMaterial* mat;
    QVector3D offset;

    GizmoHandle(QEntity* parent,QString gizmoPath);

    void setOffset(QVector3D offset);
    void setRot(float xrot,float yrot,float zrot);
    void setColor(int r,int g,int b);
};

//default axis is around Z
//first corner is top-right
//goes in clock-wise direction
#define HANDLE_TOPRIGHT 0
#define HANDLE_BOTTOMRIGHT 1
#define HANDLE_BOTTOMLEFT 2
#define HANDLE_TOPLEFT 3
class RotGizmoAxis
{
public:
    QEntity* entity;
    Qt3DCore::QTransform* transform;
    GizmoHandle* handles[4];

    RotGizmoAxis(QEntity* parent);

    void setColor(int r,int g,int b);
    void setScale(QVector3D scale);
    void setRot(float xrot,float yrot,float zrot);
};


class TransformGizmo:ISceneNodeListener
{
    SceneNode* trackedNode;
    QEntity* entity;
    Qt3DCore::QTransform* transform;

    //gizmos
    TranslationGizmo* translationGizmo;
    RotationGizmo* rotGizmo;
    ScaleGizmo* scaleGizmo;
public:
    TransformGizmo(QEntity* root);

    QEntity* getEntity()
    {
        return entity;
    }

    void setScale(QVector3D scale);
    void trackNode(SceneNode* node);

    void onTransformUpdated(SceneNode* node);
};

#define HANDLE_XPOSITIVE 0
#define HANDLE_XNEGATIVE 1
#define HANDLE_YPOSITIVE 2
#define HANDLE_YNEGATIVE 3
#define HANDLE_ZPOSITIVE 4
#define HANDLE_ZNEGATIVE 5

class TranslationGizmo
{
    GizmoHandle* handles[6];
    TransformGizmo* gizmo;
public:
    TranslationGizmo(TransformGizmo* gizmo);
    void setScale(QVector3D scale);
};

#define HANDLE_XAXIS 0
#define HANDLE_YAXIS 1
#define HANDLE_ZAXIS 2

class RotationGizmo
{
    RotGizmoAxis* handles[3];
    TransformGizmo* gizmo;
public:
    RotationGizmo(TransformGizmo* gizmo);
    void setScale(QVector3D scale);
};

class ScaleGizmo
{
    GizmoHandle* handles[6];
    TransformGizmo* gizmo;
public:
    ScaleGizmo(TransformGizmo* gizmo);

    void setScale(QVector3D scale);
};

#endif // TRANSFORMGIZMO_H
