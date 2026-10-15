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
// THE LAW OF THE READ (PHOTON-MARCH-1): the colour that comes with an opacity is the
// coverage-weighted radiance of the same half-axis faces the opacity counts - the directional
// store, (radiance x k, k) per half-axis, at EVERY tier. The Low tier stored one isotropic light
// per voxel and took a texel's MEAN radiance over every surface in it: the query here takes the
// whole of its room (0.614) in the one plane holding the wall's face, and at the wall-plate
// junction that mean holds the plate's voxels too (parallel to the ray, no opacity of theirs in
// the plane): 0.998 / 0.954 / 0.873 at 32 / 64 / 128 cells on the plate (stage 0's dump,
// spikes/photon-march-1/). Since stage 1 every tier reads the directional store: 0.999 / 0.996 /
// 0.990 on the plate, 1.000 free-standing, field off and on. The leftover 1 % at 128 cells is the
// junction voxel itself, which holds the wall's foot and the plate's top in one light.
// THE TIERS ARE ONE READER: the row reads Low and Medium and asserts Low reads Medium's value on
// every arm (`--all-tiers` adds High and Epic). It prints the light store's memory per arm (Low,
// 64 cells, one cascade: 6.86 MB before stage 1, 14.07 MB after - the directional volumes, the
// back side and the bounce's per-side twins).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
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

    // THE TIERS: the row reads Low and Medium (one reader: Low must read Medium's value); `--all-tiers`
    // adds High and Epic.
    std::vector<std::pair<GiQuality, const char *>> tiers = { { GiQuality::Low, "Low" }, { GiQuality::Medium, "Medium" } };
    if (argc > 1 && std::string(argv[1]) == "--all-tiers")
        tiers.insert(tiers.end(), { { GiQuality::High, "High" }, { GiQuality::Epic, "Epic" } });
    std::vector<double> readings[2];   // Low, Medium: per (res, wall, field) arm, in order
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
                if (arm.first == GiToggle::Off && standing) {
                    // THE LIGHT STORE'S MEMORY (the textures PhotonVoxelLighting creates: the total, the
                    // directional axes, the back side, the bounce and direct twins, the mip steps)
                    std::vector<TextureMemoryEntry> tex;
                    unsigned long long bytes = 0; int count = 0;
                    if (e->textureMemory(tex))
                        for (const TextureMemoryEntry &t : tex)
                            if (t.name.rfind("VctLighting", 0) == 0 || t.name.rfind("VCT/", 0) == 0) { bytes += t.bytes; ++count; }
                    std::printf("%s, %d cells, one cascade: the light store holds %.2f MB in %d textures\n", tier.second, res,
                                double(bytes) / (1024.0 * 1024.0), count);
                }
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
                if (tier.first == GiQuality::Low) readings[0].push_back(withWall / open);
                if (tier.first == GiQuality::Medium) readings[1].push_back(withWall / open);
            }
            s->setGlobalIllumination(off);
            rv->setScene(nullptr);
            e->destroyScene(s);
        }
    e->destroyView(rv);
    {
        // ONE READER AT EVERY TIER: Low reads Medium's value on every arm (the same store, the same law)
        double worst = 0.0;
        for (size_t k = 0; k < readings[0].size() && k < readings[1].size(); ++k)
            worst = std::max(worst, std::fabs(readings[0][k] / readings[1][k] - 1.0));
        char msg[200];
        std::snprintf(msg, sizeof msg, "Low reads Medium's value on all %zu arms (worst %.4f; bar 3 %%)",
                      readings[0].size(), worst);
        CHECK(readings[0].size() == 12u && readings[1].size() == 12u && worst <= 0.03, msg);
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
