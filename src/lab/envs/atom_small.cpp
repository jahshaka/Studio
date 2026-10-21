// atom-small — 2,000 small props scattered over 100 m (LAB-1 stage 2; the empty-top law made clean).
//
// THE GEOMETRY. Three prop meshes under 256 triangles, each authored 1 m tall: a LAMP (a 12-sided post, its caps and a
// head box: 60 triangles), a BOLT (a hex prism: 24) and a SIGN (a post and a panel: 24). 2,000 instances on the ground
// over x, z in [-50, 50], each a kind, a turn and a SIZE (its height) drawn log-uniform in [0.05, 1] m by the seed.
// A 120 m ground quad.
//
// THE EXPECTATION — THE EMPTY-TOP LAW. A whole object has nothing beside it to remain: its DAG's top is EMPTY (ATOM-6's
// prune), reached at the error of the whole object, its bounding-box diagonal e = size x diag(unit box). So a prop
// stops drawing past D = e x 1303.7 / tolerance and draws (some level of) itself before: `objects` = the props in view
// with e x 1303.7 / D >= tolerance, `triangles` = the same props' full counts (an upper bound: a prop above the pixel
// may already draw a coarser level). The dolly: eye (0, 6, 50 + d) looking at the field's centre.
// WHAT IT DOES NOT MODEL: a prop's intermediate levels (the bound above), the ground's occlusion of the props' feet.
#include "envbuild.h"

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/document/assets/mesh.h"

#include <cmath>
#include <cstdio>

