/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMORAY_H
#define GIZMORAY_H

// gizmoray — THE GIZMO'S PICK GEOMETRY WHEN THERE IS NO PIXEL
// (SPECS/VR_INPUT_SPEC.md §5.2, phase 4b stage 2).
//
// WHY THIS EXISTS. The rotation rings and the translate plane handles are
// picked IN PIXELS on the desktop, and for a good reason: a ring seen edge-on
// has no 3D annulus left to hit, and a square seen edge-on is a line (smoke
// S15, GIZMO-1 item 3). In a headset there is no pixel and no cursor — the
// "cursor" is the controller's aim RAY — so the same questions are asked in the
// only unit both a ray and a drawn handle share: the ANGLE AT THE RAY'S ORIGIN.
// A pixel distance on a screen and an angle at the eye are the same measurement
// expressed twice; this header is the conversion and the spherical geometry
// that goes with it.
//
// EVERYTHING HERE IS A PURE FUNCTION of vectors and numbers: no Qt widget, no
// camera, no engine, no document. That is what lets the size rule and the ring
// distance be asserted with no display and no GPU (the `vrgrab.h` shape,
// VR_INPUT_SPEC §10), and it is what keeps ONE drag maths with two PICK paths —
// the constraint arithmetic (the ring's own basis, the plane's two axes, the
// snap) stays where it was, and only the question "is the pointer on this
// handle" is answered twice.
//
// THE TWO NUMBERS ARE VR'S OWN, NOT THE DESK'S CONVERTED (the lead's read of
// the first draft).
//
// That draft derived both from the desktop's pixel constants at a nominal eye
// fov — the gizmo as the same FRACTION OF THE FRAME it fills on a monitor, the
// tolerance as 7 px of a 1080-tall frame at that eye. Both are wrong for a
// headset, and in the same way. A fraction of the frame is a fraction of the
// FIELD OF VIEW, and a wearer's field of view is their whole vision: the gizmo
// came out at ~24 degrees of half-extent (three to four times the retinal size
// the same rule gives on a desk, where a 45-degree frustum occupies a small
// part of what the person can see) and it GREW with the headset's fov — the
// opposite of the fixed angular size the owner asked for (decision 5).
//
// So the VR constants stand on their own physics:
//
//   * THE SIZE is an ANGLE the gizmo subtends at the eye, full stop. No fov, no
//     frame, no window, no resolution: at any distance, in any headset, the
//     translate gizmo's arrows reach kVrGizmoHalfAngleDeg from the pivot.
//   * THE PICK TOLERANCE is the POINTER'S OWN precision — how far a controller
//     ray wanders while a person holds it steady — which is a property of the
//     hand and the tracking, not of a display.
//
// Neither follows from the other any more, and saying so is the honest part: on
// the desk they were ONE decision (both fractions of one frame, so a click
// target was a fixed share of the drawn handle); here they are two measurements
// of two different things, and the ratio between them is a consequence rather
// than a design. At the numbers below the tolerance is 7.5 % of the gizmo's
// half-extent, against 3.7 % on the desk — a slightly more forgiving handle,
// which is what a hand in the air needs against a mouse on a mat.

#include <cmath>

// Nothing here needs the Gizmo CLASS — it is arithmetic over vectors and the
// handle constants, which is what lets a test drive it with no gizmo at all.
#include "irisgl/core/math/vec.h"
#include "viewport/gizmomeshes.h"

