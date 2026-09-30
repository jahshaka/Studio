// sky.atmosphere — THE PLANET'S ATMOSPHERE (SKY-ATMOSPHERE-1) — headless,
// framework-free, links JahshakaEngine only (a reachable DISPLAY and a Vulkan
// driver, like tests/engine).
//
// THE CHANGE THIS SUITE IS THE GATE ON. The realistic sky was Ogre's
// AtmosphereNpr, a non-physical gradient that pushed every view ray up and
// CLAMPED it at a border limit: every pixel under the horizon repeated the
// horizon's colour, so sky and world met in a smear and the band below the
// horizon was BRIGHTER than the horizon (measured before this lane, same
// fixture as arm (a): the luma rose monotonically through the horizon, 0.66594
// one row above it and 0.66652 one row below — a step of 5.8e-4, 0.087 % of the
// horizon's luma per 0.078-degree row, and the 5 degrees below it read brighter
// still). The sky is now Hillaire's physical model (Types.h, AtmosphereSky):
// below the horizon the view ray meets the planet.
//
// WHAT IS ASSERTED (every arm in this one process, paired where it compares)
//   a. THE HORIZON IS SHARP: the luma step across the horizon row, relative to
//      the horizon's luma, is at least kSharpN times the step measured on the
//      old model above;
//   b. BELOW THE HORIZON THE SKY IS DARKER than at it: the mean luma of the
//      5 degrees below < the mean of the 1 degree above (the planet band);
//   c. the SUN'S COLOUR at 5 degrees is redder than at 60 (the transmittance);
//   d. THE AERIAL PERSPECTIVE: with aerialScale 0 a lit surface 20 km away
//      reads its unfogged value exactly (T = 1, nothing scattered in); with 1
//      it approaches the sky behind it (T < 0.9 and the black face lies nearer
//      the sky than at 0);
//   e. THE TABLES REBUILD ONLY ON A CHANGE: 120 still frames rebuild nothing;
//      a sun move rebuilds the sky view and the aerial volume and not the
//      dial-only tables; a dial move rebuilds all four once;
//   f. NO SKY IS BLACK and lights nothing: SkyMode::NoSky draws the black
//      background and a box lit by the Sky Light alone reads black, where the
//      atmosphere lights it.
// And it PRINTS the four tables' cost (Scene::measureAtmosphere; a slope, an
// upper bound — no bar here, the number is the lane's report).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                               std::printf("\n"); ++failures; } \
                               else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
const float kPi = 3.14159265358979323846f;
const unsigned kSize = 256u;
const float kFovDeg = 20.0f;
// THE OLD MODEL'S STEP across the horizon row on arm (a)'s fixture, relative to
// the horizon's luma (the header has the measurement).
const double kOldRelativeStep = 5.8e-4 / 0.66594;
// The bar: the new horizon's step at least this many times the old one's.
const double kSharpN = 20.0;

double luma(const Colour &c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

Vec3 towardsSun(float elevationDeg, float azimuthDeg)
{
    const float e = elevationDeg * kPi / 180.0f, a = azimuthDeg * kPi / 180.0f;
    return Vec3(std::cos(e) * std::sin(a), std::sin(e), -std::cos(e) * std::cos(a));
}

SkyDesc atmosphere(float elevationDeg, float azimuthDeg, float aerialScale = 1.0f, float haze = 1.0f)
{
    SkyDesc d;
    d.mode = SkyMode::Atmosphere;
    d.atmosphere.hasSun = true;
    const Vec3 s = towardsSun(elevationDeg, azimuthDeg);
    d.atmosphere.sunDir[0] = s.x; d.atmosphere.sunDir[1] = s.y; d.atmosphere.sunDir[2] = s.z;
    // a white sun of intensity 1: pi in the renderer's units (the mirror's rule)
    d.atmosphere.sunIlluminance = Colour(kPi, kPi, kPi, 1.0f);
    d.atmosphere.aerialScale = aerialScale;
    d.atmosphere.sunHaze = haze;
    return d;
}

/// The elevation (degrees) of the centre of row y for a level camera.
double rowElevation(unsigned y)
{
    const double t = std::tan(0.5 * kFovDeg * kPi / 180.0);
    return std::atan((1.0 - 2.0 * (y + 0.5) / kSize) * t) * 180.0 / kPi;
}

double rowMean(const ImageF &img, unsigned y)
{
    double m = 0.0;
    for (unsigned x = 0; x < img.width; ++x) m += luma(img.at(x, y));
    return m / img.width;
}

struct Rig {
    Engine *e = nullptr;
    View *v = nullptr;
    Scene *s = nullptr;
};

bool shoot(Rig &r, ImageF &out, int frames = 3)
{
    for (int i = 0; i < frames; ++i) r.e->renderOneFrame();
    return r.v->readPixelsHdr(out);
}

Colour centreOf(const ImageF &img)
{
    double c[3] = { 0, 0, 0 };
    int n = 0;
    for (unsigned y = img.height / 2 - 2; y < img.height / 2 + 2; ++y)
        for (unsigned x = img.width / 2 - 2; x < img.width / 2 + 2; ++x) {
            const Colour p = img.at(x, y);
            c[0] += p.r; c[1] += p.g; c[2] += p.b; ++n;
        }
    return Colour(float(c[0] / n), float(c[1] / n), float(c[2] / n), 1.0f);
}

/// JAH_ATMO_DUMP=<prefix>: the arm's picture as a PPM, exposed by `gain` and
/// sRGB-encoded — for a person to look at, never asserted on.
void dump(const ImageF &img, const char *name, float gain)
{
    const char *prefix = std::getenv("JAH_ATMO_DUMP");
    if (!prefix) return;
    const std::string path = std::string(prefix) + name + ".ppm";
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const float v[3] = { c.r * gain, c.g * gain, c.b * gain };
            for (float l : v) {
                l = std::max(0.0f, std::min(1.0f, l));
                const float e = l <= 0.0031308f ? 12.92f * l : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
                std::fputc(int(e * 255.0f + 0.5f), f);
            }
        }
    std::fclose(f);
}

