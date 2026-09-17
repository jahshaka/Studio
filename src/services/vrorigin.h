/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRORIGIN_H
#define VRORIGIN_H

// THE RIG'S ARITHMETIC — where the wearer's room stands in the world, and what
// the fly keys do to it (SPECS/VR_SPEC.md §4.5, phase 3).
//
// IT LIVES IN services/ BECAUSE IT HAS THREE READERS (VR-4-FIX finding 10): the
// Player's VR mode (src/player), the editor's VR preview (src/modules/vr) and
// the pose spelling every verb answers with (src/bridge/vrnames.h, which needed
// nothing of the Player but this file's yawDegrees and used to reach into
// src/player for it). A shared, engine-free, Qt-free header belongs beside the
// other things every layer may include.
//
// WHY THIS IS A HEADER OF PURE FUNCTIONS. An OpenXR runtime reports the head in
// a ROOM — a floor origin with the wearer somewhere on it — and the engine
// composes that pose with a rig the host places (Engine::setVrOrigin). Every
// interesting decision in the Player's VR mode is therefore a small piece of
// vector arithmetic between two poses, and every one of them can be wrong in a
// way that is only visible ON A HEADSET: a sign that teleports you, a yaw that
// strafes you sideways, a recentre that puts the floor at your eyes. None of
// that is testable through a picture, and all of it is testable here — so the
// arithmetic lives in functions with no engine, no Qt and no runtime in them,
// and `player.vr`'s headless arm asserts it on every gate, on every box, with
// no headset and no Monado.
//
// TWO CONVENTIONS, STATED ONCE:
//
//   * YAW is degrees about +Y, and yaw 0 looks down -Z (the direction an
//     unrotated camera looks in this document model). A positive yaw is the
//     right-handed rotation about +Y, which is what
//     `Ogre::Quaternion(Degree(yaw), UNIT_Y)` builds — the engine's setVrOrigin
//     takes exactly this number, so `yawDegrees(levelForward(q))` round trips
//     through the engine and back out of `vrStatus().headRotation`.
//   * THE RIG HAS NO PITCH AND NO ROLL, ever. A room has a floor: tilting it
//     would tilt the horizon under a standing person, which is the one thing a
//     VR renderer must never do. Locomotion therefore moves a POSITION and a
//     HEADING, and the wearer's own pitch and roll stay the wearer's.

#include <cmath>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/flystep.h"