namespace gizmoray {

inline float radiansOf(float degrees) { return degrees * 0.01745329252f; }
inline float degreesOf(float radians) { return radians * 57.2957795131f; }

/// THE VR GIZMO'S HALF-EXTENT, DEGREES AT THE EYE — the one number that sets
/// how big the gizmo looks in a headset.
///
/// It is the angle from the pivot to the TRANSLATE gizmo's arrow tips, which is
/// the largest thing the three gizmos draw; the rotation and scale gizmos
/// follow from it through their own reaches (gizmomeshes.h: kRotationOuterExtent
/// and kScaleReach are both 0.073 of the scale against the arrows' 0.095, so
/// they come out at 6.2 degrees when the arrows are at 8).
///
/// WHY 8. A fist held at arm's length is about 10 degrees across and a thumb
/// about 2, so this is a gizmo roughly the size of a held-out fist: big enough
/// to aim a controller at against the 0.6-degree tolerance below, small enough
/// not to swallow the object it stands on. It is ONE number and it is here for
/// the owner's eye at the headset — nothing else has to move to change it.
inline constexpr float kVrGizmoHalfAngleDeg = 8.0f;

/// HOW FAR OFF A HANDLE THE AIM RAY MAY BE AND STILL TAKE IT, degrees.
///
/// THE POINTER'S OWN PRECISION, not a display's: a hand-held controller's ray
/// wanders a few tenths of a degree while a person holds it still (tremor plus
/// the runtime's pose noise), and a target smaller than that wander cannot be
/// hit on purpose. 0.6 degrees is about 6 mm at arm's length and 5 cm at five
/// metres — comfortably above the jitter and comfortably inside the 8-degree
/// gizmo. The other number for the owner's smoke.
inline constexpr float kVrPickToleranceDeg = 0.6f;

/// TWO HANDLES UNDER ONE POINTER ARE A TIE within this many degrees, and a tie
/// is won by the one that faces the pointer more (the pixel path's rule). This
/// is a RANKING band and not reach, so it is carried as the desktop's own
/// proportion of its tolerance (2 px of 7) rather than as a third measurement:
/// 0.171 degrees.
inline constexpr float kVrPickTieDeg = kVrPickToleranceDeg * 2.0f / 7.0f;

/// THE VR SIZE RULE (VR_INPUT_SPEC §5.2, owner decision 5) — a FIXED ANGULAR
/// SIZE, with nothing in it but the distance.
///
/// The gizmo's scale is a world length and every handle is built in units of
/// it, the translate arrows reaching kTranslateReach of it (gizmomeshes.h).
/// Asking those arrows to subtend kVrGizmoHalfAngleDeg at distance d is
///
///     kTranslateReach * scale = d * tan(halfAngle)
///
/// which is what this returns. Proportional to d is the whole point: the same
/// apparent size standing over an object as across the room — and no fov, no
/// resolution and no window appear, so it cannot change with the headset.
///
/// The desktop's rule (Gizmo::updateSize) is deliberately a different
/// expression — a constant fraction of the FRAME — because a monitor's frame is
/// a window on a desk and not the viewer's vision.
inline float vrGizmoScale(float distance)
{
    return distance * std::tan(radiansOf(kVrGizmoHalfAngleDeg)) / GizmoMeshes::kTranslateReach;
}

/// The pick tolerance and the tie band, as angles at the eye.
inline float toleranceRadians() { return radiansOf(kVrPickToleranceDeg); }
inline float tieRadians() { return radiansOf(kVrPickTieDeg); }

/// The angle between two directions, radians. atan2 of the cross and the dot
/// rather than acos of the dot: it keeps its precision at the small angles this
/// whole header works in (a 0.7-degree tolerance is 1e-4 of the cosine).
inline float angleBetween(const iris::Vec3 &a, const iris::Vec3 &b)
{
    const iris::Vec3 u = a.normalized(), v = b.normalized();
    const float s = iris::Vec3::crossProduct(u, v).length();
    const float c = iris::Vec3::dotProduct(u, v);
    return std::atan2(s, c);
}

/// THE ANGULAR DISTANCE FROM A DIRECTION TO A GREAT-CIRCLE SEGMENT — the
/// spherical twin of the pixel path's pointToSegmentPx (rotationgizmo.cpp,
/// translationgizmo.cpp), and used for the same reason: a projected ring or
/// square is walked as a chain of short segments, and the answer is the
/// distance to the nearest one.
///
/// `d`, `a`, `b` are directions from the ray's origin (normalised here). The
/// perpendicular foot on the great circle through a and b is used when it lies
/// BETWEEN them; otherwise the nearer endpoint wins, which is what makes the
/// chain of segments answer the same as one continuous curve.
inline float angleToArc(const iris::Vec3 &d, const iris::Vec3 &a, const iris::Vec3 &b)
{
    const iris::Vec3 u = d.normalized(), p = a.normalized(), q = b.normalized();
    const iris::Vec3 n = iris::Vec3::crossProduct(p, q);
    const float nl = n.length();
    const float ends = std::fmin(angleBetween(u, p), angleBetween(u, q));
    if (nl < 1e-7f) return ends;                 // the two ends are one direction
    const iris::Vec3 axis = n / nl;
    const float perp = iris::Vec3::dotProduct(u, axis);   // sin of the distance
    // The foot of the perpendicular, back on the great circle.
    iris::Vec3 foot = u - axis * perp;
    if (foot.lengthSquared() < 1e-12f) return ends;       // d is the circle's pole
    foot.normalize();
    const bool afterP = iris::Vec3::dotProduct(iris::Vec3::crossProduct(p, foot), axis) >= 0.0f;
    const bool beforeQ = iris::Vec3::dotProduct(iris::Vec3::crossProduct(foot, q), axis) >= 0.0f;
    if (afterP && beforeQ)
        return std::asin(std::fmin(std::fmax(std::fabs(perp), 0.0f), 1.0f));
    return ends;
}

/// IS A DIRECTION INSIDE A CONVEX SPHERICAL QUAD (the four corners as
/// directions, in order)? The pixel path asks the same question of a projected
/// quadrilateral with a cross-product sign per edge; on the sphere the edge's
/// "side" is the sign of dot(d, corner_i x corner_i+1), and a direction inside
/// a convex quad is on the same side of all four.
inline bool insideSphericalQuad(const iris::Vec3 &d, const iris::Vec3 corners[4])
{
    bool positive = false, negative = false;
    const iris::Vec3 u = d.normalized();
    for (int i = 0; i < 4; ++i) {
        const iris::Vec3 a = corners[i].normalized();
        const iris::Vec3 b = corners[(i + 1) % 4].normalized();
        const float side = iris::Vec3::dotProduct(u, iris::Vec3::crossProduct(a, b));
        if (side > 0.0f) positive = true;
        if (side < 0.0f) negative = true;
    }
    return !(positive && negative);
}

/// THE ANGULAR DISTANCE FROM A DIRECTION TO A SPHERICAL QUAD: zero inside it,
/// the distance to the nearest edge outside — the pixel path's answer for a
/// plane handle, in radians (translationgizmo.cpp's planeDistance).
inline float angleToQuad(const iris::Vec3 &d, const iris::Vec3 corners[4])
{
    if (insideSphericalQuad(d, corners)) return 0.0f;
    float best = -1.0f;
    for (int i = 0; i < 4; ++i) {
        const float a = angleToArc(d, corners[i], corners[(i + 1) % 4]);
        if (best < 0.0f || a < best) best = a;
    }
    return best < 0.0f ? 0.0f : best;
}

}   // namespace gizmoray

#endif   // GIZMORAY_H
