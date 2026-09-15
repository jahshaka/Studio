/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/vec.h"
#include "viewport/gizmomeshes.h"

#include <QVector>
#include <QtMath>
#include <cmath>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"

namespace
{

// ---- the one place the gizmo look is tuned --------------------------------------
// All values are in handle-local units (scaled by gizmoScale * handleScale later).
const float kShaftRadius   = 0.0175f; // thin axis line (halved 2026-08-30; before: 0.035, old OBJ girth: 0.211)
/// WHERE A SHAFT STARTS: exactly ON the surface of the core its gizmo draws —
/// the translate ball's radius and the scale cube's half-extent are the same
/// number (GizmoMeshes::kCentreBallRadius), so neither gizmo draws a line
/// inside its own centre and neither leaves a gap around it (GIZMO-3 item 3).
const float kShaftStart    = GizmoMeshes::kCentreBallRadius;
const float kConeBase      = 0.115f;  // small arrow head
const float kConeStart     = 1.56f;
const float kAxisEnd       = GizmoMeshes::kTranslateEnd;  // same reach as the old translate handle
const float kCubeHalf      = 0.11f;   // small scale-tip cube
const float kScaleEnd      = GizmoMeshes::kScaleEnd;      // same reach as the old scale handle
const float kCoreSphere    = GizmoMeshes::kCentreBallRadius;   // grown from 0.10 by GIZMO-3 item 3
const float kCoreCubeHalf  = GizmoMeshes::kCentreBallRadius;   // ...to the scale gizmo's cube, unchanged at 0.12
/// THE WEIGHT OF EVERY LINE THE GIZMOS DRAW (GIZMO-3 item 2, owner: the rings
/// should read like Blender's). Doubled from 0.01: the rings measured 3 pixels
/// wide at the shipped size and Blender's are 3-4 at a gizmo two thirds the
/// size, so its lines are two thirds heavier again relative to the circle they
/// draw. 0.02 is ~6 pixels at 1080p. Retune HERE: the outer screen ring reads
/// this number, and so does the plane frames' radius below, so the whole gizmo
/// family keeps one line weight.
const float kRingMinor     = 0.02f;
// the outer ring's radius lives in the header: picking projects the same circle
const float kScreenRingMinor = kRingMinor;   // the outer ring at the axis rings' weight (GIZMO-3 item 2)
const int   kSegments      = 20;      // round sections
const int   kRingSegments  = 64;      // ring smoothness
const int   kRingSides     = 8;

struct Builder
{
    QVector<float> pos, nrm;

    void vertex(const iris::Vec3 &p, const iris::Vec3 &n)
    {
        pos << p.x() << p.y() << p.z();
        nrm << n.x() << n.y() << n.z();
    }
    void tri(const iris::Vec3 &a, const iris::Vec3 &b, const iris::Vec3 &c,
             const iris::Vec3 &na, const iris::Vec3 &nb, const iris::Vec3 &nc)
    {
        vertex(a, na); vertex(b, nb); vertex(c, nc);
    }
    void triFlat(const iris::Vec3 &a, const iris::Vec3 &b, const iris::Vec3 &c)
    {
        const iris::Vec3 n = iris::Vec3::normal(b - a, c - a);
        tri(a, b, c, n, n, n);
    }
    void quad(const iris::Vec3 &a, const iris::Vec3 &b, const iris::Vec3 &c, const iris::Vec3 &d,
              const iris::Vec3 &na, const iris::Vec3 &nb, const iris::Vec3 &nc, const iris::Vec3 &nd)
    {
        tri(a, b, c, na, nb, nc);
        tri(a, c, d, na, nc, nd);
    }