namespace vrorigin {

/// The rig: where the reference space's origin stands, and which way the room
/// faces. Exactly what Engine::setVrOrigin takes.
struct Rig
{
    iris::Vec3 position;
    float yaw = 0.0f;          ///< degrees about +Y
};

/// A ROTATION'S HEADING, as a unit vector on the ground plane.
///
/// The forward flattened onto XZ — except when the head is looking straight up
/// or straight down, where the forward HAS no ground component and the answer
/// comes from the head's own up vector instead (looking down, "up" points along
/// the heading; looking up, it points against it). Without that branch a wearer
/// who glances at their feet and presses forward walks in a random direction,
/// which is the fly bug flystep.h documents at the poles, in a place where a
/// person actually goes.
inline iris::Vec3 levelForward(const iris::Quat &rot)
{
    const iris::Vec3 f = rot.rotatedVector(iris::Vec3(0, 0, -1));
    iris::Vec3 flat(f.x(), 0.0f, f.z());
    if (flat.lengthSquared() < 1e-8f) {
        const iris::Vec3 u = rot.rotatedVector(iris::Vec3(0, 1, 0));
        flat = f.y() < 0.0f ? iris::Vec3(u.x(), 0.0f, u.z())
                            : iris::Vec3(-u.x(), 0.0f, -u.z());
    }
    if (flat.lengthSquared() < 1e-8f) return iris::Vec3(0, 0, -1);   // degenerate
    return flat.normalized();
}

/// The heading as an ANGLE, in the convention above: yaw 0 looks down -Z.
/// `Ry(yaw) * (0,0,-1) = (-sin yaw, 0, -cos yaw)`, so the inverse is this.
inline float yawDegrees(const iris::Vec3 &levelFwd)
{
    return float(std::atan2(double(-levelFwd.x()), double(-levelFwd.z())) * 180.0 / M_PI);
}

inline float yawDegrees(const iris::Quat &rot) { return yawDegrees(levelForward(rot)); }

/// A vector turned about +Y by `deg`, right-handed — the rotation the engine
/// applies to the runtime's pose, reproduced here so the host can predict it.
inline iris::Vec3 rotateY(const iris::Vec3 &v, float deg)
{
    const double r = double(deg) * M_PI / 180.0;
    const float c = float(std::cos(r)), s = float(std::sin(r));
    return iris::Vec3(v.x() * c + v.z() * s, v.y(), -v.x() * s + v.z() * c);
}

/// The shortest signed difference between two headings, in (-180, 180].
inline float yawDelta(float from, float to)
{
    float d = std::fmod(to - from + 540.0f, 360.0f);
    if (d < 0.0f) d += 360.0f;                 // fmod keeps the sign of the dividend
    return d - 180.0f;
}

/// RE-PLACE THE RIG SO THE HEAD LANDS ON A TARGET POSE — "you are standing
/// here, looking that way".
///
/// This is the whole of "the session's origin follows the camera the Player
/// drives": the rig moves so that the wearer's head coincides with the play
/// camera, in position and in HEADING. Their pitch and roll are untouched (see
/// the file header), and so is their offset from the rig — which is what makes
/// it a RECENTRE rather than a teleport: a wearer standing a metre to the left
/// of their room's origin is still a metre to the left of it afterwards, and
/// walking back to the middle of the room moves them in the world.
///
/// Derivation, so the signs are checkable rather than remembered. Write the rig
/// as R = (yaw r, translation t) and the head's world pose as H = R * S, where
/// S is the runtime's own (scaled) pose. We want an R' with R' * S = T for the
/// target T, i.e. R' = T * H^-1 * R. In yaw-and-translation terms with
/// d = yaw(T) - yaw(H):
///
///     yaw(R') = yaw(R) + d
///     t'      = pos(T) + Ry(d) * (t - pos(H))
inline Rig placedOn(const Rig &rig, const iris::Vec3 &headPos, const iris::Quat &headRot,
                    const iris::Vec3 &targetPos, const iris::Quat &targetRot)
{
    const float d = yawDelta(yawDegrees(headRot), yawDegrees(targetRot));
    Rig out;
    out.yaw = rig.yaw + d;
    out.position = targetPos + rotateY(rig.position - headPos, d);
    return out;
}

/// ONE FRAME OF HEAD-RELATIVE FLIGHT, as a world-space offset for the RIG.
///
/// THE HEADING IS THE HEAD'S, AND IT IS LEVEL. Forward is where the wearer is
/// LOOKING, flattened onto the ground; strafe is horizontal beside it. That is
/// what nearly every VR locomotion scheme does, and the reason is physiological
/// rather than aesthetic: vertical motion the wearer did not ask for is the
/// single most reliable way to make somebody sick, and a wearer glancing down
/// at an object while walking forward must not dive into the floor. The desktop
/// Player flies along the camera's TRUE forward (flystep.h) precisely because
/// there is no vestibular system attached to a monitor.
///
/// Q/E (up/down) still move along the WORLD's up, unchanged: that is an
/// explicit request, not a side effect of where the wearer happened to look.
///
/// `seconds` is taken AT FACE VALUE — a distance, not a frame. The clamp that
/// belongs to a frame is frameSeconds() below, applied by the caller that HAS a
/// frame, exactly as flystep::delta leaves it to the camera controller: a verb
/// that asks to walk the wearer forward for two seconds must walk them forward
/// for two seconds, and a verb that silently delivered a fifteenth of it would
/// be a bug with a very quiet symptom.
inline iris::Vec3 flyDelta(const iris::Quat &headRot, const flystep::Keys &keys,
                           float speed, float seconds)
{
    const iris::Vec3 fwd = levelForward(headRot);
    const iris::Vec3 dir = flystep::directionAlong(fwd, iris::Vec3(1, 0, 0), keys);
    if (dir.isNull()) return dir;
    return dir * speed * (keys.boost ? flystep::kBoost : 1.0f) * seconds;
}

/// THE RIG, AFTER THE RUNTIME RECENTRED THE ROOM UNDER THE WEARER.
///
/// A runtime may re-origin its own reference space at any moment — the Quest's
/// long-press, a guardian re-setup — and it announces it with
/// `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING` carrying
/// `poseInPreviousSpace`: the NEW space's origin expressed in the OLD one, here
/// `pose*`. Every pose reported afterwards is in the new space, so a wearer
/// standing perfectly still JUMPS across the world by whatever the runtime
/// moved — the most violent thing a VR renderer can do to somebody, and done by
/// a gesture they may have made for an entirely different reason.
///
/// The rig absorbs it. A pose that read P in the old space reads T^-1 * P in
/// the new one and the wearer's world pose is Origin * P, so keeping that
/// constant needs `Origin' = Origin * T` — PROJECTED onto what a rig may be
/// (a position and a HEADING; a recentre that carried a pitch into the rig
/// would tilt the horizon under a standing person).
///
/// THIS IS THE HOST-SIDE STATEMENT OF ARITHMETIC THE ENGINE PERFORMS (the
/// session sees the event and must correct before the same frame's locate, and
/// it cannot include this file — the engine links no document maths). It is
/// here because it is the one place the rule can be ASSERTED: `player.vr`
/// checks the invariant it exists for — a wearer's world pose unchanged across
/// a recentre — which is what would catch either expression drifting.
inline Rig rigAfterSpaceChange(const Rig &rig, const iris::Vec3 &posePosition,
                               const iris::Quat &poseRotation, float worldScale = 1.0f)
{
    Rig out;
    out.position = rig.position + rotateY(posePosition * worldScale, rig.yaw);
    out.yaw = rig.yaw + yawDegrees(poseRotation);
    return out;
}

/// THE LONGEST A SINGLE FRAME MAY MOVE A WEARER, in seconds.
///
/// The host charges the wall clock of the frame just gone, so a UI-thread block
/// — a console script, a project open — arrives as one enormous dt (measured on
/// the desktop: a key held across a 13 second block moved the camera 110 units
/// in one frame, ledger §356). On a monitor that is an annoyance. In a headset
/// it is an involuntary 110-metre lurch with no matching vestibular signal,
/// which is the textbook recipe for making somebody sick — so the frame path
/// clamps, at exactly the number the desktop fly clamps at.
inline float frameSeconds(float dt)
{
    return dt < flystep::kMaxFlyStep ? dt : flystep::kMaxFlyStep;
}

}   // namespace vrorigin

#endif   // VRORIGIN_H
