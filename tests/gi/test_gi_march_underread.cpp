// gi.march_underread — RESERVED FOR PHOTON-MARCH-1 (not registered: this is the next spec's
// acceptance, moved out of gi.rough_sky_once by the lead's ruling, BISTRO-FIXES-3; the spec
// registers the row when its fix lands, with its FRAMES and its gate-times line).
//
// THE LAW: a rough (0.74) white metal plate seen 10 degrees above its surface, a wall emitting
// the sky's radiance L standing in its reflection (filling the rough lobe's 45-degree query),
// the hemisphere above open, a uniform sky of L. Every direction the lobe can take carries L, so
// the plate reads the open plate's value (the GI-off reading) whatever the cascade's cells: the
// sky's share (the cube times the query's visibility) plus the voxels' share (what the query
// stopped on, at the same occlusion) is one lobe of L. BAR: 3 % (gi.env_cone's furnace bar and
// its reasons), at EVERY cascade resolution.
//
// MEASURED at BISTRO-FIXES-3 (the cones and the field read alike; one 9 m cascade):
//   cells           32 (0.28 m)   64 (0.14 m)   128 (0.07 m)
//   wall on plate   0.998         0.954         0.873
//   free-standing   -             0.978         0.941    (the plate stops 0.15 m short of it)
// A 1 m thick wall reads as the 0.5 m one; a 16 m tall one 0.8 % lower. The under-read GROWS as
// the cells shrink - the signature of a cone march whose opacity is not corrected for its step
// (alpha' = 1 - (1 - alpha)^(step / voxel)): more, smaller steps accumulate more opacity for the
// same physical occluder. spikes/bistro-fixes-3/march-underread.md.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-march-underread-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *rv = e->createOffscreenView("underread", 64, 64, Colour(0, 0, 0));
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdr = false;
    fx.hdrReadback = true;
    rv->setPostFx(fx);
    const auto readHdr = [&]() {
        double acc = 0.0;
        for (int f = 0; f < 4; ++f) {
            e->renderOneFrame();
            ImageF img;
            if (!rv->readPixelsHdr(img)) return -1.0;
            double s2 = 0.0; int n = 0;
            for (unsigned y = 28; y < 36; ++y)
                for (unsigned x = 28; x < 36; ++x) {
                    const Colour c = img.at(x, y);
                    s2 += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; ++n;
                }
            acc += s2 / n;
        }
        return acc / 4.0;
    };
    const unsigned char kSkyByte = 128;
    const double L = std::pow((kSkyByte / 255.0 + 0.055) / 1.055, 2.4);
    PbrParams metal; metal.albedo = Colour(1, 1, 1); metal.metalness = 1.0f; metal.roughness = 0.74f;

    for (int res : { 32, 64, 128 }) {
        Scene *s = e->createScene("underread");
        CHECK(rv->setScene(s), "the view shows the scene");
        const unsigned char skyPx[4] = { kSkyByte, kSkyByte, kSkyByte, 255 };
        SkyDesc sky;
        sky.mode = SkyMode::Equirectangular;
        sky.equirect = s->createTexture(1, 1, skyPx, true);
        CHECK(sky.equirect && s->setSky(sky), "the uniform sky binds");
        float sh[27] = { 0 };
        for (int f = 0; f < 20 && !s->skyAmbientSh(sh); ++f) e->renderOneFrame();
        s->setAmbientSh(sh);
        s->setEnvironmentLight(Colour(1, 1, 1, 1));
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        const auto slab = [&](const Vec3 &pos, const Vec3 &scale, const PbrParams &p) {
            const NodeId nd = s->createNode();
            s->attachMesh(nd, cube, s->createPbrMaterial(p));
            s->setNodeTransform(nd, pos, Quat(), scale);
        };
        // the wall on the plate, 1.5 m beyond its centre, inside the camera-centred cascade
        PbrParams wall; wall.albedo = Colour(0, 0, 0); wall.roughness = 1.0f;
        wall.emissive = Colour(float(L), float(L), float(L));
        slab(Vec3(0.0f, 4.05f, 1.75f), Vec3(12.0f, 7.9f, 0.5f), wall);
        slab(Vec3(0.0f, 0.05f, 0.0f), Vec3(4.0f, 0.1f, 4.0f), metal);
        enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.55f, -2.5f), Vec3(0.0f, 0.1f, 0.0f));
        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        for (int f = 0; f < 10; ++f) e->renderOneFrame();
        const double open = readHdr();
        GiParams cones;
        cones.mode = GiMode::Vct;
        cones.quality = GiQuality::Low;
        cones.numBounces = 1;
        cones.ddgi = GiToggle::Off;
        cones.gather = GiToggle::Off;
        cones.cascadeCount = 1;
        cones.cascadeSet[0] = GiParams::GiCascadeDesc{ 4.5f, res, 0.0f };
        s->setGlobalIllumination(cones);
        for (int f = 0; f < 20; ++f) e->renderOneFrame();
        for (int f = 0; f < 4000 && !s->giStatus().giAtRest; ++f) e->renderOneFrame();
        const double withWall = readHdr();
        char msg[220];
        std::snprintf(msg, sizeof msg, "%d cells: a wall of L in the grazing lobe reads the open value (%.4f / %.4f "
                      "= %.3f; bar 3 %%)", res, withWall, open, withWall / open);
        CHECK(std::fabs(withWall / open - 1.0) <= 0.03, msg);
        s->setGlobalIllumination(off);
        rv->setScene(nullptr);
        e->destroyScene(s);
    }
    e->destroyView(rv);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