    iris::MeshPtr build() const
    {
        auto mesh = iris::Mesh::create();

        iris::VertexLayout posLayout;
        posLayout.addAttrib(iris::VertexAttribUsage::Position, iris::AttribTypeFloat, 3, sizeof(float) * 3);
        auto pb = iris::VertexBuffer::create(posLayout);
        pb->setData((void *)pos.constData(), pos.size() * sizeof(float));
        mesh->addVertexBuffer(pb);

        iris::VertexLayout nrmLayout;
        nrmLayout.addAttrib(iris::VertexAttribUsage::Normal, iris::AttribTypeFloat, 3, sizeof(float) * 3);
        auto nb = iris::VertexBuffer::create(nrmLayout);
        nb->setData((void *)nrm.constData(), nrm.size() * sizeof(float));
        mesh->addVertexBuffer(nb);

        mesh->setPrimitiveMode(iris::PrimitiveMode::Triangles);
        mesh->setVertexCount(pos.size() / 3);
        return mesh;
    }
};

/// The two axes a PLANE handle lies in (XYPlane -> X and Y, and so on).
bool planeAxes(GizmoAxis axis, iris::Vec3 &U, iris::Vec3 &V)
{
    switch (axis) {
    case GizmoAxis::XYPlane: U = iris::Vec3(1, 0, 0); V = iris::Vec3(0, 1, 0); return true;
    case GizmoAxis::YZPlane: U = iris::Vec3(0, 1, 0); V = iris::Vec3(0, 0, 1); return true;
    case GizmoAxis::XZPlane: U = iris::Vec3(1, 0, 0); V = iris::Vec3(0, 0, 1); return true;
    default: return false;
    }
}

/// Orthonormal frame for an axis: A is the handle direction, U/V span its cross section.
void axisFrame(GizmoAxis axis, iris::Vec3 &A, iris::Vec3 &U, iris::Vec3 &V)
{
    switch (axis) {
    case GizmoAxis::X: A = iris::Vec3(1, 0, 0); U = iris::Vec3(0, 1, 0); V = iris::Vec3(0, 0, 1); break;
    case GizmoAxis::Y: A = iris::Vec3(0, 1, 0); U = iris::Vec3(0, 0, 1); V = iris::Vec3(1, 0, 0); break;
    default:           A = iris::Vec3(0, 0, 1); U = iris::Vec3(1, 0, 0); V = iris::Vec3(0, 1, 0); break;
    }
}

void addCylinder(Builder &b, const iris::Vec3 &A, const iris::Vec3 &U, const iris::Vec3 &V,
                 float t0, float t1, float radius, int segments)
{
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(2.0 * M_PI * i / segments);
        const float a1 = float(2.0 * M_PI * (i + 1) / segments);
        const iris::Vec3 r0 = U * qCos(a0) + V * qSin(a0);
        const iris::Vec3 r1 = U * qCos(a1) + V * qSin(a1);
        b.quad(A * t0 + r0 * radius, A * t0 + r1 * radius,
               A * t1 + r1 * radius, A * t1 + r0 * radius,
               r0, r1, r1, r0);
    }
    // end caps
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(2.0 * M_PI * i / segments);
        const float a1 = float(2.0 * M_PI * (i + 1) / segments);
        const iris::Vec3 r0 = U * qCos(a0) + V * qSin(a0);
        const iris::Vec3 r1 = U * qCos(a1) + V * qSin(a1);
        b.tri(A * t0, A * t0 + r1 * radius, A * t0 + r0 * radius, -A, -A, -A);
        b.tri(A * t1, A * t1 + r0 * radius, A * t1 + r1 * radius,  A,  A,  A);
    }
}

