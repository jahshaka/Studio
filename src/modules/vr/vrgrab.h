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

// THE SNAP STEP AND THE SMOOTH RATE ARE THE DOCUMENT'S (lane VR-WORLD-1):
// `iris::kDefaultVrSnapTurnDegrees` / `kDefaultVrSmoothTurnDegreesPerSecond` are
// the defaults of the project fields `world.vr` writes, and the two constants
// that used to live here were a second definition of them. The functions below
// therefore take the step and the rate — every caller has the effective value
// already (services/vrworld.h).

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
///
/// POSITIVE IS THE STICK'S OWN SIGN, like `snapTurnDegrees` and
/// `smoothTurnDegrees` beside it and for the same reason: this file holds the
/// arithmetic and the CALLER owns the convention, because the tree's yaw is the
/// right-handed rotation about +Y (a positive angle is counter-clockwise seen
/// from above) while every VR title's stick turns the world the other way. The
/// one call site (VrInteraction::followGesture) negates, exactly as the turn
/// does, so that A FLICK RIGHT SPINS THE HELD OBJECT CLOCKWISE FROM ABOVE —
/// the same direction the same stick turns the wearer.
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
/// THE FLY ALONG THE HAND (the owner's choice, 2026-09-17, at the first
/// controller smoke: "fly like Unreal"): the stick's Y flies the wearer along
/// the AIM of the hand holding the stick — the full direction, up and down
/// included — so the wrist steers and the gaze stays free. The level fly above
/// stays as the comfort option (`vr.locomotion({fly:"level"})`) and as the
/// arrow keys' rule. Pushing forward moves along the aim, pulling back moves
/// against it; the magnitude is the dead-zoned |y|.
inline iris::Vec3 aimFlyDelta(const iris::Quat &aimRot, float y, float speed, float seconds,
                              bool boost = false, float deadZone = kStickDeadZone)
{
    const float magnitude = stickMagnitude(0.0f, y, deadZone);
    if (magnitude <= 0.0f) return iris::Vec3();
    iris::Vec3 fwd = aimRot.rotatedVector(iris::Vec3(0, 0, -1));
    if (fwd.isNull()) return iris::Vec3();
    fwd = fwd.normalized() * (y < 0.0f ? -1.0f : 1.0f);
    return fwd * (speed * (boost ? flystep::kBoost : 1.0f) * seconds * magnitude);
}

/// THE FLY WHERE YOU LOOK (the owner, 2026-09-17: "I would love to fly in the
/// direction I am looking — maybe hold a button down to override the hand"):
/// the head's FULL forward, up and down included. Held on the left squeeze it
/// overrides the hand for as long as the button is down; as the `gaze` fly
/// option it is the default. The same shape as aimFlyDelta with the head's
/// rotation, which is why it is that function.
inline iris::Vec3 gazeFlyDelta(const iris::Quat &headRot, float y, float speed, float seconds,
                               bool boost = false, float deadZone = kStickDeadZone)
{
    return aimFlyDelta(headRot, y, speed, seconds, boost, deadZone);
}

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
inline float smoothTurnDegrees(float stickX, float seconds, float degreesPerSecond)
{
    if (std::fabs(stickX) < kStickDeadZone) return 0.0f;
    return stickX * degreesPerSecond * seconds;
}

/// SNAP TURN: the step a stick crossing the dead zone asks for, or 0. The
/// RE-ARM is the caller's (it is state, and this file holds none):
/// `armed` false until |x| falls below kStickRearm again.
inline float snapTurnDegrees(float stickX, bool armed, float step)
{
    if (!armed || std::fabs(stickX) < kStickDeadZone) return 0.0f;
    return stickX > 0.0f ? step : -step;
}

/// Has the stick come back far enough to arm the next snap turn?
inline bool snapTurnRearmed(float stickX) { return std::fabs(stickX) <= kStickRearm; }


