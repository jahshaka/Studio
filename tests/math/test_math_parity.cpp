/**************************************************************************
This file is part of the Jahshaka test suite
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)
*************************************************************************/

// -----------------------------------------------------------------------------
// math.parity — iris::Vec2/Vec3/Vec4/Quat/Mat3/Mat4 against Qt, BIT FOR BIT.
//
// The document model's math types were Qt's until this suite existed. Replacing
// them is a compile-time and layering win only; it must be a numeric NO-OP,
// because every scene ever saved has its rotations, positions and scales written
// by Qt's arithmetic, and scene.reopen_fidelity asserts those values come back
// unchanged. "Close enough" is not a passing grade here: a rotation that differs
// in the last bit is a scene that reopens rotated.
//
// So every check below is a memcmp of the float bits, not a tolerance. The
// harness runs 4,000 randomized iterations plus a full euler lattice sweep, and
// it drives CALL SEQUENCES (identity -> translate -> rotate -> scale -> invert)
// rather than isolated calls, because QMatrix4x4's flag bits make its arithmetic
// path depend on its history.
//
// If this suite ever goes red, the answer is never to loosen it. Disassembling
// the Qt function in question is how the last six mismatches were resolved
// (Qt 6 rewrote getEulerAngles, normalized() and getAxisAndAngle relative to
// Qt 5, and QMatrix4x4::inverted has a separate affine path that assigns its
// last row instead of solving for it).
// -----------------------------------------------------------------------------

#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QQuaternion>
#include <QMatrix4x4>
#include <QMatrix3x3>

#include "core/math/vec.h"
#include "core/math/quat.h"
#include "core/math/mat4.h"
#include "core/math/qtinterop.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>

static int failures = 0;
static int checks = 0;

