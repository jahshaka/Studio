/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

//#include <Qt3DCore/QTransform>
#include "advancedtransformgizmo.h"
#include <QtCore/QtMath>
#include "../../geometry/linerenderer.h"
#include <QGeometryRenderer>
#include <Qt3DRender/QObjectPicker>
#include "../../globals.h"
#include "../../editor/editorcameracontroller.h"
#include "../../core/surfaceview.h"
#include "../../helpers/collisionhelper.h"

//using namespace Qt3DCore;

AdvancedGizmoHandle::AdvancedGizmoHandle(Qt3DCore::QEntity* parent,QString gizmoPath,bool showHalf)
{
    entity = new Qt3DCore::QEntity(parent);
    entity->setObjectName("__gizmo");
    mesh = new QMesh();
    mesh->setSource(QUrl::fromLocalFile(gizmoPath));

    float size = 2.0f;

    transform = new Qt3DCore::QTransform();
    transform->setScale(size);
    mat = new GizmoMaterial();
    mat->showHalf(showHalf);

    //picker
    picker = new Qt3DRender::QObjectPicker(entity);
    picker->setHoverEnabled(false);
    picker->setObjectName("__gizmo");

    entity->addComponent(mesh);
    entity->addComponent(transform);
    entity->addComponent(mat);
    //entity->addComponent(picker);
    entity->addComponent(RenderLayers::gizmoLayer);
}

AdvancedGizmoHandle::AdvancedGizmoHandle(Qt3DCore::QEntity* parent,Qt3DRender::QGeometryRenderer* geomRenderer,bool showHalf)
{
    entity = new Qt3DCore::QEntity(parent);
    entity->setObjectName("__gizmo");
    float size = 2.0f;

    transform = new Qt3DCore::QTransform();
    transform->setScale(size);
    mat = new GizmoMaterial();
    mat->showHalf(showHalf);

    //picker
    picker = new Qt3DRender::QObjectPicker(entity);
    picker->setHoverEnabled(false);
    picker->setObjectName("__gizmo");

    entity->addComponent(geomRenderer);
    entity->addComponent(transform);
    entity->addComponent(mat);
    //entity->addComponent(picker);
}

void AdvancedGizmoHandle::setName(QString name)
{
    entity->setObjectName(name);
}

QString AdvancedGizmoHandle::getName()
{
    return entity->objectName();
}

void AdvancedGizmoHandle::setOffset(QVector3D offset)
{
    this->offset = offset;
    transform->setTranslation(offset);
}

void AdvancedGizmoHandle::setRot(float xrot,float yrot,float zrot)
{
    transform->setRotationX(xrot);
    transform->setRotationY(yrot);
    transform->setRotationZ(zrot);
}

void AdvancedGizmoHandle::setColor(int r,int g,int b)
{
    color = QColor::fromRgb(r,g,b);
    mat->setColor(color);
}

void AdvancedGizmoHandle::setAxis(float x,float y,float z)
{
    axis = QVector3D(x,y,z);
}

void AdvancedGizmoHandle::highlight()
{
    mat->setColor(Qt::yellow);
}

void AdvancedGizmoHandle::removeHighlight()
{
    mat->setColor(color);
}

/**
 * @brief calculates if ray hits one of the axis's hit planes
 * user is in charge of deallocating returned GizmoHitData
 * @param rayOrigin
 * @param rayDir
 * @return
 */
GizmoHitData* AdvancedGizmoHandle::getRayHit(QVector3D rayOrigin,QVector3D rayDir)
{
    //GizmoHitData hit;

    for(auto plane:hitPlanes)
    {
        float t;
        auto hitPoint = plane->rayIntersect(rayOrigin,rayDir,t);
        if(hitPoint!=rayOrigin)
        {
            //hit!
            auto hit = new GizmoHitData();
            //hit->hitPoint = rayOrigin + rayDir*t;
            hit->hitPoint = hitPoint;
            hit->plane1 = *plane;

            return hit;
        }

    }

    return nullptr;
}


AdvancedTransformGizmo::AdvancedTransformGizmo(Qt3DCore::QEntity* root)
{
    trackedNode = nullptr;
    entity = new Qt3DCore::QEntity(root);

    transform = new Qt3DCore::QTransform();
    entity->addComponent(transform);

    transformMode = GizmoTransformMode::Translation;
    transformSpace = GizmoTransformSpace::Global;

    translationGizmo = new AdvancedTranslationGizmo(this);
    //rotGizmo = new AdvancedRotationGizmo(this);
    //scaleGizmo = new AdvancedScaleGizmo(this);

    activeGizmo = nullptr;

    setTransformMode(GizmoTransformMode::Translation);
}

void AdvancedTransformGizmo::setTransformMode(GizmoTransformMode mode)
{
    transformMode = mode;
    switch(mode)
    {
    case GizmoTransformMode::Translation:
        activeGizmo = translationGizmo;
        break;

    case GizmoTransformMode::Rotation:
        activeGizmo = rotGizmo;
        break;

    case GizmoTransformMode::Scale:
        activeGizmo = scaleGizmo;
        break;

    }
}

