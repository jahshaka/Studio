/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ADVANCEDTRANSFORMGIZMO_H
#define ADVANCEDTRANSFORMGIZMO_H

#include <QEntity>
#include <Qt3DExtras/QCuboidMesh>
#include <Qt3DRender/QMesh>
#include <Qt3DRender/QObjectPicker>
#include <Qt3DCore/QTransform>

#include "materials.h"
#include "scenenode.h"
#include <QGeometryRenderer>

#include "helpers/collisionhelper.h"
#include "gizmo/gizmotransform.h"


//length of the line defining the axis
#define AXIS_SIZE 10000

//using namespace Qt3DCore;
using namespace Qt3DRender;
using namespace Qt3DExtras;

class AdvancedTranslationGizmo;
class AdvancedRotationGizmo;
class AdvancedScaleGizmo;
class EditorCameraController;
class SurfaceView;

struct GizmoHitData
{
    //rotation gizmo needs only one plane
    //scale and translation gizmos needs two
    Plane plane1;
    Plane plane2;

    QVector3D hitPoint;
};

class AdvancedGizmoHandle
{
    QColor color;
public:
    Qt3DCore::QEntity* entity;
    QMesh* mesh;
    Qt3DCore::QTransform* transform;
    GizmoMaterial* mat;
    QVector3D offset;
    Qt3DRender::QObjectPicker* picker;

    //denotes the axis the gizmo corresponds to
    QVector3D axis;
    QVector<Plane*> hitPlanes;

    AdvancedGizmoHandle(Qt3DCore::QEntity* parent,QString gizmoPath,bool showHalf = false);
    AdvancedGizmoHandle(Qt3DCore::QEntity* parent,Qt3DRender::QGeometryRenderer* geom,bool showHalf = false);

    void setName(QString name);
    QString getName();

    void setOffset(QVector3D offset);
    void setRot(float xrot,float yrot,float zrot);
    void setColor(int r,int g,int b);
    void setAxis(float x,float y,float z);
    QVector3D getAxis()
    {
        return axis;
    }

    void highlight();
    void removeHighlight();

    GizmoHitData* getRayHit(QVector3D rayOrigin,QVector3D rayDir);
};

/*
class AdvancedRotGizmoAxis
{
public:
    Qt3DCore::QEntity* entity;
    Qt3DCore::QTransform* transform;
    AdvancedGizmoHandle* handles[3];

    AdvancedRotGizmoAxis(Qt3DCore::QEntity* parent);

    void setColor(int r,int g,int b);
    void setScale(QVector3D scale);
    void setRot(float xrot,float yrot,float zrot);
};
*/

enum class GizmoTransformMode
{
    Translation,
    Rotation,
    Scale
};

enum class GizmoTransformSpace
{
    Local,
    Global
};

/**
 * @brief This class acts as a container class for the
 * translation, rotation and scale gizmos
 */
class AdvancedTransformGizmo:ISceneNodeListener
{
    SceneNode* trackedNode;
    Qt3DCore::QEntity* entity;
    Qt3DCore::QTransform* transform;

    //only needed for the viewport's width and height
    SurfaceView* surface;

    //gizmos
    AdvancedTranslationGizmo* translationGizmo;
    AdvancedRotationGizmo* rotGizmo;
    AdvancedScaleGizmo* scaleGizmo;

    QMap<QString,AdvancedGizmoHandle*> handles;
    EditorCameraController* camControl;

    GizmoTransformMode transformMode;
    GizmoTransformSpace transformSpace;
    GizmoTransform* activeGizmo;

    friend class AdvancedTranslationGizmo;
    friend class AdvancedRotationGizmo;
    friend class AdvancedScaleGizmo;

public:
    AdvancedTransformGizmo(Qt3DCore::QEntity* root);

    void setTransformMode(GizmoTransformMode mode);

    Qt3DCore::QEntity* getEntity()
    {
        return entity;
    }

    QVector3D getPos()
    {
        return transform->translation();
    }

    void setCameraController(EditorCameraController* camController)
    {
        this->camControl = camController;
    }

    void setSurface(SurfaceView* surfaceView)
    {
        this->surface = surfaceView;
    }

    /**
     * @brief unprojects screen coordinates x and y to the far clip plane
     * the direction (not the position) to the unprojected pixel is returned
     * @param x
     * @param y
     * @return
     */
    QVector3D unproject(int x,int y);

    QVector3D getCameraPos();



    void trackNode(SceneNode* node);

    void onTransformUpdated(SceneNode* node);

    /**
     * @brief evaluate if object is a gizmo by the name
     * the name should be a name from the picker
     * @param name
     * @return
     */
    bool isGizmoHandle(QString name);

    /**
     * @brief gets gizmo handle by name
     * returns null if no gizmo is found by that name
     * @param name
     * @return
     */
    AdvancedGizmoHandle* getGizmoHandle(QString name);

    void addGizmoHandle(AdvancedGizmoHandle* handle);

    QMap<QString,AdvancedGizmoHandle*> getHandles()
    {
        return handles;
    }

    void onHandleSelected(QString name,int x,int y);

    void onMousePress(int x,int y);

    void onMouseMove(int x,int y);

    void onMouseRelease(int x,int y);

    /**
     * @brief Checks if active gizmo is hit
     * @param x
     * @param y
     * @return
     */
    bool isGizmoHit(int x,int y);
};

#define HANDLE_XAXIS 0
#define HANDLE_YAXIS 1
#define HANDLE_ZAXIS 2

#include "gizmo/translationgizmo.h"
#include "gizmo/scalegizmo.h"
#include "gizmo/rotationgizmo.h"

#endif // ADVANCEDTRANSFORMGIZMO_H