static bool bitEq(float a, float b)
{
    if (std::isnan(a) && std::isnan(b)) return true;
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

static void chk(const char *what, float q, float i)
{
    ++checks;
    if (!bitEq(q, i)) {
        if (failures < 40)
            std::printf("FAIL %-42s qt=%.9g (%08x) iris=%.9g (%08x)\n", what, q,
                        *(unsigned *)&q, i, *(unsigned *)&i);
        ++failures;
    }
}

static void chk3(const char *what, QVector3D q, iris::Vec3 i)
{
    std::string s(what);
    chk((s + ".x").c_str(), q.x(), i.x());
    chk((s + ".y").c_str(), q.y(), i.y());
    chk((s + ".z").c_str(), q.z(), i.z());
}
static void chk4(const char *what, QVector4D q, iris::Vec4 i)
{
    std::string s(what);
    chk((s + ".x").c_str(), q.x(), i.x());
    chk((s + ".y").c_str(), q.y(), i.y());
    chk((s + ".z").c_str(), q.z(), i.z());
    chk((s + ".w").c_str(), q.w(), i.w());
}
static void chkq(const char *what, QQuaternion q, iris::Quat i)
{
    std::string s(what);
    chk((s + ".s").c_str(), q.scalar(), i.scalar());
    chk((s + ".x").c_str(), q.x(), i.x());
    chk((s + ".y").c_str(), q.y(), i.y());
    chk((s + ".z").c_str(), q.z(), i.z());
}
static void chkm(const char *what, const QMatrix4x4 &q, const iris::Mat4 &i)
{
    std::string s(what);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            chk((s + "(" + char('0' + r) + "," + char('0' + c) + ")").c_str(), q(r, c), i(r, c));
}
static void chkm3(const char *what, const QMatrix3x3 &q, const iris::Mat3 &i)
{
    std::string s(what);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            chk((s + "(" + char('0' + r) + "," + char('0' + c) + ")").c_str(), q(r, c), i(r, c));
}

int main()
{
    std::mt19937 rng(20260904u);
    std::uniform_real_distribution<float> d(-3.0f, 3.0f);
    std::uniform_real_distribution<float> ang(-720.0f, 720.0f);
    std::uniform_real_distribution<float> sml(-1e-6f, 1e-6f);

    for (int iter = 0; iter < 4000; ++iter) {
        const float a = d(rng), b = d(rng), c = d(rng), e = d(rng);
        const float f = d(rng), g = d(rng), h = d(rng), k = d(rng);

        QVector3D qv(a, b, c), qw(e, f, g);
        iris::Vec3 iv(a, b, c), iw(e, f, g);

        chk("v3.length", qv.length(), iv.length());
        chk("v3.lengthSquared", qv.lengthSquared(), iv.lengthSquared());
        chk3("v3.normalized", qv.normalized(), iv.normalized());
        chk("v3.dot", QVector3D::dotProduct(qv, qw), iris::Vec3::dotProduct(iv, iw));
        chk3("v3.cross", QVector3D::crossProduct(qv, qw), iris::Vec3::crossProduct(iv, iw));
        chk3("v3.normal", QVector3D::normal(qv, qw), iris::Vec3::normal(iv, iw));
        chk("v3.distanceToPoint", qv.distanceToPoint(qw), iv.distanceToPoint(iw));
        chk3("v3.add", qv + qw, iv + iw);
        chk3("v3.mul", qv * qw, iv * iw);
        chk3("v3.scale", qv * e, iv * e);
        { QVector3D t = qv; t.normalize(); iris::Vec3 u = iv; u.normalize(); chk3("v3.normalize", t, u); }

        QVector2D qv2(a, b);
        iris::Vec2 iv2(a, b);
        chk("v2.length", qv2.length(), iv2.length());
        chk("v2.normalized.x", qv2.normalized().x(), iv2.normalized().x());

        QVector4D qv4(a, b, c, e);
        iris::Vec4 iv4(a, b, c, e);
        chk("v4.length", qv4.length(), iv4.length());
        chk4("v4.normalized", qv4.normalized(), iv4.normalized());
        chk3("v4.toVector3D", qv4.toVector3D(), iv4.toVector3D());

        // --------------------------------------------------------- quaternion
        QQuaternion qq(a, b, c, e), qq2(f, g, h, k);
        iris::Quat iq(a, b, c, e), iq2(f, g, h, k);

        chkq("q.normalized", qq.normalized(), iq.normalized());
        chkq("q.conjugated", qq.conjugated(), iq.conjugated());
        chkq("q.inverted", qq.inverted(), iq.inverted());
        chk("q.length", qq.length(), iq.length());
        chk("q.lengthSquared", qq.lengthSquared(), iq.lengthSquared());
        chk("q.dot", QQuaternion::dotProduct(qq, qq2), iris::Quat::dotProduct(iq, iq2));
        chkq("q.mul", qq * qq2, iq * iq2);
        chkq("q.add", qq + qq2, iq + iq2);
        chkq("q.neg", -qq, -iq);
        chk3("q.rotatedVector", qq.rotatedVector(qv), iq.rotatedVector(iv));
        chk3("q.vector", qq.vector(), iq.vector());
        { QQuaternion t = qq; t.normalize(); iris::Quat u = iq; u.normalize(); chkq("q.normalize", t, u); }

        const float p1 = ang(rng), y1 = ang(rng), r1 = ang(rng);
        chkq("q.fromEulerAngles", QQuaternion::fromEulerAngles(p1, y1, r1),
             iris::Quat::fromEulerAngles(p1, y1, r1));

        QQuaternion qe = QQuaternion::fromEulerAngles(p1, y1, r1);
        iris::Quat ie = iris::Quat::fromEulerAngles(p1, y1, r1);
        chk3("q.toEulerAngles", qe.toEulerAngles(), ie.toEulerAngles());
        chk3("q.toEulerAngles.raw", qq.toEulerAngles(), iq.toEulerAngles());
        chkq("q.euler.roundtrip", QQuaternion::fromEulerAngles(qe.toEulerAngles()),
             iris::Quat::fromEulerAngles(ie.toEulerAngles()));

        chkq("q.fromAxisAndAngle", QQuaternion::fromAxisAndAngle(qv, p1),
             iris::Quat::fromAxisAndAngle(iv, p1));
        chkq("q.fromAxisAndAngle4", QQuaternion::fromAxisAndAngle(a, b, c, p1),
             iris::Quat::fromAxisAndAngle(a, b, c, p1));
        {
            float ax, ay, az, aa, bx, by, bz, ba;
            qe.getAxisAndAngle(&ax, &ay, &az, &aa);
            ie.getAxisAndAngle(&bx, &by, &bz, &ba);
            chk("q.axisAngle.x", ax, bx);
            chk("q.axisAngle.y", ay, by);
            chk("q.axisAngle.z", az, bz);
            chk("q.axisAngle.a", aa, ba);
        }
        chkq("q.rotationTo", QQuaternion::rotationTo(qv, qw), iris::Quat::rotationTo(iv, iw));
        chkq("q.slerp", QQuaternion::slerp(qe, QQuaternion::fromEulerAngles(r1, p1, y1), std::abs(a) / 3.0f),
             iris::Quat::slerp(ie, iris::Quat::fromEulerAngles(r1, p1, y1), std::abs(a) / 3.0f));
        chkq("q.nlerp", QQuaternion::nlerp(qe, QQuaternion::fromEulerAngles(r1, p1, y1), std::abs(a) / 3.0f),
             iris::Quat::nlerp(ie, iris::Quat::fromEulerAngles(r1, p1, y1), std::abs(a) / 3.0f));
        chkq("q.fromDirection", QQuaternion::fromDirection(qv, qw), iris::Quat::fromDirection(iv, iw));
        chkm3("q.toRotationMatrix", qe.toRotationMatrix(), ie.toRotationMatrix());
        chkq("q.fromRotationMatrix", QQuaternion::fromRotationMatrix(qe.toRotationMatrix()),
             iris::Quat::fromRotationMatrix(ie.toRotationMatrix()));
        {
            QVector3D xa, ya, za;
            iris::Vec3 ixa, iya, iza;
            qe.getAxes(&xa, &ya, &za);
            ie.getAxes(&ixa, &iya, &iza);
            chk3("q.getAxes.x", xa, ixa);
            chkq("q.fromAxes", QQuaternion::fromAxes(xa, ya, za), iris::Quat::fromAxes(ixa, iya, iza));
        }

        // ------------------------------------------------------------- matrix
        // TRS built exactly the way SceneNode::getLocalTransform builds it.
        QMatrix4x4 qm;
        iris::Mat4 im;
        qm.setToIdentity();
        im.setToIdentity();
        qm.translate(qv);
        im.translate(iv);
        qm.rotate(qe);
        im.rotate(ie);
        qm.scale(qw);
        im.scale(iw);
        chkm("m.trs", qm, im);
        chk("m.flags", float(int(qm.flags().toInt())), float(im.flags()));

        chkm("m.trs.inverted", qm.inverted(), im.inverted());
        chkm("m.trs.transposed", qm.transposed(), im.transposed());
        chkm3("m.trs.normalMatrix", qm.normalMatrix(), im.normalMatrix());
        chk3("m.map", qm.map(qv), im.map(iv));
        chk3("m.mapVector", qm.mapVector(qw), im.mapVector(iw));
        chk4("m.mapv4", qm.map(QVector4D(a, b, c, e)), im.map(iris::Vec4(a, b, c, e)));
        chk4("m.column", qm.column(2), im.column(2));
        chk4("m.row", qm.row(1), im.row(1));
        chk("m.determinant", float(qm.determinant()), float(im.determinant()));

        // rotate(angle, axis) — the axis-aligned fast paths and the general one
        {
            QMatrix4x4 q2; iris::Mat4 i2;
            q2.rotate(p1, 1, 0, 0); i2.rotate(p1, 1, 0, 0);
            q2.rotate(y1, 0, 1, 0); i2.rotate(y1, 0, 1, 0);
            q2.rotate(r1, 0, 0, 1); i2.rotate(r1, 0, 0, 1);
            q2.rotate(p1, qv);      i2.rotate(p1, iv);
            chkm("m.rotateAxis", q2, i2);
            chkm("m.rotateAxis.inv", q2.inverted(), i2.inverted());
        }

        // translation-only and scale-only flag paths
        {
            QMatrix4x4 q2; iris::Mat4 i2;
            q2.translate(qv); i2.translate(iv);
            chkm("m.tOnly", q2, i2);
            chkm("m.tOnly.inv", q2.inverted(), i2.inverted());
            q2.scale(qw); i2.scale(iw);
            chkm("m.ts", q2, i2);
            chkm("m.ts.inv", q2.inverted(), i2.inverted());
            chk3("m.ts.map", q2.map(qv), i2.map(iv));
            chk3("m.ts.mapVector", q2.mapVector(qv), i2.mapVector(iv));
            chkm3("m.ts.normalMatrix", q2.normalMatrix(), i2.normalMatrix());
        }

        // projection + view, the camera path
        {
            QMatrix4x4 qp, qvw; iris::Mat4 ip, ivw;
            qp.setToIdentity(); ip.setToIdentity();
            qp.perspective(45.0f + std::abs(a) * 5.0f, 1.7777f, 0.1f, 500.0f);
            ip.perspective(45.0f + std::abs(a) * 5.0f, 1.7777f, 0.1f, 500.0f);
            chkm("m.perspective", qp, ip);
            qvw.setToIdentity(); ivw.setToIdentity();
            qvw.lookAt(qv, qw, QVector3D(0, 1, 0));
            ivw.lookAt(iv, iw, iris::Vec3(0, 1, 0));
            chkm("m.lookAt", qvw, ivw);
            chkm("m.viewProj", qp * qvw, ip * ivw);
            chkm("m.viewProj.inv", (qp * qvw).inverted(), (ip * ivw).inverted());
            QMatrix4x4 qo; iris::Mat4 io;
            qo.setToIdentity(); io.setToIdentity();
            qo.ortho(-e, e, -f, f, -500.0f, 500.0f);
            io.ortho(-e, e, -f, f, -500.0f, 500.0f);
            chkm("m.ortho", qo, io);
            chkm("m.orthoInv", qo.inverted(), io.inverted());
            QMatrix4x4 qf; iris::Mat4 ifr;
            qf.setToIdentity(); ifr.setToIdentity();
            qf.frustum(-e, e, -f, f, 0.5f, 100.0f);
            ifr.frustum(-e, e, -f, f, 0.5f, 100.0f);
            chkm("m.frustum", qf, ifr);
        }

        // matrix products, chained
        {
            QMatrix4x4 qa2 = qm, qb2;
            iris::Mat4 ia2 = im, ib2;
            qb2.translate(qw); qb2.rotate(qq.normalized()); qb2.scale(e, f, g);
            ib2.translate(iw); ib2.rotate(iq.normalized()); ib2.scale(e, f, g);
            chkm("m.product", qa2 * qb2, ia2 * ib2);
            QMatrix4x4 qc2 = qa2; qc2 *= qb2;
            iris::Mat4 ic2 = ia2; ic2 *= ib2;
            chkm("m.productEq", qc2, ic2);
            chkm("m.product.inv", (qa2 * qb2).inverted(), (ia2 * ib2).inverted());
            chkm3("m.product.normalMatrix", (qa2 * qb2).normalMatrix(), (ia2 * ib2).normalMatrix());
        }

        // raw 16-float construction (mesh bake / bullet paths)
        {
            float vals[16];
            for (int n = 0; n < 16; ++n) vals[n] = d(rng);
            QMatrix4x4 qr(vals);
            iris::Mat4 ir(vals);
            chkm("m.fromFloats", qr, ir);
            chkm("m.fromFloats.T", qr.transposed(), ir.transposed());
            chkm("m.fromFloats.inv", qr.inverted(), ir.inverted());
            chkm3("m.fromFloats.normal", qr.normalMatrix(), ir.normalMatrix());
            chk4("m.fromFloats.mul", qr * QVector4D(a, b, c, e), ir * iris::Vec4(a, b, c, e));
            chk4("m.fromFloats.vmul", QVector4D(a, b, c, e) * qr, iris::Vec4(a, b, c, e) * ir);
            chk3("m.fromFloats.map", qr.map(qv), ir.map(iv));
            QMatrix4x4 qs(vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7],
                          vals[8], vals[9], vals[10], vals[11], vals[12], vals[13], vals[14], vals[15]);
            iris::Mat4 is(vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7],
                          vals[8], vals[9], vals[10], vals[11], vals[12], vals[13], vals[14], vals[15]);
            chkm("m.from16", qs, is);
            chkq("q.fromRotMat.general", QQuaternion::fromRotationMatrix(qr.normalMatrix()),
                 iris::Quat::fromRotationMatrix(ir.normalMatrix()));
        }


        // vector setters / accessors / the remaining operators
        {
            QVector3D q1(a, b, c); iris::Vec3 i1(a, b, c);
            q1.setX(e); i1.setX(e);
            q1.setY(f); i1.setY(f);
            q1.setZ(g); i1.setZ(g);
            chk3("v3.setters", q1, i1);
            chk3("v3.sub", q1 - qv, i1 - iv);
            chk3("v3.div", q1 / e, i1 / e);
            chk3("v3.divv", q1 / qw, i1 / iw);
            chk3("v3.negate", -q1, -i1);
            chk("v3.eq", float(q1 == q1), float(i1 == i1));
            chk("v3.isNull", float(q1.isNull()), float(i1.isNull()));
            chk("v3.distanceToPlane", qv.distanceToPlane(qw, q1.normalized()),
                iv.distanceToPlane(iw, i1.normalized()));
            chk("v3.distanceToPlane3", qv.distanceToPlane(qw, q1, QVector3D(e, f, g)),
                iv.distanceToPlane(iw, i1, iris::Vec3(e, f, g)));
            chk("v3.distanceToLine", qv.distanceToLine(qw, q1.normalized()),
                iv.distanceToLine(iw, i1.normalized()));
            chk3("v3.normal3", QVector3D::normal(qv, qw, q1), iris::Vec3::normal(iv, iw, i1));
            QVector3D acc = qv; acc += qw; acc -= q1; acc *= e; acc /= f;
            iris::Vec3 iacc = iv; iacc += iw; iacc -= i1; iacc *= e; iacc /= f;
            chk3("v3.compound", acc, iacc);

            QVector4D q4(a, b, c, e); iris::Vec4 i4(a, b, c, e);
            q4.setW(g); i4.setW(g);
            chk4("v4.setW", q4, i4);
            chk4("v4.sub", q4 - QVector4D(e, f, g, h), i4 - iris::Vec4(e, f, g, h));
            chk("v4.dot", QVector4D::dotProduct(q4, QVector4D(e, f, g, h)),
                iris::Vec4::dotProduct(i4, iris::Vec4(e, f, g, h)));
            chk3("v4.toVector3DAffine", q4.toVector3DAffine(), i4.toVector3DAffine());
            chk("v4.lengthSquared", q4.lengthSquared(), i4.lengthSquared());

            QVector2D q2(a, b); iris::Vec2 i2(a, b);
            chk("v2.dot", QVector2D::dotProduct(q2, QVector2D(e, f)),
                iris::Vec2::dotProduct(i2, iris::Vec2(e, f)));
            chk("v2.lengthSquared", q2.lengthSquared(), i2.lengthSquared());
            chk("v2.distanceToPoint", q2.distanceToPoint(QVector2D(e, f)),
                i2.distanceToPoint(iris::Vec2(e, f)));
            chk3("v2.toVector3D", q2.toVector3D(), i2.toVector3D());
        }

        // quaternion compound assignment and conversions
        {
            QQuaternion q1(a, b, c, e); iris::Quat i1(a, b, c, e);
            q1 += qq2; i1 += iq2;
            q1 -= QQuaternion(e, f, g, h); i1 -= iris::Quat(e, f, g, h);
            q1 *= f; i1 *= f;
            q1 /= g; i1 /= g;
            chkq("q.compound", q1, i1);
            QQuaternion q3 = qq; q3 *= qq2;
            iris::Quat i3 = iq; i3 *= iq2;
            chkq("q.mulEq", q3, i3);
            chk4("q.toVector4D", qq.toVector4D(), iq.toVector4D());
            chkq("q.fromVector4D", QQuaternion(QVector4D(a, b, c, e)),
                 iris::Quat(iris::Vec4(a, b, c, e)));
            QQuaternion q5 = qq; q5.setVector(QVector3D(e, f, g));
            iris::Quat i5 = iq; i5.setVector(iris::Vec3(e, f, g));
            chkq("q.setVector", q5, i5);
            chk("q.isNull", float(qq.isNull()), float(iq.isNull()));
            chk("q.isIdentity", float(qq.isIdentity()), float(iq.isIdentity()));
            chk3("q.vecMul", qq.normalized() * qv, iq.normalized() * iv);
        }

        // matrix element access, the scalar operators, and the odd builders
        {
            QMatrix4x4 q1 = qm; iris::Mat4 i1 = im;
            q1.setColumn(1, QVector4D(a, b, c, e));
            i1.setColumn(1, iris::Vec4(a, b, c, e));
            q1.setRow(2, QVector4D(e, f, g, h));
            i1.setRow(2, iris::Vec4(e, f, g, h));
            chkm("m.setColumnRow", q1, i1);
            chk("m.isAffine", float(q1.isAffine()), float(i1.isAffine()));
            chk("m.isIdentity", float(q1.isIdentity()), float(i1.isIdentity()));
            chkm("m.scalarMul", q1 * e, i1 * e);
            chkm("m.scalarMulL", e * q1, e * i1);
            chkm("m.scalarDiv", q1 / e, i1 / e);
            chkm("m.unaryMinus", -q1, -i1);
            chkm("m.plus", q1 + qm, i1 + im);
            chkm("m.minus", q1 - qm, i1 - im);
            QMatrix4x4 q2 = q1; q2 *= e; q2 /= f; q2 += qm; q2 -= qm;
            iris::Mat4 i2 = i1; i2 *= e; i2 /= f; i2 += im; i2 -= im;
            chkm("m.scalarCompound", q2, i2);
            float qd[16], id[16];
            q1.copyDataTo(qd); i1.copyDataTo(id);
            for (int n = 0; n < 16; ++n) chk("m.copyDataTo", qd[n], id[n]);
            for (int n = 0; n < 16; ++n) chk("m.constData", q1.constData()[n], i1.constData()[n]);
            QMatrix4x4 q3; iris::Mat4 i3;
            q3.fill(e); i3.fill(e);
            chkm("m.fill", q3, i3);
            QMatrix4x4 q4; iris::Mat4 i4;
            q4(1, 2) = e; i4(1, 2) = e;
            chk("m.elemWrite", q4(1, 2), i4(1, 2));
        }

        // scale overloads and the Rotation2D flag path (rotate about Z only)
        {
            QMatrix4x4 q1; iris::Mat4 i1;
            const float zAngle = ang(rng);
            q1.translate(a, b); i1.translate(a, b);
            q1.rotate(zAngle, 0, 0, 1); i1.rotate(zAngle, 0, 0, 1);
            chk("m.r2d.flags", float(q1.flags().toInt()), float(i1.flags()));
            q1.translate(qv); i1.translate(iv);
            chkm("m.r2d.translate", q1, i1);
            q1.scale(e, f); i1.scale(e, f);
            chkm("m.r2d.scale2", q1, i1);
            q1.scale(g); i1.scale(g);
            chkm("m.r2d.scale1", q1, i1);
            chkm("m.r2d.inverted", q1.inverted(), i1.inverted());
            chk3("m.r2d.map", q1.map(qv), i1.map(iv));
            chk3("m.r2d.mapVector", q1.mapVector(qv), i1.mapVector(iv));

            QMatrix4x4 q2; iris::Mat4 i2;
            q2.scale(e); i2.scale(e);
            chkm("m.scaleOnly", q2, i2);
            q2.translate(qv); i2.translate(iv);
            chkm("m.scaleThenTranslate", q2, i2);
            chkm("m.scaleThenTranslate.inv", q2.inverted(), i2.inverted());

            QMatrix4x4 q3; iris::Mat4 i3;
            q3.scale(a, b); i3.scale(a, b);
            chkm("m.scale2Only", q3, i3);

            QMatrix4x4 q4; iris::Mat4 i4;
            q4.viewport(a, b, std::abs(c) + 1.0f, std::abs(e) + 1.0f);
            i4.viewport(a, b, std::abs(c) + 1.0f, std::abs(e) + 1.0f);
            chkm("m.viewport", q4, i4);
            QMatrix4x4 q5 = qm; iris::Mat4 i5 = im;
            q5.flipCoordinates(); i5.flipCoordinates();
            chkm("m.flipCoordinates", q5, i5);
            QMatrix4x4 q6; iris::Mat4 i6;
            q6.scale(e, f, g); q6.flipCoordinates();
            i6.scale(e, f, g); i6.flipCoordinates();
            chkm("m.flipCoordinates.ts", q6, i6);
        }

        // Mat3 element layout — the mirror builds one from a raw float[9] every
        // frame, for every node, and hands the resulting quaternion to the engine.
        {
            float v9[9];
            for (int n = 0; n < 9; ++n) v9[n] = d(rng);
            QMatrix3x3 q3(v9);
            iris::Mat3 i3(v9);
            chkm3("m3.fromFloats", q3, i3);
            for (int n = 0; n < 9; ++n) chk("m3.constData", q3.constData()[n], i3.constData()[n]);
            chkq("q.fromRotationMatrix.raw", QQuaternion::fromRotationMatrix(q3),
                 iris::Quat::fromRotationMatrix(i3));
        }

        // qtinterop: the boundary every UI, QVariant and serializer site now
        // crosses. It has to be an exact identity in both directions.
        {
            chk3("interop.v3", qv, iris::fromQt(iris::toQt(iv)));
            chk3("interop.v3.qt", iris::toQt(iv), iv);
            chk4("interop.v4", qv4, iris::fromQt(iris::toQt(iv4)));
            chk4("interop.v4.qt", iris::toQt(iv4), iv4);
            chk("interop.v2.x", iris::toQt(iv2).x(), qv2.x());
            chk("interop.v2.y", iris::toQt(iv2).y(), qv2.y());
            chkq("interop.quat", qq, iris::fromQt(iris::toQt(iq)));
            chkq("interop.quat.qt", iris::toQt(iq), iq);
            chkm("interop.mat4", qm, iris::fromQt(iris::toQt(im)));
            chkm("interop.mat4.qt", iris::toQt(im), im);
            chkm3("interop.mat3", iris::toQt(ie.toRotationMatrix()), ie.toRotationMatrix());
            chkm3("interop.mat3.back", qe.toRotationMatrix(),
                  iris::fromQt(qe.toRotationMatrix()));
        }

        // ---------------------------------------------------------------------
        // THE FLAG SWEEP. QMatrix4x4 chooses its arithmetic by what it knows
        // about itself, and a fast path that only triggers for one flag
        // combination is invisible to any test that never builds a matrix with
        // those flags. This one builds every reachable combination and runs the
        // whole derived-matrix surface over each.
        //
        // It exists because normalMatrix()'s ORTHONORMAL path was missed the
        // first time: every earlier check happened to use a matrix that also
        // carried Scale, so the copy-the-basis path never ran, the general
        // solve stood in for it, and the two disagreed by one ulp — which
        // reached the screen as three changed pixels through
        // CameraNode::lookAt. Whole-surface-per-flag-class is the fix.
        // ---------------------------------------------------------------------
        {
            const iris::Vec3 tv(a, b, c), sv(e != 0.f ? e : 1.f, f != 0.f ? f : 1.f, g != 0.f ? g : 1.f);
            const QVector3D qtv(a, b, c), qsv(e != 0.f ? e : 1.f, f != 0.f ? f : 1.f, g != 0.f ? g : 1.f);
            const float az = ang(rng), ay2 = ang(rng);

            for (int variant = 0; variant < 16; ++variant) {
                QMatrix4x4 q; iris::Mat4 i;
                switch (variant) {
                case 0:  break;                                                   // Identity
                case 1:  q.translate(qtv); i.translate(tv); break;                // T
                case 2:  q.scale(qsv); i.scale(sv); break;                        // S
                case 3:  q.translate(qtv); q.scale(qsv);
                         i.translate(tv);  i.scale(sv); break;                    // T|S
                case 4:  q.rotate(az, 0, 0, 1); i.rotate(az, 0, 0, 1); break;     // R2D
                case 5:  q.translate(qtv); q.rotate(az, 0, 0, 1);
                         i.translate(tv);  i.rotate(az, 0, 0, 1); break;          // T|R2D
                case 6:  q.scale(qsv); q.rotate(az, 0, 0, 1);
                         i.scale(sv);  i.rotate(az, 0, 0, 1); break;              // S|R2D
                case 7:  q.rotate(ay2, 0, 1, 0); i.rotate(ay2, 0, 1, 0); break;   // R
                case 8:  q.translate(qtv); q.rotate(ay2, qtv.normalized());
                         i.translate(tv);  i.rotate(ay2, tv.normalized()); break; // T|R
                case 9:  q.translate(qtv); q.rotate(ay2, qtv.normalized()); q.scale(qsv);
                         i.translate(tv);  i.rotate(ay2, tv.normalized());  i.scale(sv); break; // T|S|R
                case 10: q.perspective(60.f, 1.5f, 0.1f, 500.f);
                         i.perspective(60.f, 1.5f, 0.1f, 500.f); break;           // P
                case 11: q(1, 2) = a; i(1, 2) = a; break;                         // General
                case 12: q.ortho(-2, 2, -1, 1, -50, 50); i.ortho(-2, 2, -1, 1, -50, 50); break;
                case 13: q.frustum(-1, 1, -1, 1, 0.5f, 90.f);
                         i.frustum(-1, 1, -1, 1, 0.5f, 90.f); break;
                case 14: q.lookAt(qtv, qsv, QVector3D(0, 1, 0));
                         i.lookAt(tv, sv, iris::Vec3(0, 1, 0)); break;
                case 15: q.viewport(0, 0, 800, 600); i.viewport(0, 0, 800, 600); break;
                }
                char tag[48];
                std::snprintf(tag, sizeof(tag), "flag%d", variant);
                std::string T(tag);
                chk((T + ".flags").c_str(), float(q.flags().toInt()), float(i.flags()));
                chkm((T + ".m").c_str(), q, i);
                chkm((T + ".inverted").c_str(), q.inverted(), i.inverted());
                chkm((T + ".transposed").c_str(), q.transposed(), i.transposed());
                chkm3((T + ".normalMatrix").c_str(), q.normalMatrix(), i.normalMatrix());
                chk3((T + ".map").c_str(), q.map(qv), i.map(iv));
                chk3((T + ".mapVector").c_str(), q.mapVector(qv), i.mapVector(iv));
                chk4((T + ".mapv4").c_str(), q.map(QVector4D(a, b, c, e)), i.map(iris::Vec4(a, b, c, e)));
                chk((T + ".determinant").c_str(), float(q.determinant()), float(i.determinant()));
                chk((T + ".isAffine").c_str(), float(q.isAffine()), float(i.isAffine()));
                chk((T + ".isIdentity").c_str(), float(q.isIdentity()), float(i.isIdentity()));
                chkq((T + ".fromRotationMatrix").c_str(),
                     QQuaternion::fromRotationMatrix(q.normalMatrix()),
                     iris::Quat::fromRotationMatrix(i.normalMatrix()));
                // and the full CameraNode::lookAt chain, which is what found
                // the missing path: lookAt -> inverted -> normalMatrix -> quat.
                {
                    QMatrix4x4 ql = q; iris::Mat4 il = i;
                    ql.lookAt(qv, qw, QVector3D(0, 1, 0));
                    il.lookAt(iv, iw, iris::Vec3(0, 1, 0));
                    chkm((T + ".lookAt").c_str(), ql, il);
                    const QMatrix4x4 qli = ql.inverted();
                    const iris::Mat4 ili = il.inverted();
                    chkm((T + ".lookAt.inv").c_str(), qli, ili);
                    chkm3((T + ".lookAt.inv.normal").c_str(), qli.normalMatrix(), ili.normalMatrix());
                    chkq((T + ".lookAt.decompose").c_str(),
                         QQuaternion::fromRotationMatrix(qli.normalMatrix()),
                         iris::Quat::fromRotationMatrix(ili.normalMatrix()));
                    chk4((T + ".lookAt.col3").c_str(), qli.column(3), ili.column(3));
                }
                // and every flag class multiplied by every other
                for (int other = 0; other < 12; ++other) {
                    QMatrix4x4 q2; iris::Mat4 i2;
                    switch (other) {
                    case 1:  q2.translate(qsv); i2.translate(sv); break;
                    case 2:  q2.scale(qtv); i2.scale(tv); break;
                    case 4:  q2.rotate(ay2, 0, 0, 1); i2.rotate(ay2, 0, 0, 1); break;
                    case 7:  q2.rotate(az, 1, 0, 0); i2.rotate(az, 1, 0, 0); break;
                    case 10: q2.perspective(35.f, 1.f, 1.f, 90.f);
                             i2.perspective(35.f, 1.f, 1.f, 90.f); break;
                    default: continue;
                    }
                    chkm((T + ".prod").c_str(), q * q2, i * i2);
                    chkm((T + ".prod.inv").c_str(), (q * q2).inverted(), (i * i2).inverted());
                    chkm3((T + ".prod.normal").c_str(), (q * q2).normalMatrix(), (i * i2).normalMatrix());
                }
            }
        }
        // degenerate / near-zero inputs
        {
            QVector3D qz(sml(rng), sml(rng), sml(rng));
            iris::Vec3 iz(qz.x(), qz.y(), qz.z());
            chk3("v3.tiny.normalized", qz.normalized(), iz.normalized());
            chk("v3.tiny.length", qz.length(), iz.length());
            QQuaternion qzq(sml(rng), sml(rng), sml(rng), sml(rng));
            iris::Quat izq(qzq.scalar(), qzq.x(), qzq.y(), qzq.z());
            chkq("q.tiny.normalized", qzq.normalized(), izq.normalized());
            chkq("q.tiny.inverted", qzq.inverted(), izq.inverted());
            chk3("q.tiny.euler", qzq.toEulerAngles(), izq.toEulerAngles());
        }

        // gimbal lock: pitch at +/-90
        {
            for (float pl : { 90.0f, -90.0f, 89.9999f, -89.9999f, 0.0f, 180.0f }) {
                QQuaternion qg = QQuaternion::fromEulerAngles(pl, y1, r1);
                iris::Quat ig = iris::Quat::fromEulerAngles(pl, y1, r1);
                chkq("q.gimbal", qg, ig);
                chk3("q.gimbal.euler", qg.toEulerAngles(), ig.toEulerAngles());
            }
        }
    }

    // exhaustive euler sweep on a lattice — the persisted path
    for (int p = -180; p <= 180; p += 5) {
        for (int y = -180; y <= 180; y += 5) {
            for (int r = -180; r <= 180; r += 45) {
                QQuaternion q = QQuaternion::fromEulerAngles(float(p), float(y), float(r));
                iris::Quat i = iris::Quat::fromEulerAngles(float(p), float(y), float(r));
                chkq("sweep.fromEuler", q, i);
                chk3("sweep.toEuler", q.toEulerAngles(), i.toEulerAngles());
                QMatrix4x4 qm; iris::Mat4 im;
                qm.rotate(q); im.rotate(i);
                chkm("sweep.rotate", qm, im);
            }
        }
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
