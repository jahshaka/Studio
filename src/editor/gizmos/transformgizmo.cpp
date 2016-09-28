/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <Qt3DCore/QTransform>
#include "transformgizmo.h"


GizmoHandle::GizmoHandle(QEntity* parent,QString gizmoPath)
{
    entity = new QEntity(parent);
    mesh = new QMesh();
    mesh->setSource(QUrl::fromLocalFile(gizmoPath));
    float size = 0.3f;

    transform = new Qt3DCore::QTransform();
    transform->setScale(size);
    mat = new GizmoMaterial();

    //entity->addComponent(cube);
    entity->addComponent(mesh);
    entity->addComponent(transform);
    entity->addComponent(mat);
    //setOffset(offset);
}

void GizmoHandle::setOffset(QVector3D offset)
{
    this->offset = offset;
    transform->setTranslation(offset);
}

void GizmoHandle::setRot(float xrot,float yrot,float zrot)
{
    transform->setRotationX(xrot);
    transform->setRotationY(yrot);
    transform->setRotationZ(zrot);
}

void GizmoHandle::setColor(int r,int g,int b)
{
    mat->setColor(QColor::fromRgb(r,g,b));
}


TransformGizmo::TransformGizmo(QEntity* root)
{
    trackedNode = nullptr;
    entity = new QEntity(root);

    transform = new Qt3DCore::QTransform();
    entity->addComponent(transform);

    translationGizmo = new TranslationGizmo(this);
    rotGizmo = new RotationGizmo(this);
    scaleGizmo = new ScaleGizmo(this);
}

void TransformGizmo::setScale(QVector3D scale)
{
    //todo: multiply scale by model extents
    translationGizmo->setScale(scale/2);
    scaleGizmo->setScale(scale/2);
    rotGizmo->setScale(scale/2);
}

RotGizmoAxis::RotGizmoAxis(QEntity* parent)
{
    entity = new QEntity(parent);

    transform = new Qt3DCore::QTransform();
    entity->addComponent(transform);

    //add 4 handles
    const QString path = ":/assets/models/rot_arrow.obj";

    handles[HANDLE_TOPRIGHT] = new GizmoHandle(entity,path);
    handles[HANDLE_TOPRIGHT]->setOffset(QVector3D(1,1,0));
    handles[HANDLE_TOPRIGHT]->setRot(0,0,0);

    handles[HANDLE_BOTTOMRIGHT] = new GizmoHandle(entity,path);
    handles[HANDLE_BOTTOMRIGHT]->setOffset(QVector3D(1,-1,0));
    handles[HANDLE_BOTTOMRIGHT]->setRot(0,0,-90);

    handles[HANDLE_BOTTOMLEFT] = new GizmoHandle(entity,path);
    handles[HANDLE_BOTTOMLEFT]->setOffset(QVector3D(-1,-1,0));
    handles[HANDLE_BOTTOMLEFT]->setRot(0,0,180);

    handles[HANDLE_TOPLEFT] = new GizmoHandle(entity,path);
    handles[HANDLE_TOPLEFT]->setOffset(QVector3D(-1,1,0));
    handles[HANDLE_TOPLEFT]->setRot(0,0,90);
}

void RotGizmoAxis::setColor(int r,int g,int b)
{
    for(int i=0;i<4;i++)
        handles[i]->setColor(r,g,b);
}

void RotGizmoAxis::setScale(QVector3D scale)
{
    handles[HANDLE_TOPRIGHT]->setOffset(QVector3D(scale.x(),scale.y(),0));
    handles[HANDLE_BOTTOMRIGHT]->setOffset(QVector3D(scale.x(),-scale.y(),0));
    handles[HANDLE_BOTTOMLEFT]->setOffset(QVector3D(-scale.x(),-scale.y(),0));
    handles[HANDLE_TOPLEFT]->setOffset(QVector3D(-scale.x(),scale.y(),0));
}

void RotGizmoAxis::setRot(float xrot,float yrot,float zrot)
{
    transform->setRotationX(xrot);
    transform->setRotationY(yrot);
    transform->setRotationZ(zrot);
}

void TranslationGizmo::setScale(QVector3D scale)
{
    handles[HANDLE_XPOSITIVE]->setOffset(QVector3D(scale.x(),0,0));
    handles[HANDLE_XNEGATIVE]->setOffset(QVector3D(-scale.x(),0,0));
    handles[HANDLE_YPOSITIVE]->setOffset(QVector3D(0,scale.y(),0));
    handles[HANDLE_YNEGATIVE]->setOffset(QVector3D(0,-scale.y(),0));
    handles[HANDLE_ZPOSITIVE]->setOffset(QVector3D(0,0,scale.z()));
    handles[HANDLE_ZNEGATIVE]->setOffset(QVector3D(0,0,-scale.z()));
}

void ScaleGizmo::setScale(QVector3D scale)
{
    handles[HANDLE_XPOSITIVE]->setOffset(QVector3D(scale.x(),0,0));
    handles[HANDLE_XNEGATIVE]->setOffset(QVector3D(-scale.x(),0,0));
    handles[HANDLE_YPOSITIVE]->setOffset(QVector3D(0,scale.y(),0));
    handles[HANDLE_YNEGATIVE]->setOffset(QVector3D(0,-scale.y(),0));
    handles[HANDLE_ZPOSITIVE]->setOffset(QVector3D(0,0,scale.z()));
    handles[HANDLE_ZNEGATIVE]->setOffset(QVector3D(0,0,-scale.z()));
}

