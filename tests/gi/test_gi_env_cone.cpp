// gi.env_cone — THE ENVIRONMENT'S CONE LOOKUP, MEASURED (PHOTON-ENV-1).
//
// Every escape in the renderer reads the sky through ONE function,
// jahEnvCone( dir, tan(half-angle) ) (src/rayquery/include/jah_environment.glsl):
// ONE fetch of the GGX-prefiltered sky chain at the mip whose lobe matches the
// cone. A GGX lobe is not a box, so this is an approximation — the mapping
// picks the lobe whose mean of (1 - cos) about its axis is 0.6 of the uniform
// cone's (the factor measured HERE: 1.0 is the band-1-exact match, but a GGX
// lobe's heavy tail then reaches the horizon glow from a zenith cone; the
// derivation and the sweep are in jah_environment.glsl). This suite measures
// the lookup's error on the SHIPPED
// sky (the default scene's analytic atmosphere) against the thing the lookup
// stands in for: the mean of the cube's finest mip over 64 directions spread
// uniformly over the cone's solid angle, computed in the same job
// (Engine::environmentCones).
//
// THREE APERTURES, the ones the renderer asks for: the six-cone diffuse set's
// (tan 0.577), the irradiance field's probe ray (tan(2 pi / 144), 144 rays per
// probe), and a 0.1 rad specular cone. BAR: the mean relative luminance error
// over 26 directions below 5 % at each, at two sun heights (noon-ish and a low
// sun, whose horizon glow is the sky's sharpest feature).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static double lum(const float c[3]) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-env-cone-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("envcone", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("envcone");
    view->setScene(scene);

    // 26 directions: the six axes, the twelve edge diagonals, the eight corners.
    std::vector<Vec3> dirs;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z) {
                if (!x && !y && !z) continue;
                const float l = std::sqrt(float(x * x + y * y + z * z));
                dirs.push_back(Vec3(x / l, y / l, z / l));
            }

    struct Aperture { const char *name; float tan; };
    const Aperture apertures[] = {
        { "six-cone diffuse (tan 0.577)", 0.577f },
        { "field probe ray (tan 2pi/144)", std::tan(6.28318531f / 144.0f) },
        { "specular 0.1 rad", std::tan(0.1f) },
        { "four-cone diffuse (tan 0.983), printed", 0.98269f },
    };
    const float kBar = 0.05f;

    for (float elevDeg : { 35.0f, 5.0f }) {
        SkyDesc sky;
        sky.mode = SkyMode::Atmosphere;
        sky.atmosphere.hasSun = true;
        const float rad = elevDeg * 3.14159265f / 180.0f;
        sky.atmosphere.sunDir[0] = std::cos(rad);
        sky.atmosphere.sunDir[1] = std::sin(rad);
        sky.atmosphere.sunDir[2] = 0.0f;
        CHECK(scene->setSky(sky), "the analytic sky binds");
        // The capture runs inside a frame and the convolution lands at the top
        // of the next: wait for the cube in FRAMES.
        std::vector<EnvironmentConeAnswer> probe;
        const std::vector<EnvironmentConeQuery> one = { EnvironmentConeQuery{ Vec3(0, 1, 0), 0.577f } };
        bool ready = false;
        for (int f = 0; f < 30 && !ready; ++f) {
            e->renderOneFrame();
            if (f >= 4) ready = e->environmentCones(scene, one, probe);
        }
        CHECK(ready, "the environment cube exists (the sky's capture and convolution landed)");
        if (!ready) { std::printf("   %s\n", e->takeLastError().c_str()); continue; }
        std::printf("\n== sun elevation %.0f deg ==\n", elevDeg);
        for (const Aperture &a : apertures) {
            std::vector<EnvironmentConeQuery> q;
            for (const Vec3 &d : dirs) q.push_back(EnvironmentConeQuery{ d, a.tan });
            std::vector<EnvironmentConeAnswer> ans;
            const bool ok = e->environmentCones(scene, q, ans);
            CHECK(ok, "the cone harness ran");
            if (!ok) { std::printf("   %s\n", e->takeLastError().c_str()); continue; }
            double sumRel = 0.0, worst = 0.0;
            size_t counted = 0, worstIdx = 0;
            for (size_t i = 0; i < ans.size(); ++i) {
                const double ref = lum(ans[i].reference);
                if (ref <= 1e-6) continue;      // a black direction has no relative error
                const double rel = std::fabs(lum(ans[i].lookup) - ref) / ref;
                sumRel += rel;
                ++counted;
                if (rel > worst) { worst = rel; worstIdx = i; }
            }
            const double mean = counted ? sumRel / double(counted) : 1.0;
            std::printf("   %-40s lod %.2f  mean |err| %.2f %%  worst %.2f %% (dir %.2f %.2f %.2f: "
                        "lookup %.4f ref %.4f)  over %zu dirs\n",
                        a.name, double(ans[0].lod), 100.0 * mean, 100.0 * worst,
                        double(dirs[worstIdx].x), double(dirs[worstIdx].y), double(dirs[worstIdx].z),
                        lum(ans[worstIdx].lookup), lum(ans[worstIdx].reference), counted);
            if (std::string(a.name).find("printed") != std::string::npos) continue;
            char msg[160];
            std::snprintf(msg, sizeof msg,
                          "the cone lookup is within %.0f %% of the cone integral (%s, sun %.0f deg)",
                          100.0 * kBar, a.name, elevDeg);
            CHECK(counted == dirs.size() && mean < kBar, msg);
        }
        // THE SKY'S IRRADIANCE AT AN UPWARD NORMAL, three ways, PRINTED (the
        // measurement behind the field-against-cones step of gi.chain_face): the
        // TRUTH is the cosine-weighted mean of the cube's radiance over the upper
        // hemisphere (64 cosine-stratified directions, each a 0.05 cone's
        // reference); against it the nine-band SH's irradiance / pi at +Y and the
        // pixel's six-cone set (weights .25 / 5 x .15, tan 0.577) through the lookup.
        {
            std::vector<EnvironmentConeQuery> q;
            const double g = 2.39996323;
            for (int k = 0; k < 64; ++k) {
                const double u = (k + 0.5) / 64.0, c = std::sqrt(1.0 - u), sn = std::sqrt(u);
                q.push_back(EnvironmentConeQuery{ Vec3(float(sn * std::cos(g * k)), float(c),
                                                       float(sn * std::sin(g * k))), 0.05f });
            }
            std::vector<EnvironmentConeAnswer> ans;
            e->environmentCones(scene, q, ans);
            double truth = 0.0;
            for (const auto &x : ans) truth += lum(x.reference);
            truth /= double(ans.size() ? ans.size() : 1);
            const double six[6][3] = { { 0, 1, 0 }, { 0.866025, 0.5, 0 }, { 0.267617, 0.5, 0.823639 },
                                       { -0.700629, 0.5, 0.509037 }, { -0.700629, 0.5, -0.509037 },
                                       { 0.267617, 0.5, -0.823639 } };
            const double w6[6] = { 0.25, 0.15, 0.15, 0.15, 0.15, 0.15 };
            std::vector<EnvironmentConeQuery> q6;
            for (const auto &d : six)
                q6.push_back(EnvironmentConeQuery{ Vec3(float(d[0]), float(d[1]), float(d[2])), 0.577f });
            std::vector<EnvironmentConeAnswer> a6;
            e->environmentCones(scene, q6, a6);
            double cones = 0.0;
            for (size_t i = 0; i < a6.size(); ++i) cones += w6[i] * lum(a6[i].lookup);
            float sh[27] = { 0 };
            scene->skyAmbientSh(sh);
            // +Y: 1, y = 1, z = 0, x = 0 -> c0 + c1 + c6 (3z^2 - 1 = -1) x -1 + c8 (x^2 - y^2 = -1) x -1
            float shUp[3];
            for (int c = 0; c < 3; ++c) shUp[c] = sh[c] + sh[3 + c] - sh[18 + c] - sh[24 + c];
            std::printf("   IRRADIANCE AT +Y (luminance, radiance units): truth %.4f | SH %.4f (%.2fx) "
                        "| six cones %.4f (%.2fx)\n", truth, lum(shUp), lum(shUp) / truth, cones,
                        cones / truth);
        }
    }

    // ===========================================================================
    // THE SPECULAR ENVIRONMENT, OUTSIDE AND INSIDE A VOXEL VOLUME, AT ONE ROUGHNESS
    // (PHOTON-ENV-1, audit F3). The convolution stores perceptual roughness r at
    // mip (N - 1) r (2 - r) (CompositorPassIblSpecular::lodToPerceptualRoughness);
    // HlmsPbs's envSpecularRoughness read N r (2 - r) — one mip rougher — until
    // this round, and jah_environment.glsl's jahEnvLobe reads (N - 1) r (2 - r).
    // Inside a voxel volume the specular environment is exactly ONE term: the
    // datablock's cube where it carries one (HlmsPbs's CubemapGlobal), the cone's
    // escape where it does not — the cone's escape was added ON TOP of the cube
    // before (measured 1.95 / 1.60 / 1.81x at roughness 0.25 / 0.5 / 0.8). A white
    // metal plate facing a camera straight above it reflects the zenith: outside
    // any volume and inside one (nothing above it) the pixel must agree within 1 %
    // at every roughness — a rougher-by-one-mip reader or a second copy of the sky
    // is a specular step at the volume's face.
    // ===========================================================================
    {
        SkyDesc sky;
        sky.mode = SkyMode::Atmosphere;
        sky.atmosphere.hasSun = true;
        sky.atmosphere.sunDir[0] = std::cos(0.61f);
        sky.atmosphere.sunDir[1] = std::sin(0.61f);
        sky.atmosphere.sunDir[2] = 0.0f;
        scene->setSky(sky);
        for (int f = 0; f < 10; ++f) e->renderOneFrame();
        float sh[27] = { 0 };
        scene->skyAmbientSh(sh);
        scene->setAmbientSh(sh);
        scene->setEnvironmentLight(Colour(1, 1, 1, 1));
        const MeshId cube = scene->createMesh(enginetest::unitCubeMesh());
        const NodeId plate = scene->createNode();
        scene->setNodeTransform(plate, Vec3(0.0f, -0.05f, 0.0f), Quat(), Vec3(4.0f, 0.1f, 4.0f));
        enginetest::testCameraLookAt(view, Vec3(0.02f, 3.0f, 0.02f), Vec3(0.0f, 0.0f, 0.0f));
        const auto centre = [&]() {
            double acc = 0.0;
            for (int f = 0; f < 4; ++f) {
                e->renderOneFrame();
                Image img;
                view->readPixels(img);
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
        for (float rough : { 0.1f, 0.25f, 0.5f, 0.8f }) {
            PbrParams p;
            p.albedo = Colour(1, 1, 1);
            p.metalness = 1.0f;
            p.roughness = rough;
            scene->attachMesh(plate, cube, scene->createPbrMaterial(p));
            GiParams off; off.mode = GiMode::Off;
            scene->setGlobalIllumination(off);
            for (int f = 0; f < 6; ++f) e->renderOneFrame();
            const double outside = centre();
            GiParams vct;
            vct.mode = GiMode::Vct;
            vct.quality = GiQuality::Low;          // isotropic: the escape is the composite
            vct.numBounces = 1;
            vct.ddgi = GiToggle::Off;
            vct.gather = GiToggle::Off;
            vct.testBoundsMin = Vec3(-4.0f, -2.0f, -4.0f);
            vct.testBoundsMax = Vec3(4.0f, 4.0f, 4.0f);
            scene->setGlobalIllumination(vct);
            for (int f = 0; f < 20; ++f) e->renderOneFrame();
            const double inside = centre();
            std::printf("   SPECULAR ENVIRONMENT at roughness %.2f: outside a volume %.4f, inside "
                        "%.4f (%.3fx)\n", rough, outside, inside, inside / outside);
            char msg[200];
            std::snprintf(msg, sizeof msg, "one roughness, one mip: the volume's specular escape "
                          "reads what PBS reads outside it within 1 %% (roughness %.2f: %.4f / %.4f)",
                          rough, inside, outside);
            CHECK(std::fabs(inside / outside - 1.0) < 0.01, msg);
            // THE SAME PLATE WITH THE FIELD ON (PHOTON-VOXEL-5; the VOXEL-4 audit's F4): a rough
            // lobe's occluded share then reads the FIELD's diffuse estimate, whose atlas holds
            // the voxels' light and the sky's as one sum - applyVctRoughSpecular subtracts the
            // analytic sky times the SPECULAR query's transmittance from it. Open sky above the
            // plate: whatever that subtraction leaves is a second copy of the sky or a lost one.
            GiParams field = vct;
            field.ddgi = GiToggle::On;
            scene->setGlobalIllumination(field);
            for (int f = 0; f < 20; ++f) e->renderOneFrame();
            for (int f = 0; f < 4000 && !scene->giStatus().giAtRest; ++f) e->renderOneFrame();
            const double withField = centre();
            std::printf("   SPECULAR ENVIRONMENT at roughness %.2f, THE FIELD ON: inside %.4f (%.3fx of outside)\n",
                        rough, withField, withField / outside);
            std::snprintf(msg, sizeof msg, "the field on: the volume's specular escape reads what PBS reads outside "
                          "it within 1 %% (roughness %.2f: %.4f / %.4f)", rough, withField, outside);
            CHECK(std::fabs(withField / outside - 1.0) < 0.01, msg);
        }
        GiParams off; off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
    }

    // THE ROUGH LOBE IN A SEALED ROOM (PHOTON-VOXEL-5, F4b). A lobe wider than tan 1 reads its
    // occluded share from the diffuse estimator's VOXEL light (applyVctRoughSpecular) - never an
    // environment cube. THE FURNACE is its closed form: a sealed box whose every inner face
    // emits radiance L (black albedo: no bounce), no sky, no environment light, no ambient. The
    // rough metal plate inside sees radiance L in every direction, so it must read exactly what
    // the same plate reads under a UNIFORM SKY of radiance L outside any volume (the split-sum's
    // specular albedo times L, both ways). GI off it must read nothing (no cube stands in for
    // the room). The cones and the field are both asserted. Scene radiance, float readback.
    // THE BAR: 3 % - the four cones' coverage of the box's corners and edges at Low's cells (a
    // cone whose composite stops short of 0.95 lets the (empty) environment in) and the
    // half-float store; measured 0.987x (the cones), 0.999x (the field), 0.0000 GI off.
    // (VOXEL-5's first form of this arm read 0.3400 in every arm: its view was never switched -
    // View::setScene refuses a second scene until the first is detached - and it measured the
    // open plate. Every setScene here is checked.)
    {
        View *rv = e->createOffscreenView("envcone-room", 64, 64, Colour(0, 0, 0));
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        fx.hdr = false;
        fx.hdrReadback = true;
        rv->setPostFx(fx);
        const auto readHdr = [&](View *v) {
            double acc = 0.0;
            for (int f = 0; f < 4; ++f) {
                e->renderOneFrame();
                ImageF img;
                if (!v->readPixelsHdr(img)) return -1.0;
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
        const double L = std::pow((kSkyByte / 255.0 + 0.055) / 1.055, 2.4);   // the byte's linear radiance
        PbrParams metal; metal.albedo = Colour(1, 1, 1); metal.metalness = 1.0f; metal.roughness = 0.8f;
        // THE REFERENCE: the plate under a uniform sky of radiance L, no volume.
        double reference = -1.0;
        {
            Scene *open = e->createScene("envcone-uniform");
            CHECK(rv->setScene(open), "the reference view shows the uniform-sky scene");
            const unsigned char skyPx[4] = { kSkyByte, kSkyByte, kSkyByte, 255 };
            SkyDesc sky;
            sky.mode = SkyMode::Equirectangular;
            sky.equirect = open->createTexture(1, 1, skyPx, true);
            CHECK(sky.equirect && open->setSky(sky), "the uniform sky binds");
            float sh[27] = { 0 };
            for (int f = 0; f < 20 && !open->skyAmbientSh(sh); ++f) e->renderOneFrame();
            open->setAmbientSh(sh);
            open->setEnvironmentLight(Colour(1, 1, 1, 1));
            const NodeId nd = open->createNode();
            open->attachMesh(nd, open->createMesh(enginetest::unitCubeMesh()), open->createPbrMaterial(metal));
            open->setNodeTransform(nd, Vec3(0.0f, 0.05f, 0.0f), Quat(), Vec3(4.0f, 0.1f, 4.0f));
            enginetest::testCameraLookAt(rv, Vec3(0.02f, 3.0f, 0.02f), Vec3(0.0f, 0.0f, 0.0f));
            for (int f = 0; f < 10; ++f) e->renderOneFrame();
            reference = readHdr(rv);
            rv->setScene(nullptr);
            e->destroyScene(open);
        }
        Scene *room = e->createScene("envcone-furnace");
        CHECK(rv->setScene(room), "the view shows the sealed furnace");
        {
            SkyDesc none;
            none.mode = SkyMode::NoSky;
            room->setSky(none);
            room->setEnvironmentLight(Colour(0, 0, 0, 1));
        }
        room->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        const MeshId cubeR = room->createMesh(enginetest::unitCubeMesh());
        const auto slab = [&](const Vec3 &pos, const Vec3 &scale, const PbrParams &p) {
            const NodeId nd = room->createNode();
            room->attachMesh(nd, cubeR, room->createPbrMaterial(p));
            room->setNodeTransform(nd, pos, Quat(), scale);
        };
        PbrParams wall; wall.albedo = Colour(0, 0, 0); wall.roughness = 1.0f;
        wall.emissive = Colour(float(L), float(L), float(L));
        slab(Vec3(0, 5.25f, 0), Vec3(9.0f, 0.5f, 9.0f), wall);
        slab(Vec3(0, -0.25f, 0), Vec3(9.0f, 0.5f, 9.0f), wall);
        slab(Vec3(-4.25f, 2.5f, 0), Vec3(0.5f, 5.0f, 9.0f), wall);
        slab(Vec3(4.25f, 2.5f, 0), Vec3(0.5f, 5.0f, 9.0f), wall);
        slab(Vec3(0, 2.5f, -4.25f), Vec3(9.0f, 5.0f, 0.5f), wall);
        slab(Vec3(0, 2.5f, 4.25f), Vec3(9.0f, 5.0f, 0.5f), wall);
        slab(Vec3(0.0f, 0.05f, 0.0f), Vec3(4.0f, 0.1f, 4.0f), metal);
        enginetest::testCameraLookAt(rv, Vec3(0.02f, 3.0f, 0.02f), Vec3(0.0f, 0.0f, 0.0f));
        GiParams none; none.mode = GiMode::Off;
        room->setGlobalIllumination(none);
        for (int f = 0; f < 6; ++f) e->renderOneFrame();
        const double viaNone = readHdr(rv);
        GiParams cones;
        cones.mode = GiMode::Vct;
        cones.quality = GiQuality::Low;
        cones.numBounces = 1;
        cones.ddgi = GiToggle::Off;
        cones.gather = GiToggle::Off;
        cones.testBoundsMin = Vec3(-4.5f, -0.5f, -4.5f);
        cones.testBoundsMax = Vec3(4.5f, 5.5f, 4.5f);
        room->setGlobalIllumination(cones);
        for (int f = 0; f < 4000 && !room->giStatus().giAtRest; ++f) e->renderOneFrame();
        const double viaCones = readHdr(rv);
        GiParams field = cones;
        field.ddgi = GiToggle::On;
        room->setGlobalIllumination(field);
        for (int f = 0; f < 20; ++f) e->renderOneFrame();
        for (int f = 0; f < 4000 && !room->giStatus().giAtRest; ++f) e->renderOneFrame();
        const double viaField = readHdr(rv);
        std::printf("   ROUGH METAL (0.80) IN A SEALED FURNACE of radiance L = %.4f: the plate under a uniform sky "
                    "of L %.4f; in the furnace GI off %.4f, the cones %.4f (%.3fx), the field %.4f (%.3fx)\n", L,
                    reference, viaNone, viaCones, viaCones / reference, viaField, viaField / reference);
        CHECK(reference > 0.01, "the reference plate reads the uniform sky");
        CHECK(viaNone >= 0.0 && viaNone < 0.002 * reference,
              "GI off, the sealed room's plate reads NO environment cube (nothing stands in for the room)");
        char rmsg[240];
        std::snprintf(rmsg, sizeof rmsg, "THE FURNACE, the cones: the rough lobe reads the room's voxel light as the "
                      "plate reads a uniform sky of the same radiance (%.4f / %.4f; bar 3 %%)", viaCones, reference);
        CHECK(std::fabs(viaCones / reference - 1.0) <= 0.03, rmsg);
        std::snprintf(rmsg, sizeof rmsg, "THE FURNACE, the field: the same (%.4f / %.4f; bar 3 %%)", viaField, reference);
        CHECK(std::fabs(viaField / reference - 1.0) <= 0.03, rmsg);
        GiParams off; off.mode = GiMode::Off;
        room->setGlobalIllumination(off);
        rv->setScene(nullptr);
        e->destroyScene(room);
        e->destroyView(rv);
    }

    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