// ---- THE TWO-HAND GRAB (VR_INPUT_SPEC §5.1's two-hand paragraph; the owner's
// ---- answer 7, "the full version with roll") -------------------------------
//
// WHAT TWO HANDS ADD, AND WHY IT IS A DIFFERENT FORMULA. One hand can carry an
// object and turn it, and that is all it can do: a rigid attach has no scale in
// it and no way to say "make this bigger" or "roll it about the line between my
// palms". Two hands hold a thing the way a person holds a box — the SPAN
// between the palms is the size, the LINE between them is an axis, and rolling
// both wrists about that line rolls the box. So the gesture is read as the
// three numbers a pair of points and a pair of orientations can carry:
//
//     scale       = |A| / |A0|            (the span, now over the span then)
//     rotation    = roll(A) * minimal(A0 -> A)
//     translation = M - M0                (the midpoint's own move)
//
// and the object is put through the similarity those three describe, about the
// midpoint AT THE CAPTURE. Nothing else is invented: with the hands held still
// the three are identity, one and zero.
//
// THE ROLL IS THE HALF A PAIR OF POINTS CANNOT SEE. `minimal(A0 -> A)` is the
// SHORTEST rotation carrying the old axis onto the new one, and by construction
// its own axis is perpendicular to both — so it contains no turn ABOUT the line
// between the hands, and a wearer who rolls both wrists without moving either
// palm would, with that term alone, see the object sit perfectly still. The
// missing degree of freedom is taken from the grips themselves: each hand's
// rotation since the capture is split into a swing and a TWIST about the
// current axis (the standard swing-twist decomposition, below), and the average
// of the two twists is the roll. Averaged rather than taken from one hand
// because the gesture belongs to the pair: one wrist rolling while the other
// holds still is half a roll, which is what a person doing it expects to see.
//
// WHY THE TWO TERMS CANNOT DOUBLE COUNT: the minimal rotation's axis is
// `A0 x A`, which is perpendicular to A, so its twist about A is exactly zero.
// The composition `roll * minimal` therefore adds the roll to a rotation that
// has none, and a pure wrist roll (A unchanged, minimal = identity) is read as
// roll alone.

/// A PAIR OF HANDS AS CAPTURED — the frame every later frame is measured
/// against. `hand[0]` and `hand[1]` are the LEFT and the RIGHT hand in that
/// order at every call site (VrInteraction captures them by index), so the
/// axis has a stable sign and a roll never flips halfway through a gesture.
struct TwoHandStart
{
    Pose hand[2];
    iris::Vec3 midpoint;
    iris::Vec3 axis;        ///< hand[1].position - hand[0].position
    float span = 0.0f;      ///< |axis| at the capture
};

/// WHAT THE PAIR IS DOING NOW, relative to that capture.
struct TwoHandDelta
{
    iris::Vec3 midpoint;            ///< M this frame (the translation is M - M0)
    iris::Quat rotation;            ///< roll(A) * minimal(A0 -> A)
    float scale = 1.0f;             ///< |A| / |A0|
    float rollDegrees = 0.0f;       ///< the roll term alone, reported for the suites
    float span = 0.0f;              ///< |A| this frame
};

inline TwoHandStart twoHandStart(const Pose &left, const Pose &right)
{
    TwoHandStart out;
    out.hand[0] = left;
    out.hand[1] = right;
    out.midpoint = (left.position + right.position) * 0.5f;
    out.axis = right.position - left.position;
    out.span = out.axis.length();
    return out;
}

/// THE TWIST OF A ROTATION ABOUT AN AXIS, in degrees, signed by the right hand
/// rule about `axis` — the swing-twist decomposition, which is the only honest
/// way to ask "how much of this wrist turn was a roll about the line between my
/// palms".
///
/// The vector part of a quaternion lies along its rotation axis, so projecting
/// it onto `axis` and keeping the scalar gives the twist quaternion directly;
/// the angle comes back through atan2 so that a half turn either way is told
/// apart, and it is wrapped into (-180, 180] because a quaternion and its
/// negation are the same rotation and would otherwise answer 360 degrees apart.
inline float wrapDegrees(float degrees)
{
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees <= -180.0f) degrees += 360.0f;
    return degrees;
}

inline float twistDegreesAbout(const iris::Quat &rotation, const iris::Vec3 &axis)
{
    const iris::Vec3 a = axis.isNull() ? iris::Vec3() : axis.normalized();
    if (a.isNull()) return 0.0f;
    const iris::Quat q = rotation.normalized();
    const iris::Vec3 v(q.x(), q.y(), q.z());
    const float along = iris::Vec3::dotProduct(v, a);
    const float len = std::sqrt(q.scalar() * q.scalar() + along * along);
    if (len < 1e-8f) return 0.0f;                  // a half turn ABOUT a perpendicular axis
    const float degrees =
        2.0f * std::atan2(along / len, q.scalar() / len) * 57.29577951308232f;
    return wrapDegrees(degrees);
}

/// THE AVERAGE OF TWO ANGLES, taken on the SHORT way round: +179 and -179
/// average to 180, not to 0. (The naive mean is the classic way a roll flips
/// through half a turn when the two wrists straddle the wrap.)
inline float averageDegrees(float a, float b)
{
    return wrapDegrees(a + wrapDegrees(b - a) * 0.5f);
}

