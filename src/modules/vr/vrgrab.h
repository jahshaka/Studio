/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRGRAB_H
#define VRGRAB_H

// THE GRAB'S ARITHMETIC — what a hand does to an object it is holding, and what
// a thumbstick does to the wearer (SPECS/VR_INPUT_SPEC.md §5.1, §6; stage 1).
//
// WHY THIS IS A HEADER OF PURE FUNCTIONS, for exactly the reason vrorigin.h is
// (and that header's own note says it best): every decision here is a small
// piece of pose algebra that can be wrong in a way which is only visible ON A
// HEADSET, minutes later, to a person who now distrusts the tool — an object
// that rotates about its own origin instead of about the hand, a snap that
// quantises the absolute position so a grab teleports the thing to the grid, a
// turn that swings the wearer around the room's centre instead of their own
// feet. None of that is testable through a picture and all of it is testable
// here, so it lives in functions with no engine, no Qt, no OpenXR and no
// service in them, and `vr.grab_maths` asserts it on every gate, on every box,
// with no headset and no runtime.
//
// THE CONVENTIONS, STATED ONCE:
//
//   * A POSE is a world-space position and rotation. Composition is written
//     the way it is read: `T_node = T_hand * T_hand0^-1 * T_node0` — "where
//     the object was, carried by however the hand moved since the grab".
//   * AN AIM RAY POINTS ALONG -Z of its rotation, which is OpenXR's aim-pose
//     convention AND this document model's camera convention (vrorigin.h's
//     note) — one less place for a sign to hide.
//   * YAW is degrees about +Y, 0 looking down -Z: vrorigin.h's convention, and
//     the rig functions here are that file's `placedOn` derived for a turn
//     about the wearer instead of a placement onto a camera.
//   * SNAPPING QUANTISES THE DELTA, never the absolute. Ctrl on the desktop
//     gizmo snaps how far a drag MOVED; a hand grab that snapped the absolute
//     position would jump an object to the nearest grid line the instant the
//     modifier came down, which is not what the modifier means anywhere else
//     in the editor.
//   * NOTHING HERE HOLDS STATE. A gesture's captured start poses, the
//     accumulated turntable angle and the re-arm of a snap turn belong to the
//     service that runs the gesture (vrinteraction.h); these functions are
//     pure so that one frame of a grab can be reproduced in a test by calling
//     them with numbers.

#include <cmath>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "services/vrorigin.h"
#include "viewport/flystep.h"

