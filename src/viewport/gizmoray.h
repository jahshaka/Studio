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
// THE UNITS, ONCE. A perspective frame of vertical angle `fov` and height `h`
// pixels subtends, at its centre, `h / (2 tan(fov/2))` pixels per radian. So a
// tolerance of `p` pixels of that frame is
//
//     p / h * 2 tan(fov/2)   radians
//
// — and since the gizmo's own size rule is `kGizmoScreenFraction * d *
// tan(fov/2)` (gizmo.cpp), i.e. also proportional to `tan(fov/2)`, the RATIO of
// the tolerance to the drawn handle is the same number on the desk and in the
// headset. A handle that is a 4 % target of its ring on a monitor is a 4 %
// target of the same ring at arm's length, which is the property that makes the
// desktop's tuned constants (7 px on a ring, 4 px around a plane square) worth
// carrying over rather than re-inventing.

#include <cmath>

#include "irisgl/core/math/vec.h"
#include "viewport/gizmo.h"

namespace gizmoray {

/// THE FRAME THE DESKTOP'S PIXEL TOLERANCES WERE TUNED IN. 1080 logical pixels
/// tall is the rig's own display (CLAUDE.md's 1920x1080 law) and the shape the
/// pick suites measure in; it is the denominator that turns a pixel constant
/// into a fraction of the frame, and nothing else.
inline constexpr float kNominalFrameHeightPx = 1080.0f;

/// THE EYE THE VR GIZMO IS SIZED FOR lives in gizmo.h beside the desktop's own
/// calibration constant (`kVrNominalEyeFovDegrees`), because the two are one
/// decision: the gizmo's size rule is the same expression on both surfaces and
/// only the angle it is evaluated at differs.

inline float radiansOf(float degrees) { return degrees * 0.01745329252f; }
inline float degreesOf(float radians) { return radians * 57.2957795131f; }

/// tan(fov/2), guarded at the ends of the range the way Gizmo::updateSize is.
inline float halfFovTangent(float fovDegrees)
{
    const float f = std::fmin(std::fmax(fovDegrees, 1.0f), 179.0f);
    return std::tan(radiansOf(f * 0.5f));
}

/// THE VR SIZE RULE (VR_INPUT_SPEC §5.2, owner decision 5).
///
/// `gizmoScale = kGizmoScreenFraction * distance * tan(fov/2)` — the desktop's
/// own expression (gizmo.cpp's long note) with the EYE's angle in place of a
/// document camera's and the distance measured from the wearer's head. Being
/// proportional to the distance is the whole point: the gizmo then subtends a
/// CONSTANT ANGLE, so it is the same apparent size whether the wearer is
/// standing over the object or across the room, exactly as it is the same
/// fraction of the frame at every dolly on the desk.
///
/// At the nominal 90-degree eye this is 4.33 * distance: a translate arrow
/// (handleLength 1.7 * handleScale 0.05 = 0.085 of the scale) reaches 0.368 *
/// distance, i.e. about 20 degrees of the wearer's view, which is the same
/// fraction of the frame the desktop gizmo occupies.
inline float vrGizmoScale(float distance, float fovDegrees = kVrNominalEyeFovDegrees)
{
    return kGizmoScreenFraction * distance * halfFovTangent(fovDegrees);
}

/// A PIXEL TOLERANCE OF THE NOMINAL FRAME, IN RADIANS AT THE EYE (see the unit
/// note above). 7 px of a 1080-tall frame at a 90-degree eye = 0.01296 rad =
/// 0.743 degrees; 4 px = 0.424 degrees; the 2 px ring tie band = 0.212 degrees.
inline float toleranceRadians(float pixels, float fovDegrees = kVrNominalEyeFovDegrees)
{
    return pixels / kNominalFrameHeightPx * 2.0f * halfFovTangent(fovDegrees);
}

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
