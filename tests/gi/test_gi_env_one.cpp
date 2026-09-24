// gi.env_one — ONE ENVIRONMENT, FOUR READERS, ONE NUMBER (PHOTON-ENV-1).
//
// THE CLAIM. A matte surface (albedo rho, perceptual roughness 1) under a
// UNIFORM sky of radiance L, lit by nothing but the Sky Light at 1, reflects
//
//     rho * L * A(v, 1)
//
// where A is the normalised Disney diffuse lobe's DIRECTIONAL ALBEDO at the view
// angle v — the lobe the direct light is shaded with (MEASURE-1a decision b),
// integrated over the uniform sky (PHOTON-WRITER-1: the renderer's
// jahDiffuseAlbedo; this suite computes it from the lobe by direct integration,
// enginetest::disneyDiffuseAlbedo). It replaced the constant energy factor
// lerp(1, 1/1.51, r) the environment lobe carried (0.662 at r = 1, against the
// lobe's own 0.688 at normal view and 0.97 at a grazing one).
// Four readers of the environment must each give that number:
//
//   (a) OUTSIDE ANY VOLUME: HlmsPbs's SH lookup at the normal. For a uniform
//       sky the SH is exact.
//   (b) THE VOXEL CONES (no field, no gather): every cone that escapes reads
//       the environment in its own direction at its own aperture
//       (jah_environment.glsl's jahEnvCone). With nothing in the volume but
//       the fixture, the escape is (close to) 1, so (b) = (a) within 2 %.
//   (c) THE IRRADIANCE FIELD: its probes' escaping rays read the environment
//       and the cosine integration carries it into the atlas. Within 2 % of
//       (a); and (c2) it holds a sky above its old UNORM ceiling.
//   (d) THE GATHER'S RAY MISS: every miss reads the environment at the ray's
//       own footprint and returns L; the gathered irradiance of an open top
//       face is then L and the pixel (a). Within 2 %. (Skipped cleanly on a
//       machine with no ray queries.)
//
// And the three facts the one environment is made of:
//   (e) ONE GATE: no Sky Light (gain 0, SH 0) is no environment for ANY reader
//       — the cones' escape included, which read the pair before.
//   (f) THE DIFFUSE LOOKUP IS CAMERA-INDEPENDENT: a vertical SH gradient lights
//       the box's top face identically from three camera poses with both yaw
//       and pitch (upstream's AmbientLighting transformed the view normal with
//       the TRANSPOSE of the view->world rotation — exact only for a camera
//       turned about one axis; the fork's fix is the vector-first product
//       every other environment read already uses).
//   (g) THE ENVIRONMENT IS DISC-FREE WITHOUT A SECOND SKY RENDER: a sun disc
//       enabled INTO the probes (kVisibleBit) changes neither the sky's SH nor
//       the environment cube where the disc stands (the capture pass's queue
//       range and mask exclude it — JahshakaSkyCapture.compositor).
//   (h) THE SKY ENTERS THE BOUNCE, ONCE, AT INJECTION: a floor at the foot of
//       a white wall under the sky alone gains light when the chain bounces
//       (two bounces against one) — the bounce job's escaping cones see the
//       sky, so the wall's voxels hold the sky it reflects. Before this lane
//       the voxels of a sky-only scene held NOTHING at any bounce count (the
//       sky was never in them), and the two readings were identical.
//
// Its own binary.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static double srgbToLinear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }

static const unsigned kSize = 128u;
static const double kRho = 0.8;           // the box's albedo (linear)
static const unsigned char kSkyByte = 190; // the sky's sRGB byte -> L

