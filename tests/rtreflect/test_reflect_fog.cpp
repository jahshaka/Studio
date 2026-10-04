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
// answers every mirror pixel (an off-screen hit). A FIFTH medium is the height fog
// with a START DISTANCE past the mirror (16 m, the eye 2 m off it): the colour pass
// fogs nothing of the eye's leg, and the reflection's leg starts where the eye's
// 16 m end — never a second 16 m from the mirror.
//
// ONE LAW ON EVERY REFLECTION PATH. The SCREEN arm: a mirror turned so that what it
// shows is ON the screen beside it (a cube 40 degrees from the mirror, a 110-degree
// view), so the screen march answers — the screen holds the cube fogged along the
// EYE's 10 m, the reflection must carry it fogged along the virtual eye's 16.8 m
// (the resolve re-fogs a screen hit: JahSsrResolve_ps.glsl). Run in this process
// (the ray tier fills whatever the march declines) and, with JAHSHAKA_NO_RAY_QUERY=1
// (gi.reflect_fog_norays), with the march alone. The VOXEL MARCH arm
// (JAHSHAKA_NO_RAY_QUERY=1, no screen march): the mirror's specular cone through
// the voxels, under a BLACK World fog, asserted as a RATIO — the mirror fogged over
// the mirror clear against the direct view fogged over the direct view clear — so
// the voxels' own resolution of the cube cancels and only the medium is measured.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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
    const bool costFrame = argc > 1 && std::string(argv[1]) == "--cost-frame";
    const bool cost = costFrame || ( argc > 1 && std::string(argv[1]) == "--cost" );
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
    // THE NO-RAYS RUN (gi.reflect_fog_norays): JAHSHAKA_NO_RAY_QUERY=1 takes the ray
    // tier off the device, and the screen march and the voxel march are the paths.
    const bool raysWanted = !std::getenv("JAHSHAKA_NO_RAY_QUERY");
    const bool haveRays = e->rayQueryAvailable() && e->rayTracing();
    if (raysWanted && !haveRays) {
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
    // (Scene::setReflectionFogEnabled, the door), alternating 30-frame blocks in ONE
    // process, each block's first 10 frames skipped (the timestamps come back late);
    // the still wall and the moving one (the write-back's share).
    if (cost && !costFrame) {
        FogDesc f; f.enabled = true; f.colour = Colour(0.2f, 0.2f, 0.2f); f.density = 0.04f;
        s->setFog(f);
        HeightFogDesc h; h.enabled = true; h.density = 0.04f;
        s->setHeightFog(h);
        s->setNodeVisible(mirror, true);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 0.0f));
        enginetest::GpuTimingWindow gpuTiming(e);   // reflectMs is the monitor's row (lane TEST-1)
        for (int which = 0; which < 2; ++which) {
            s->setNodeVisible(walls[0], which == 0);
            s->setNodeVisible(walls[1], which == 1);
            render(e, 120);
            double sum[2] = { 0, 0 };
            int n[2] = { 0, 0 };
            for (int round = 0; round < 24; ++round)
                for (int arm = 0; arm < 2; ++arm) {
                    s->setReflectionFogEnabled(arm == 0);
                    for (int i = 0; i < 30; ++i) {
                        if (which) enginetest::setNodePosition(s, walls[1], Vec3(0.0f, 0.001f * float(i & 1), kWallZ + 0.1f));
                        e->renderOneFrame();
                        const RayQueryStatus st = s->rayQueryStatus();
                        if (i >= 10 && st.reflectMs > 0.0f) { sum[arm] += st.reflectMs; ++n[arm]; }
                    }
                }
            s->setReflectionFogEnabled(true);
            std::printf("COST 1920x1080, %s: the reflection %.4f ms with the fog along it (n %d), %.4f ms without "
                        "(n %d)\n", which ? "a moving wall (records)" : "a still wall", n[0] ? sum[0] / n[0] : -1.0,
                        n[0], n[1] ? sum[1] / n[1] : -1.0, n[1]);
        }
        return 0;
    }
    struct Medium { const char *name; bool world, height, atmosphere; float start; };
    const Medium media[5] = { { "no medium (control)", false, false, false, 0.0f },
                              { "World fog (homogeneous)", true, false, false, 0.0f },
                              { "height fog", false, true, false, 0.0f },
                              { "World fog under the atmosphere", true, false, true, 0.0f },
                              { "height fog from 16 m", false, true, false, 16.0f } };
    SkyDesc atmo;
    atmo.mode = SkyMode::Atmosphere;
    atmo.atmosphere.hasSun = true;
    atmo.atmosphere.sunDir[0] = 0.6f; atmo.atmosphere.sunDir[1] = 0.8f; atmo.atmosphere.sunDir[2] = 0.0f;
    atmo.atmosphere.aerialScale = 50.0f;   // the air thick enough to read over 32 m
    for (int which = 0; which < 2 && haveRays && !costFrame; ++which)
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
        h.density = m.start > 0.0f ? 0.1f : 0.04f;   // thick enough that 2 m of it reads
        h.heightFalloff = 0.02f;
        h.baseHeight = 0.0f;
        h.startDistance = m.start;
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
    // ===== THE SCREEN ARM and THE VOXEL MARCH ARM (the header). =====
    {
        for (NodeId w : walls) s->setNodeVisible(w, false);
        s->setNodeVisible(mirror, false);
        s->setSky(sky);
        // E at the origin; the mirror's centre M 10 m away at -20 degrees, the cube's
        // H 10 m away at +20 degrees; the mirror's normal bisects M->E and M->H.
        const float d2r = 3.14159265f / 180.0f;
        const Vec3 M(10.0f * std::sin(-20.0f * d2r), 0.0f, 10.0f * std::cos(-20.0f * d2r));
        const Vec3 H(10.0f * std::sin(20.0f * d2r), 0.0f, 10.0f * std::cos(20.0f * d2r));
        auto norm = [](Vec3 v) { const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return Vec3(v.x / l, v.y / l, v.z / l); };
        const Vec3 a = norm(Vec3(-M.x, -M.y, -M.z)), b = norm(Vec3(H.x - M.x, H.y - M.y, H.z - M.z));
        const Vec3 n = norm(Vec3(a.x + b.x, a.y + b.y, a.z + b.z));
        const float phi = std::atan2(n.x, n.z);
        const Quat q(0.0f, std::sin(0.5f * phi), 0.0f, std::cos(0.5f * phi));
        const float dEM = -(M.x * n.x + M.y * n.y + M.z * n.z);           // (E - M) . n
        const Vec3 Ev(-2.0f * dEM * n.x, -2.0f * dEM * n.y, -2.0f * dEM * n.z);   // the virtual eye
        const NodeId mirror2 = s->createNode();
        s->attachMesh(mirror2, cube, s->createPbrMaterial(mp));
        s->setNodeTransform(mirror2, Vec3(M.x - 0.05f * n.x, M.y, M.z - 0.05f * n.z), q, Vec3(6.0f, 6.0f, 0.1f));
        const NodeId box = s->createNode();
        s->attachMesh(box, cube, wallMat);
        s->setNodeTransform(box, H, Quat(), Vec3(4.0f, 4.0f, 4.0f));
        CameraDesc camA = enginetest::testCameraDescLookAt(Vec3(0.0f, 0.0f, 0.0f), M);
        CameraDesc camB = enginetest::testCameraDescLookAt(Ev, H);
        camA.fovDegrees = camB.fovDegrees = 110.0f;
        struct ScreenMedium { const char *name; bool world, height; float start; };
        const ScreenMedium sm[4] = { { "no medium (control)", false, false, 0.0f },
                                     { "World fog (homogeneous)", true, false, 0.0f },
                                     { "height fog", false, true, 0.0f },
                                     { "height fog from 12 m", false, true, 12.0f } };
        // THE FRAME'S COST (--cost-frame, run under scripts/gpu-exclusive.sh with locked
        // clocks, an A/B of two builds): this mirror at 1080p under the World fog and
        // the height fog, the whole frame's GPU time (every pass and every compute job
        // the monitor times), 120 frames of warm-up, the median of 300.
        if (costFrame) {
            FogDesc f; f.enabled = true; f.colour = Colour(0.2f, 0.2f, 0.2f); f.density = 0.04f;
            s->setFog(f);
            HeightFogDesc h; h.enabled = true; h.density = 0.04f;
            s->setHeightFog(h);
            s->setNodeVisible(mirror2, true);
            view->setCamera(camA);
            render(e, 120);
            e->setFrameMonitor(MonitorLevel::Review);
            std::vector<double> frameMs;
            for (int i = 0; i < 300; ++i) {
                e->renderOneFrame();
                std::vector<FrameRecord> recs;
                e->takeFrameRecords(recs);
                for (const FrameRecord &r : recs) {
                    double g = 0.0;
                    bool any = false;
                    for (const FramePass &fp : r.passes)
                        if (fp.gpuMs >= 0.0f) { g += fp.gpuMs; any = true; }
                    for (const CacheWork &w : r.cacheWork)
                        if (w.gpuMs >= 0.0f) g += w.gpuMs;
                    if (any) frameMs.push_back(g);
                }
            }
            std::sort(frameMs.begin(), frameMs.end());
            std::printf("COSTFRAME 1920x1080, %s: the frame's GPU %.4f ms (median of %zu)\n",
                        raysWanted ? "rays" : "no rays", frameMs.empty() ? -1.0 : frameMs[frameMs.size() / 2],
                        frameMs.size());
            return 0;
        }
        const char *path = raysWanted ? "the screen march, the ray tier filling" : "the screen march, the voxel march filling";
        // (Without rays the march's weight on this mirror is about a third and the
        // voxel march fills the rest; with GI off the march alone reads the law's
        // first leg to 0.1-1 % — spikes/photon-i-1/r2-norays-gioff-ssr-attribution.log.)
        Rgb controlA, controlB;
        for (const ScreenMedium &m : sm) {
            FogDesc f;
            f.enabled = m.world;
            f.colour = Colour(0.2f, 0.2f, 0.2f);
            f.density = 0.1f;
            f.breakFalloff = 0.0f;
            s->setFog(f);
            HeightFogDesc h;
            h.enabled = m.height;
            h.density = 0.1f;
            h.heightFalloff = 0.02f;
            h.startDistance = m.start;
            s->setHeightFog(h);
            s->setNodeVisible(mirror2, true);
            view->setCamera(camA);
            render(e, 120);
            const Rgb ra = centre(view);
            s->setNodeVisible(mirror2, false);
            view->setCamera(camB);
            render(e, 120);
            const Rgb rb = centre(view);
            if (!m.world && !m.height) { controlA = ra; controlB = rb; }
            // AGAINST THE CONTROL: each arm over the no-medium one, the mirror's and the
            // direct view's — what the medium did — so the march's own reading of the cube
            // (its weight, the hit's texel) cancels and only the medium is compared.
            auto over = [](double x, double c) { return x / std::max(c, 1e-4); };
            const double rel = std::max({ std::fabs(over(ra.r, controlA.r) - over(rb.r, controlB.r)) / over(rb.r, controlB.r),
                                          std::fabs(over(ra.g, controlA.g) - over(rb.g, controlB.g)) / over(rb.g, controlB.g),
                                          std::fabs(over(ra.b, controlA.b) - over(rb.b, controlB.b)) / over(rb.b, controlB.b) });
            std::printf("== SCREEN, %s, %s: mirror (%.4f %.4f %.4f)  direct from the virtual eye (%.4f %.4f %.4f)  "
                        "worst channel over the control %.1f %%\n", m.name, path, ra.r, ra.g, ra.b, rb.r, rb.g, rb.b,
                        100.0 * rel);
            if (!m.world && !m.height)
                CHECK_MSG(ra.r > 0.3 * rb.r, "SCREEN, %s, %s: the mirror shows the cube (red %.4f, direct %.4f)", m.name,
                          path, ra.r, rb.r);
            else
                CHECK_MSG(rel < 0.05, "SCREEN, %s, %s: the mirror's cube crosses the medium the virtual eye's does "
                          "(over the control, worst channel %.1f %%, bar 5 %%)", m.name, path, 100.0 * rel);
        }
        if (!raysWanted) {
            // THE VOXEL MARCH: no screen march, no rays — the specular cone through the
            // voxels answers the mirror. A black World fog: transmittance alone.
            PostFxDesc vfx = fx;
            vfx.ssr = 0;
            view->setPostFx(vfx);
            double ratio[2][2] = { { 0, 0 }, { 0, 0 } };
            for (int fogOn = 0; fogOn < 2; ++fogOn) {
                FogDesc f;
                f.enabled = fogOn == 1;
                f.colour = Colour(0.0f, 0.0f, 0.0f);
                f.density = 0.1f;
                f.breakFalloff = 0.0f;
                s->setFog(f);
                s->setHeightFog(HeightFogDesc());
                s->setNodeVisible(mirror2, true);
                view->setCamera(camA);
                render(e, 120);
                ratio[0][fogOn] = centre(view).r;
                s->setNodeVisible(mirror2, false);
                view->setCamera(camB);
                render(e, 120);
                ratio[1][fogOn] = centre(view).r;
            }
            const double tm = ratio[0][1] / std::max(ratio[0][0], 1e-6);
            const double td = ratio[1][1] / std::max(ratio[1][0], 1e-6);
            std::printf("== VOXEL MARCH, black World fog: mirror %.4f -> %.4f (x %.4f), direct %.4f -> %.4f (x %.4f)\n",
                        ratio[0][0], ratio[0][1], tm, ratio[1][0], ratio[1][1], td);
            CHECK_MSG(ratio[0][0] > 0.02, "VOXEL MARCH: the mirror's specular cone shows the cube (red %.4f)",
                      ratio[0][0]);
            CHECK_MSG(std::fabs(tm - td) / td < 0.05, "VOXEL MARCH: the mirror's cone crosses the medium the virtual "
                      "eye's ray crosses (x %.4f against x %.4f, bar 5 %%)", tm, td);
            view->setPostFx(fx);
        }
    }
    view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
