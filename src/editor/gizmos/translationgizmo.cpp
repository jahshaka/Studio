/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "advancedtransformgizmo.h"

/**
 * @brief
 * hit planes are local-space
 * @param gizmo
 */
AdvancedTranslationGizmo::AdvancedTranslationGizmo(AdvancedTransformGizmo* gizmo)
{
    this->gizmo = gizmo;
    const QString path = ":/app/models/trans_arrow.obj";
    //const QString path = "assets/models/advancedgizmo/trans_arrow.obj";

    //X
    handles[HANDLE_XAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_XAXIS]->setRot(0,0,-90);
    handles[HANDLE_XAXIS]->setColor(255,0,0);
    //handles[HANDLE_XAXIS]->setName("__gizmo_translate_x");

    //hit planes
    handles[HANDLE_XAXIS]->hitPlanes.append(new Plane(QVector3D(0,1,0),0));//y axis
    handles[HANDLE_XAXIS]->hitPlanes.append(new Plane(QVector3D(0,0,1),0));//z axis

    handles[HANDLE_XAXIS]->setAxis(1,0,0);//x axis

    //Y
    handles[HANDLE_YAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_YAXIS]->setRot(0,90,0);
    handles[HANDLE_YAXIS]->setColor(0,255,0);
    //handles[HANDLE_YAXIS]->setName("__gizmo_translate_y");

    //hit planes
    handles[HANDLE_YAXIS]->hitPlanes.append(new Plane(QVector3D(1,0,0),0));//x axis
    handles[HANDLE_YAXIS]->hitPlanes.append(new Plane(QVector3D(0,0,1),0));//z axis

    handles[HANDLE_YAXIS]->setAxis(0,1,0);//y axis

    //Z
    handles[HANDLE_ZAXIS] = new AdvancedGizmoHandle(gizmo->getEntity(),path);
    handles[HANDLE_ZAXIS]->setRot(90,0,0);
    handles[HANDLE_ZAXIS]->setColor(0,0,255);
    //handles[HANDLE_ZAXIS]->setName("__gizmo_translate_z");

    //hit planes
    handles[HANDLE_ZAXIS]->hitPlanes.append(new Plane(QVector3D(1,0,0),0));//x axis
    handles[HANDLE_ZAXIS]->hitPlanes.append(new Plane(QVector3D(0,1,0),0));//y axis

    handles[HANDLE_ZAXIS]->setAxis(0,0,1);//z axis

    gizmo->addGizmoHandle(handles[HANDLE_XAXIS]);
    gizmo->addGizmoHandle(handles[HANDLE_YAXIS]);
    gizmo->addGizmoHandle(handles[HANDLE_ZAXIS]);

    activeHandle = nullptr;

    handleThickness = 0.5f;
    handleLength = 2;
}


bool AdvancedTranslationGizmo::onHandleSelected(QString name,int x,int y)
{
    Q_UNUSED(name);
    Q_UNUSED(x);
    Q_UNUSED(y);
    return false;
}

bool AdvancedTranslationGizmo::onMousePress(int x,int y)
{
    this->removeAllHighlights();

    auto handle = this->getHitGizmoHandle(x,y);
    if(handle)
    {
        //qDebug()<<"Mouse hit!"<<endl;
        this->initDragging(x,y,handle);
        handle->highlight();
    }

    return false;
}

