#ifndef SCALEGIZMO_H
#define SCALEGIZMO_H

#include "advancedtransformgizmo.h"

class AdvancedScaleGizmo:public GizmoTransform
{
    AdvancedGizmoHandle* handles[3];
    AdvancedTransformGizmo* gizmo;

    AdvancedGizmoHandle* activeHandle;

    //note: all his data is stored in a local space
    //plane hit on initial click
    Plane hitPlane;

    //transform of the scene node when intitially hit
    Qt3DCore::QTransform hitTransform;

    //helper to make it easier to apply local and global translations
    Qt3DCore::QTransform offsetTransform;

    //this is the initial hit point on the axis line of the handle
    QVector3D hitPointOnLine;
public:
    AdvancedScaleGizmo(AdvancedTransformGizmo* gizmo);

    virtual bool onHandleSelected(QString name,int x,int y) override;

    virtual bool onMousePress(int x,int y) override;

    virtual bool onMouseMove(int x,int y) override;

    virtual bool onMouseRelease(int x,int y) override;


    bool isHandleHit(int x,int y,AdvancedGizmoHandle* handle);


    AdvancedGizmoHandle* getHitGizmoHandle(int x,int y);

    bool isGizmoHit(int x,int y) override;

    //removes highlight from all handles
    void removeAllHighlights();

private:
    /**
     * @brief does a pick in order to get the variables necessary for dragging
     * @param x
     * @param y
     * @param handle
     * @return
     */
    void initDragging(int x,int y,AdvancedGizmoHandle* handle);
};

#endif // SCALEGIZMO_H
