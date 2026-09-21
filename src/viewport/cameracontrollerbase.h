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
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QKeyEvent>
#include <QSet>
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
    /// WHAT THE FLY THINKS IS HELD, as Qt key codes (ledger §356). Empty for a
    /// controller that tracks no keys. Reported by `editor.viewportState()`:
    /// a key stuck in this set is silent — Left and Right in it together
    /// cancel to no movement, which reads as "the arrows are dead".
    virtual QSet<int> heldKeyCodes() const { return QSet<int>(); }
    /// True while this controller's fly keys are armed (the right button is
    /// held). Reported beside the held set.
    virtual bool isFlying() const { return rightMouseDown; }
    virtual void keyReleaseEvent(QKeyEvent *event);
    virtual void setMousePos(int x, int y);

    /// SOMEBODY ELSE IS BEING FLOWN (VR-4-FIX finding 5; VR_SPEC §5 phase 4).
    ///
    /// While the editor's VR preview runs, the fly gesture — right button plus
    /// the arrow cluster — walks the WEARER instead of this camera. Only the
    /// FLY is somebody else's: the axis-view lerp, the orbit and everything
    /// else update() does are still this controller's business and still run,
    /// which is what the first cut got wrong by skipping the whole update.
    void setFlySuppressed(bool on) { flySuppressed = on; }
    bool isFlySuppressed() const { return flySuppressed; }

    virtual void start();
    virtual void update(float dt);
    virtual void end();

    virtual void postUpdate(float dt){}


    void resetMouseStates();

    /// Alt+LMB orbit (Maya/Unreal convention). The viewport turns this on
    /// when Alt+left-drag starts somewhere OTHER than the gizmo (a gizmo hit
    /// keeps its own Alt meaning: duplicate-while-transforming), and off on
    /// release. `pivot` is the point to orbit around — the point under the
    /// cursor at the press (a scene pick), else a point on the view ray at the
    /// working distance (EngineSceneViewport::altOrbitPivotAt).
    ///
    /// THE ORBIT THAT CANNOT JUMP (owner report 2026-09-15, ledger §353: "it
    /// seems to jump the viewport like an F focus on the point — hard to
    /// navigate"). Both controllers used to RE-PLACE the camera on a sphere
    /// around the pivot at their own yaw/pitch — `pos = pivot + rot·(0,0,1)·d`
    /// — so unless the camera already looked straight at the pivot, the FIRST
    /// frame of the drag moved it to centre the pivot. That was the jump, and
    /// it happened before the user had dragged anything at all.
    ///
    /// The orbit is a ROTATION now, not a placement: the camera's CURRENT
    /// offset from the pivot is turned by the drag's delta and its orientation
    /// is turned by the same delta,
    ///
    ///     offset' = R·(pos − pivot)      rot' = R·rot
    ///
    /// which leaves the pivot at exactly the same PIXEL (its position in
    /// camera space is R⁻¹R(pivot − pos) = pivot − pos, invariant) and is the
    /// identity for a zero-length drag — so nothing can move until the mouse
    /// does. Roll survives too, which the yaw/pitch rebuild destroyed.
    ///
    /// The pose is captured HERE, at the press, so the whole drag differences
    /// against one frame the way a gizmo drag does.
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
    /// Accumulates the drag's turn and writes the camera — see setAltOrbit for
    /// the maths and the reason. A no-op while the gesture is not armed, while
    /// there is no camera, and (to the bit) before the drag has moved.
    void applyAltOrbit(float yawDegrees, float pitchDegrees);

    bool altOrbit = false;
    iris::Vec3 altOrbitPivot;
    /// The pose the Alt+drag started from, and the yaw/pitch it decomposes to:
    /// the whole drag is differenced against these, so it cannot drift and a
    /// zero-length drag writes nothing at all.
    iris::Vec3 altOrbitStartPos;
    iris::Quat altOrbitStartRot;
    float altOrbitStartYaw = 0.0f, altOrbitStartPitch = 0.0f;
    /// Degrees dragged so far, cumulative.
    float altOrbitYaw = 0.0f, altOrbitPitch = 0.0f;
    bool rotationLocked = false;
    /// The fly belongs to a wearer for now (setFlySuppressed). Only the fly.
    bool flySuppressed = false;

    QSharedPointer<iris::CameraNode> camera;

	SettingsManager* settings;

    bool leftMouseDown = false;
    bool middleMouseDown = false;
    bool rightMouseDown = false;
    int mouseX = 0;
    int mouseY = 0;
};

#endif // ICAMERACONTROLLER_H