inline TwoHandDelta twoHandDelta(const TwoHandStart &start, const Pose &left, const Pose &right)
{
    TwoHandDelta out;
    out.midpoint = (left.position + right.position) * 0.5f;
    const iris::Vec3 axis = right.position - left.position;
    out.span = axis.length();
    // A DEGENERATE PAIR IS A HOLD, NOT A DIVISION BY ZERO: hands that meet (or
    // a capture taken with them together) leave the object exactly as it was.
    if (start.span < 1e-4f || out.span < 1e-4f) {
        out.scale = 1.0f;
        out.rotation = iris::Quat();
        return out;                     // the midpoint still moves: it is the translation
    }
    out.scale = out.span / start.span;
    const iris::Quat minimal = iris::Quat::rotationTo(start.axis, axis).normalized();
    // THE ROLL, from the two grips' own turns about THIS frame's axis.
    const iris::Quat turn0 = (left.rotation * start.hand[0].rotation.conjugated()).normalized();
    const iris::Quat turn1 = (right.rotation * start.hand[1].rotation.conjugated()).normalized();
    out.rollDegrees = averageDegrees(twistDegreesAbout(turn0, axis),
                                     twistDegreesAbout(turn1, axis));
    const iris::Quat roll = iris::Quat::fromAxisAndAngle(axis.normalized(), out.rollDegrees);
    out.rotation = (roll * minimal).normalized();
    return out;
}

/// THE NODE UNDER A TWO-HAND GESTURE: the similarity applied about the
/// midpoint at the capture.
///
///     p = M + R * (s * (p0 - M0))
///     q = R * q0
///
/// Read it as one sentence: take where the object was relative to the point
/// between the hands, scale that offset by how much the hands have spread,
/// turn it by how the pair has turned, and hang it off where the hands are now.
inline Pose twoHandFollow(const TwoHandStart &start, const TwoHandDelta &delta,
                          const Pose &node0, const iris::Vec3 &pivot)
{
    Pose out;
    out.rotation = (delta.rotation * node0.rotation).normalized();
    out.position = pivot + (delta.midpoint - start.midpoint) +
                   delta.rotation.rotatedVector((node0.position - pivot) * delta.scale);
    return out;
}

/// THE SAME, ABOUT THE POINT BETWEEN THE HANDS — the spec's own arrangement,
/// and what a NEAR gesture wants: the hands are ON the object, so the object
/// scales and turns about them.
///
/// A FAR gesture passes a different pivot (VrInteraction does: the object's own
/// captured position), because the hands are then nowhere near the thing they
/// are steering — at ten metres, spreading the palms about the wearer's own
/// midpoint would throw the object another ten metres away and a thirty degree
/// turn of the pair would sweep it five metres sideways. The lever arm that
/// makes a far grab twitchy (kLeverTauPerMetre) makes a far SCALE violent, and
/// the cure is the same one every tool uses: far away, the pair is a steering
/// wheel and the object turns and grows where it stands.
inline Pose twoHandFollow(const TwoHandStart &start, const TwoHandDelta &delta, const Pose &node0)
{
    return twoHandFollow(start, delta, node0, start.midpoint);
}

/// The whole gesture in one call, for a caller that holds the four poses — the
/// spelling VR_INPUT_SPEC names. `scaleOut` (optional) takes the uniform factor
/// the caller must also apply to the node's own scale.
inline Pose twoHandFollow(const Pose &left0, const Pose &right0, const Pose &left,
                          const Pose &right, const Pose &node0, float *scaleOut = nullptr)
{
    const TwoHandStart start = twoHandStart(left0, right0);
    const TwoHandDelta delta = twoHandDelta(start, left, right);
    if (scaleOut) *scaleOut = delta.scale;
    return twoHandFollow(start, delta, node0);
}

/// SNAP THE SCALE FACTOR (SnapSettings::scaleSize() under `menu`, §5.1). The
/// FACTOR is quantised, not the resulting size: the modifier means "in steps of
/// a quarter" and the object must not jump to a grid the instant it comes down.
/// Never zero — a snapped factor below one step is one step, because a scale of
/// nothing is a deleted object with extra steps.
inline float snappedScale(float factor, float step)
{
    if (step <= 0.0f || factor <= 0.0f) return factor;
    const float snapped = std::round(factor / step) * step;
    return snapped < step ? step : snapped;
}

// ---- TELEPORT (VR_INPUT_SPEC §6 row L4; the owner's answer 8) --------------
//
// THE ARC IS A THROWN BALL, and that is the whole of it: the wearer's hand
// throws a marker at a constant speed along the aim, gravity brings it down,
// and where it lands is where they will stand. Every VR tool since Budget Cuts
// draws this curve for the same two reasons — a straight ray cannot say "behind
// that ledge" and cannot be aimed at the floor without pointing the controller
// at your own feet, while a parabola aims itself: the wearer raises the hand
// and the landing walks away from them, in a curve they can read at a glance.
//
// THE SPEED IS THE DIAL and the only one. At 10 m/s from a hand at 1.4 m, held
// level, the marker lands 5.3 m away; at 45 degrees up it reaches 10.6 m; at
// 45 degrees down it is 2.8 m in front of the wearer's feet. That is the range
// a room and a landscape both want, and the shape is the same at every scale
// because the only other number is gravity, which is 9.81 everywhere a person
// has ever stood.