namespace vrgrab {

/// One rigid frame in the world: where it is and which way it faces.
struct Pose
{
    iris::Vec3 position;
    iris::Quat rotation;
};

// ---- THE DIALS, AND WHY EACH ONE IS THE NUMBER IT IS ----------------------

/// ARM'S REACH, metres — the boundary between a NEAR grab (the object is in
/// the hand, and rides the grip pose) and a FAR grab (the object is out along
/// the aim ray, and rides a virtual hand at the hit). 0.9 m is a tall adult's
/// fingertip reach from the shoulder, and the only thing it decides is which
/// of two arrangements of the same formula is used.
inline constexpr float kArmReach = 0.9f;

/// THE FAR-GRAB LEVER'S FILTER, seconds of lag per metre beyond arm's reach.
///
/// At 20 m a 1 mm tremor of the wrist is a 2 cm swing of the object (§12.6):
/// the lever arm multiplies the hand's noise by the distance, and nothing about
/// the formula can avoid that — it is what "the object is welded to my hand"
/// means at range. So the virtual hand is low-passed, with a time constant that
/// GROWS with the distance: at arm's reach there is no filter at all (a near
/// grab must feel direct), at 5 m about 80 ms, at 20 m about 380 ms. The cost
/// is stated rather than hidden: a far grab lags, deliberately, and the gizmo
/// (stage 2) is the real answer for far precision.
inline constexpr float kLeverTauPerMetre = 0.02f;

/// PUSH/PULL, e-folds per second at full stick. The distance is multiplied
/// rather than added (`d *= exp(k*y*dt)`) so one flick of the stick moves an
/// object 1 cm away by a millimetre and one 100 m away by ten metres — the
/// same GESTURE at every scale, which is the only way one dial can serve a
/// room and a landscape. 1.5 doubles the distance in about half a second.
inline constexpr float kPushPullRate = 1.5f;
inline constexpr float kMinGrabDistance = 0.05f;    ///< do not pull it inside the eye
inline constexpr float kMaxGrabDistance = 500.0f;   ///< nor push it past the far clip

/// THE TURNTABLE, degrees per second at full stick — the one rotation a far
/// grab cannot do with the wrist (you cannot walk around a thing you are
/// holding at ten metres).
inline constexpr float kTurntableDegreesPerSecond = 90.0f;

/// SNAP TURN: the owner's answer (§16 decision 2) is snap by default, and 30
/// degrees is the step every shipping VR tool uses — twelve of them is a full
/// circle, and it is small enough to aim with and large enough to be worth a
/// flick.
inline constexpr float kSnapTurnDegrees = 30.0f;
/// SMOOTH TURN (a session option), degrees per second at full stick.
inline constexpr float kSmoothTurnDegreesPerSecond = 90.0f;

/// THE STICK'S DEAD ZONE and the RE-ARM threshold. A thumbstick at rest does
/// not read zero (a Touch controller idles around 0.02-0.08, and a worn one
/// drifts further), so anything below the dead zone is "not pushed". The
/// re-arm is deliberately LOWER than the trigger: a snap turn fires once when
/// the stick crosses the dead zone and cannot fire again until it has come
/// back below the re-arm — one flick, one turn, with no chatter in the band
/// between.
inline constexpr float kStickDeadZone = 0.5f;
inline constexpr float kStickRearm = 0.2f;

// ---- THE RAY ---------------------------------------------------------------

/// WHERE AN AIM POSE POINTS: -Z of its rotation (see the header's conventions).
inline iris::Vec3 aimDirection(const iris::Quat &rotation)
{
    const iris::Vec3 d = rotation.rotatedVector(iris::Vec3(0, 0, -1));
    return d.isNull() ? iris::Vec3(0, 0, -1) : d.normalized();
}

/// A point along the aim ray.
inline iris::Vec3 rayPoint(const Pose &aim, float distance)
{
    return aim.position + aimDirection(aim.rotation) * distance;
}

// ---- THE GRAB: RIGID ATTACH ------------------------------------------------

/// THE WHOLE OF DIRECT MANIPULATION: `T_node = T_hand * T_hand0^-1 * T_node0`.
///
/// Read it right to left: take where the object was when the grab started,
/// express it in the hand's frame at that moment, and put it back into the
/// hand's frame now. The object is welded to the hand — a 10 cm hand move
/// moves it 10 cm, and a 90 degree wrist turn turns it 90 degrees ABOUT THE
/// HAND, carrying its position round with it, which is the difference between
/// holding something and spinning it on a spit.
///
/// Both halves of stage 1 are this one function: a NEAR grab passes the grip
/// poses, a FAR grab passes a virtual hand out on the aim ray (see
/// virtualFarHand). Nothing else differs between them — one gesture, two
/// distances.
inline Pose rigidFollow(const Pose &hand0, const Pose &hand, const Pose &node0)
{
    // The hand's rotation SINCE the grab. Normalised: a runtime's poses are
    // unit quaternions to about 1e-6, and this multiplies twice per frame for
    // as long as somebody holds the trigger.
    const iris::Quat turn = (hand.rotation * hand0.rotation.conjugated()).normalized();
    Pose out;
    out.rotation = (turn * node0.rotation).normalized();
    out.position = hand.position + turn.rotatedVector(node0.position - hand0.position);
    return out;
}

/// THE VIRTUAL HAND OF A FAR GRAB: the point on the aim ray at `distance`,
/// carrying the aim pose's own rotation. Feed it to rigidFollow as the hand
/// and the object rides the end of the ray like a fish on a line — it keeps
/// its offset from the hit, turns with the wrist, and slides when the distance
/// changes.
inline Pose virtualFarHand(const Pose &aim, float distance)
{
    Pose out;
    out.position = rayPoint(aim, distance);
    out.rotation = aim.rotation;
    return out;
}

// ---- THE FAR-GRAB FILTER ---------------------------------------------------

/// A ONE-POLE LOW PASS'S BLEND FACTOR for a step of `seconds` at time constant
/// `tau`. tau <= 0 means no filter at all (alpha 1 = follow exactly), and the
/// exponential form is used rather than `dt/tau` so the filter cannot overshoot
/// on a long frame — a 200 ms hitch must not make a held object fly past.
inline float onePoleAlpha(float seconds, float tau)
{
    if (tau <= 0.0f || seconds <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-seconds / tau);
}

/// The lever's time constant at a distance (see kLeverTauPerMetre).
inline float leverTau(float distance)
{
    const float beyond = distance - kArmReach;
    return beyond > 0.0f ? beyond * kLeverTauPerMetre : 0.0f;
}

/// Blend a pose towards another. The rotation is a nlerp (Quat::nlerp's
/// shortest arc), which is what a filter wants: the shortest way round, never
/// the long one.
inline Pose smoothedTowards(const Pose &current, const Pose &target, float alpha)
{
    if (alpha >= 1.0f) return target;
    if (alpha <= 0.0f) return current;
    Pose out;
    out.position = current.position + (target.position - current.position) * alpha;
    out.rotation = iris::Quat::nlerp(current.rotation, target.rotation, alpha);
    return out;
}

// ---- PUSH / PULL AND THE TURNTABLE ----------------------------------------

/// THE STICK ALONG THE RAY: `d *= exp(rate * y * dt)`, clamped. See
/// kPushPullRate for why it multiplies.
inline float pushPulled(float distance, float stickY, float seconds,
                        float rate = kPushPullRate)
{
    if (std::fabs(stickY) < kStickDeadZone || seconds <= 0.0f) return distance;
    const float d = distance * std::exp(rate * stickY * seconds);
    if (d < kMinGrabDistance) return kMinGrabDistance;
    if (d > kMaxGrabDistance) return kMaxGrabDistance;
    return d;
}

/// SPIN A POSE ABOUT THE WORLD'S UP THROUGH A PIVOT. With the pivot at the
/// pose's own position this is the turntable — the object spins where it is —
/// and with any other pivot it is an orbit, which is what a two-hand gesture
/// will want later.
inline Pose spunAboutUp(const Pose &pose, const iris::Vec3 &pivot, float degrees)
{
    Pose out;
    out.position = pivot + vrorigin::rotateY(pose.position - pivot, degrees);
    out.rotation = (iris::Quat::fromAxisAndAngle(iris::Vec3(0, 1, 0), degrees) * pose.rotation)
                       .normalized();
    return out;
}

/// How far the turntable turns in one frame at this stick deflection.
inline float turntableDegrees(float stickX, float seconds,
                              float degreesPerSecond = kTurntableDegreesPerSecond)
{
    if (std::fabs(stickX) < kStickDeadZone) return 0.0f;
    return stickX * degreesPerSecond * seconds;
}

// ---- SNAPPING (the DELTA, never the absolute) -----------------------------

/// Each component to the nearest multiple of `step`. A step <= 0 is "no snap".
inline iris::Vec3 snappedTranslation(const iris::Vec3 &delta, float step)
{
    if (step <= 0.0f) return delta;
    return iris::Vec3(std::round(delta.x() / step) * step,
                      std::round(delta.y() / step) * step,
                      std::round(delta.z() / step) * step);
}

/// The rotation's ANGLE to the nearest multiple of `stepDegrees`, about its own
/// axis. Quantising the angle and keeping the axis is what "snap the rotation"
/// means for a hand: the wrist chose the axis, the grid chooses how far.
inline iris::Quat snappedRotation(const iris::Quat &delta, float stepDegrees)
{
    if (stepDegrees <= 0.0f) return delta;
    iris::Vec3 axis;
    float angle = 0.0f;
    delta.normalized().getAxisAndAngle(&axis, &angle);
    if (axis.isNull()) return iris::Quat();
    const float snapped = std::round(angle / stepDegrees) * stepDegrees;
    if (std::fabs(snapped) < 1e-4f) return iris::Quat();
    return iris::Quat::fromAxisAndAngle(axis, snapped);
}

/// A FOLLOWED POSE, SNAPPED: the gesture's delta off its start, quantised, put
/// back. `start` is where the object was when the grab began, `live` where the
/// unsnapped follow just put it.
inline Pose snappedFrom(const Pose &start, const Pose &live, float translateStep,
                        float rotateStepDegrees)
{
    const iris::Quat deltaRot = (live.rotation * start.rotation.conjugated()).normalized();
    Pose out;
    out.position = start.position + snappedTranslation(live.position - start.position, translateStep);
    out.rotation = (snappedRotation(deltaRot, rotateStepDegrees) * start.rotation).normalized();
    return out;
}

// ---- LOCOMOTION -----------------------------------------------------------

/// THE STICK AS FLY KEYS — the eight-way reduction, for the cases that want
/// vrorigin::flyDelta's exact behaviour (a verb, a test, a comparison).
inline flystep::Keys keysFromStick(float x, float y, float deadZone = kStickDeadZone)
{
    flystep::Keys keys;
    keys.forward = y >= deadZone;
    keys.back    = y <= -deadZone;
    keys.right   = x >= deadZone;
    keys.left    = x <= -deadZone;
    return keys;
}

/// The stick's deflection with the dead zone taken out and rescaled so that the
/// edge of the dead zone is 0 and full deflection is 1. Clamped at 1: a
/// diagonal stick reads 1.41 on a square-gated pad and must not fly faster.
inline float stickMagnitude(float x, float y, float deadZone = kStickDeadZone)
{
    const float len = std::sqrt(x * x + y * y);
    if (len <= deadZone) return 0.0f;
    if (deadZone >= 1.0f) return 1.0f;
    const float m = (len - deadZone) / (1.0f - deadZone);
    return m > 1.0f ? 1.0f : m;
}

/// ONE FRAME OF STICK FLIGHT, as a world offset for the RIG — the analog form
/// of vrorigin::flyDelta.
///
/// THE HEADING RULE IS vrorigin's, not a second copy of it: forward is
/// `levelForward(headRot)` (where the wearer is LOOKING, flattened onto the
/// ground, with that file's pole branch), strafe is horizontal beside it. What
/// this adds is the one thing a boolean key cannot express — a stick is ANALOG,
/// in direction and in amount. A stick at (0.3, 0.9) means "mostly forward,
/// slightly right, at 95 % speed"; reduced to keys it would mean "exactly 45
/// degrees, at full speed", a 15 degree error on every frame the wearer is not
/// pushing a cardinal direction.
///
/// At FULL cardinal deflection it is exactly vrorigin::flyDelta's answer, and
/// `vr.grab_maths` asserts that equality — which is what keeps the two from
/// drifting.
inline iris::Vec3 stickFlyDelta(const iris::Quat &headRot, float x, float y, float speed,
                                float seconds, bool boost = false,
                                float deadZone = kStickDeadZone)
{
    const float magnitude = stickMagnitude(x, y, deadZone);
    if (magnitude <= 0.0f) return iris::Vec3();
    const iris::Vec3 fwd = vrorigin::levelForward(headRot);
    iris::Vec3 right = iris::Vec3::crossProduct(fwd, iris::Vec3(0, 1, 0)).normalized();
    if (right.isNull()) right = headRot.rotatedVector(iris::Vec3(1, 0, 0));
    const float len = std::sqrt(x * x + y * y);
    // The stick's own direction, unit length, times the dead-zoned magnitude.
    const iris::Vec3 dir = (fwd * (y / len) + right * (x / len));
    if (dir.isNull()) return iris::Vec3();
    return dir.normalized() * (speed * (boost ? flystep::kBoost : 1.0f) * seconds * magnitude);
}

/// TURN THE WEARER ABOUT THEIR OWN HEAD — the rig after a snap or a smooth turn
/// of `degrees`.
///
/// Derivation, so the signs are checkable rather than remembered. The rig is
/// R = (yaw r, translation t) and the head's world pose is H = R * S for the
/// runtime's own pose S. A turn by d must leave the head WHERE IT IS and only
/// change which way the room faces, so we want an R' with
/// pos(R' * S) = pos(H):
///
///     yaw(R') = r + d
///     t'      = pos(H) + Ry(d) * (t - pos(H))
///
/// which is vrorigin::placedOn with the target set to the head turned by d —
/// the same arithmetic, arranged for the other question. Turning about the
/// RIG's origin instead (the cheap version) swings a wearer standing at the
/// edge of their room in a circle around its middle, which is a metre of
/// sideways motion nobody asked for and the fastest way to make somebody sick.
inline vrorigin::Rig turnedAboutHead(const vrorigin::Rig &rig, const iris::Vec3 &headPos,
                                     float degrees)
{
    vrorigin::Rig out;
    out.yaw = rig.yaw + degrees;
    out.position = headPos + vrorigin::rotateY(rig.position - headPos, degrees);
    return out;
}

/// SMOOTH TURN: how far this frame's stick turns the wearer.
inline float smoothTurnDegrees(float stickX, float seconds,
                               float degreesPerSecond = kSmoothTurnDegreesPerSecond)
{
    if (std::fabs(stickX) < kStickDeadZone) return 0.0f;
    return stickX * degreesPerSecond * seconds;
}

/// SNAP TURN: the step a stick crossing the dead zone asks for, or 0. The
/// RE-ARM is the caller's (it is state, and this file holds none):
/// `armed` false until |x| falls below kStickRearm again.
inline float snapTurnDegrees(float stickX, bool armed, float step = kSnapTurnDegrees)
{
    if (!armed || std::fabs(stickX) < kStickDeadZone) return 0.0f;
    return stickX > 0.0f ? step : -step;
}

/// Has the stick come back far enough to arm the next snap turn?
inline bool snapTurnRearmed(float stickX) { return std::fabs(stickX) <= kStickRearm; }

}   // namespace vrgrab

#endif   // VRGRAB_H