void TransformGizmo::trackNode(SceneNode* node)
{
    if(trackedNode!=nullptr)
        trackedNode->setListener(nullptr);
    trackedNode = node;
    node->setListener(this);
    onTransformUpdated(node);
}

void TransformGizmo::onTransformUpdated(SceneNode* node)
{
    transform->setTranslation(node->pos);
    auto rot = node->rot;
    transform->setRotation(QQuaternion::fromEulerAngles(rot.y(),rot.x(),rot.z()));
    this->setScale(node->getScale());
}

TranslationGizmo::TranslationGizmo(TransformGizmo* gizmo)
{
    this->gizmo = gizmo;
    const QString path = ":/assets/models/trans_arrow.obj";

    handles[HANDLE_XPOSITIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_XPOSITIVE]->setOffset(QVector3D(1,0,0));
    handles[HANDLE_XPOSITIVE]->setRot(0,0,-90);
    handles[HANDLE_XPOSITIVE]->setColor(255,0,0);

    handles[HANDLE_XNEGATIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_XNEGATIVE]->setOffset(QVector3D(-1,0,0));
    handles[HANDLE_XNEGATIVE]->setRot(0,0,90);
    handles[HANDLE_XNEGATIVE]->setColor(255,0,0);

    handles[HANDLE_YPOSITIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_YPOSITIVE]->setOffset(QVector3D(0,1,0));
    handles[HANDLE_YPOSITIVE]->setRot(0,90,0);
    handles[HANDLE_YPOSITIVE]->setColor(0,255,0);

    handles[HANDLE_YNEGATIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_YNEGATIVE]->setOffset(QVector3D(0,-1,0));
    handles[HANDLE_YNEGATIVE]->setRot(0,-90,180);
    handles[HANDLE_YNEGATIVE]->setColor(0,255,0);

    handles[HANDLE_ZPOSITIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_ZPOSITIVE]->setOffset(QVector3D(0,0,1));
    handles[HANDLE_ZPOSITIVE]->setRot(90,0,0);
    handles[HANDLE_ZPOSITIVE]->setColor(0,0,255);

    handles[HANDLE_ZNEGATIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_ZNEGATIVE]->setOffset(QVector3D(0,0,-1));
    handles[HANDLE_ZNEGATIVE]->setRot(-90,0,0);
    handles[HANDLE_ZNEGATIVE]->setColor(0,0,255);
}

RotationGizmo::RotationGizmo(TransformGizmo* gizmo)
{
    this->gizmo = gizmo;

    handles[HANDLE_XAXIS] = new RotGizmoAxis(gizmo->getEntity());
    handles[HANDLE_XAXIS]->setRot(0,90,0);
    handles[HANDLE_XAXIS]->setColor(255,0,0);

    handles[HANDLE_YAXIS] = new RotGizmoAxis(gizmo->getEntity());
    handles[HANDLE_YAXIS]->setRot(90,0,0);
    handles[HANDLE_YAXIS]->setColor(0,255,0);

    handles[HANDLE_ZAXIS] = new RotGizmoAxis(gizmo->getEntity());
    //handles[HANDLE_ZAXIS]->setRot(90,90,0);
    handles[HANDLE_ZAXIS]->setColor(0,0,255);
}

void RotationGizmo::setScale(QVector3D scale)
{
    handles[HANDLE_XAXIS]->setScale(QVector3D(scale.z(),scale.y(),1));

    handles[HANDLE_YAXIS]->setScale(QVector3D(scale.x(),scale.z(),1));

    handles[HANDLE_ZAXIS]->setScale(QVector3D(scale.x(),scale.y(),1));
}

ScaleGizmo::ScaleGizmo(TransformGizmo* gizmo)
{
    this->gizmo = gizmo;
    const QString path = ":/assets/models/scale_arrow.obj";

    handles[HANDLE_XPOSITIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_XPOSITIVE]->setOffset(QVector3D(1,0,0));
    handles[HANDLE_XPOSITIVE]->setRot(0,0,-90);
    handles[HANDLE_XPOSITIVE]->setColor(255,0,0);

    handles[HANDLE_XNEGATIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_XNEGATIVE]->setOffset(QVector3D(-1,0,0));
    handles[HANDLE_XNEGATIVE]->setRot(0,0,90);
    handles[HANDLE_XNEGATIVE]->setColor(255,0,0);

    handles[HANDLE_YPOSITIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_YPOSITIVE]->setOffset(QVector3D(0,1,0));
    handles[HANDLE_YPOSITIVE]->setRot(0,90,0);
    handles[HANDLE_YPOSITIVE]->setColor(0,255,0);

    handles[HANDLE_YNEGATIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_YNEGATIVE]->setOffset(QVector3D(0,-1,0));
    handles[HANDLE_YNEGATIVE]->setRot(0,-90,180);
    handles[HANDLE_YNEGATIVE]->setColor(0,255,0);

    handles[HANDLE_ZPOSITIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_ZPOSITIVE]->setOffset(QVector3D(0,0,1));
    handles[HANDLE_ZPOSITIVE]->setRot(90,0,0);
    handles[HANDLE_ZPOSITIVE]->setColor(0,0,255);

    handles[HANDLE_ZNEGATIVE] = new GizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_ZNEGATIVE]->setOffset(QVector3D(0,0,-1));
    handles[HANDLE_ZNEGATIVE]->setRot(-90,0,0);
    handles[HANDLE_ZNEGATIVE]->setColor(0,0,255);
}
