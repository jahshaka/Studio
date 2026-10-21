// atom-occlusion — a front wall with 20 buildings behind it (LAB-1 stage 2; the occlusion test's law made clean).
//
// THE GEOMETRY. A wall 30 m wide (x in [-15, 15]), 8 m tall, 0.5 m thick, its front at z = 0, with a full-height GAP
// 3 m wide at x in [4, 7] (two boxes, one mesh). Behind it 20 buildings (closed boxes: 3-6 m wide and deep, 4-16 m tall,
// at x in [-14, 14], their fronts 2-60 m behind the wall — the seed places them). A ground quad. The dolly faces the
// wall square on, the eye 1.7 m up at (0, 1.7, d), looking straight down -Z (no pitch: the wall's face projects to two
// axis-aligned screen rectangles, the covered region C).
//
// THE EXPECTATION — THE HIDDEN SHARE, with MUTUAL HIDING (the stage-2 read). With no pitch every box's screen
// rectangle is axis-aligned and every front face is at one depth, so the truth is exact: a building is HIDDEN iff its
// rectangle lies inside the UNION of the occluders' rectangles — the wall's two parts, the ground in front of the wall
// (the rows below the wall's foot) and every building wholly NEARER
// (its far face in front of this one's near face; the generator forbids overlapping volumes) — and VISIBLE otherwise
// (`visibleObjects`). THE DEPTH TEST IS CONSERVATIVE BY ONE PYRAMID TEXEL: the instance is tested at the mip where its
// rectangle is at most F texels per axis (F = the footprint: 2 at this base, JahCullTest_cs.glsl:325-336, `--footprint
// N`), every texel it touches read — the rectangle rounded OUT to that mip's texels; a hidden building whose rounded
// rectangle still reaches past the occluders' union reads a farther depth there and is DRAWN though hidden
// (`hiddenDrawn`). Measured: a building the cut drew with no visible pixel (the cut probe).
// WHAT IT DOES NOT MODEL: the previous frame's pyramid (the dolly settles first), the late list's re-test, the boxes'
// near-plane cases, the occluders' side faces (a nearer box's side face covers more than its front rectangle: the
// prediction is the conservative side).
#include "envbuild.h"

