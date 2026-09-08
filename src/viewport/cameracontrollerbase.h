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

    /// ADOPT a camera. THE CONTRACT, and it is load-bearing: a controller may
    /// read whatever it needs out of the camera's pose here (both real ones
    /// decompose the rotation into yaw/pitch), but it must NOT WRITE THE NODE.
    /// Adoption happens constantly and invisibly — every project open, every
    /// pilot/eject, every screenshot that borrows the viewport — and the node
    /// is the user's DOCUMENT. Writing a pose back through a yaw/pitch round
    /// trip zeroes roll permanently, which broke socketed and authored cameras
    /// (fixed 2026-09-06; the regression gates are cameras.e2e.pilot's roll
    /// case and sockets.e2e's screenshot case). Only real navigation input
    /// moves a camera.
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

    /// THE AXIS-VIEW LOCK (owner report 2026-09-08: "when in Top/Left/Right/
    /// Bottom views we should not be able to rotate the camera — only pan and
    /// zoom; the camera should be locked top-down, bottom-up etc").
    ///
    /// While it is on, no GESTURE may turn the camera: the RMB look drag, the
    /// Alt+LMB orbit and the arcball's own drag are IGNORED — not redirected,
    /// not answered by dropping out of the view (Blender snaps back to
    /// perspective there; Unreal and Maya ignore, and so do we — a gesture
    /// that silently changes which view you are in is the surprising answer).
    /// Panning, zooming and the fly keys keep working; the fly keys move on the
    /// camera's OWN basis instead of the world's while locked, so a top view
    /// pans across the map rather than dollying into the floor.
    ///
    /// The lock is a VIEWPORT decision (EngineSceneViewport::cameraRotationLocked:
    /// an axis view, and not piloting a scene camera), pushed to BOTH editor
    /// controllers so a camera-mode switch cannot lose it. It constrains
    /// gestures only — editor.setCamera and the other placement verbs still
    /// write whatever pose they are given, and editor.setView("perspective")
    /// unlocks and restores the remembered perspective pose.
    void setRotationLocked(bool locked) { rotationLocked = locked; }
    bool isRotationLocked() const { return rotationLocked; }

protected:
    bool altOrbit = false;
    iris::Vec3 altOrbitPivot;
    bool rotationLocked = false;

    QSharedPointer<iris::CameraNode> camera;

	SettingsManager* settings;

    bool leftMouseDown;
    bool middleMouseDown;
    bool rightMouseDown;
    int mouseX;
    int mouseY;
};

#endif // ICAMERACONTROLLER_H