/// A thin tube between two arbitrary points — the "line" the plane frames are
/// drawn as (GIZMO-2 item 1). A cross-section frame is derived from the
/// segment's own direction, so the caller does not have to supply one.
void addTube(Builder &b, const iris::Vec3 &p0, const iris::Vec3 &p1, float radius, int segments)
{
    iris::Vec3 A = p1 - p0;
    const float len = A.length();
    if (len < 1e-6f) return;
    A /= len;
    // Any vector not parallel to A gives a stable cross section.
    const iris::Vec3 seed = std::fabs(A.x()) < 0.9f ? iris::Vec3(1, 0, 0) : iris::Vec3(0, 1, 0);
    const iris::Vec3 U = iris::Vec3::crossProduct(A, seed).normalized();
    const iris::Vec3 V = iris::Vec3::crossProduct(A, U).normalized();
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(2.0 * M_PI * i / segments);
        const float a1 = float(2.0 * M_PI * (i + 1) / segments);
        const iris::Vec3 r0 = U * qCos(a0) + V * qSin(a0);
        const iris::Vec3 r1 = U * qCos(a1) + V * qSin(a1);
        b.quad(p0 + r0 * radius, p0 + r1 * radius, p1 + r1 * radius, p1 + r0 * radius,
               r0, r1, r1, r0);
        // caps: the frames meet at right angles, so the ends are visible
        b.tri(p0, p0 + r1 * radius, p0 + r0 * radius, -A, -A, -A);
        b.tri(p1, p1 + r0 * radius, p1 + r1 * radius,  A,  A,  A);
    }
}

void addCone(Builder &b, const iris::Vec3 &A, const iris::Vec3 &U, const iris::Vec3 &V,
             float tBase, float tTip, float radius, int segments)
{
    const iris::Vec3 tip = A * tTip;
    const float slope = radius / (tTip - tBase);
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(2.0 * M_PI * i / segments);
        const float a1 = float(2.0 * M_PI * (i + 1) / segments);
        const iris::Vec3 r0 = U * qCos(a0) + V * qSin(a0);
        const iris::Vec3 r1 = U * qCos(a1) + V * qSin(a1);
        const iris::Vec3 n0 = (r0 + A * slope).normalized();
        const iris::Vec3 n1 = (r1 + A * slope).normalized();
        b.tri(A * tBase + r0 * radius, A * tBase + r1 * radius, tip, n0, n1, (n0 + n1).normalized());
        // base cap
        b.tri(A * tBase, A * tBase + r1 * radius, A * tBase + r0 * radius, -A, -A, -A);
    }
}

void addCube(Builder &b, const iris::Vec3 &center, float half)
{
    const iris::Vec3 x(half, 0, 0), y(0, half, 0), z(0, 0, half);
    auto face = [&](const iris::Vec3 &n, const iris::Vec3 &u, const iris::Vec3 &v) {
        const iris::Vec3 c = center + n * half;
        b.quad(c - u - v, c + u - v, c + u + v, c - u + v, n, n, n, n);
    };
    face(iris::Vec3( 1, 0, 0), y.normalized() * half, z.normalized() * half);
    face(iris::Vec3(-1, 0, 0), z.normalized() * half, y.normalized() * half);
    face(iris::Vec3(0,  1, 0), z.normalized() * half, x.normalized() * half);
    face(iris::Vec3(0, -1, 0), x.normalized() * half, z.normalized() * half);
    face(iris::Vec3(0, 0,  1), x.normalized() * half, y.normalized() * half);
    face(iris::Vec3(0, 0, -1), y.normalized() * half, x.normalized() * half);
}

void addSphere(Builder &b, float radius, int rings, int segments)
{
    auto point = [&](int ri, int si) {
        const float phi = float(M_PI) * ri / rings;             // 0..pi
        const float theta = float(2.0 * M_PI) * si / segments;  // 0..2pi
        return iris::Vec3(qSin(phi) * qCos(theta), qCos(phi), qSin(phi) * qSin(theta));
    };
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s) {
            const iris::Vec3 a = point(r, s), c = point(r + 1, s + 1);
            const iris::Vec3 bb = point(r, s + 1), d = point(r + 1, s);
            b.quad(a * radius, bb * radius, c * radius, d * radius, a, bb, c, d);
        }
}