namespace lab {
namespace envs {

namespace {

constexpr int kProps = 2000;

void lamp(Arrays &a)
{
    const int n = 12;
    const float r = 0.05f, h = 0.8f;
    for (int i = 0; i < n; ++i) {
        const float t0 = 2.0f * float(M_PI) * float(i) / n, t1 = 2.0f * float(M_PI) * float(i + 1) / n, tm = 0.5f * (t0 + t1);
        const iris::Vec3 p0(std::cos(t0) * r, 0, std::sin(t0) * r), p1(std::cos(t1) * r, 0, std::sin(t1) * r);
        const iris::Vec3 up(0, h, 0);
        a.quad(p0, p1, p1 + up, p0 + up, iris::Vec3(std::cos(tm), 0, std::sin(tm)));   // the side (2 triangles)
        // the caps (a fan as quads to the axis: 2 triangles each, one degenerate-free)
        a.quad(iris::Vec3(0, h, 0), p0 + up, p1 + up, iris::Vec3(0, h, 0) + (p0 + p1) * 0.25f, iris::Vec3(0, 1, 0));
        a.quad(iris::Vec3(0, 0, 0), p1, p0, (p0 + p1) * 0.25f, iris::Vec3(0, -1, 0));
    }
    a.box(iris::Vec3(0, 0.9f, 0), iris::Vec3(0.15f, 0.1f, 0.15f));
}

void bolt(Arrays &a)
{
    const int n = 6;
    const float r = 0.5f, h = 1.0f;
    for (int i = 0; i < n; ++i) {
        const float t0 = 2.0f * float(M_PI) * float(i) / n, t1 = 2.0f * float(M_PI) * float(i + 1) / n, tm = 0.5f * (t0 + t1);
        const iris::Vec3 p0(std::cos(t0) * r, 0, std::sin(t0) * r), p1(std::cos(t1) * r, 0, std::sin(t1) * r);
        const iris::Vec3 up(0, h, 0);
        a.quad(p0, p1, p1 + up, p0 + up, iris::Vec3(std::cos(tm), 0, std::sin(tm)));
        a.quad(up, p0 + up, p1 + up, up + (p0 + p1) * 0.25f, iris::Vec3(0, 1, 0));
    }
}

void sign(Arrays &a)
{
    a.box(iris::Vec3(0, 0.35f, 0), iris::Vec3(0.03f, 0.35f, 0.03f));
    a.box(iris::Vec3(0, 0.8f, 0.04f), iris::Vec3(0.35f, 0.2f, 0.01f));
}

Built build(unsigned seed, bool bake)
{
    Builder b("atom-small", seed, bake);
    Lcg r(seed);
    Arrays kinds[3];
    lamp(kinds[0]); bolt(kinds[1]); sign(kinds[2]);
    iris::MeshPtr meshes[3];
    float diag[3];
    unsigned tris[3];
    for (int k = 0; k < 3; ++k) {
        meshes[k] = b.mesh(kinds[k]);
        const iris::AABB bb = meshes[k]->getAABB();
        diag[k] = (bb.getMax() - bb.getMin()).length();
        tris[k] = kinds[k].triangles();
    }
    const iris::PbrMaterialPtr mats[3] = { b.material(0.2f, 0.2f, 0.22f), b.material(0.6f, 0.6f, 0.62f, 0.5f),
                                           b.material(0.7f, 0.15f, 0.1f) };
    for (int i = 0; i < kProps; ++i) {
        const int k = r.irange(0, 2);
        const float size = 0.05f * std::pow(20.0f, r.next());   // log-uniform in [0.05, 1] m
        const iris::Vec3 pos(r.range(-50, 50), 0, r.range(-50, 50));
        const float yaw = r.range(0, 360);
        const int o = b.instance(meshes[k], QStringLiteral("prop-%1").arg(i), QStringLiteral("prop"), pos, yaw,
                                 iris::Vec3(size, size, size), mats[k]);
        Feature f;
        f.kind = Feature::Shell;
        f.object = o;
        f.centre = pos + iris::Vec3(0, size * 0.5f, 0);
        f.radius = 0.5f * diag[k] * size;
        f.error = diag[k] * size;
        f.triangles = tris[k];
        b.feature(f);
    }
    Arrays g;
    g.quad(iris::Vec3(-60, 0, 60), iris::Vec3(60, 0, 60), iris::Vec3(60, 0, -60), iris::Vec3(-60, 0, -60), iris::Vec3(0, 1, 0));
    const int go = b.instance(b.mesh(g), QStringLiteral("ground"), QStringLiteral("ground"), iris::Vec3(0, 0, 0), 0.0f,
                              iris::Vec3(1, 1, 1), b.material(0.4f, 0.38f, 0.34f), false);
    Feature gf;
    gf.kind = Feature::Fixed;
    gf.object = go;
    gf.centre = iris::Vec3(0, 0, 0);
    gf.radius = 85.0f;
    gf.triangles = 2;
    b.feature(gf);
    return b.finish();
}

void stop(double d, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 6, 50.0f + float(d));
    target = iris::Vec3(0, 0, 0);
}

std::vector<Prediction> expect(const Built &b, const Optics &o)
{
    double objects = 0;
    for (const Feature &f : b.features) {
        if (f.kind != Feature::Shell || !o.inFrustum(f.centre, f.radius)) continue;
        const double d = std::max(0.1, double((f.centre - o.eye).length() - f.radius));
        if (double(f.error) >= o.allowed(d)) objects += 1;
    }
    return { { "objects", objects, "props in view whose bounding-box diagonal projects to >= tolerance px" },
             { "triangles", predictedTriangles(b, o), "those props' full counts + the ground (an upper bound)" } };
}

std::string describe()
{
    return "atom-small: 2,000 props under 256 triangles (lamps, bolts, signs) of 0.05-1 m over 100 m. Predicts THE "
           "EMPTY-TOP LAW: a whole object's DAG top is empty at its bounding-box diagonal e, so a prop stops drawing "
           "past D = e x 1303.7 / tolerance — the drawn-object count per stop in closed form.";
}

}   // namespace

Env atomSmall() { return Env{ "atom-small", describe, build, stop, expect, 900.0f, true }; }

}   // namespace envs
}   // namespace lab
