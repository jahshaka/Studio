// atom-occlusion — a front wall with 20 buildings behind it (LAB-1 stage 2; the occlusion test's law made clean).
//
// THE GEOMETRY. A wall 30 m wide (x in [-15, 15]), 8 m tall, 0.5 m thick, its front at z = 0, with a full-height GAP
// 3 m wide at x in [4, 7] (two boxes, one mesh). Behind it 20 buildings (closed boxes: 3-6 m wide and deep, 4-16 m tall,
// at x in [-14, 14], their fronts 2-60 m behind the wall — the seed places them). A ground quad. The dolly faces the
// wall square on, the eye 1.7 m up at (0, 1.7, d), looking straight down -Z (no pitch: the wall's face projects to two
// axis-aligned screen rectangles, the covered region C).
//
// THE EXPECTATION — THE HIDDEN SHARE. A building whose screen rectangle B lies inside C is hidden (the roofline is C's
// top edge, the gap and the wall's ends its sides); one that reaches over the roofline, into the gap or past an end is
// visible (`visibleObjects`). The depth test is CONSERVATIVE by one pyramid texel: an instance is tested at the mip
// where its rectangle spans at most F texels (texel t = 2^ceil(log2(max(w, h) / F)) pixels) and the rectangle is read
// rounded OUT to that mip's texels, so a hidden building whose rounded rectangle still reaches past C's edge reads the
// sky's (or the farther building's) depth there and is DRAWN though hidden — the "hidden drawn" count, predicted per
// footprint F = 4 and F = 8 texels (`hiddenDrawn@4`, `hiddenDrawn@8`): only the buildings within one texel of the
// roofline, the gap's reveal or an end. Measured: a building the cut drew with no visible pixel (the cut probe).
// WHAT IT DOES NOT MODEL: the previous frame's pyramid (the dolly settles first), the late list's re-test, the boxes'
// near-plane cases, buildings occluding buildings (counted visible or hidden against the wall alone).
#include "envbuild.h"

#include <cmath>
#include <cstdio>

