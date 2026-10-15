// gi.march_underread — A WALL OF L IN A GRAZING ROUGH LOBE READS L AT EVERY CASCADE RESOLUTION
// (PHOTON-MARCH-1; moved out of gi.rough_sky_once by BISTRO-FIXES-3, registered at PHOTON-MARCH-1
// stage 0 RED on the base, its acceptance row).
//
// THE LAW: a rough (0.74) white metal plate seen 10 degrees above its surface, a wall emitting
// the sky's radiance L standing in its reflection (filling the rough lobe's 45-degree query),
// the hemisphere above open, a uniform sky of L. Every direction the lobe can take carries L, so
// the plate reads the open plate's value (the GI-off reading) whatever the cascade's cells: the
// sky's share (the cube times the query's visibility) plus the voxels' share (what the query
// stopped on, at the same occlusion) is one lobe of L. BAR: 3 % (gi.env_cone's furnace bar and
// its reasons), at EVERY cascade resolution, the field off and on, the wall standing on the
// plate and free-standing (the plate stopping 0.15 m short of it).
//
// WHERE THE BASE LOSES IT (stage 0, measured: spikes/photon-march-1/, the march of the centre
// pixel's ray dumped per plane through the parity job). The query crosses empty planes and takes
// the whole of its room (1 - the below-horizon share, 0.614) in ONE plane, the one holding the
// wall's face: the opacity is exact and nothing accumulates over steps. The colour that comes with
// that opacity is the isotropic texel's MEAN radiance (jahVoxelReadPlane: cA = s.rgb aA / s.a) -
// every voxel of the fetch's neighbourhood weighted by its conservative opacity, the plate's
// voxels too, whose faces are parallel to the ray and give the plane no opacity. The fetch at the
// wall straddles the texel holding the wall-plate junction (the texel is 0.5625 m at every cell
// count; the ray meets the wall lower as the one-cell start bias shrinks: y 0.685 / 0.544 / 0.474
// m), so the plane's colour reads 0.9998 / 0.950 / 0.874 L at 32 / 64 / 128 cells (rebuilt from
// mip 0 on the CPU to 4 digits) - the readings below. The free-standing wall's texel holds no
// plate voxel: its plane reads 0.9998 L.
// THE ANISOTROPIC TIERS (`--all-tiers`, stage 0's second measurement): Medium, High and Epic read
// the same plane from the directional store and pass, 0.999 / 0.996 / 0.990 on the plate and 1.000
// free-standing - the plane's colour 0.9998 / 0.995 / 0.990 L; the row reads Low (the tier that fails).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

int main(int argc, char **argv)
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-march-underread-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
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

    // THE TIERS: the row reads Low; `--all-tiers` adds the anisotropic ones (Medium, High, Epic: the
    // directional store, its level-0 back side and normal) - a measurement, not the row's.
    std::vector<std::pair<GiQuality, const char *>> tiers = { { GiQuality::Low, "Low" } };
    if (argc > 1 && std::string(argv[1]) == "--all-tiers")
        tiers.insert(tiers.end(), { { GiQuality::Medium, "Medium" }, { GiQuality::High, "High" }, { GiQuality::Epic, "Epic" } });
    for (const auto &tier : tiers)
    for (int res : { 32, 64, 128 })
        for (int standing = 1; standing >= 0; --standing) {
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
            // the wall 1.5 m beyond the plate's centre (its face at z 1.5), inside the camera-centred
            // cascade; standing on the plate (z -2 .. 2) or free (the plate stops 0.15 m short, z -2 .. 1.35)
            PbrParams wall; wall.albedo = Colour(0, 0, 0); wall.roughness = 1.0f;
            wall.emissive = Colour(float(L), float(L), float(L));
            slab(Vec3(0.0f, 4.05f, 1.75f), Vec3(12.0f, 7.9f, 0.5f), wall);
            if (standing) slab(Vec3(0.0f, 0.05f, 0.0f), Vec3(4.0f, 0.1f, 4.0f), metal);
            else slab(Vec3(0.0f, 0.05f, -0.325f), Vec3(4.0f, 0.1f, 3.35f), metal);
            enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.55f, -2.5f), Vec3(0.0f, 0.1f, 0.0f));
            GiParams off; off.mode = GiMode::Off;
            s->setGlobalIllumination(off);
            for (int f = 0; f < 10; ++f) e->renderOneFrame();
            const double open = readHdr();
            for (const auto &arm : { std::make_pair(GiToggle::Off, "field off"), std::make_pair(GiToggle::On, "field on") }) {
                GiParams cones;
                cones.mode = GiMode::Vct;
                cones.quality = tier.first;
                cones.numBounces = 1;
                cones.ddgi = arm.first;
                cones.gather = GiToggle::Off;
                cones.cascadeCount = 1;
                cones.cascadeSet[0] = GiParams::GiCascadeDesc{ 4.5f, res, 0.0f };
                s->setGlobalIllumination(cones);
                for (int f = 0; f < 20; ++f) e->renderOneFrame();
                for (int f = 0; f < 4000 && !s->giStatus().giAtRest; ++f) e->renderOneFrame();
                const double withWall = readHdr();
                const GiStatus st = s->giStatus();
                const bool fieldBuilt = st.ifdMax.x > st.ifdMin.x;
                if (arm.first == GiToggle::On)
                    CHECK(fieldBuilt, "the field-on arm has a field (its volume is not empty)");
                else
                    CHECK(!fieldBuilt, "the field-off arm has no field");
                char msg[240];
                std::snprintf(msg, sizeof msg, "%s, %d cells, the wall %s, %s: a wall of L in the grazing lobe reads the open "
                              "value (%.4f / %.4f = %.3f; bar 3 %%)", tier.second, res, standing ? "on the plate" : "free-standing",
                              arm.second, withWall, open, withWall / open);
                CHECK(std::fabs(withWall / open - 1.0) <= 0.03, msg);
            }
            s->setGlobalIllumination(off);
            rv->setScene(nullptr);
            e->destroyScene(s);
        }
    e->destroyView(rv);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