/// A torus, or an ARC of one: the ring sweeps `sweep` radians starting at
/// `start`, measured from +U towards +V. A full circle is start 0, sweep 2*pi.
void addTorusArc(Builder &b, const iris::Vec3 &A, const iris::Vec3 &U, const iris::Vec3 &V,
                 float major, float minor, int segments, int sides, float start, float sweep)
{
    auto point = [&](int si, int ti, iris::Vec3 &n) {
        const float theta = start + sweep * si / segments;       // around the ring
        const float phi = float(2.0 * M_PI) * ti / sides;        // around the tube
        const iris::Vec3 dir = U * qCos(theta) + V * qSin(theta); // outward in the ring plane
        n = dir * qCos(phi) + A * qSin(phi);
        return dir * major + n * minor;
    };
    for (int s = 0; s < segments; ++s)
        for (int t = 0; t < sides; ++t) {
            iris::Vec3 na, nb, nc, nd;
            const iris::Vec3 a = point(s, t, na),         bb = point(s + 1, t, nb);
            const iris::Vec3 c = point(s + 1, t + 1, nc), d = point(s, t + 1, nd);
            b.quad(a, bb, c, d, na, nb, nc, nd);
        }
    // No end caps: the overlay material is unlit and two-sided (CULL_NONE),
    // so an open tube end shows its own inside, in the same flat colour.
}

void addTorus(Builder &b, const iris::Vec3 &A, const iris::Vec3 &U, const iris::Vec3 &V,
              float major, float minor, int segments, int sides)
{
    addTorusArc(b, A, U, V, major, minor, segments, sides, 0.0f, float(2.0 * M_PI));
}

} // namespace