bool AdvancedTranslationGizmo::onMouseMove(int x,int y)
{
    this->removeAllHighlights();

    //highlight test
    //only if no active handle is chosen
    auto hitHandle = this->getHitGizmoHandle(x,y);
    if(hitHandle!=nullptr)
        hitHandle->highlight();


    if(activeHandle==nullptr)
        return false;
    else
        activeHandle->highlight();

    if(gizmo->transformSpace == GizmoTransformSpace::Global)
    {

        //qDebug()<<"moved";
        activeHandle->highlight();

        //use original hit transform to get back matrix to convert
        //ray dir and origin to local space
        Qt3DCore::QTransform transform;
        transform.setTranslation(hitTransform.translation());

        auto invMatrix = transform.matrix().inverted();

        auto rayDir = gizmo->unproject(x,y);
        auto rayOrigin = gizmo->getCameraPos();

        //transform rayDir and rayOrigin to local space
        auto dir = invMatrix*QVector4D(rayDir,0);
        rayDir = QVector3D(dir.x(),dir.y(),dir.z());
        rayOrigin = invMatrix*rayOrigin;

        //do hit test
        float t = 0;
        auto hitPoint = hitPlane.rayIntersect(rayOrigin,rayDir,t);
        if(hitPoint!=rayOrigin)
        {
            //hit the original hit plane
            //calculate offset and transform trackedNode

            //get point along axis segment
            QVector3D pointOnLine;
            auto axis = activeHandle->getAxis();
            CollisionHelper::CollisionClosestPointOnSegment(hitPoint,
                                                            axis*-AXIS_SIZE,
                                                            axis*AXIS_SIZE,
                                                            pointOnLine);

            //calculate distance from original hit point to this hit point
            auto offset = pointOnLine - hitPointOnLine;

            //shortcut, this can do for now
            //global space translation
            gizmo->trackedNode->pos = hitTransform.translation()+offset;
            //gizmo->trackedNode->pos = hitPoint;
            //todo: change to true when done testing
            gizmo->trackedNode->_updateTransform(false);

        }
    }

    return false;
}

bool AdvancedTranslationGizmo::onMouseRelease(int x,int y)
{
    this->removeAllHighlights();
    activeHandle = nullptr;

    Q_UNUSED(x);
    Q_UNUSED(y);
    return false;
}

bool AdvancedTranslationGizmo::isHandleHit(int x,int y,AdvancedGizmoHandle* handle)
{
    if(gizmo->transformSpace == GizmoTransformSpace::Global)
    {
        Qt3DCore::QTransform transform;
        transform.setScale(1);
        transform.setTranslation(gizmo->trackedNode->getTransform()->translation());

        QVector3D rayDir,rayOrigin;
        calcRayDirAndOrigin(x,y,transform.matrix(),rayDir,rayOrigin);

        //do hit test
        //float t = 0;

        //auto hitPoint = hitPlane.rayIntersect(rayOrigin,rayDir,t);
        auto hitData = handle->getRayHit(rayOrigin,rayDir);
        if(hitData==nullptr)
            return false;

        auto hitPoint = hitData->hitPoint;
        delete hitData;

        if(hitPoint!=rayOrigin)
        {

            //get point along axis segment
            QVector3D pointOnLine;
            auto axis = handle->getAxis();
            CollisionHelper::CollisionClosestPointOnSegment(hitPoint,
                                                            axis*-AXIS_SIZE,
                                                            axis*AXIS_SIZE,
                                                            pointOnLine);

            auto perpDistSqrd = (hitPoint-pointOnLine).lengthSquared();
            if(pointOnLine.lengthSquared() <= handleLength*handleLength
                    && perpDistSqrd<=(handleThickness*handleThickness)
                    && QVector3D::dotProduct(axis,pointOnLine)>0)
            {
                return true;
            }

            return false;

        }

        return false;
    }

    return false;
}

