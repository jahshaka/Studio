/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef ROTATIONGIZMO_H
#define ROTATIONGIZMO_H

#include "gizmotransform.h"
#include "../advancedtransformgizmo.h"

class AdvancedRotationGizmo:public GizmoTransform
{
    AdvancedGizmoHandle* handles[3];
    AdvancedTransformGizmo* gizmo;
public:
    AdvancedRotationGizmo(AdvancedTransformGizmo* gizmo);

    bool onMousePress(int x,int y) override;

    bool onMouseMove(int x,int y) override;

    bool onMouseRelease(int x,int y) override;

private:
    LineRenderer* createCircle(float radius);
    void addBillboardCircle(float radius,QColor color);

    AdvancedGizmoHandle* activeHandle;

    //note: all his data is stored in a local space
    //plane hit on initial click
    Plane hitPlane;

    //transform of the scene node when intitially hit
    Qt3DCore::QTransform hitTransform;

    //this is the initial hit point on the axis line of the handle
    QVector3D hitPointOnPlane;

    bool isHandleHit(int x,int y,AdvancedGizmoHandle* handle);


    /**
     * @brief If a handle is it then it is returned. Otherwise, nullptr is returned.
     * @param x
     * @param y
     * @return
     */
    AdvancedGizmoHandle* getHitGizmoHandle(int x,int y);

    /**
     * @brief Does a hit check on all handles.
     * Returns true if at least one handle is hit, returns false otherwise
     * @return
     */
    bool isGizmoHit(int x,int y) override;

    void doHandleDrag(int x,int y);


};


#endif // ROTATIONGIZMO_H