namespace GizmoMeshes
{

// THE PLANE FRAMES ARE DRAWN AT THE RINGS' WIDTH, derived rather than copied
// (second reader, GIZMO-2 round 2): a ring's tube is kRingMinor of the ROTATION
// handleScale, and a plane frame is drawn at the TRANSLATE one, so the radius
// that matches them is kRingMinor * kRotationHandleScale / kHandleScale —
// 0.028456 at today's tuning. Written this way, retuning kRotationExtentRatio
// cannot silently un-match the two.
const float kPlaneFrameRadius = kRingMinor * kRotationHandleScale / kHandleScale;

iris::MeshPtr translateHandle(GizmoAxis axis)
{
    iris::Vec3 A, U, V;
    axisFrame(axis, A, U, V);
    Builder b;
    addCylinder(b, A, U, V, kShaftStart, kConeStart, kShaftRadius, kSegments);
    addCone(b, A, U, V, kConeStart, kAxisEnd, kConeBase, kSegments);
    return b.build();
}

iris::MeshPtr scaleHandle(GizmoAxis axis)
{
    iris::Vec3 A, U, V;
    axisFrame(axis, A, U, V);
    Builder b;
    addCylinder(b, A, U, V, kShaftStart, kScaleEnd - 2.0f * kCubeHalf, kShaftRadius, kSegments);
    addCube(b, A * (kScaleEnd - kCubeHalf), kCubeHalf);
    return b.build();
}

iris::MeshPtr centerSphere()
{
    Builder b;
    addSphere(b, kCoreSphere, 10, 16);
    return b.build();
}

iris::MeshPtr centerCube()
{
    Builder b;
    addCube(b, iris::Vec3(0, 0, 0), kCoreCubeHalf);
    return b.build();
}

iris::MeshPtr rotationRingHalf(GizmoAxis axis)
{
    iris::Vec3 A, U, V;
    axisFrame(axis, A, U, V);
    Builder b;
    // The 180-degree arc CENTRED on +U, i.e. [-90, +90] degrees: half the
    // segments for half the circle, so the chords stay the same length (and
    // therefore the same fraction of a pixel off the true circle) as before.
    addTorusArc(b, A, U, V, 1.0f, kRingMinor, kRingSegments / 2, kRingSides,
                -float(M_PI) * 0.5f, float(M_PI));
    return b.build();
}

iris::MeshPtr planeHandle(GizmoAxis axis)
{
    iris::Vec3 U, V;
    Builder b;
    if (!planeAxes(axis, U, V)) return b.build();
    // THE FRAME OF THE PLANE IT REPRESENTS (owner §368): four thin lines, the
    // inner corner at the origin, the two sides running out along the two
    // arrows' axes. Nothing inside it — the square is still what is PICKED
    // (TranslationHandle::planeDistance), the frame is what is drawn.
    //
    // Tubes rather than a line primitive because the whole gizmo is a triangle
    // soup drawn through one unlit material: the rotation rings are tubes of
    // the same drawn width, so a frame drawn this way matches them exactly at
    // every distance, and needs no second draw path in the overlay.
    const float s = kPlaneHandleSpan;
    const float r = kPlaneFrameRadius;
    // ...AND THE TWO INNER LEGS START ON THE BALL (GIZMO-3 item 3). The corner
    // is still the gizmo's origin — the legs still run along the two arrows —
    // but the part of them that used to be drawn INSIDE the white centre ball
    // is not drawn: the ball is what the eye reads as the corner, exactly as
    // the scale gizmo's cube is. Measured before: the two inner legs put 88
    // teal/purple pixels inside the ball's disc.
    const float b0 = GizmoMeshes::kCentreBallRadius;
    const iris::Vec3 u = U * s, v = V * s, uv = U * s + V * s;
    addTube(b, U * b0, u,  r, 8);     // along the first axis, at the second's 0
    addTube(b, V * b0, v,  r, 8);     // along the second axis, at the first's 0
    addTube(b, u,      uv, r, 8);     // the outer side parallel to the second axis
    addTube(b, v,      uv, r, 8);     // the outer side parallel to the first axis
    return b.build();
}

// ---- the drag marker (GIZMO-2 item 3) --------------------------------------
// In handle-local units, where the axis ring is the unit circle: the hub is a
// filled disc of kDragHubRadius in the ring's own plane, the arrow a thin shaft
// from the hub's edge to kDragHead ending in a cone that stops exactly ON
// the ring. The shaft is the rings' own thickness so the marker reads as part
// of the same drawing.
namespace {
const float kDragHubRadius   = 0.15f;
const float kDragShaftRadius = 0.012f;
const float kDragHead        = 0.78f;   // where the arrowhead starts
const float kDragConeBase    = 0.075f;  // the arrowhead's radius
}

iris::MeshPtr dragHub(GizmoAxis axis)
{
    iris::Vec3 A, U, V;
    axisFrame(axis == GizmoAxis::Screen ? GizmoAxis::Z : axis, A, U, V);
    Builder b;
    // A filled disc in the plane the ring lies in, both sides: the marker is
    // looked at from wherever the camera happens to be.
    const int segments = 24;
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(2.0 * M_PI * i / segments);
        const float a1 = float(2.0 * M_PI * (i + 1) / segments);
        const iris::Vec3 r0 = (U * qCos(a0) + V * qSin(a0)) * kDragHubRadius;
        const iris::Vec3 r1 = (U * qCos(a1) + V * qSin(a1)) * kDragHubRadius;
        b.tri(iris::Vec3(0, 0, 0), r0, r1,  A,  A,  A);
        b.tri(iris::Vec3(0, 0, 0), r1, r0, -A, -A, -A);
    }
    return b.build();
}

iris::MeshPtr dragArrow(GizmoAxis axis)
{
    Builder b;
    if (axis != GizmoAxis::X && axis != GizmoAxis::Y && axis != GizmoAxis::Z) return b.build();
    iris::Vec3 A, U, V;
    axisFrame(axis, A, U, V);
    addCylinder(b, A, U, V, kDragHubRadius, kDragHead, kDragShaftRadius, kSegments);
    // The head stops at 1.0 — the axis ring's own radius, so the arrow points
    // from the centre out TO the ring being dragged.
    addCone(b, A, U, V, kDragHead, 1.0f, kDragConeBase, kSegments);
    return b.build();
}

iris::MeshPtr screenRing()
{
    Builder b;
    addTorus(b, iris::Vec3(0, 0, 1), iris::Vec3(1, 0, 0), iris::Vec3(0, 1, 0),
             GizmoMeshes::kScreenRingRadius, kScreenRingMinor, kRingSegments, kRingSides);
    return b.build();
}

} // namespace GizmoMeshes