namespace lab {
namespace envs {

namespace {

constexpr float kHalfWidth = 15.0f, kWallH = 8.0f, kGap0 = 4.0f, kGap1 = 7.0f;

struct Rect { float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f; bool empty() const { return x1 <= x0 || y1 <= y0; } };

Built build(unsigned seed, bool bake)
{
    Builder b("atom-occlusion", seed, bake);
    Lcg r(seed);
    Arrays wall;
    wall.box(iris::Vec3((-kHalfWidth + kGap0) * 0.5f, kWallH * 0.5f, -0.25f), iris::Vec3((kGap0 + kHalfWidth) * 0.5f, kWallH * 0.5f, 0.25f));
    wall.box(iris::Vec3((kGap1 + kHalfWidth) * 0.5f, kWallH * 0.5f, -0.25f), iris::Vec3((kHalfWidth - kGap1) * 0.5f, kWallH * 0.5f, 0.25f));
    const int w = b.instance(b.mesh(wall), QStringLiteral("wall"), QStringLiteral("wall"), iris::Vec3(0, 0, 0), 0.0f,
                             iris::Vec3(1, 1, 1), b.material(0.7f, 0.68f, 0.64f));
    Feature wf;
    wf.kind = Feature::Fixed;
    wf.object = w;
    wf.centre = iris::Vec3(0, 4, 0);
    wf.radius = 16.0f;
    wf.triangles = 24;
    b.feature(wf);
    Arrays unit;
    unit.box(iris::Vec3(0, 0.5f, 0), iris::Vec3(0.5f, 0.5f, 0.5f));
    const iris::MeshPtr box = b.mesh(unit);
    const auto brick = b.material(0.55f, 0.35f, 0.28f);
    for (int i = 0; i < 20; ++i) {
        const float wd = r.range(3, 6), dp = r.range(3, 6), h = r.range(4, 16);
        const float x = r.range(-14.0f + wd * 0.5f, 14.0f - wd * 0.5f), behind = r.range(2, 60);
        const iris::Vec3 pos(x, 0, -0.5f - behind - dp * 0.5f);
        const int o = b.instance(box, QStringLiteral("building-%1").arg(i), QStringLiteral("building"), pos, 0.0f,
                                 iris::Vec3(wd, h, dp), brick);
        Feature f;
        f.kind = Feature::Fixed;
        f.object = o;
        f.centre = pos + iris::Vec3(0, h * 0.5f, 0);
        f.radius = 0.5f * iris::Vec3(wd, h, dp).length();
        f.triangles = 12;
        b.feature(f);
    }
    Arrays g;
    g.quad(iris::Vec3(-200, 0, 400), iris::Vec3(200, 0, 400), iris::Vec3(200, 0, -100), iris::Vec3(-200, 0, -100),
           iris::Vec3(0, 1, 0));
    const int go = b.instance(b.mesh(g), QStringLiteral("ground"), QStringLiteral("ground"), iris::Vec3(0, 0, 0), 0.0f,
                              iris::Vec3(1, 1, 1), b.material(0.3f, 0.3f, 0.3f), false);
    Feature gf;
    gf.kind = Feature::Fixed;
    gf.object = go;
    gf.centre = iris::Vec3(0, 0, 150);
    gf.radius = 320.0f;
    gf.triangles = 2;
    b.feature(gf);
    return b.finish();
}

void stop(double d, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 1.7f, float(d));
    target = iris::Vec3(0, 1.7f, float(d) - 1.0f);
}

/// the screen rectangle (pixels, y down) of the world box [mn, mx] seen from `o` looking down -Z; empty off screen
Rect project(const Optics &o, const iris::Vec3 &mn, const iris::Vec3 &mx)
{
    const float k = float(o.pixelScale()), H = o.heightPixels, W = H * o.aspect;
    Rect r{ 1e30f, 1e30f, -1e30f, -1e30f };
    for (int c = 0; c < 8; ++c) {
        const iris::Vec3 p(c & 1 ? mx.x() : mn.x(), c & 2 ? mx.y() : mn.y(), c & 4 ? mx.z() : mn.z());
        const float z = o.eye.z() - p.z();
        if (z < 0.1f) return Rect{ 0, 0, 0, 0 };   // reaches behind the eye: not this law's case
        const float x = W * 0.5f + k * (p.x() - o.eye.x()) / z, y = H * 0.5f - k * (p.y() - o.eye.y()) / z;
        r.x0 = std::min(r.x0, x); r.x1 = std::max(r.x1, x);
        r.y0 = std::min(r.y0, y); r.y1 = std::max(r.y1, y);
    }
    r.x0 = std::max(r.x0, 0.0f); r.y0 = std::max(r.y0, 0.0f);
    r.x1 = std::min(r.x1, W); r.y1 = std::min(r.y1, H);
    return r;
}

bool inside(const Rect &b, const Rect &c) { return b.x0 >= c.x0 && b.x1 <= c.x1 && b.y0 >= c.y0 && b.y1 <= c.y1; }

std::vector<Prediction> expect(const Built &b, const Optics &o)
{
    const Rect left = project(o, iris::Vec3(-kHalfWidth, 0, 0), iris::Vec3(kGap0, kWallH, 0));
    const Rect right = project(o, iris::Vec3(kGap1, 0, 0), iris::Vec3(kHalfWidth, kWallH, 0));
    int visible = 0, kept4 = 0, kept8 = 0, hidden = 0;
    for (const Object &obj : b.objects) {
        if (obj.role != "building") continue;
        const Rect r = project(o, obj.boxMin, obj.boxMax);
        if (r.empty()) continue;
        const bool covered = inside(r, left) || inside(r, right);
        if (!covered) { ++visible; continue; }
        ++hidden;
        for (int F : { 4, 8 }) {
            const float side = std::max(r.x1 - r.x0, r.y1 - r.y0);
            const float t = std::pow(2.0f, std::max(0.0f, std::ceil(std::log2(std::max(1.0f, side / float(F))))));
            const Rect g{ std::floor(r.x0 / t) * t, std::floor(r.y0 / t) * t, std::ceil(r.x1 / t) * t, std::ceil(r.y1 / t) * t };
            if (!(inside(g, left) || inside(g, right))) (F == 4 ? kept4 : kept8)++;
        }
    }
    char note[160];
    std::snprintf(note, sizeof note, "%d of the buildings in view hidden behind the wall", hidden);
    return { { "visibleObjects", double(visible), "buildings reaching over the roofline, into the gap or past an end" },
             { "hiddenDrawn@4", double(kept4), note },
             { "hiddenDrawn@8", double(kept8), note },
             { "triangles", predictedTriangles(b, o), "every object in the frustum (no occlusion in this sum)" } };
}

std::string describe()
{
    return "atom-occlusion: a 30 x 8 m wall with a 3 m gap and 20 buildings behind it at 2-60 m. Predicts THE HIDDEN "
           "SHARE: the buildings visible over the roofline, through the gap or past an end, and the HIDDEN buildings "
           "the conservative depth test still draws — those whose rectangle, rounded out to the pyramid texel at which "
           "it spans 4 (or 8) texels, still reaches past the wall's edge: hiddenDrawn@4 / @8 per stop.";
}

}   // namespace

Env atomOcclusion() { return Env{ "atom-occlusion", describe, build, stop, expect, 900.0f, true }; }

}   // namespace envs
}   // namespace lab