/*
AdvancedRotGizmoAxis::AdvancedRotGizmoAxis(Qt3DCore::QEntity* parent)
{
    entity = new Qt3DCore::QEntity(parent);

    transform = new Qt3DCore::QTransform();
    entity->addComponent(transform);

    const QString path = ":/assets/models/advancedgizmo/rot_arrow.obj";

    handles[HANDLE_XAXIS] = new AdvancedGizmoHandle(entity,path);
    handles[HANDLE_XAXIS]->setOffset(QVector3D(1,1,0));
    handles[HANDLE_XAXIS]->setRot(0,0,0);

    handles[HANDLE_YAXIS] = new AdvancedGizmoHandle(entity,path);
    handles[HANDLE_YAXIS]->setOffset(QVector3D(1,-1,0));
    handles[HANDLE_YAXIS]->setRot(0,0,-90);

    handles[HANDLE_ZAXIS] = new AdvancedGizmoHandle(entity,path);
    handles[HANDLE_ZAXIS]->setOffset(QVector3D(-1,-1,0));
    handles[HANDLE_ZAXIS]->setRot(0,0,180);
}

void AdvancedRotGizmoAxis::setColor(int r,int g,int b)
{
    for(int i=0;i<3;i++)
        handles[i]->setColor(r,g,b);
}

void AdvancedRotGizmoAxis::setRot(float xrot,float yrot,float zrot)
{
    transform->setRotationX(xrot);
    transform->setRotationY(yrot);
    transform->setRotationZ(zrot);
}
*/

QVector3D AdvancedTransformGizmo::unproject(int x,int y)
{
    return camControl->unproject(surface->width(),surface->height(),QPoint(x,y));
}

QVector3D AdvancedTransformGizmo::getCameraPos()
{
    return camControl->getPos();
}

void AdvancedTransformGizmo::trackNode(SceneNode* node)
{

    if(node==nullptr)
    {
        trackedNode = nullptr;
        return;
    }


    if(trackedNode!=nullptr)
        trackedNode->setListener(nullptr);
    trackedNode = node;
    node->setListener(this);
    onTransformUpdated(node);
    //auto ent = node->getEntity();
    //entity->setParent(ent);
}

void AdvancedTransformGizmo::onTransformUpdated(SceneNode* node)
{
    transform->setTranslation(node->pos);
    //transform->setScale3D(node->scale);

    if(transformSpace == GizmoTransformSpace::Local)
    {
        auto r = node->rot;
        auto rot = QQuaternion::fromEulerAngles(r.y(),r.x(),r.z());
        transform->setRotation(rot);
    }

    //scale gizmo according to
}

/**
 * @brief evaluate if object is a gizmo by the name
 * the name should be a name from the picker
 * @param name
 * @return
 */
bool AdvancedTransformGizmo::isGizmoHandle(QString name)
{
    if(handles.contains(name))
        return true;

    return false;
}

/**
 * @brief gets gizmo handle by name
 * returns null if no gizmo is found by that name
 * @param name
 * @return
 */
AdvancedGizmoHandle* AdvancedTransformGizmo::getGizmoHandle(QString name)
{
    if(handles.contains(name))
        return handles[name];

    return nullptr;
}

void AdvancedTransformGizmo::addGizmoHandle(AdvancedGizmoHandle* handle)
{
    handles.insert(handle->getName(),handle);
}

//todo: remove
void AdvancedTransformGizmo::onHandleSelected(QString name,int x,int y)
{
    Q_UNUSED(name);
    Q_UNUSED(x);
    Q_UNUSED(y);
    /*
    activeGizmo = nullptr;

    //todo: remove this. active gizmo is dceided by gizmoMode and handle hits are to be done in onMousePress
    if(name.contains("__gizmo_translate"))
        activeGizmo = this->translationGizmo;

    else if(name.contains("__gizmo_rotation"))
        activeGizmo = this->rotGizmo;

    else if(name.contains("__gizmo_scale"))
        activeGizmo = this->scaleGizmo;

    activeGizmo->onHandleSelected(name,x,y);
    */
}

void AdvancedTransformGizmo::onMousePress(int x,int y)
{
    //check if gizmo was selected here as well
    if(activeGizmo!=nullptr)
        activeGizmo->onMousePress(x,y);
}

void AdvancedTransformGizmo::onMouseMove(int x,int y)
{
    if(activeGizmo!=nullptr)
        activeGizmo->onMouseMove(x,y);
}

void AdvancedTransformGizmo::onMouseRelease(int x,int y)
{
    if(activeGizmo!=nullptr)
        activeGizmo->onMouseRelease(x,y);

    //qDebug()<<"mouse released";
    //activeGizmo = nullptr;
}

bool AdvancedTransformGizmo::isGizmoHit(int x,int y)
{
    if(activeGizmo!=nullptr)
        return activeGizmo->isGizmoHit(x,y);

    return false;
}
