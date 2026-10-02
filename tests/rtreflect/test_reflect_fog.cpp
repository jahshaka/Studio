// gi.reflect_fog — A REFLECTION IS FOGGED ALONG ITS OWN PATH (PHOTON-I-1 fix 5).
//
// THE PHYSICS. A camera looks into a flat mirror; the mirror shows an emissive wall
// behind the camera. The light the eye receives left the wall, crossed the medium to
// the mirror (the reflection ray's length), reflected, and crossed the medium again
// to the eye (the primary ray's length). For an exponential medium the transmittances
// of the two legs multiply and the in-scatter composes, so the eye sees EXACTLY what a
// camera at the mirror's VIRTUAL eye would see looking straight at the wall through
// the same total length with the mirror taken away. The suite holds the two to each
// other: the mirror arm (camera A at z = +2 facing a metal mirror, roughness 0) against
// the direct arm (camera B at the virtual eye, z = -2, facing the wall, mirror hidden),
// the same pixels, under four media — none (the control: the mirror's own reflectance
// and the ray tier's answer for the wall), the World fog (FogDesc, homogeneous), the
// height fog (HeightFogDesc), and the World fog under the planet's atmosphere (the
// air's aerial perspective and the fog towards the sky's own radiance) — for a STILL
// wall (the hit is a card's or a voxel's, fogged by the trace) and a MOVING one (the
// hit is a record the decode shades, fogged by the write-back).
//
// The wall is behind camera A, so the screen march has nothing to find: the ray tier
// answers every mirror pixel (an off-screen hit).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), __VA_ARGS__);                         \
        if (cond) std::printf("ok: %s\n", buf_);                                \
        else { std::printf("FAIL: %s\n", buf_); ++failures; }                   \
    } while (0)

namespace {
const unsigned kSize = 256u;
const float kWallZ = 30.0f;      // the wall's mirror-facing side, metres from the mirror
const float kEyeZ = 2.0f;        // camera A in front of the mirror

void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

struct Rgb { double r = 0, g = 0, b = 0; };
Rgb centre(View *v)
{
    ImageF img;
    Rgb out;
    if (!v->readPixelsHdr(img)) return out;
    int n = 0;
    for (unsigned y = kSize / 2 - 8; y < kSize / 2 + 8; ++y)
        for (unsigned x = kSize / 2 - 8; x < kSize / 2 + 8; ++x) {
            const Colour c = img.at(x, y);
            out.r += c.r; out.g += c.g; out.b += c.b; ++n;
        }
    out.r /= n; out.g /= n; out.b /= n;
    return out;
}
}  // namespace