/// The mean of a block of pixels (green channel: the fixture is grey).
static double blockMean(const ImageF &img, unsigned cx, unsigned cy, int half)
{
    double s = 0.0;
    int n = 0;
    for (int y = int(cy) - half; y <= int(cy) + half; ++y)
        for (int x = int(cx) - half; x <= int(cx) + half; ++x) {
            if (x < 0 || y < 0 || x >= int(img.width) || y >= int(img.height)) continue;
            s += img.at(unsigned(x), unsigned(y)).g;
            ++n;
        }
    return n ? s / n : 0.0;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-env-one-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("envone", kSize, kSize, Colour(0, 0, 0));

    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    // Asked AFTER the first view: the device is created with it (Engine.h).
    const bool rays = e->rayQueryAvailable() && e->rayTracing();
    {
        // The chain the gather needs, from the first frame, for EVERY arm: one
        // view, one pipeline, so the arms differ only in the reader.
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        fx.hdrReadback = true;   // every number below is RADIANCE (HDR-READBACK-1)
        view->setPostFx(fx);
    }
    Scene *s = e->createScene("envone");
    view->setScene(s);

    // ---- THE FIXTURE: one matte box, nothing else ---------------------------
    NodeId boxNode = 0;
    {
        const NodeId n = s->createNode();
        boxNode = n;
        const MeshId m = s->createMesh(enginetest::unitCubeMesh());
        PbrParams p;
        p.albedo = Colour(float(kRho), float(kRho), float(kRho));
        p.roughness = 1.0f;
        // MATTE by the ground's recipe (GF1): the Specular workflow at ior 1.0
        // with a black specular colour is F0 = 0 — no specular term, fresnelD 1.
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        const MaterialId mat = s->createPbrMaterial(p);
        CHECK(n && m && mat && s->attachMesh(n, m, mat), "the matte box exists");
    }

    // ---- THE UNIFORM SKY -------------------------------------------------------
    const double L = srgbToLinear(kSkyByte / 255.0);
    const unsigned char skyPx[4] = { kSkyByte, kSkyByte, kSkyByte, 255 };
    SkyDesc sky;
    sky.mode = SkyMode::Equirectangular;
    sky.equirect = s->createTexture(1, 1, skyPx, true);
    CHECK(sky.equirect && s->setSky(sky), "the uniform sky binds");
    // The mirror's push, at the engine boundary: the sky's own integral times the
    // Sky Light (1, white), and the same gain on the cube.
    float skySh[27] = { 0.0f };
    bool shReady = false;
    for (int f = 0; f < 20 && !shReady; ++f) {
        e->renderOneFrame();
        shReady = s->skyAmbientSh(skySh);
    }
    CHECK(shReady, "the sky's SH is integrated");
    std::printf("   sky L = %.4f (byte %u); its SH band 0 = %.4f (a uniform sky's band 0 is L)\n",
                L, unsigned(kSkyByte), double(skySh[0]));
    const auto skyLightOn = [&](float gain) {
        float sh[27];
        for (int i = 0; i < 27; ++i) sh[i] = skySh[i] * gain;
        s->setAmbientSh(sh);
        s->setEnvironmentLight(Colour(gain, gain, gain, 1.0f));
    };
    skyLightOn(1.0f);

    const Vec3 topFace(0.0f, 0.5f, 0.0f);
    const auto lookAtTop = [&](const Vec3 &from) { enginetest::testCameraLookAt(view, from, topFace); };
    const Vec3 kTopCam(0.35f, 3.2f, 1.4f);
    // THE LOBE AT A POSE: the top face's normal is +Y, so cos theta_v is the
    // camera's elevation seen from the face's centre (the pixel read is there).
    const auto lobeAt = [&](const Vec3 &cam) {
        const double dx = cam.x - topFace.x, dy = cam.y - topFace.y, dz = cam.z - topFace.z;
        return enginetest::disneyDiffuseAlbedo(dy / std::sqrt(dx * dx + dy * dy + dz * dz), 1.0);
    };
    const double kEnergyFactor1 = lobeAt(kTopCam);

    // THE READBACK, checked off the sky itself: the corner pixel is the sky,
    // radiance L exactly, read back as radiance (HDR-READBACK-1).
    lookAtTop(kTopCam);
    render(e, 6);
    {
        ImageF img;
        CHECK(view->readPixelsHdr(img), "the view reads its radiance back");
        const double c = blockMean(img, 3, 3, 2);
        std::printf("   the sky pixel reads %.5f against L = %.5f\n", c, L);
        CHECK_MSG(std::fabs(c - L) < 0.005 * L,
                  "the sky draws its own radiance (%.5f against L = %.5f, bar 0.5 %%) — the "
                  "currency of every number below", c, L);
    }
    const auto readTop = [&](int frames) {
        double acc = 0.0;
        for (int f = 0; f < frames; ++f) {
            e->renderOneFrame();
            ImageF img;
            view->readPixelsHdr(img);
            acc += blockMean(img, kSize / 2u, kSize / 2u, 3);
        }
        return acc / frames;
    };

    GiParams base;
    base.mode = GiMode::Vct;
    base.quality = GiQuality::Medium;
    base.numBounces = 1;
    base.cascades = false;
    base.ddgi = GiToggle::Off;
    base.gather = GiToggle::Off;
    base.cards = GiToggle::Off;
    base.testBoundsMin = Vec3(-4.0f, -2.0f, -4.0f);
    base.testBoundsMax = Vec3(4.0f, 4.0f, 4.0f);

    const double closedForm = kRho * L * kEnergyFactor1;
    std::printf("\n   THE ONE NUMBER: rho L A(v, 1) = %.3f x %.4f x %.4f = %.4f\n", kRho, L,
                kEnergyFactor1, closedForm);

    // ---- (a) OUTSIDE ANY VOLUME ------------------------------------------------
    GiParams off;
    off.mode = GiMode::Off;
    CHECK(s->setGlobalIllumination(off), "GI off");
    render(e, 6);
    const double ra = readTop(4);
    std::printf("   (a) the SH at the normal:        %.4f  (%.3f of the closed form)\n", ra,
                ra / closedForm);
    CHECK_MSG(std::fabs(ra / closedForm - 1.0) < 0.02,
              "(a) outside any volume the matte top reads rho L A(v, 1) within 2 %% "
              "(%.4f against %.4f)", ra, closedForm);

    // ---- (b) THE VOXEL CONES ---------------------------------------------------
    // TWO TIERS, because the ESCAPE is not this suite's subject and one tier's
    // escape is not 1 on this fixture. At Low the volume is isotropic and a
    // cone's escape is its own composite: nothing but the box is in the volume
    // and every cone leaves the top face upward, so the escape is 1 and the
    // pixel is the environment's own number. Above Low the escape rides the
    // anisotropic OCCUPANCY estimate (jah_voxel_sample.glsl, jahVoxelOccupancy:
    // the min of three axis composites one mip finer), which a cone STARTING on
    // a surface reads its own slab through as its footprint grows — deliberately
    // an under-estimate (READER-1's measured trade against a sealed room's
    // leak). Its reading is printed and fenced at its measured value, and the
    // number it is short by is the escape, not the environment.
    {
        GiParams low = base;
        low.quality = GiQuality::Low;
        CHECK(s->setGlobalIllumination(low), "VCT Low (isotropic, no field, no gather) builds");
        render(e, 20);
        const double rbl = readTop(4);
        std::printf("   (b) the cones' escape, Low:      %.4f  (%.3f of (a))\n", rbl, rbl / ra);
        CHECK_MSG(std::fabs(rbl / ra - 1.0) < 0.02,
                  "(b) the voxel cones' escape reads the SAME number within 2 %% (isotropic, where "
                  "the escape of an open face is 1: %.4f against %.4f)", rbl, ra);
    }
    CHECK(s->setGlobalIllumination(base), "VCT Medium (anisotropic, no field, no gather) builds");
    render(e, 20);
    const double rb = readTop(4);
    std::printf("   (b) the cones' escape, Medium:   %.4f  (%.3f of (a): the occupancy "
                "estimate's escape)\n", rb, rb / ra);
    CHECK_MSG(rb / ra > 0.92 && rb / ra < 1.02,
              "(b) anisotropic: the same number times the occupancy estimate's escape, fenced at "
              "its measured 0.94 (%.4f against %.4f)", rb, ra);

    // ---- (c) THE IRRADIANCE FIELD ----------------------------------------------
    {
        GiParams g = base;
        g.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(g), "the field binds");
        render(e, 40);
        const GiStatus st = s->giStatus();
        CHECK(st.ifdBound && st.ifdRefinesOwed < st.ifdTargetSamples, "the field is bound and whole (every probe sampled)");
        const double rc = readTop(4);
        std::printf("   (c) the field's sky term:        %.4f  (%.3f of (a))\n", rc, rc / ra);
        CHECK_MSG(std::fabs(rc / ra - 1.0) < 0.02,
                  "(c) the field at a probe in the open reads the SAME number within 2 %% "
                  "(%.4f against %.4f)", rc, ra);
    }

    // ---- (d) THE GATHER'S RAY MISS ---------------------------------------------
    if (rays) {
        GiParams g = base;
        g.gather = GiToggle::On;
        CHECK(s->setGlobalIllumination(g), "the gather arms");
        GatherTuning t;
        s->setGatherTuning(t);
        render(e, 40);
        CHECK(s->giStatus().gather.running, "the gather really runs");
        // AVERAGED OVER 48 FRAMES: the probe's directions are keyed on the frame
        // index, so one frame is one sample of the estimator.
        const double rd = readTop(48);
        std::printf("   (d) the gather's misses:         %.4f  (%.3f of (a)); a miss read %.4f "
                    "of L\n", rd, rd / ra, rd / (kRho * kEnergyFactor1) / L);
        CHECK_MSG(std::fabs(rd / ra - 1.0) < 0.02,
                  "(d) the gather's ray misses read L — the SAME number within 2 %% "
                  "(%.4f against %.4f)", rd, ra);
    } else {
        std::printf("ok: (d) no ray queries on this machine — the gather arm skips cleanly\n");
    }

    // ---- (c2) THE FIELD HOLDS A SKY BRIGHTER THAN ITS OLD CEILING ---------------
    // The irradiance atlas stores radiance over the voxels' decode multiplier
    // (D_max / pi, D_max the brightest LIGHT; 1 in a scene with no light), and it
    // was R10G10B10A2_UNORM: a sky above radiance 1.0 clipped there the moment the
    // sky entered the atlas. It is RGBA16_FLOAT now. A Sky Light at 4 makes the
    // environment 4 L = 2.06, and the SAME box (rho 0.8) then reads about 1.1 —
    // above the display's 1.0, which is why this arm reads RADIANCE: the dark
    // box it needed while the readback was 8-bit is gone (HDR-READBACK-1). The
    // field must read the SH's number, not the clipped one (rho x 1.0 x A).
    {
        skyLightOn(4.0f);
        GiParams offG; offG.mode = GiMode::Off;
        s->setGlobalIllumination(offG);
        render(e, 6);
        const double shBright = readTop(4);
        GiParams g = base;
        g.ddgi = GiToggle::On;
        s->setGlobalIllumination(g);
        render(e, 40);
        const double fieldBright = readTop(4);
        const double clipped = kRho * 1.0 * kEnergyFactor1;
        std::printf("   (c2) Sky Light 4 (environment %.3f): SH %.4f, field %.4f (%.3f of it); an "
                    "atlas clipped at 1.0 would read %.4f\n", 4.0 * L, shBright, fieldBright,
                    fieldBright / shBright, clipped);
        CHECK_MSG(shBright > 1.0,
                  "(c2) the pixel is read ABOVE the display's ceiling (%.4f) — the radiance "
                  "readback, not a darker fixture", shBright);
        CHECK_MSG(std::fabs(fieldBright / shBright - 1.0) < 0.03 && fieldBright > 1.5 * clipped,
                  "(c2) the field holds a sky twice its old ceiling (%.4f against the SH's %.4f)",
                  fieldBright, shBright);
        skyLightOn(1.0f);
    }

    // ---- (e) ONE GATE ----------------------------------------------------------
    {
        skyLightOn(0.0f);
        CHECK(s->setGlobalIllumination(base), "VCT again, with the Sky Light out");
        render(e, 20);
        const double re = readTop(2);
        std::printf("   (e) no Sky Light, the cones:     %.4f\n", re);
        // ZERO, not "under a code": the read is float (HDR-READBACK-1), so the
        // bar is the RGBA16F store's own floor and not 1.5/255.
        CHECK_MSG(re < 1.0e-4,
                  "(e) no Sky Light is no environment for the cones' escape either (%.6f)", re);
        CHECK(s->setGlobalIllumination(off), "GI off");
        render(e, 4);
        const double re2 = readTop(2);
        CHECK_MSG(re2 < 1.0e-4, "(e) ...nor for the SH outside a volume (%.6f)", re2);
        skyLightOn(1.0f);
    }

    // ---- (f) THE DIFFUSE LOOKUP IS CAMERA-INDEPENDENT --------------------------
    {
        SkyDesc none;
        s->setSky(none);
        // A vertical gradient: band 0 and the y band, world axes.
        float sh[27] = { 0.0f };
        for (int c = 0; c < 3; ++c) { sh[c] = 0.30f; sh[3 + c] = 0.20f; }
        s->setAmbientSh(sh);
        // THE LOOKUP, not the lobe: the lobe is view-dependent by physics (its
        // directional albedo), so each pose's read is divided by the lobe at that
        // pose and what is left is the irradiance the SH lookup handed the pixel.
        const double expected = kRho * (0.30 + 0.20);
        const Vec3 poses[3] = { Vec3(0.0f, 3.2f, 1.6f), Vec3(2.2f, 2.4f, 1.3f),
                                Vec3(-1.6f, 2.9f, -2.0f) };
        double reads[3];
        for (int i = 0; i < 3; ++i) {
            lookAtTop(poses[i]);
            render(e, 4);
            reads[i] = readTop(2) / lobeAt(poses[i]);
        }
        std::printf("   (f) the top under a y-band SH from three poses: %.4f %.4f %.4f "
                    "(closed form %.4f)\n", reads[0], reads[1], reads[2], expected);
        const double spread = std::max({ reads[0], reads[1], reads[2] }) -
                              std::min({ reads[0], reads[1], reads[2] });
        // 1 % OF THE VALUE, where the 8-bit read allowed 1.5 codes (0.0059, 1.5 %):
        // measured 0.0011 = 0.28 % in float, which is the lobe's shader fit
        // against the suite's integral over the 7 x 7 block's view angles, not
        // the lookup (the transposed lookup this guards moved the y band by tens
        // of per cent).
        CHECK_MSG(spread < 0.01 * expected,
                  "(f) the SH lookup is camera-independent: one surface, three camera poses "
                  "with yaw AND pitch, within 1 %% (spread %.4f, bar %.4f)", spread,
                  0.01 * expected);
        CHECK_MSG(std::fabs(reads[1] / expected - 1.0) < 0.02,
                  "(f) ...and it is the world's +Y band that lights an upward face (%.4f against "
                  "%.4f)", reads[1], expected);
        lookAtTop(kTopCam);
    }

    // ---- (g) THE ENVIRONMENT IS DISC-FREE --------------------------------------
    {
        SkyDesc lit = sky;
        CHECK(s->setSky(lit), "the uniform sky again");
        render(e, 8);
        float before[27];
        CHECK(s->skyAmbientSh(before), "its SH");
        const Vec3 sunDir(0.3f, 0.8f, 0.52f);
        const float sl = std::sqrt(sunDir.x * sunDir.x + sunDir.y * sunDir.y + sunDir.z * sunDir.z);
        std::vector<EnvironmentConeAnswer> coneBefore, coneAfter;
        const std::vector<EnvironmentConeQuery> q = {
            EnvironmentConeQuery{ Vec3(sunDir.x / sl, sunDir.y / sl, sunDir.z / sl), 0.0f },
            EnvironmentConeQuery{ Vec3(sunDir.x / sl, sunDir.y / sl, sunDir.z / sl), 0.577f } };
        CHECK(e->environmentCones(s, q, coneBefore), "the cube, read where the disc will stand");
        lit.sun.enabled = true;
        lit.sun.inProbes = true;                      // kVisibleBit beside kSunDiscBit
        lit.sun.angularDiameterDeg = 10.0f;
        lit.sun.colour = Colour(20.0f, 20.0f, 20.0f, 1.0f);
        lit.sun.dir[0] = sunDir.x / sl; lit.sun.dir[1] = sunDir.y / sl; lit.sun.dir[2] = sunDir.z / sl;
        CHECK(s->setSky(lit), "a 10-degree disc at 20x the sky, INTO the probes");
        render(e, 8);
        float after[27];
        CHECK(s->skyAmbientSh(after), "the SH again");
        CHECK(e->environmentCones(s, q, coneAfter), "the cube again");
        const bool shSame = std::memcmp(before, after, sizeof before) == 0;
        std::printf("   (g) band 0 %.5f -> %.5f; the cube at the disc (mirror / six-cone) "
                    "%.5f / %.5f -> %.5f / %.5f\n", double(before[0]), double(after[0]),
                    double(coneBefore[0].lookup[1]), double(coneBefore[1].lookup[1]),
                    double(coneAfter[0].lookup[1]), double(coneAfter[1].lookup[1]));
        CHECK(shSame, "(g) the disc changes no bit of the sky's SH");
        CHECK(coneAfter.size() == 2 && coneBefore.size() == 2 &&
                  coneAfter[0].lookup[1] == coneBefore[0].lookup[1] &&
                  coneAfter[1].lookup[1] == coneBefore[1].lookup[1],
              "(g) ...nor the environment cube where it stands (the capture excludes it)");
    }

    GiParams offAll;
    offAll.mode = GiMode::Off;
    s->setGlobalIllumination(offAll);
    view->setScene(nullptr);
    e->destroyScene(s);

    // ---- (h) THE SKY ENTERS THE BOUNCE -----------------------------------------
    {
        Scene *b = e->createScene("envone_bounce");
        view->setScene(b);
        const MeshId cube = b->createMesh(enginetest::unitCubeMesh());
        const auto slab = [&](float albedo, const Vec3 &pos, const Vec3 &scale) {
            PbrParams p;
            p.albedo = Colour(albedo, albedo, albedo);
            p.roughness = 1.0f;
            p.workflow = PbrParams::Workflow::Specular;
            p.ior = 1.0f;
            p.specularColour = Colour(0.0f, 0.0f, 0.0f);
            const NodeId n = b->createNode();
            const MaterialId m = b->createPbrMaterial(p);
            b->attachMesh(n, cube, m);
            b->setNodeTransform(n, pos, Quat(), scale);
        };
        slab(0.8f, Vec3(0.0f, -0.1f, 0.0f), Vec3(8.0f, 0.2f, 8.0f));   // the floor
        slab(0.9f, Vec3(0.0f, 1.5f, -1.2f), Vec3(6.0f, 3.0f, 0.4f));   // the wall
        SkyDesc bsky;
        bsky.mode = SkyMode::Equirectangular;
        bsky.equirect = b->createTexture(1, 1, skyPx, true);
        b->setSky(bsky);
        float bsh[27] = { 0.0f };
        bool ready = false;
        for (int f = 0; f < 20 && !ready; ++f) { e->renderOneFrame(); ready = b->skyAmbientSh(bsh); }
        b->setAmbientSh(bsh);
        b->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f, 1.0f));
        // The floor just in front of the wall's foot, from above.
        enginetest::testCameraLookAt(view, Vec3(0.2f, 2.6f, 1.2f), Vec3(0.0f, 0.0f, -0.7f));
        const auto armAt = [&](int bounces) {
            GiParams g = base;
            g.quality = GiQuality::Low;          // isotropic: the escape is the composite
            g.numBounces = bounces;
            b->setGlobalIllumination(g);
            render(e, 30);
            return readTop(4);
        };
        const double one = armAt(1);
        const double two = armAt(2);
        std::printf("   (h) the floor at the wall's foot, sky only: one bounce %.4f, two %.4f "
                    "(+%.4f)\n", one, two, two - one);
        // A RELATIVE bar in float (it was 2/255, the 8-bit read's noise guard):
        // measured +26 %; 2 % is twenty times the lobe fit's own spread above.
        CHECK_MSG(two - one > 0.02 * one,
                  "(h) the sky enters the bounce at injection: two bounces light the floor at a "
                  "sky-lit wall's foot more than one (+%.4f, bar +2 %%)", two - one);
        b->setGlobalIllumination(offAll);
        view->setScene(nullptr);
        e->destroyScene(b);
    }

    e->destroyView(view);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
