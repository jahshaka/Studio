/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FLYSTEP_H
#define FLYSTEP_H

// THE FLY STEP — "which way do the fly keys move the camera", in one place.
//
// The editor's free flight (EditorCameraController::update) is the definition:
// forward/back along the camera's own forward, strafe along a HORIZONTAL right
// (forward x worldUp, falling back to the camera's right at the poles, where
// the cross product degenerates and A/D silently died), up/down along the
// world's up, and a Shift boost. The Assets preview had no keyboard at all
// (owner smoke S7, 2026-09-11) and was not going to grow a second copy of
// those eight lines, so the direction lives here and the two surfaces differ
// only in what they MAP onto it: the editor answers to the arrow cluster
// (WASD is free for gameplay since the navigation lane), the Assets preview
// answers to both, because it is a viewer and the hand that lands on it comes
// from either habit.
//
// Header-only, document-side: iris maths, no viewport, no Qt widgets.

#include <QSet>
#include <Qt>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"

namespace flystep {

/// What is held right now, as intentions rather than keys.
struct Keys
{
    bool forward = false, back = false, left = false, right = false;
    bool up = false, down = false, boost = false;

    bool any() const { return forward || back || left || right || up || down; }
};

/// Shift's multiplier on the fly speed (the editor's, shared).
inline constexpr float kBoost = 3.0f;

/// The world-space direction the held keys mean for a camera at `rot`, or a
/// null vector when nothing is held. NORMALISED (diagonals do not go faster).
inline iris::Vec3 direction(const iris::Quat &rot, const Keys &keys)
{
    const iris::Vec3 worldUp(0, 1, 0);
    const iris::Vec3 forward = rot.rotatedVector(iris::Vec3(0, 0, -1));
    const iris::Vec3 camRight = rot.rotatedVector(iris::Vec3(1, 0, 0));
    // Strafe stays HORIZONTAL in free flight — except at the poles, where
    // forward x worldUp degenerates to zero (Vec3::normalized() returns a null
    // vector there) and the camera's own right is the only defined answer.
    iris::Vec3 right = iris::Vec3::crossProduct(forward, worldUp).normalized();
    if (right.isNull()) right = camRight;

    iris::Vec3 move;
    if (keys.forward) move += forward;
    if (keys.back)    move -= forward;
    if (keys.right)   move += right;
    if (keys.left)    move -= right;
    if (keys.up)      move += worldUp;
    if (keys.down)    move -= worldUp;
    if (move.isNull()) return move;
    return move.normalized();
}

/// One frame of flight: the world-space offset for `dt` seconds at `speed`
/// metres per second (Shift included).
inline iris::Vec3 delta(const iris::Quat &rot, const Keys &keys, float speed, float dt)
{
    const iris::Vec3 dir = direction(rot, keys);
    if (dir.isNull()) return dir;
    return dir * speed * (keys.boost ? kBoost : 1.0f) * dt;
}

/// The held-key bookkeeping for a surface that has no camera controller of its
/// own (the Assets preview). ARROWS + WASD + Q/E + PageUp/PageDown, because a
/// viewer is flown by whichever hand lands on it.
class HeldKeys
{
public:
    /// True when the key is a fly key (the caller then accepts the event).
    bool press(int key)
    {
        if (!isFlyKey(key)) return false;
        mHeld.insert(key);
        return true;
    }
    bool release(int key)
    {
        if (!isFlyKey(key)) return false;
        mHeld.remove(key);
        return true;
    }
    void clear() { mHeld.clear(); mBoost = false; }
    void setBoost(bool on) { mBoost = on; }

    Keys state() const
    {
        Keys k;
        k.forward = held(Qt::Key_Up)   || held(Qt::Key_W);
        k.back    = held(Qt::Key_Down) || held(Qt::Key_S);
        k.left    = held(Qt::Key_Left) || held(Qt::Key_A);
        k.right   = held(Qt::Key_Right)|| held(Qt::Key_D);
        k.up      = held(Qt::Key_E)    || held(Qt::Key_PageUp);
        k.down    = held(Qt::Key_Q)    || held(Qt::Key_PageDown);
        k.boost   = mBoost;
        return k;
    }

    static bool isFlyKey(int key)
    {
        switch (key) {
        case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_W: case Qt::Key_A: case Qt::Key_S: case Qt::Key_D:
        case Qt::Key_Q: case Qt::Key_E:
        case Qt::Key_PageUp: case Qt::Key_PageDown:
            return true;
        default:
            return false;
        }
    }

private:
    bool held(int key) const { return mHeld.contains(key); }

    QSet<int> mHeld;
    bool mBoost = false;
};

}   // namespace flystep

#endif   // FLYSTEP_H