int main(int argc, char **argv)
{
    const bool cost = argc > 1 && std::string(argv[1]) == "--cost";
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-reflect-fog-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = cost ? e->createOffscreenView("reflectfog-cost", 1920, 1080, Colour(0, 0, 0))
                      : e->createOffscreenView("reflectfog", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("reflectfog");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: no ray queries on this machine — gi.reflect_fog is about the ray tier; skipping\n");
        return 0;
    }
    view->setOffscreenContract(OffscreenContract::StillPicture);

    // A dark constant sky: the fog's in-scatter colour is the authored grey below,
    // the sky adds nothing the two arms do not share.
    const unsigned char px[4] = { 8, 8, 8, 255 };
    SkyDesc sky;
    sky.mode = SkyMode::Equirectangular;
    sky.equirect = s->createTexture(1, 1, px, true);
    s->setSky(sky);
    s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));

    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    // THE WALL: emissive red, matte black (F0 0): its radiance is its emission alone.
    PbrParams wp;
    wp.albedo = Colour(0, 0, 0);
    wp.workflow = PbrParams::Workflow::Specular;
    wp.ior = 1.0f;
    wp.specularColour = Colour(0, 0, 0);
    wp.roughness = 1.0f;
    wp.emissive = Colour(1.0f, 0.15f, 0.05f);
    const MaterialId wallMat = s->createPbrMaterial(wp);
    NodeId walls[2];
    for (int k = 0; k < 2; ++k) {
        walls[k] = s->createNode();
        if (k) s->setNodeMovable(walls[k], true);   // told BEFORE its geometry
        s->attachMesh(walls[k], cube, wallMat);
        enginetest::setNodeScale(s, walls[k], Vec3(40.0f, 40.0f, 0.2f));
        enginetest::setNodePosition(s, walls[k], Vec3(0.0f, 0.0f, kWallZ + 0.1f));
    }
    // THE MIRROR: metal, albedo 1, roughness 0, its face at z = 0.
    PbrParams mp; mp.albedo = Colour(1, 1, 1); mp.metalness = 1.0f; mp.roughness = 0.0f;
    const NodeId mirror = s->createNode();
    s->attachMesh(mirror, cube, s->createPbrMaterial(mp));
    enginetest::setNodeScale(s, mirror, Vec3(6.0f, 6.0f, 0.1f));
    enginetest::setNodePosition(s, mirror, Vec3(0.0f, 0.0f, -0.05f));

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 40.0f, 64, 0.0f };
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.ssao = false;
    fx.hdrReadback = true;
    view->setPostFx(fx);

    // THE COST (--cost; under scripts/gpu-exclusive.sh with locked clocks): the World
    // fog and the height fog on, the mirror filling a 1080p view, the reflection's own
    // timestamps (trace + decode + filter) with the fog along it and without
    // (JAHSHAKA_REFLECT_FOG_OFF, read per frame), alternating 30-frame blocks in ONE
    // process, each block's first 10 frames skipped (the timestamps come back late);
    // the still wall and the moving one (the write-back's share).
    if (cost) {
        FogDesc f; f.enabled = true; f.colour = Colour(0.2f, 0.2f, 0.2f); f.density = 0.04f;
        s->setFog(f);
        HeightFogDesc h; h.enabled = true; h.density = 0.04f;
        s->setHeightFog(h);
        s->setNodeVisible(mirror, true);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 0.0f));
        for (int which = 0; which < 2; ++which) {
            s->setNodeVisible(walls[0], which == 0);
            s->setNodeVisible(walls[1], which == 1);
            render(e, 120);
            double sum[2] = { 0, 0 };
            int n[2] = { 0, 0 };
            for (int round = 0; round < 24; ++round)
                for (int arm = 0; arm < 2; ++arm) {
                    if (arm) setenv("JAHSHAKA_REFLECT_FOG_OFF", "1", 1); else unsetenv("JAHSHAKA_REFLECT_FOG_OFF");
                    for (int i = 0; i < 30; ++i) {
                        if (which) enginetest::setNodePosition(s, walls[1], Vec3(0.0f, 0.001f * float(i & 1), kWallZ + 0.1f));
                        e->renderOneFrame();
                        const RayQueryStatus st = s->rayQueryStatus();
                        if (i >= 10 && st.reflectMs > 0.0f) { sum[arm] += st.reflectMs; ++n[arm]; }
                    }
                }
            unsetenv("JAHSHAKA_REFLECT_FOG_OFF");
            std::printf("COST 1920x1080, %s: the reflection %.4f ms with the fog along it (n %d), %.4f ms without "
                        "(n %d)\n", which ? "a moving wall (records)" : "a still wall", n[0] ? sum[0] / n[0] : -1.0,
                        n[0], n[1] ? sum[1] / n[1] : -1.0, n[1]);
        }
        return 0;
    }
    struct Medium { const char *name; bool world, height, atmosphere; };
    const Medium media[4] = { { "no medium (control)", false, false, false },
                              { "World fog (homogeneous)", true, false, false },
                              { "height fog", false, true, false },
                              { "World fog under the atmosphere", true, false, true } };
    SkyDesc atmo;
    atmo.mode = SkyMode::Atmosphere;
    atmo.atmosphere.hasSun = true;
    atmo.atmosphere.sunDir[0] = 0.6f; atmo.atmosphere.sunDir[1] = 0.8f; atmo.atmosphere.sunDir[2] = 0.0f;
    atmo.atmosphere.aerialScale = 50.0f;   // the air thick enough to read over 32 m
    for (int which = 0; which < 2; ++which)
    for (const Medium &m : media) {
        s->setNodeVisible(walls[0], which == 0);
        s->setNodeVisible(walls[1], which == 1);
        s->setSky(m.atmosphere ? atmo : sky);
        FogDesc f;
        f.enabled = m.world;
        f.colour = Colour(0.2f, 0.2f, 0.2f);
        f.density = 0.04f;               // exp2 per metre: 2^-(0.04 x 32) = 0.41 over the path
        f.breakFalloff = 0.0f;           // pure exponential (no brightness breakthrough)
        s->setFog(f);
        HeightFogDesc h;
        h.enabled = m.height;
        h.density = 0.04f;
        h.heightFalloff = 0.02f;
        h.baseHeight = 0.0f;
        s->setHeightFog(h);

        // ARM A: the mirror, from in front of it.
        s->setNodeVisible(mirror, true);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, kEyeZ), Vec3(0.0f, 0.0f, 0.0f));
        render(e, 120);
        const Rgb a = centre(view);
        // ARM B: the virtual eye, the mirror taken away.
        s->setNodeVisible(mirror, false);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, -kEyeZ), Vec3(0.0f, 0.0f, kWallZ));
        render(e, 120);
        const Rgb b = centre(view);
        const double rel = std::max({ std::fabs(a.r - b.r) / std::max(b.r, 1e-4),
                                      std::fabs(a.g - b.g) / std::max(b.g, 1e-4),
                                      std::fabs(a.b - b.b) / std::max(b.b, 1e-4) });
        const char *wallName = which ? "a MOVING wall (a decoded record)" : "a still wall (a card or voxel hit)";
        std::printf("== %s, %s: mirror (%.4f %.4f %.4f)  direct from the virtual eye (%.4f %.4f %.4f)  worst "
                    "channel %.1f %%\n", m.name, wallName, a.r, a.g, a.b, b.r, b.g, b.b, 100.0 * rel);
        if (m.world || m.height)
            CHECK_MSG(rel < 0.05, "%s, %s: the mirror's reflection is fogged along its own path, as the virtual eye "
                      "sees the wall (%.1f %%, bar 5 %%)", m.name, wallName, 100.0 * rel);
        else
            CHECK_MSG(rel < 0.05, "%s, %s: the mirror shows the wall as the virtual eye sees it (%.1f %%, bar 5 %%)",
                      m.name, wallName, 100.0 * rel);
    }
    view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