#include <algorithm>
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
    // NO TWO VOLUMES OVERLAP (with 0.5 m between footprints), so "wholly nearer" is a total order on any two that
    // share screen space: a placement that overlaps an earlier one is drawn again (the same LCG: deterministic)
    std::vector<std::pair<iris::Vec3, iris::Vec3>> placed;   // footprint min / max (x, -, z)
    for (int i = 0; i < 20; ++i) {
        float wd = 0, dp = 0, h = 0, x = 0, behind = 0;
        for (int attempt = 0; attempt < 500; ++attempt) {
            wd = r.range(3, 6); dp = r.range(3, 6); h = r.range(4, 16);
            x = r.range(-14.0f + wd * 0.5f, 14.0f - wd * 0.5f); behind = r.range(2, 60);
            const float z0 = -0.5f - behind - dp, z1 = -0.5f - behind;
            bool clash = false;
            for (const auto &q : placed)
                clash = clash || (x - wd * 0.5f < q.second.x() + 0.5f && x + wd * 0.5f > q.first.x() - 0.5f &&
                                  z0 < q.second.z() + 0.5f && z1 > q.first.z() - 0.5f);
            if (!clash) break;
        }
        placed.push_back({ iris::Vec3(x - wd * 0.5f, 0, -0.5f - behind - dp), iris::Vec3(x + wd * 0.5f, 0, -0.5f - behind) });
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

/// true when the rectangle `b` lies inside the union of `cover` (exact: the cells of b cut by every cover edge)
bool covered(const Rect &b, const std::vector<Rect> &cover)
{
    if (b.empty()) return true;
    std::vector<float> xs{ b.x0, b.x1 }, ys{ b.y0, b.y1 };
    for (const Rect &c : cover) {
        if (c.x0 > b.x0 && c.x0 < b.x1) xs.push_back(c.x0);
        if (c.x1 > b.x0 && c.x1 < b.x1) xs.push_back(c.x1);
        if (c.y0 > b.y0 && c.y0 < b.y1) ys.push_back(c.y0);
        if (c.y1 > b.y0 && c.y1 < b.y1) ys.push_back(c.y1);
    }
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    for (size_t i = 0; i + 1 < xs.size(); ++i)
        for (size_t j = 0; j + 1 < ys.size(); ++j) {
            if (xs[i + 1] - xs[i] <= 0.0f || ys[j + 1] - ys[j] <= 0.0f) continue;
            const float cx = 0.5f * (xs[i] + xs[i + 1]), cy = 0.5f * (ys[j] + ys[j + 1]);
            bool in = false;
            for (const Rect &c : cover) in = in || (cx >= c.x0 && cx <= c.x1 && cy >= c.y0 && cy <= c.y1);
            if (!in) return false;
        }
    return true;
}

std::vector<Prediction> expect(const Built &b, const Optics &o)
{
    // the wall's two BOXES (front and back faces: the gap's side faces narrow the gap seen off-axis)
    std::vector<Rect> wall = { project(o, iris::Vec3(-kHalfWidth, 0, -0.5f), iris::Vec3(kGap0, kWallH, 0)),
                               project(o, iris::Vec3(kGap1, 0, -0.5f), iris::Vec3(kHalfWidth, kWallH, 0)) };
    // THE GROUND IN FRONT OF THE WALL is nearer than every building: the screen below the wall's foot (its rows)
    {
        const Rect foot = project(o, iris::Vec3(-1000.0f, 0, 0), iris::Vec3(1000.0f, 0, 0));
        const float H = o.heightPixels, W = H * o.aspect;
        if (foot.y1 < H) wall.push_back(Rect{ 0.0f, foot.y0, W, H });
    }
    struct Box { Rect r; float nearZ = 0, farZ = 0; };   // distances along the view (larger = farther)
    std::vector<Box> boxes;
    for (const Object &obj : b.objects) {
        if (obj.role != "building") continue;
        boxes.push_back({ project(o, obj.boxMin, obj.boxMax), o.eye.z() - obj.boxMax.z(), o.eye.z() - obj.boxMin.z() });
    }
    const float F = float(std::max(1, o.footprint));
    int visible = 0, hidden = 0, drawnHidden = 0;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Rect &r = boxes[i].r;
        if (r.empty()) continue;   // off screen
        std::vector<Rect> cover = wall;   // the wall stands in front of every building
        for (size_t j = 0; j < boxes.size(); ++j)
            if (j != i && !boxes[j].r.empty() && boxes[j].farZ < boxes[i].nearZ) cover.push_back(boxes[j].r);
        if (!covered(r, cover)) { ++visible; continue; }
        ++hidden;
        // the test's mip: the rectangle at most F texels per axis; the texels it touches, every one (floor .. floor)
        const float widest = std::max(r.x1 - r.x0, r.y1 - r.y0);
        const float t = std::pow(2.0f, std::max(0.0f, std::ceil(std::log2(std::max(widest, 1.0f) / F))));
        const Rect g{ std::floor(r.x0 / t) * t, std::floor(r.y0 / t) * t, (std::floor(r.x1 / t) + 1.0f) * t,
                      (std::floor(r.y1 / t) + 1.0f) * t };
        const float H = o.heightPixels, W = H * o.aspect;
        const Rect gc{ std::max(g.x0, 0.0f), std::max(g.y0, 0.0f), std::min(g.x1, W), std::min(g.y1, H) };
        if (!covered(gc, cover)) ++drawnHidden;
    }
    char note[200];
    std::snprintf(note, sizeof note, "%d of the buildings in view hidden (the wall and nearer buildings); footprint %d "
                  "texels", hidden, o.footprint);
    return { { "visibleObjects", double(visible), "buildings not inside the union of the wall and the nearer buildings" },
             { "hiddenDrawn", double(drawnHidden), note },
             { "triangles", predictedTriangles(b, o), "every object in the frustum (no occlusion in this sum)" } };
}

std::string describe()
{
    return "atom-occlusion: a 30 x 8 m wall with a 3 m gap and 20 buildings behind it at 2-60 m. Predicts THE HIDDEN "
           "SHARE: the buildings visible past the wall AND the nearer buildings (mutual hiding, exact without pitch), and "
           "the HIDDEN buildings the conservative depth test still draws — those whose rectangle, rounded out to the "
           "texels of the mip where it spans the footprint (2 at this base), reaches past the occluders: hiddenDrawn.";
}

}   // namespace

Env atomOcclusion() { return Env{ "atom-occlusion", describe, build, stop, expect, 900.0f, true }; }

}   // namespace envs
}   // namespace lab
