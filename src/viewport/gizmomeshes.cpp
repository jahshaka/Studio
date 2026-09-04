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

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"

namespace
{

// ---- the one place the gizmo look is tuned --------------------------------------
// All values are in handle-local units (scaled by gizmoScale * handleScale later).
const float kShaftRadius   = 0.0175f; // thin axis line (halved 2026-08-30; before: 0.035, old OBJ girth: 0.211)
const float kShaftStart    = 0.12f;   // leave the core clear
const float kConeBase      = 0.115f;  // small arrow head
const float kConeStart     = 1.56f;
const float kAxisEnd       = 1.90f;   // same reach as the old translate handle
const float kCubeHalf      = 0.11f;   // small scale-tip cube
const float kScaleEnd      = 1.46f;   // same reach as the old scale handle
const float kCoreSphere    = 0.10f;
const float kCoreCubeHalf  = 0.12f;
const float kRingMinor     = 0.01f;   // thin rotation circles (halved 2026-08-30; old rings were flat fat bands)
const float kScreenRingR   = 1.18f;   // just outside the axis rings
const float kScreenRingMinor = 0.007f;  // halved 2026-08-30 (was 0.014)
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

void addTorus(Builder &b, const iris::Vec3 &A, const iris::Vec3 &U, const iris::Vec3 &V,
              float major, float minor, int segments, int sides)
{
    auto point = [&](int si, int ti, iris::Vec3 &n) {
        const float theta = float(2.0 * M_PI) * si / segments;   // around the ring
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
}

} // namespace

namespace GizmoMeshes
{

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

iris::MeshPtr rotationRing(GizmoAxis axis)
{
    iris::Vec3 A, U, V;
    axisFrame(axis, A, U, V);
    Builder b;
    addTorus(b, A, U, V, 1.0f, kRingMinor, kRingSegments, kRingSides);
    return b.build();
}

iris::MeshPtr screenRing()
{
    Builder b;
    addTorus(b, iris::Vec3(0, 0, 1), iris::Vec3(1, 0, 0), iris::Vec3(0, 1, 0),
             kScreenRingR, kScreenRingMinor, kRingSegments, kRingSides);
    return b.build();
}

} // namespace GizmoMeshes
