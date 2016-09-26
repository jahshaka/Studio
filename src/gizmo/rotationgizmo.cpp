/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
//#include "../advancedtransformgizmo.h"
#include "rotationgizmo.h"
#include "../linerenderer.h"
#include <QtMath>

AdvancedRotationGizmo::AdvancedRotationGizmo(AdvancedTransformGizmo* gizmo)
{
    this->gizmo = gizmo;
    const QString path = ":/assets/models/advancedgizmo/rot_circle.obj";
    //const QString path = "assets/models/advancedgizmo/rot_circle.obj";

    handles[HANDLE_XAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),createCircle(1.0f),true);
    handles[HANDLE_XAXIS]->setRot(0,90,0);
    handles[HANDLE_XAXIS]->setColor(255,0,0);
    //handles[HANDLE_XAXIS]->setName("__gizmo_rotate_x");


    //Y
    handles[HANDLE_YAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),createCircle(1.0f),true);
    handles[HANDLE_YAXIS]->setRot(90,0,0);
    handles[HANDLE_YAXIS]->setColor(0,255,0);
    //handles[HANDLE_YAXIS]->setName("__gizmo_rotate_y");

    handles[HANDLE_YAXIS]->hitPlanes.append(new Plane(QVector3D(0,1,0),0));//y axis
    handles[HANDLE_YAXIS]->setAxis(0,1,0);//y axis


    handles[HANDLE_ZAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),createCircle(1.0f),true);
    //handles[HANDLE_ZAXIS]->setRot(90,90,0);
    handles[HANDLE_ZAXIS]->setColor(0,0,255);
    //handles[HANDLE_ZAXIS]->setName("__gizmo_rotate_z");

    //INNER CIRCLE
    addBillboardCircle(2.1,QColor(20,20,20,100));

    //OUTER CIRCLE
    addBillboardCircle(2.5,QColor(255,255,255,255));

    gizmo->addGizmoHandle(handles[HANDLE_XAXIS]);
    gizmo->addGizmoHandle(handles[HANDLE_YAXIS]);
    gizmo->addGizmoHandle(handles[HANDLE_ZAXIS]);
}

bool AdvancedRotationGizmo::onMousePress(int x,int y)
{
    if(isHandleHit(x,y,handles[HANDLE_XAXIS]))
        activeHandle = handles[HANDLE_XAXIS];
    else if(isHandleHit(x,y,handles[HANDLE_YAXIS]))
        activeHandle = handles[HANDLE_YAXIS];
    else if(isHandleHit(x,y,handles[HANDLE_ZAXIS]))
        activeHandle = handles[HANDLE_ZAXIS];

    return false;
}

bool AdvancedRotationGizmo::onMouseMove(int x,int y)
{
    //simple highlighting test
    if(activeHandle!=nullptr)
    {
        //do mouse move
    }
    else
    {
        //if no handle is selected,
        if(isHandleHit(x,y,handles[HANDLE_YAXIS]))
            handles[HANDLE_YAXIS]->highlight();
        else
            handles[HANDLE_YAXIS]->removeHighlight();
    }

    return false;
}


bool AdvancedRotationGizmo::onMouseRelease(int x,int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);

    activeHandle = nullptr;
    return false;
}

bool AdvancedRotationGizmo::isHandleHit(int x,int y,AdvancedGizmoHandle* handle)
{
    if(gizmo->trackedNode==nullptr)
        return false;

    float innerRadius = 1.9f;
    float outerRadius = 2.1f;

    if(gizmo->transformSpace == GizmoTransformSpace::Global)
    {
        Qt3DCore::QTransform transform;
        transform.setScale(1);
        transform.setTranslation(gizmo->trackedNode->getTransform()->translation());

        auto invMatrix = transform.matrix().inverted();

        auto rayDir = gizmo->unproject(x,y);
        auto rayOrigin = gizmo->getCameraPos();

        //transform rayDir and rayOrigin to local space
        auto d = invMatrix*QVector4D(rayDir,0);
        rayDir = QVector3D(d.x(),d.y(),d.z());
        rayOrigin = invMatrix*rayOrigin;

        //do hit test
        auto hitData = handle->getRayHit(rayOrigin,rayDir);
        if(hitData==nullptr)
        {

            delete hitData;
            return false;

        }

        auto localHitDist = hitData->hitPoint.length();
        delete hitData;

        if(localHitDist >= innerRadius && localHitDist <= outerRadius)
        {
            return true;
        }


        qDebug()<<"dist: "<<localHitDist;

        return false;

    }
    else
    {
        //no localspace support yet
        return false;
    }
}