inline constexpr float kTeleportSpeed = 10.0f;          ///< m/s, the throw
inline constexpr float kTeleportGravity = 9.81f;        ///< m/s^2, down
/// HOW LONG THE THROW MAY FLY before the arc gives up, seconds. Two seconds is
/// a 20 m throw and a 19.6 m drop — past that the landing is a guess the wearer
/// cannot see anyway, and the refusal ("nothing under the arc") is the honest
/// answer.
inline constexpr float kTeleportMaxSeconds = 2.0f;
/// HOW MANY STRAIGHT PIECES THE CURVE IS MADE OF. Each one is a segment the
/// document's picker is asked about, so this is a cost as much as a shape: 20
/// pieces over 2 s is a 1 m piece at the start of a level throw, which reads as
/// a smooth curve at arm's length and misses nothing a person can stand on.
inline constexpr int kTeleportSegments = 20;
/// THE STEEPEST GROUND A WEARER MAY BE PUT ON, degrees from level. 45 is the
/// angle every engine's character controller uses: a ramp is a floor, a wall is
/// not, and the line between them has to be somewhere.
inline constexpr float kTeleportMaxSlopeDegrees = 45.0f;

/// A POINT ON THE THROWN ARC at `seconds` after it left the hand.
inline iris::Vec3 arcPoint(const Pose &aim, float seconds, float speed = kTeleportSpeed,
                           float gravity = kTeleportGravity)
{
    const iris::Vec3 dir = aimDirection(aim.rotation);
    return aim.position + dir * (speed * seconds) -
           iris::Vec3(0, 1, 0) * (0.5f * gravity * seconds * seconds);
}

/// WHEN THE ARC CROSSES A HORIZONTAL PLANE, seconds, or -1 when it never does.
///
/// The DESCENDING crossing (the larger root): an arc thrown upward from below a
/// plane passes it twice and the landing is the second one — you come down on
/// the floor, you do not stand on it on the way up.
inline float arcPlaneTime(const Pose &aim, float planeY, float speed = kTeleportSpeed,
                          float gravity = kTeleportGravity)
{
    const iris::Vec3 dir = aimDirection(aim.rotation);
    const float vy = dir.y() * speed;
    const float dy = aim.position.y() - planeY;
    if (gravity <= 0.0f) return vy < 0.0f ? -dy / vy : -1.0f;
    // 0.5*g*t^2 - vy*t - dy = 0
    const float disc = vy * vy + 2.0f * gravity * dy;
    if (disc < 0.0f) return -1.0f;
    const float t = (vy + std::sqrt(disc)) / gravity;
    return t > 0.0f ? t : -1.0f;
}

/// MAY A WEARER STAND ON THIS FACE? `normal` is the surface's, pointing back
/// at the arc that struck it; anything steeper than `maxSlopeDegrees` from the
/// world's up is a wall.
inline bool landingAllowed(const iris::Vec3 &normal,
                           float maxSlopeDegrees = kTeleportMaxSlopeDegrees)
{
    if (normal.isNull()) return true;      // a face we cannot measure is not refused
    const float up = normal.normalized().y();
    return up >= std::cos(maxSlopeDegrees * 0.017453292519943295f);
}

/// THE RIG THAT PUTS THE WEARER ON A LANDING POINT, LEVEL AND FACING THE WAY
/// THEY ALREADY FACE.
///
/// It is vrorigin::placedOn with the target being the wearer's own head, moved:
/// the same head rotation in and out, so the yaw delta is zero and nothing
/// turns (a teleport that also spun the room is the fastest way to lose
/// somebody), and the head's HEIGHT ABOVE THE RIG'S FLOOR carried across, so a
/// person who is standing arrives standing and a person who is crouching
/// arrives crouching. `landing` is the point on the ground, not the eye.
inline vrorigin::Rig teleportedTo(const vrorigin::Rig &rig, const iris::Vec3 &headPos,
                                  const iris::Quat &headRot, const iris::Vec3 &landing)
{
    const iris::Vec3 target(landing.x(), landing.y() + (headPos.y() - rig.position.y()),
                            landing.z());
    return vrorigin::placedOn(rig, headPos, headRot, target, headRot);
}

}   // namespace vrgrab

#endif   // VRGRAB_H
