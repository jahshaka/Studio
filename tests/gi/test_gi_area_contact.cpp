// gi.area_contact (PHOTON-7 stage 7's read, F6; L9: every light blocked by geometry on every path) - THE NO-RAY AREA
// VISIBILITY'S MARCH STARTS OFF ITS SURFACE ALONG THE NORMAL, SO A WALL AT A FLOOR POINT'S FOOT STILL SHADOWS IT.
//
// The no-ray route traces each forward LTC light's visibility through the voxel chain (Jahshaka/ScreenAreaVis,
// jah_area_vis_law.glsl), its march clear of its own surface's voxels by one cell of cascade 0 (7.8 cm at High). Taken
// ALONG the stratum's ray (lift / cos), a grazing ray started past whatever stood within cell x cot(elevation) of the
// point: an area light behind a wall reached the floor along the wall's base. The cones' convention - the start a cell
// along the NORMAL, the march from there - keeps the wall in the ray's way.
// THE FIXTURE: a floor (red), a wall 2 m tall and 0.1 m thick across it (blue), an area light 1 x 0.5 m facing down
// 0.3 m over the floor 0.8 m behind the wall - every floor point in front of the wall is in its shadow, geometrically,
// for every point of the rectangle. High, rays off, a 256 x 256 still picture looking at the wall's foot from the front.
// THE LAW: at the GI's rest and the visibility's hold, every floor pixel in front of the wall (the picture's floor hue)
// reads the light's diffuse visibility at most 0.1 (the march's fractional coverage) - before the fix the band along
// the wall's foot read it lit.
// A fixture (the engine alone, one offscreen view). Counted in frames.

#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
NodeId cube(Scene *s, const Vec3 &at, const Vec3 &size, const Colour &albedo) {
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 0.9f;
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-area-contact-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };

    Scene *s = e->createScene("area_contact");
    View *v = e->createOffscreenView("area_contact", 256, 256, Colour(0, 0, 0));
    if (!s || !v) { std::printf("FAIL: the scene / view\n"); return 1; }
    s->setRayTracing(RayTracingMode::Off);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        v->setPostFx(fx);
    }
    v->setOffscreenContract(OffscreenContract::StillPicture);
    v->setScene(s);
    cube(s, Vec3(0.0f, -0.05f, 0.0f), Vec3(4.0f, 0.1f, 6.0f), Colour(0.7f, 0.25f, 0.2f));    // the floor
    cube(s, Vec3(0.05f, 1.0f, 0.0f), Vec3(0.1f, 2.0f, 6.0f), Colour(0.2f, 0.3f, 0.7f));     // the wall, x 0 .. 0.1
    {
        const NodeId lamp = s->createNode();
        LightDesc l;
        l.type = LightType::Area;
        l.rectWidth = 1.0f;
        l.rectHeight = 0.5f;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 6.0f;
        l.range = 20.0f;
        s->setNodeTransform(lamp, Vec3(0.9f, 0.3f, 0.0f), Quat(), Vec3(1, 1, 1));   // facing down, behind the wall
        s->setLight(lamp, l);
    }
    {
        float sh[27] = { 0 };
        sh[0] = 0.05f; sh[1] = 0.05f; sh[2] = 0.06f;
        s->setAmbientSh(sh);
    }
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    enginetest::testCameraLookAt(v, Vec3(-1.6f, 0.9f, 0.0f), Vec3(-0.15f, 0.0f, 0.0f));
    e->setArm("area.visReadback", 1.0);
    e->waitForAsyncShaders();

    int rest = -1;
    for (int f = 0, quiet = 0; f < 900; ++f) {
        run(1);
        const GiStatus g = s->published().gi;
        const AreaVisStatus a = s->published().areaVis;
        quiet = !g.chainOwed && !g.feedbackOwed && !g.cascades.empty() && a.held ? quiet + 1 : 0;
        if (quiet >= 12) { rest = f - quiet; break; }
    }
    const AreaVisStatus a = s->published().areaVis;
    ImageF pic;
    const bool read = v->readPixelsHdr(pic);
    // THE FLOOR IN FRONT OF THE WALL: the floor's hue (red over blue), every pixel with the visibility read back
    unsigned floorPx = 0, lit = 0;
    float worst = 0.0f;
    int worstY = -1;
    if (read && a.visW == pic.width && a.visH == pic.height && !a.vis.empty()) {
        for (unsigned y = 0; y < pic.height; ++y)
            for (unsigned x = 0; x < pic.width; ++x) {
                const Colour c = pic.at(x, y);
                if (!(c.r > 2.0f * c.b && c.r > 1e-4f)) continue;
                ++floorPx;
                const float vis = a.vis[(size_t(y) * a.visW + x) * 8u] / 255.0f;   // light 0's diffuse share
                if (vis > 0.1f) ++lit;
                if (vis > worst) { worst = vis; worstY = int(y); }
            }
    }
    std::printf("   at rest after %d frames (held %d, %u x %u traced); %u floor pixels in front of the wall, %u read lit "
                "(> 0.1), the worst %.3f at row %d\n", rest, int(a.held), a.traceW, a.traceH, floorPx, lit, double(worst),
                worstY);
    CHECK(rest >= 0 && read && floorPx > 5000u, "premise: at rest, the visibility read back, the floor in the picture");
    CHECK(lit == 0u, "every floor point in front of the wall is in its shadow - the band along its foot too (L9)");
    e->setArm("area.visReadback", 0.0);
    v->setScene(nullptr);
    e->destroyView(v);
    e->destroyScene(s);
    run(2);
    std::printf("\ngi.area_contact: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