void AdvancedTranslationGizmo::initDragging(int x,int y,AdvancedGizmoHandle* handle)
{
    if(gizmo->transformSpace == GizmoTransformSpace::Global)
    {
        Qt3DCore::QTransform transform;
        transform.setScale(1);
        transform.setTranslation(gizmo->trackedNode->getTransform()->translation());

        QVector3D rayDir,rayOrigin;
        calcRayDirAndOrigin(x,y,transform.matrix(),rayDir,rayOrigin);

        //do hit test
        //float t = 0;

        //auto hitPoint = hitPlane.rayIntersect(rayOrigin,rayDir,t);
        auto hitData = handle->getRayHit(rayOrigin,rayDir);
        if(hitData==nullptr)
            return;

        auto hitPoint = hitData->hitPoint;


        if(hitPoint!=rayOrigin)
        {

            //get point along axis segment
            QVector3D pointOnLine;
            auto axis = handle->getAxis();
            CollisionHelper::CollisionClosestPointOnSegment(hitPoint,
                                                            axis*-AXIS_SIZE,
                                                            axis*AXIS_SIZE,
                                                            pointOnLine);

            auto perpDistSqrd = (hitPoint-pointOnLine).lengthSquared();
            if(pointOnLine.lengthSquared() <= handleLength*handleLength
                    && perpDistSqrd<=(handleThickness*handleThickness)
                    && QVector3D::dotProduct(axis,pointOnLine)>0)//must be on the positive axis
            {

                hitPlane = hitData->plane1;
                hitPointOnLine = pointOnLine;
                //hitTransform = *(gizmo->trackedNode->getTransform());
                hitTransform.setMatrix(gizmo->trackedNode->getTransform()->matrix());
                activeHandle = handle;
            }

        }

        //delete hitData;
    }
}

AdvancedGizmoHandle* AdvancedTranslationGizmo::getHitGizmoHandle(int x,int y)
{
    if(gizmo->trackedNode==nullptr)
        return nullptr;

    if(gizmo->transformSpace == GizmoTransformSpace::Global)
    {
        Qt3DCore::QTransform transform;
        transform.setScale(1);
        transform.setTranslation(gizmo->trackedNode->getTransform()->translation());

        QVector3D rayDir,rayOrigin;
        calcRayDirAndOrigin(x,y,transform.matrix(),rayDir,rayOrigin);


        for(int i=0;i<3;i++)
        {
            auto handle = handles[i];

            //auto hitPoint = hitPlane.rayIntersect(rayOrigin,rayDir,t);
            auto hitData = handle->getRayHit(rayOrigin,rayDir);
            if(hitData==nullptr)
                return nullptr;

            auto hitPoint = hitData->hitPoint;
            delete hitData;

            if(hitPoint!=rayOrigin)
            {

                //get point along axis segment
                QVector3D pointOnLine;
                auto axis = handle->getAxis();
                CollisionHelper::CollisionClosestPointOnSegment(hitPoint,
                                                                axis*-AXIS_SIZE,
                                                                axis*AXIS_SIZE,
                                                                pointOnLine);

                auto perpDistSqrd = (hitPoint-pointOnLine).lengthSquared();
                if(pointOnLine.lengthSquared() <= handleLength*handleLength
                        && perpDistSqrd<=(handleThickness*handleThickness)
                        && QVector3D::dotProduct(axis,pointOnLine)>0)//must be on the positive axis)
                {
                    return handle;
                }

            }
        }
    }

    return nullptr;
}

void AdvancedTranslationGizmo::calcRayDirAndOrigin(int x,int y,QMatrix4x4 transform, QVector3D& rayDir,QVector3D& rayOrigin)
{
    auto invMatrix = transform.inverted();

    rayDir = gizmo->unproject(x,y);
    rayOrigin = gizmo->getCameraPos();

    //transform rayDir and rayOrigin to local space
    auto dir = invMatrix*QVector4D(rayDir,0);
    rayDir = QVector3D(dir.x(),dir.y(),dir.z());
    rayOrigin = invMatrix*rayOrigin;
}

bool AdvancedTranslationGizmo::calcHandleHitPoint(int x,int y,AdvancedGizmoHandle* handle,QMatrix4x4 transform,QVector3D hitPoint)
{
    QVector3D rayDir,rayOrigin;
    calcRayDirAndOrigin(x,y,transform,rayDir,rayOrigin);

    auto hitData = handle->getRayHit(rayOrigin,rayDir);
    if(hitData==nullptr)
        return false;

    hitPoint = hitData->hitPoint;
    delete hitData;

    return true;
}

bool AdvancedTranslationGizmo::isGizmoHit(int x,int y)
{
    auto handle = getHitGizmoHandle(x,y);
    return (handle!=nullptr);
}

void AdvancedTranslationGizmo::removeAllHighlights()
{
    for(int i=0;i<3;i++)
    {
        handles[i]->removeHighlight();
    }
}