AdvancedGizmoHandle* AdvancedRotationGizmo::getHitGizmoHandle(int x,int y)
{
    if(gizmo->trackedNode==nullptr)
        return nullptr;

    float innerRadius = 1.9f;
    float outerRadius = 2.1f;

    if(gizmo->transformSpace == GizmoTransformSpace::Global)
    {
        Qt3DCore::QTransform transform;
        transform.setScale(1);
        transform.setTranslation(gizmo->trackedNode->getTransform()->translation());

        auto invMatrix = transform.matrix().inverted();

        auto rayDir = gizmo->unproject(x,y);
        auto rayOrigin = gizmo->getCameraPos();

        //transform rayDir and rayOrigin to local space
        auto d = invMatrix*QVector4D(rayDir,0);
        rayDir = QVector3D(d.x(),d.y(),d.z());
        rayOrigin = invMatrix*rayOrigin;

        for(int i=0;i<3;i++)
        {
            auto handle = handles[i];

            //do hit test
            auto hitData = handle->getRayHit(rayOrigin,rayDir);
            if(hitData==nullptr)
            {
                //no hit
                continue;
            }

            auto localHitDist = hitData->hitPoint.length();
            delete hitData;

            if(localHitDist >= innerRadius && localHitDist <= outerRadius)
            {
                return handle;
            }

        }

        //no hit
        return nullptr;

    }
    else
    {
        //no localspace support yet
        return nullptr;
    }
}

bool AdvancedRotationGizmo::isGizmoHit(int x,int y)
{
    auto handle = getHitGizmoHandle(x,y);

    return handle != nullptr;
}

void AdvancedRotationGizmo::doHandleDrag(int x,int y)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
}


LineRenderer* AdvancedRotationGizmo::createCircle(float radius)
{
    std::vector<Line3D> lines;
    for(float i=0;i<2*3.142;i+=0.1f)
    {
        Line3D line;
        float size = radius;
        line.point1 = QVector3D(size*qCos(i),size*qSin(i),0);
        line.point2 = QVector3D(size*qCos(i+0.1f),size*qSin(i+0.1f),0);
        line.color = QColor(255,0,0,255);
        lines.push_back(line);
    }

    return new LineRenderer(&lines);
}

void AdvancedRotationGizmo::addBillboardCircle(float radius,QColor color)
{
    std::vector<Line3D> lines;
    for(float i=0;i<2*3.142;i+=0.1f)
    {
        Line3D line;
        float size = radius;
        line.point1 = QVector3D(size*qCos(i),size*qSin(i),0);
        line.point2 = QVector3D(size*qCos(i+0.1f),size*qSin(i+0.1f),0);
        line.color = QColor(255,0,0,255);
        lines.push_back(line);
    }
    LineRenderer* lineRenderer = new LineRenderer(&lines);
    auto lineEnt = new Qt3DCore::QEntity(gizmo->getEntity());
    lineEnt->addComponent(lineRenderer);

    auto billMat = new BillboardMaterial();
    billMat->setUseTexture(false);
    billMat->setColor(color);
    lineEnt->addComponent(billMat);
}

/*
AdvancedScaleGizmo::AdvancedScaleGizmo(AdvancedTransformGizmo* gizmo)
{
    this->gizmo = gizmo;
    const QString path = ":/assets/models/advancedgizmo/scale_arrow.obj";

    //lines
    //LineRenderer* lineRenderer = new LineRenderer();
    //auto lines = new std::vector<Line3D>();

    //gizmo->getEntity()->addComponent(lineRenderer);

    handles[HANDLE_XAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_XAXIS]->setRot(0,0,-90);
    handles[HANDLE_XAXIS]->setColor(255,0,0);
    handles[HANDLE_XAXIS]->setName("__gizmo_scale_x");

    handles[HANDLE_YAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_YAXIS]->setRot(0,90,0);
    handles[HANDLE_YAXIS]->setColor(0,255,0);
    handles[HANDLE_YAXIS]->setName("__gizmo_scale_y");

    handles[HANDLE_ZAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_ZAXIS]->setRot(90,0,0);
    handles[HANDLE_ZAXIS]->setColor(0,0,255);
    handles[HANDLE_ZAXIS]->setName("__gizmo_scale_z");

    gizmo->addGizmoHandle(handles[HANDLE_XAXIS]);
    gizmo->addGizmoHandle(handles[HANDLE_YAXIS]);
    gizmo->addGizmoHandle(handles[HANDLE_ZAXIS]);
}
*/
