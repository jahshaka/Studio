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
// 0.990 on the plate, 1.000 free-standing, field off and on - and since stage 2 (a metal's voxel holds
// its diffuse lobe, 0 for this metal plate, where its base colour stood) 0.996 / 0.985 / 0.980 on the
// plate. THE LEFTOVER IS THE PERPENDICULAR JUNCTION: the voxels holding the wall's foot and the plate's
// top, whose light the black plate now dilutes; free-standing reads 1.000. `--junction-256` measures it
// beyond the shipped resolutions (the tiers ship 64 and 128 cells): 0.980 at 128, 0.994 at 256 - not a
// trend in the cell size (the ray meets the wall at a height set by the one-cell start bias, against a
// texel grid set by the cascade's snap), the debt JUNCTION-NORMAL-1.
// THE THIN SLAB (stage 3, the law's own proof): one voxel holding two opposite faces of different
// radiance, the reading follows the face that looks back (a half-axis store), never their mean (an
// axis or isotropic store) - see the slab block below; on the isotropic store (irisgl 2f3edcd55, Low)
// it read the mean: law 0.627 / 2.510 at 32 cells, 0.742 / 3.001 at 64, 0.882 / 3.620 at 128.
// THE TIERS ARE ONE READER: the row reads Low and Medium and asserts Low reads Medium's value on
// every arm within 1e-4 - the same store and the same reader path, so the same number (`--all-tiers`
// adds High and Epic). It prints the light store's memory per arm (Low,
// 64 cells, one cascade: 6.86 MB before stage 1, 14.07 MB after - the directional volumes, the
// back side and the bounce's per-side twins).
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/PhotonStatus.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
            enginetest::frames(e, 1);
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
    // `--junction-256`: THE PERPENDICULAR JUNCTION'S MEASURE (the wall standing on the plate at 128 and
    // 256 cells, Medium, printed and not asserted: 256 is no shipped cascade resolution - the tiers ship
    // 64 and 128 - so its reading is the debt JUNCTION-NORMAL-1's number, not the row's bar).
    const bool junction = argc > 1 && std::string(argv[1]) == "--junction-256";
    // `--veils`: THE INSIDE FRACTION'S OWN CASES (gi.march_inside, ENGINE-UPGRADE-U5 decision 22.2), alone
    const bool veils = argc > 1 && std::string(argv[1]) == "--veils";
    std::vector<std::pair<GiQuality, const char *>> tiers = { { GiQuality::Low, "Low" }, { GiQuality::Medium, "Medium" } };
    if (argc > 1 && std::string(argv[1]) == "--all-tiers")
        tiers.insert(tiers.end(), { { GiQuality::High, "High" }, { GiQuality::Epic, "Epic" } });
    if (junction) tiers = { { GiQuality::Medium, "Medium" } };
    if (veils) tiers.clear();
    const std::vector<int> resolutions = junction ? std::vector<int>{ 128, 256 } : std::vector<int>{ 32, 64, 128 };
    std::vector<double> readings[2];   // Low, Medium: per (res, wall, field) arm, in order
    for (const auto &tier : tiers)
    for (int res : resolutions)
        for (int standing = 1; standing >= (junction ? 1 : 0); --standing) {
            Scene *s = e->createScene("underread");
            CHECK(rv->setScene(s), "the view shows the scene");
            const unsigned char skyPx[4] = { kSkyByte, kSkyByte, kSkyByte, 255 };
            SkyDesc sky;
            sky.mode = SkyMode::Equirectangular;
            sky.equirect = s->createTexture(1, 1, skyPx, true);
            CHECK(sky.equirect && s->setSky(sky), "the uniform sky binds");
            float sh[27] = { 0 };
            enginetest::frames(e, 1);   // the snapshot moves at a frame's close (U3 stage 2b)
            for (int f = 0; f < 20 && !s->published().copySkyAmbientSh(sh); ++f) enginetest::frames(e, 1);
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
            enginetest::frames(e, 10);
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
                enginetest::frames(e, 20);
                for (int f = 0; f < 4000 && !s->published().gi.giAtRest; ++f) enginetest::frames(e, 1);
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
                const GiStatus st = s->published().gi;
                const bool fieldBuilt = st.ifdMax.x > st.ifdMin.x;
                if (arm.first == GiToggle::On)
                    CHECK(fieldBuilt, "the field-on arm has a field (its volume is not empty)");
                else
                    CHECK(!fieldBuilt, "the field-off arm has no field");
                char msg[240];
                std::snprintf(msg, sizeof msg, "%s, %d cells, the wall %s, %s: a wall of L in the grazing lobe reads the open "
                              "value (%.4f / %.4f = %.3f; bar 3 %%)", tier.second, res, standing ? "on the plate" : "free-standing",
                              arm.second, withWall, open, withWall / open);
                if (junction) std::printf("MEASURE (JUNCTION-NORMAL-1): %s\n", msg);
                if (standing && arm.first == GiToggle::Off) {
                    // THE JUNCTION VOXELS (JUNCTION-NORMAL-1): the face column at x = 0, the rows from the plate's top
                    // up - the voxel's emissive (an area MEAN over every surface in it) against L, its two z halves'
                    // and its +y half's coverage, and where the wall's face sits in its cell
                    GiVoxelVolume v;
                    if (s->giVoxelVolume(0, v) && v.available && !v.emissive.empty()) {
                        const int cx = int((0.0f - v.origin[0]) / v.cell[0]);
                        const int cz = int((1.5f - v.origin[2]) / v.cell[2]);
                        const double frac = (1.5 - v.origin[2]) / v.cell[2] - cz;
                        // THE EMISSIVE PER HALF (JUNCTION-NORMAL-1, decision 21.2): every voxel of the face column holding
                        // the wall's face emits L on the half that face looks to (-z), the plate's area in the voxel
                        // notwithstanding (it used to emit the voxel's AREA MEAN: 0.554 L at 128 cells). BAR 1 %.
                        double worstHalf = 0.0;
                        int halves = 0;
                        for (int cy = int((0.05f - v.origin[1]) / v.cell[1]); cy <= int((0.05f - v.origin[1]) / v.cell[1]) + 2; ++cy) {
                            const size_t i = 4 * (size_t(cx) + size_t(v.width) * (size_t(cy) + size_t(v.height) * size_t(cz)));
                            // the half the wall's face looks to (-z): its emissive factor f (JUNCTION-NORMAL-1)
                            const double cN = std::floor(double(v.emitN[i + 2]) * 1023.0 + 0.5), fN = cN / (1022.0 - cN);
                            if (v.coverageN[i + 2] > 0.05f) {
                                worstHalf = std::max(worstHalf, std::fabs(double(v.emissive[i]) / L * fN - 1.0));
                                ++halves;
                            }
                            if (junction)
                            std::printf("MEASURE (JUNCTION-NORMAL-1): %d cells, row y %.4f .. %.4f (the face at %.3f of its cell): "
                                        "emissive / L %.4f, the -z half emits %.4f L, covN.z %.3f covP.y %.3f, normal %.3f %.3f %.3f "
                                        "two-sided %.0f\n", res,
                                        double(v.origin[1] + cy * v.cell[1]), double(v.origin[1] + (cy + 1) * v.cell[1]), frac,
                                        double(v.emissive[i]) / L, double(v.emissive[i]) / L * fN, double(v.coverageN[i + 2]),
                                        double(v.coverageP[i + 1]),
                                        2.0 * v.normal[i] - 1.0, 2.0 * v.normal[i + 1] - 1.0, 2.0 * v.normal[i + 2] - 1.0,
                                        double(v.normal[i + 3]));
                        }
                        char hmsg[200];
                        std::snprintf(hmsg, sizeof hmsg, "%s, %d cells: the wall's half emits L in every junction voxel of "
                                      "its face (%d; worst %.4f; bar 1 %%)", tier.second, res, halves, worstHalf);
                        CHECK(halves > 0 && worstHalf <= 0.01, hmsg);
                    }
                }
                if (!junction) CHECK(std::fabs(withWall / open - 1.0) <= 0.03, msg);
                if (tier.first == GiQuality::Low) readings[0].push_back(withWall / open);
                if (tier.first == GiQuality::Medium) readings[1].push_back(withWall / open);
            }
            s->setGlobalIllumination(off);
            rv->setScene(nullptr);
            e->destroyScene(s);
        }
    for (int res : { 32, 64, 128 }) {
        if (junction) break;
        // THE LAW'S OWN PROOF (PHOTON-MARCH-1 stage 3): ONE VOXEL HOLDING TWO OPPOSITE FACES OF
        // DIFFERENT RADIANCE. A 0.02 m white slab (thinner than every cell here), lit by two suns, one per
        // face, 1 : 0.25, beyond the free plate; the plate's grazing rough lobe fills with the face it looks
        // at. The reference is the same lobe filled by a 0.5 m wall (each face in its own voxels) under the
        // same suns. LAW: reading / reference = R_face / R_wall, R from the GI-off HDR pixel of each face
        // (no sky, no ambient: the voxels hold the suns' light alone, which the pixel shows). A store that
        // holds a light per half-axis follows the lit face; an axis store reads the mean of both faces
        // (0.625 of the strong one) whichever face looks back. At rest, after the suns swap (the plate's
        // face 0.25), and after a sub-cell slab move.
        Scene *s = e->createScene("underread-slab");
        CHECK(rv->setScene(s), "the view shows the slab's scene");
        float sh[27] = { 0 };
        s->setAmbientSh(sh);
        s->setEnvironmentLight(Colour(0, 0, 0, 1));
        const NodeId nearSun = enginetest::addDirectionalLight(s, Vec3(0.0f, 0.0f, 1.0f), 3.0f);
        const NodeId farSun = enginetest::addDirectionalLight(s, Vec3(0.0f, 0.0f, -1.0f), 0.75f);
        const auto sunPower = [&](NodeId n, float power) {
            LightDesc l;
            l.type = LightType::Directional;
            l.colour = Colour(1.f, 1.f, 1.f);
            l.intensity = power / 3.14159265358979323846f;
            s->setLight(n, l);
        };
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        PbrParams white; white.albedo = Colour(1, 1, 1); white.roughness = 1.0f;
        const MaterialId whiteMat = s->createPbrMaterial(white);
        const NodeId wallNode = s->createNode();
        s->attachMesh(wallNode, cube, whiteMat);
        const NodeId plateNode = s->createNode();
        s->attachMesh(plateNode, cube, s->createPbrMaterial(metal));
        s->setNodeTransform(plateNode, Vec3(0.0f, 0.05f, -0.325f), Quat(), Vec3(4.0f, 0.1f, 3.35f));
        GiParams off; off.mode = GiMode::Off;
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::Medium;
        gi.numBounces = 1;
        gi.ddgi = GiToggle::Off;
        gi.gather = GiToggle::Off;
        gi.cascadeCount = 1;
        gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 4.5f, res, 0.0f };
        const auto settle = [&]() {
            enginetest::frames(e, 20);
            for (int f = 0; f < 4000 && !s->published().gi.giAtRest; ++f) enginetest::frames(e, 1);
        };
        // one reading: the face's radiance (GI off, the camera square on the face) and the plate's lobe (GI on)
        const auto measure = [&](double &face, double &lobe) {
            s->setGlobalIllumination(off);
            enginetest::testCameraLookAt(rv, Vec3(0.0f, 2.0f, 0.5f), Vec3(0.0f, 2.0f, 1.5f));
            enginetest::frames(e, 6);
            face = readHdr();
            enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.55f, -2.5f), Vec3(0.0f, 0.1f, 0.0f));
            s->setGlobalIllumination(gi);
            settle();
            lobe = readHdr();
        };
        // the reference wall: its lit face at z 1.5
        s->setNodeTransform(wallNode, Vec3(0.0f, 4.05f, 1.75f), Quat(), Vec3(12.0f, 7.9f, 0.5f));
        double faceWall = 0.0, lobeWall = 0.0;
        measure(faceWall, lobeWall);
        // THE SUN-LIT WALL'S LOBE PER RESOLUTION (SUN-INJECTION-RES-1, printed: the debt's own table - 1.019 / 1.003 /
        // 0.982 at 32 / 64 / 128 cells on the base, 1.019 / 1.007 / 0.995 with the shadow march's entries and exits
        // apart; the residual is the coverage's statistics - see the column law below)
        std::printf("MEASURE (SUN-INJECTION-RES-1): %d cells, the sun-lit wall's lobe / its face %.5f / %.5f = %.5f\n", res,
                    lobeWall, faceWall, lobeWall / faceWall);
        if (!veils) {
            // THE INJECTION'S SHADOW OF A THIN SLAB (SUN-INJECTION-RES-1, ENGINE-UPGRADE-U5 stage 5): the wall sunk 2 m
            // under the plate's plane, so the 0.1 m plate (y 0 .. 0.1, end on to the suns) shadows a band of its lit face.
            // A voxel of the face column stores the face's light times the transmittance of the cells between it and the
            // sun, and the plate's end faces cover the share of the voxel's height the plate fills - so a voxel of the
            // column at [y0, y0 + cell] holds 1 - |[y0, y0 + cell] n [0, 0.1]| / cell of the lit face's light: the
            // coverage's own law, at every resolution. The plate is crossed END TO END - in at z 1.35, out at z -2 - and
            // its two end faces carry the one band: summed, the march stopped the ray twice (the row at 64 cells black,
            // 0.000 against the law's 0.286; 0.155 against 0.571 at 128). BAR 1 % of the lit face (one half-float quantum
            // is 0.1 %).
            s->setNodeTransform(wallNode, Vec3(0.0f, 3.05f, 1.75f), Quat(), Vec3(12.0f, 9.9f, 0.5f));
            s->refreshGlobalIllumination();
            double f2 = 0.0, l2 = 0.0;
            measure(f2, l2);
            GiVoxelVolume v;
            const bool got = s->giVoxelVolume(0, v) && v.available && !v.direct.empty();
            CHECK(got, "the store's direct volume reads back");
            if (got) {
                const int cx = int((0.0f - v.origin[0]) / v.cell[0]);
                const int cz = int((1.5f - v.origin[2]) / v.cell[2]);
                const auto litAt = [&](int cy) {
                    const size_t i = 4 * (size_t(cx) + size_t(v.width) * (size_t(cy) + size_t(v.height) * size_t(cz)));
                    return v.direct[i + 3] > 0.0f ? double(v.direct[i + 1]) / double(v.direct[i + 3]) / double(v.multiplier) : 0.0;
                };
                const double lit = litAt(int((-1.0f - v.origin[1]) / v.cell[1]));   // a voxel well under the band
                double worst = 0.0;
                for (int cy = int((-0.3f - v.origin[1]) / v.cell[1]); cy <= int((0.5f - v.origin[1]) / v.cell[1]); ++cy) {
                    const double y0 = double(v.origin[1]) + double(cy) * double(v.cell[1]);
                    const double y1 = y0 + double(v.cell[1]);
                    const double band = std::max(0.0, std::min(y1, 0.1) - std::max(y0, 0.0));
                    const double want = 1.0 - band / double(v.cell[1]);
                    const double reading = lit > 0.0 ? litAt(cy) / lit : 0.0;
                    worst = std::max(worst, std::fabs(reading - want));
                    if (band > 0.0) {
                        std::printf("%d cells, the face column's voxel at y %.4f .. %.4f: %.4f of the lit face (the law %.4f)\n",
                                    res, y0, y1, reading, want);
                        if (std::getenv("JAH_UNDERREAD_DUMP"))
                            for (int z = 0; z < v.depth; ++z) {
                                const size_t j = 4 * (size_t(cx) + size_t(v.width) * (size_t(cy) + size_t(v.height) * size_t(z)));
                                if (v.coverageP[j + 2] > 0.0f || v.coverageN[j + 2] > 0.0f)
                                    std::printf("   DUMP z %.4f: covP.z %.4f covN.z %.4f posP.z %.4f posN.z %.4f\n",
                                                double(v.origin[2] + z * v.cell[2]), double(v.coverageP[j + 2]),
                                                double(v.coverageN[j + 2]), double(v.positionP[j + 2]), double(v.positionN[j + 2]));
                            }
                    }
                }
                char msg[240];
                std::snprintf(msg, sizeof msg, "%d cells: the plate's shadow on the face column is its coverage's, crossed "
                              "once (worst %.4f of the lit face %.5f; bar 0.01)", res, worst, lit);
                CHECK(lit > 0.0 && worst <= 0.01, msg);
            }
            s->setNodeTransform(wallNode, Vec3(0.0f, 4.05f, 1.75f), Quat(), Vec3(12.0f, 7.9f, 0.5f));
        }
        if (veils) {
            // THE INSIDE FRACTION'S OWN CASES (ENGINE-UPGRADE-U5 decision 22.2): the march takes the faces it crosses in
            // order, a FRONT face blocking and entering, a BACK face first leaving what was entered and blocking only its
            // remainder (an open surface seen from behind). Between the lit face (z 1.5, the near sun travelling +z, so
            // the march goes -z) and the sun, a band y 3 .. 4 the width of the wall:
            //   OPEN: two ONE-SIDED quads at opacity 0.6 facing AWAY from each other along the ray - the nearer (z 1.0)
            //   looking -z (the ray's way: seen from behind), the farther (z 0.5) looking +z - transmittance
            //   (1 - c1)(1 - c2) = 0.16, where the max of the entries and the exits read them as one solid, 0.4;
            //   SOLID: one box of the same opacity (z 0.4 .. 0.6): crossed in and out, 1 - c = 0.4.
            // Each reading against the store's own coverage of those faces (the independent coverage), BAR 1 % of the
            // lit face, on the face column's rows inside the band.
            s->setNodeTransform(wallNode, Vec3(0.0f, 4.05f, 1.75f), Quat(), Vec3(12.0f, 7.9f, 0.5f));
            MeshData quad;
            quad.positions = { -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f };
            quad.normals = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
            quad.indices = { 0, 1, 2, 0, 2, 3 };
            const MeshId quadMesh = s->createMesh(quad);
            PbrParams veil; veil.albedo = Colour(0.5f, 0.5f, 0.5f); veil.roughness = 1.0f;
            veil.alphaMode = PbrAlphaMode::Blend;
            veil.alpha = 0.6f;
            const MaterialId veilMat = s->createPbrMaterial(veil);
            const Quat facingMinusZ(0.0f, 1.0f, 0.0f, 0.0f);   // 180 degrees about y: the quad looks -z
            struct Case { const char *what; bool open; };
            for (const Case &c : { Case{ "two open quads facing away from each other", true },
                                   Case{ "one solid of the same opacity", false } }) {
                std::vector<NodeId> added;
                const auto put = [&](MeshId m, const Vec3 &pos, const Quat &q, const Vec3 &scale) {
                    const NodeId nd = s->createNode();
                    s->attachMesh(nd, m, veilMat);
                    s->setNodeTransform(nd, pos, q, scale);
                    added.push_back(nd);
                };
                if (c.open) {
                    put(quadMesh, Vec3(0.0f, 3.5f, 1.0f), facingMinusZ, Vec3(12.0f, 1.0f, 1.0f));
                    put(quadMesh, Vec3(0.0f, 3.5f, 0.5f), Quat(), Vec3(12.0f, 1.0f, 1.0f));
                } else {
                    put(cube, Vec3(0.0f, 3.5f, 0.5f), Quat(), Vec3(12.0f, 1.0f, 0.2f));
                }
                s->refreshGlobalIllumination();
                double fq = 0.0, lq = 0.0;
                measure(fq, lq);
                GiVoxelVolume v;
                const bool got = s->giVoxelVolume(0, v) && v.available && !v.direct.empty();
                CHECK(got, "the store reads back under the veils");
                if (got) {
                    const int cx = int((0.0f - v.origin[0]) / v.cell[0]);
                    const auto at = [&](int cy, float z) {
                        const int cz = int((z - v.origin[2]) / v.cell[2]);
                        return 4 * (size_t(cx) + size_t(v.width) * (size_t(cy) + size_t(v.height) * size_t(cz)));
                    };
                    const auto litAt = [&](int cy) {
                        const size_t i = at(cy, 1.5f);
                        return v.direct[i + 3] > 0.0f ? double(v.direct[i + 1]) / double(v.direct[i + 3]) / double(v.multiplier) : 0.0;
                    };
                    const double lit = litAt(int((2.0f - v.origin[1]) / v.cell[1]));   // a row the veils do not shade
                    double worst = 0.0, worstWant = 0.0;
                    int rows = 0;
                    for (int cy = int((3.0f - v.origin[1]) / v.cell[1]); cy <= int((4.0f - v.origin[1]) / v.cell[1]); ++cy) {
                        const double y0 = double(v.origin[1]) + double(cy) * double(v.cell[1]);
                        if (y0 < 3.0 - 1e-4 || y0 + double(v.cell[1]) > 4.0 + 1e-4) continue;   // the band's whole rows
                        double want;
                        if (c.open) {
                            const double c1 = double(v.coverageN[at(cy, 1.0f) + 2]);   // the near quad looks -z
                            const double c2 = double(v.coverageP[at(cy, 0.5f) + 2]);   // the far one +z
                            want = (1.0 - c1) * (1.0 - c2);
                        } else {
                            const double cf = double(v.coverageP[at(cy, 0.6f) + 2]);   // the box's face the ray enters
                            want = 1.0 - cf;
                        }
                        const double reading = lit > 0.0 ? litAt(cy) / lit : 0.0;
                        std::printf("%d cells, %s: the face column at y %.4f reads %.4f of the lit face (the law %.4f)\n",
                                    res, c.what, y0, reading, want);
                        worst = std::max(worst, std::fabs(reading - want));
                        worstWant = std::max(worstWant, want);
                        ++rows;
                    }
                    char msg[260];
                    std::snprintf(msg, sizeof msg, "%d cells, %s: the march's transmittance is the coverage's law on %d rows "
                                  "(worst %.4f; bar 0.01; the law %s)", res, c.what, rows, worst,
                                  c.open ? "(1 - c1)(1 - c2), 0.16 at 0.6" : "1 - c, 0.4 at 0.6");
                    CHECK(lit > 0.0 && rows > 0 && worst <= 0.01 &&
                              (c.open ? worstWant <= 0.2 : worstWant >= 0.35 && worstWant <= 0.45), msg);
                }
                for (NodeId nd : added) s->removeNode(nd);
            }
        }
        // the slab: its plate face at z 1.5, 0.02 m thick
        s->setNodeTransform(wallNode, Vec3(0.0f, 4.05f, 1.51f), Quat(), Vec3(12.0f, 7.9f, 0.02f));
        const struct { const char *what; float nearP, farP; float dz; } arms[3] = {
            { "at rest, the plate's face lit 1", 3.0f, 0.75f, 0.0f },
            { "the suns swapped, the plate's face lit 0.25", 0.75f, 3.0f, 0.0f },
            { "moved 0.05 m, the plate's face lit 0.25", 0.75f, 3.0f, 0.05f } };
        if (!veils)
        for (const auto &a : arms) {
            sunPower(nearSun, a.nearP);
            sunPower(farSun, a.farP);
            s->setNodeTransform(wallNode, Vec3(0.0f, 4.05f, 1.51f + a.dz), Quat(), Vec3(12.0f, 7.9f, 0.02f));
            s->refreshGlobalIllumination();   // the host's settle (lights and geometry moved)
            double face = 0.0, lobe = 0.0;
            measure(face, lobe);
            const double law = (lobe / lobeWall) / (face / faceWall);
            char msg[260];
            std::snprintf(msg, sizeof msg, "%d cells, the slab %s: the lobe reads the face it looks at (lobe %.4f / wall's "
                          "%.4f = %.4f; face %.4f / wall's %.4f = %.4f; law %.4f, bar 3 %%)", res, a.what, lobe, lobeWall,
                          lobe / lobeWall, face, faceWall, face / faceWall, law);
            CHECK(std::fabs(law - 1.0) <= 0.03, msg);
        }
        s->setGlobalIllumination(off);
        rv->setScene(nullptr);
        e->destroyScene(s);
    }
    if (!junction && !veils) {
        // THE LOW TIER'S OWN CHAIN (two 64^3 cascades): its light store and the mip dispatches its
        // build and settle make. The total's chain exists only for the specular SDF skip, which
        // runs on a one-cascade chain only; a shipped chain allocates and regenerates none.
        Scene *s = e->createScene("underread-low");
        CHECK(rv->setScene(s), "the view shows the Low chain's scene");
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        PbrParams wall; wall.albedo = Colour(0, 0, 0); wall.roughness = 1.0f;
        wall.emissive = Colour(float(L), float(L), float(L));
        const NodeId nd = s->createNode();
        s->attachMesh(nd, cube, s->createPbrMaterial(wall));
        s->setNodeTransform(nd, Vec3(0.0f, 4.05f, 1.75f), Quat(), Vec3(12.0f, 7.9f, 0.5f));
        enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.55f, -2.5f), Vec3(0.0f, 0.1f, 0.0f));
        const unsigned long long mips0 = photonDispatchStatus().mipDispatches;
        GiParams low;
        low.mode = GiMode::Vct;
        low.quality = GiQuality::Low;
        low.numBounces = 1;
        low.gather = GiToggle::Off;
        s->setGlobalIllumination(low);
        enginetest::frames(e, 20);
        for (int f = 0; f < 4000 && !s->published().gi.giAtRest; ++f) enginetest::frames(e, 1);
        const unsigned long long mips = photonDispatchStatus().mipDispatches - mips0;
        std::vector<TextureMemoryEntry> tex;
        unsigned long long bytes = 0; int count = 0;
        if (e->textureMemory(tex))
            for (const TextureMemoryEntry &t : tex)
                if (t.name.rfind("VctLighting", 0) == 0 || t.name.rfind("VCT/", 0) == 0) { bytes += t.bytes; ++count; }
        const size_t cascades = s->published().gi.cascades.size();
        std::printf("Low's own chain: %zu cascades, the light store holds %.2f MB in %d textures; %llu mip "
                    "dispatches from the build to rest\n", cascades, double(bytes) / (1024.0 * 1024.0), count, mips);
        CHECK(cascades == 2u, "the Low tier's chain is two cascades");
        s->setGlobalIllumination(GiParams());
        rv->setScene(nullptr);
        e->destroyScene(s);
    }
    e->destroyView(rv);
    if (!junction && !veils) {
        // ONE READER AT EVERY TIER: Low reads Medium's value on every arm (the same store, the same law)
        double worst = 0.0;
        for (size_t k = 0; k < readings[0].size() && k < readings[1].size(); ++k)
            worst = std::max(worst, std::fabs(readings[0][k] / readings[1][k] - 1.0));
        char msg[200];
        std::snprintf(msg, sizeof msg, "Low reads Medium's value on all %zu arms (worst %.6f; bar 1e-4: one path)",
                      readings[0].size(), worst);
        CHECK(readings[0].size() == 12u && readings[1].size() == 12u && worst <= 1e-4, msg);
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