double dist(const Colour &a, const Colour &b)
{
    return std::sqrt(double(a.r - b.r) * (a.r - b.r) + double(a.g - b.g) * (a.g - b.g) +
                     double(a.b - b.b) * (a.b - b.b));
}
}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atmosphere-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);

    Rig r;
    r.e = engine.get();
    r.v = r.e->createOffscreenView("atmo", kSize, kSize, Colour(0, 0, 0));
    r.s = r.e->createScene("atmo");
    if (!r.v || !r.s) { std::printf("FAIL: view/scene\n"); return 1; }
    r.v->setScene(r.s);
    PostFxDesc fx;
    fx.hdrReadback = true;      // the linear radiance, before any grade
    r.v->setPostFx(fx);
    CameraDesc level = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 2.0f, -100.0f));
    level.fovDegrees = kFovDeg;
    level.farClip = 40000.0f;
    r.v->setCamera(level);

    // ---- a + b. THE HORIZON ------------------------------------------------
    // The sun behind the camera, 45 degrees up: no glow in view, the whole
    // frame the sky and the planet under it.
    CHECK_MSG(r.s->setSky(atmosphere(45.0f, 180.0f)), "the atmosphere applies: %s", r.e->lastError().c_str());
    {
        ImageF img;
        CHECK_MSG(shoot(r, img), "the horizon renders");
        dump(img, "horizon", 2.0f);
        // The horizon of an observer 2 m up dips 0.045 degrees: inside row 128
        // (-0.0 .. -0.078). The step is taken across rows 127 | 129 so the row
        // that holds the line itself cannot flatter it.
        const double above = rowMean(img, 127), below = rowMean(img, 129);
        const double rel = std::fabs(above - below) / std::max(above, 1e-9);
        const double n = rel / kOldRelativeStep;
        std::printf("    horizon: row 127 (%+.3f deg) %.5f  row 129 (%+.3f deg) %.5f  relative step %.4f "
                    "= %.0fx the old model's\n", rowElevation(127), above, rowElevation(129), below, rel, n);
        CHECK_MSG(n >= kSharpN, "(a) THE HORIZON IS SHARP: the step across it is %.0fx the old model's "
                  "(bar %.0fx)", n, kSharpN);
        // 1 degree above, 5 below
        double up = 0.0, down = 0.0;
        int nu = 0, nd = 0;
        for (unsigned y = 0; y < kSize; ++y) {
            const double e = rowElevation(y);
            if (e > 0.0 && e <= 1.0) { up += rowMean(img, y); ++nu; }
            if (e < -0.1 && e >= -5.0) { down += rowMean(img, y); ++nd; }
        }
        up /= std::max(nu, 1); down /= std::max(nd, 1);
        std::printf("    the 1 degree above the horizon %.5f (%d rows), the 5 below %.5f (%d rows)\n",
                    up, nu, down, nd);
        CHECK_MSG(nu > 0 && nd > 0 && down < up,
                  "(b) BELOW THE HORIZON THE SKY IS DARKER: the planet band %.5f < the horizon %.5f",
                  down, up);
    }

    // ---- c. THE SUN'S COLOUR -----------------------------------------------
    {
        const Colour low = r.s->atmosphereSunTint(towardsSun(5.0f, 0.0f));
        const Colour high = r.s->atmosphereSunTint(towardsSun(60.0f, 0.0f));
        const Colour noon = r.s->atmosphereSunTint(towardsSun(90.0f, 0.0f));
        const double rbLow = low.r / std::max(low.b, 1e-6f), rbHigh = high.r / std::max(high.b, 1e-6f);
        std::printf("    sun tint: 90 deg %.4f %.4f %.4f | 60 deg %.4f %.4f %.4f | 5 deg %.4f %.4f %.4f\n",
                    noon.r, noon.g, noon.b, high.r, high.g, high.b, low.r, low.g, low.b);
        CHECK_MSG(std::fabs(noon.r - 1.0f) < 1e-4f && std::fabs(noon.g - 1.0f) < 1e-4f &&
                  std::fabs(noon.b - 1.0f) < 1e-4f, "the tint at the zenith is exactly white");
        CHECK_MSG(rbLow > rbHigh * 1.5 && low.g < high.g,
                  "(c) THE SUN AT 5 DEGREES IS REDDER (r/b %.3f against %.3f at 60) and dimmer", rbLow, rbHigh);
    }

    // ---- d. THE AERIAL PERSPECTIVE -----------------------------------------
    {
        // BLACK AND METALLIC: no diffuse and a specular F0 of the black albedo,
        // so the face reflects nothing and its pixel is its emission alone —
        // what the air then does to it is all there is to read.
        PbrParams p;
        p.albedo = Colour(0, 0, 0);
        p.metalness = 1.0f;
        p.roughness = 1.0f;
        const MaterialId mat = r.s->createPbrMaterial(p);
        const NodeId box = r.s->createNode();
        const MeshId mesh = r.s->createMesh(enginetest::unitCubeMesh());
        const bool built = mat && box && mesh && r.s->attachMesh(box, mesh, mat);
        CHECK_MSG(built, "the far box is built");
        enginetest::poseRegistry()[r.s][box] = enginetest::NodePose{};
        const float d = 20000.0f, size = 0.1f * d;
        enginetest::setNodeScale(r.s, box, Vec3(size, size, size));
        // centred on the view ray, a hair above the horizon
        enginetest::setNodePosition(r.s, box, Vec3(0.0f, 2.0f + 0.5f * size, -(d + 0.5f * size)));
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 0.0f),
                                                        Vec3(0.0f, 2.0f + 0.5f * size, -d));
        c.fovDegrees = kFovDeg;
        c.farClip = 40000.0f;
        r.v->setCamera(c);
        auto emissive = [&](float e) {
            PbrParams q = p;
            q.emissive = Colour(e, e, e);
            r.s->setPbrMaterial(mat, q);
        };
        auto measure = [&](float scale, double &t, Colour &black, Colour &sky) {
            r.s->setSky(atmosphere(45.0f, 180.0f, scale));
            ImageF a, b, k;
            emissive(0.2f); shoot(r, a);
            emissive(0.8f); shoot(r, b);
            emissive(0.0f); shoot(r, k);
            t = (double(centreOf(b).g) - double(centreOf(a).g)) / 0.6;
            black = centreOf(k);
            sky = k.at(k.width / 2, 20);   // the sky above the box
        };
        double t0 = 0.0, t1 = 0.0;
        Colour k0, k1, s0, s1;
        measure(0.0f, t0, k0, s0);
        measure(1.0f, t1, k1, s1);
        std::printf("    20 km: aerialScale 0 T %.5f black %.5f %.5f %.5f | aerialScale 1 T %.5f black "
                    "%.5f %.5f %.5f | sky above %.5f %.5f %.5f\n", t0, k0.r, k0.g, k0.b, t1, k1.r, k1.g,
                    k1.b, s1.r, s1.g, s1.b);
        CHECK_MSG(std::fabs(t0 - 1.0) < 2e-3 && luma(k0) < 1e-4,
                  "(d) aerialScale 0: the surface reads its unfogged value (T %.5f, nothing scattered in: "
                  "%.6f)", t0, luma(k0));
        CHECK_MSG(t1 < 0.9 && dist(k1, s1) < dist(k0, s1) * 0.8 && k1.b > k1.r,
                  "(d) aerialScale 1: at 20 km the surface keeps %.3f of itself and approaches the sky "
                  "behind it (distance to the sky %.4f against %.4f with no air), blue at a high sun",
                  t1, dist(k1, s1), dist(k0, s1));
        r.s->setNodeVisible(box, false);
        r.v->setCamera(level);
    }

    // ---- e. THE TABLES REBUILD ONLY ON A CHANGE -----------------------------
    {
        r.s->setSky(atmosphere(45.0f, 180.0f));
        for (int i = 0; i < 4; ++i) r.e->renderOneFrame();
        const AtmosphereStatus a = r.s->atmosphereStatus();
        for (int i = 0; i < 120; ++i) r.e->renderOneFrame();
        const AtmosphereStatus b = r.s->atmosphereStatus();
        std::printf("    tables: transmittance %u multi-scatter %u sky view %u aerial %u -> %u %u %u %u "
                    "over 120 still frames\n", a.transmittanceBuilds, a.multiScatterBuilds,
                    a.skyViewBuilds, a.aerialBuilds, b.transmittanceBuilds, b.multiScatterBuilds,
                    b.skyViewBuilds, b.aerialBuilds);
        CHECK_MSG(a.on && b.transmittanceBuilds == a.transmittanceBuilds &&
                  b.multiScatterBuilds == a.multiScatterBuilds && b.skyViewBuilds == a.skyViewBuilds &&
                  b.aerialBuilds == a.aerialBuilds,
                  "(e) 120 STILL FRAMES REBUILD NO TABLE");
        r.s->setSky(atmosphere(30.0f, 180.0f));
        r.e->renderOneFrame();
        const AtmosphereStatus c = r.s->atmosphereStatus();
        CHECK_MSG(c.transmittanceBuilds == b.transmittanceBuilds && c.multiScatterBuilds == b.multiScatterBuilds &&
                  c.skyViewBuilds == b.skyViewBuilds + 1 && c.aerialBuilds == b.aerialBuilds + 1,
                  "(e) a SUN move rebuilds the sky view and the aerial volume once, and not the dial "
                  "tables (%u %u %u %u)", c.transmittanceBuilds, c.multiScatterBuilds, c.skyViewBuilds,
                  c.aerialBuilds);
        r.s->setSky(atmosphere(30.0f, 180.0f, 1.0f, 3.0f));
        r.e->renderOneFrame();
        const AtmosphereStatus h = r.s->atmosphereStatus();
        CHECK_MSG(h.transmittanceBuilds == c.transmittanceBuilds + 1 &&
                  h.multiScatterBuilds == c.multiScatterBuilds + 1 && h.skyViewBuilds == c.skyViewBuilds + 1 &&
                  h.aerialBuilds == c.aerialBuilds + 1,
                  "(e) a DIAL move (the haze) rebuilds all four once");
        r.s->setSky(atmosphere(30.0f, 180.0f, 0.5f, 3.0f));
        r.e->renderOneFrame();
        const AtmosphereStatus q = r.s->atmosphereStatus();
        CHECK_MSG(q.transmittanceBuilds == h.transmittanceBuilds && q.skyViewBuilds == h.skyViewBuilds &&
                  q.aerialBuilds == h.aerialBuilds,
                  "(e) the aerial scale is a constant: it rebuilds nothing");
        AtmosphereCost cost;
        if (r.s->measureAtmosphere(20u, cost))
            std::printf("    cost per rebuild (slope over 20 dispatches, an upper bound): transmittance "
                        "%.4f ms, multi-scatter %.4f ms, sky view %.4f ms, aerial %.4f ms\n",
                        cost.transmittanceMs, cost.multiScatterMs, cost.skyViewMs, cost.aerialMs);
    }

    // ---- f. NO SKY -----------------------------------------------------------
    {
        PbrParams p;
        p.albedo = Colour(0.8f, 0.8f, 0.8f);
        p.roughness = 1.0f;
        const MaterialId mat = r.s->createPbrMaterial(p);
        const NodeId box = r.s->createNode();
        const MeshId mesh = r.s->createMesh(enginetest::unitCubeMesh());
        r.s->attachMesh(box, mesh, mat);
        enginetest::poseRegistry()[r.s][box] = enginetest::NodePose{};
        enginetest::setNodeScale(r.s, box, Vec3(1.0f, 1.0f, 1.0f));
        enginetest::setNodePosition(r.s, box, Vec3(0.0f, 2.0f, -6.0f));
        r.s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));   // the Sky Light, no sun light
        ImageF lit, none;
        r.s->setSky(atmosphere(45.0f, 180.0f));
        shoot(r, lit, 6);
        SkyDesc no;
        no.mode = SkyMode::NoSky;
        CHECK_MSG(r.s->setSky(no), "NoSky applies");
        shoot(r, none, 6);
        float sh[27];
        const bool haveSh = r.s->skyAmbientSh(sh);
        double shSum = 0.0;
        if (haveSh) for (float v : sh) shSum += std::fabs(v);
        const Colour boxLit = centreOf(lit), boxNone = centreOf(none);
        const Colour skyNone = none.at(4, 4);
        std::printf("    no sky: box %.5f (lit by the atmosphere %.5f), sky corner %.5f, SH %s %.6f\n",
                    luma(boxNone), luma(boxLit), luma(skyNone), haveSh ? "held" : "none", shSum);
        CHECK_MSG(luma(boxLit) > 1e-3, "the atmosphere lights the box through the Sky Light (%.5f)", luma(boxLit));
        CHECK_MSG(luma(skyNone) == 0.0 && luma(boxNone) < 1e-5 && (!haveSh || shSum == 0.0),
                  "(f) NO SKY IS BLACK and lights nothing (background %.6f, box %.6f)", luma(skyNone),
                  luma(boxNone));
    }

    // ---- THE SKY PASS'S COST AT 1080p (JAH_ATMO_COST=1; a measurement, never
    // asserted — run it under scripts/gpu-exclusive.sh) ----------------------
    // Paired, interleaved arms in ONE process on a 1920x1080 view holding a
    // floor and a box: the atmosphere against a one-texel image sky (both a
    // full-screen quad at queue 0). The difference in the frame's GPU time is
    // the sky pass's table read plus every lit pixel's aerial-perspective read.
    if (std::getenv("JAH_ATMO_COST")) {
        View *big = r.e->createOffscreenView("atmo-1080", 1920u, 1080u, Colour(0, 0, 0));
        Scene *cs = r.e->createScene("atmo-cost");
        big->setScene(cs);
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 8.0f), Vec3(0.0f, 1.0f, 0.0f));
        c.farClip = 4000.0f;
        big->setCamera(c);
        PbrParams fp;
        fp.albedo = Colour(0.5f, 0.5f, 0.5f);
        fp.roughness = 0.8f;
        const MaterialId fm = cs->createPbrMaterial(fp);
        const NodeId floor = cs->createNode();
        cs->attachMesh(floor, cs->createMesh(enginetest::unitCubeMesh()), fm);
        enginetest::poseRegistry()[cs][floor] = enginetest::NodePose{};
        enginetest::setNodeScale(cs, floor, Vec3(400.0f, 0.1f, 400.0f));
        enginetest::setNodePosition(cs, floor, Vec3(0.0f, -0.05f, 0.0f));
        const unsigned char px[4] = { 110, 130, 160, 255 };
        SkyDesc image;
        image.mode = SkyMode::Equirectangular;
        image.equirect = cs->createTexture(1, 1, px, true);
        r.v->setEnabled(false);
        r.e->setFrameMonitor(MonitorLevel::Review);
        double sum[2] = { 0.0, 0.0 };
        int n[2] = { 0, 0 };
        for (int round = 0; round < 6; ++round)
            for (int arm = 0; arm < 2; ++arm) {
                cs->setSky(arm ? atmosphere(45.0f, 150.0f) : image);
                for (int i = 0; i < 20; ++i) r.e->renderOneFrame();   // warm, and the tables built
                std::vector<FrameRecord> drop;
                r.e->takeFrameRecords(drop);
                for (int i = 0; i < 60; ++i) r.e->renderOneFrame();
                r.e->setFrameMonitor(MonitorLevel::Off);   // flushes what is waiting
                std::vector<FrameRecord> recs;
                r.e->takeFrameRecords(recs);
                r.e->setFrameMonitor(MonitorLevel::Review);
                for (const FrameRecord &f : recs)
                    if (f.gpuMs > 0.0f) { sum[arm] += f.gpuMs; ++n[arm]; }
            }
        r.e->setFrameMonitor(MonitorLevel::Off);
        const double img = n[0] ? sum[0] / n[0] : -1.0, atm = n[1] ? sum[1] / n[1] : -1.0;
        std::printf("    COST 1080p frame GPU: image sky %.4f ms (%d frames), atmosphere %.4f ms (%d), "
                    "difference %.4f ms\n", img, n[0], atm, n[1], atm - img);
        r.v->setEnabled(true);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok (%d failures)\n", failures);
    return failures ? 1 : 0;
}
