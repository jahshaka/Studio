/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ICAMERACONTROLLER_H
#define ICAMERACONTROLLER_H
#include "irisgl/core/math/vec.h"
#include <QKeyEvent>
#include <Qt>
#include <QSharedPointer>
#include "irisgl/irisglfwd.h"

namespace iris
{
    class CameraNode;
}

class SettingsManager;
class CameraControllerBase
{
public:
    CameraControllerBase();

    virtual void setCamera(iris::CameraNodePtr  cam);
    iris::CameraNodePtr getCamera() { return camera; }

    virtual void onMouseDown(Qt::MouseButton button);
    virtual void onMouseUp(Qt::MouseButton button);
    virtual void onMouseMove(int x,int y);
    virtual void onMouseWheel(int val);
    virtual void onKeyPressed(Qt::Key key);
    virtual void onKeyReleased(Qt::Key key);
    /// Drops any held-key state (focus lost, controller switched) so keys can
    /// never stick down. Only the fly controller tracks keys today.
    virtual void clearKeys() {}
    virtual void keyReleaseEvent(QKeyEvent *event);
    virtual void setMousePos(int x, int y);

    virtual void start();
    virtual void update(float dt);
    virtual void end();

    virtual void postUpdate(float dt){}


    void resetMouseStates();

    /// Alt+LMB orbit (Maya/Unreal convention). The viewport turns this on
    /// when Alt+left-drag starts somewhere OTHER than the gizmo (a gizmo hit
    /// keeps its own Alt meaning: duplicate-while-transforming), and off on
    /// release. `pivot` is the point to orbit around — the selection's
    /// centre, else the last focus point / world origin.
    ///
    /// The arcball simply routes Alt+LMB into its existing orbit; the free
    /// camera gains a TEMPORARY orbit for the duration of the drag (it
    /// captures its distance to the pivot here and restores plain fly
    /// behaviour when the drag ends). Held-modifier input: deliberately NOT
    /// a ShortcutRegistry entry — the Shortcuts page lists it read-only with
    /// the other held keys.
    virtual void setAltOrbit(bool active, const iris::Vec3 &pivot);
    bool isAltOrbiting() const { return altOrbit; }

protected:
    bool altOrbit = false;
    iris::Vec3 altOrbitPivot;

    QSharedPointer<iris::CameraNode> camera;

	SettingsManager* settings;

    bool leftMouseDown;
    bool middleMouseDown;
    bool rightMouseDown;
    int mouseX;
    int mouseY;
};

#endif // ICAMERACONTROLLER_H
