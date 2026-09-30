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
//   g. THE HAZE IS THE SKY'S (owner 2026-09-30): at the defaults (aerialScale 0)
//      a surface 2 km away reads its own value at haze 10 and 100; the haze
//      moves the horizon's luminance and whiteness and the sun's tint;
//   h. THE SKY'S BRIGHTNESS: 2 doubles a sky pixel and the Sky Light's SH, and
//      leaves the sun's direct term on a floor unchanged;
//   i. THE OBSERVER IS THE CAMERA: at 500 m and 5 km the horizon dips by the
//      geometric angle within a pixel and a level surface is seen through the
//      air at that altitude.
// And it PRINTS the four tables' cost (Scene::measureAtmosphere; a slope, an
// upper bound — no bar here, the number is the lane's report).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <functional>
#include <vector>
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
        r.s->setNodeVisible(box, false);
        r.s->setEnvironmentLight(Colour(0.0f, 0.0f, 0.0f));
    }

    // ---- g. THE HAZE IS THE SKY'S, NOT THE SCENE'S (owner 2026-09-30) ---------
    // With the defaults (aerialScale 0) a distant lit surface reads its own
    // value however hazy the sky; the haze moves the sky's horizon and the sun.
    {
        PbrParams p;
        p.albedo = Colour(0, 0, 0);
        p.metalness = 1.0f;
        p.roughness = 1.0f;
        const MaterialId mat = r.s->createPbrMaterial(p);
        const NodeId box = r.s->createNode();
        r.s->attachMesh(box, r.s->createMesh(enginetest::unitCubeMesh()), mat);
        enginetest::poseRegistry()[r.s][box] = enginetest::NodePose{};
        const float d = 2000.0f, size = 0.1f * d;
        enginetest::setNodeScale(r.s, box, Vec3(size, size, size));
        enginetest::setNodePosition(r.s, box, Vec3(0.0f, 2.0f + 0.5f * size, -(d + 0.5f * size)));
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 0.0f),
                                                        Vec3(0.0f, 2.0f + 0.5f * size, -d));
        c.fovDegrees = kFovDeg;
        c.farClip = 40000.0f;
        r.v->setCamera(c);
        auto tAt = [&](float haze, Colour &black) {
            SkyDesc sky = atmosphere(45.0f, 180.0f, 1.0f, haze);
            sky.atmosphere.aerialScale = AtmosphereSky().aerialScale;   // THE DEFAULT
            r.s->setSky(sky);
            ImageF a, b, k;
            PbrParams q = p;
            q.emissive = Colour(0.2f, 0.2f, 0.2f); r.s->setPbrMaterial(mat, q); shoot(r, a);
            q.emissive = Colour(0.8f, 0.8f, 0.8f); r.s->setPbrMaterial(mat, q); shoot(r, b);
            q.emissive = Colour(0.0f, 0.0f, 0.0f); r.s->setPbrMaterial(mat, q); shoot(r, k);
            black = centreOf(k);
            return (double(centreOf(b).g) - double(centreOf(a).g)) / 0.6;
        };
        Colour k10, k100;
        const double t10 = tAt(10.0f, k10), t100 = tAt(100.0f, k100);
        std::printf("    defaults, 2 km: haze 10 T %.5f black %.6f | haze 100 T %.5f black %.6f\n",
                    t10, luma(k10), t100, luma(k100));
        CHECK_MSG(AtmosphereSky().aerialScale == 0.0f && std::fabs(t10 - 1.0) < 2e-3 &&
                  std::fabs(t100 - 1.0) < 2e-3 && luma(k10) < 1e-4 && luma(k100) < 1e-4,
                  "(g) THE DEFAULTS PUT NO AIR ON THE SCENE: a surface 2 km away reads its own value at "
                  "haze 10 and at haze 100 (T %.4f, %.4f)", t10, t100);
        r.s->setNodeVisible(box, false);
        r.v->setCamera(level);
        // ...and the haze moves the SKY: the band just above the horizon whitens.
        auto horizonBand = [&](float haze) {
            r.s->setSky(atmosphere(45.0f, 180.0f, 1.0f, haze));
            ImageF img;
            shoot(r, img);
            double rr = 0.0, bb = 0.0, yy = 0.0;
            int n = 0;
            for (unsigned y = 0; y < kSize; ++y) {
                const double e = rowElevation(y);
                if (e <= 0.0 || e > 2.0) continue;
                for (unsigned x = 0; x < kSize; ++x) {
                    const Colour px = img.at(x, y);
                    rr += px.r; bb += px.b; yy += luma(px); ++n;
                }
            }
            return Colour(float(rr / n), float(yy / n), float(bb / n), 1.0f);
        };
        const Colour clean = horizonBand(1.0f), day = horizonBand(10.0f), hazy = horizonBand(50.0f);
        std::printf("    horizon band (0-2 deg): haze 1 luma %.4f b/r %.3f | haze 10 %.4f %.3f | haze 50 "
                    "%.4f %.3f\n", clean.g, clean.b / clean.r, day.g, day.b / day.r, hazy.g,
                    hazy.b / hazy.r);
        CHECK_MSG(std::fabs(hazy.g / clean.g - 1.0f) > 0.05f &&
                  hazy.b / hazy.r < clean.b / clean.r * 0.9f,
                  "(g) the haze moves the sky's horizon: its luminance (%.4f to %.4f) and it WHITENS "
                  "(b/r %.3f to %.3f)", clean.g, hazy.g, clean.b / clean.r, hazy.b / hazy.r);
        SkyDesc hz = atmosphere(20.0f, 180.0f, 1.0f, 1.0f);
        r.s->setSky(hz);
        const Colour tClean = r.s->atmosphereSunTint(towardsSun(20.0f, 180.0f));
        hz.atmosphere.sunHaze = 50.0f;
        r.s->setSky(hz);
        const Colour tHazy = r.s->atmosphereSunTint(towardsSun(20.0f, 180.0f));
        CHECK_MSG(tHazy.g < tClean.g * 0.9f,
                  "(g) ...and the sun's tint: green at a 20-degree sun %.4f at haze 1, %.4f at haze 50",
                  tClean.g, tHazy.g);
    }

    // ---- h. THE SKY'S BRIGHTNESS SCALES THE SKY AND ITS LIGHT, NOT THE SUN ---
    {
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        p.roughness = 1.0f;
        const MaterialId mat = r.s->createPbrMaterial(p);
        const NodeId floor = r.s->createNode();
        r.s->attachMesh(floor, r.s->createMesh(enginetest::unitCubeMesh()), mat);
        enginetest::poseRegistry()[r.s][floor] = enginetest::NodePose{};
        enginetest::setNodeScale(r.s, floor, Vec3(20.0f, 0.1f, 20.0f));
        enginetest::setNodePosition(r.s, floor, Vec3(0.0f, -0.05f, -6.0f));
        const NodeId lightNode = r.s->createNode();
        enginetest::poseRegistry()[r.s][lightNode] = enginetest::NodePose{};   // emits down -Y
        LightDesc sunL;
        sunL.type = LightType::Directional;
        sunL.castShadows = false;
        r.s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 2.3f, -10.0f));
        c.fovDegrees = 60.0f;
        c.farClip = 40000.0f;
        r.v->setCamera(c);
        struct Arm { double sky = 0, lit = 0, dark = 0; float sh0 = 0; };
        auto arm = [&](float brightness) {
            Arm a;
            SkyDesc sky = atmosphere(60.0f, 180.0f, 0.0f, 10.0f);
            sky.atmosphere.skyBrightness = brightness;
            r.s->setSky(sky);
            ImageF img;
            sunL.intensity = 1.0f; r.s->setLight(lightNode, sunL);
            shoot(r, img, 6);
            a.sky = luma(img.at(kSize / 2, 8));                 // the sky, 30 degrees up
            a.lit = luma(img.at(kSize / 2, kSize - 8));         // the floor, sun + sky
            float sh[27];
            if (r.s->skyAmbientSh(sh)) a.sh0 = sh[1];           // band 0, green
            sunL.intensity = 0.0f; r.s->setLight(lightNode, sunL);
            shoot(r, img, 6);
            a.dark = luma(img.at(kSize / 2, kSize - 8));        // the floor, sky alone
            return a;
        };
        const Arm one = arm(1.0f), two = arm(2.0f);
        const double direct1 = one.lit - one.dark, direct2 = two.lit - two.dark;
        std::printf("    brightness 1 -> 2: sky %.5f -> %.5f (x%.3f), SH band 0 %.5f -> %.5f (x%.3f), the "
                    "floor's direct term %.5f -> %.5f\n", one.sky, two.sky, two.sky / one.sky, one.sh0,
                    two.sh0, two.sh0 / one.sh0, direct1, direct2);
        CHECK_MSG(std::fabs(two.sky / one.sky - 2.0) < 0.02,
                  "(h) brightness 2 doubles a sky pixel (x%.3f)", two.sky / one.sky);
        CHECK_MSG(one.sh0 > 0.0f && std::fabs(two.sh0 / one.sh0 - 2.0) < 0.02,
                  "(h) ...and the Sky Light's ambient (SH band 0 x%.3f)", two.sh0 / one.sh0);
        CHECK_MSG(direct1 > 0.01 && std::fabs(direct2 / direct1 - 1.0) < 0.01,
                  "(h) ...and leaves the sun's direct light on the floor unchanged (%.5f against %.5f)",
                  direct2, direct1);
        r.s->setNodeVisible(floor, false);
        sunL.intensity = 0.0f; r.s->setLight(lightNode, sunL);
        r.s->setEnvironmentLight(Colour(0.0f, 0.0f, 0.0f));
        r.v->setCamera(level);
    }

    // ---- i. THE OBSERVER IS THE CAMERA (the merge read's D2) -----------------
    // At 500 m and 5 km the horizon dips by acos(R / (R + h)) — 0.72 and 2.27
    // degrees — within a pixel (0.078 degrees here); a level surface 2 km away is
    // seen through the air AT that altitude (the model's own column, integrated
    // here), with the scene air on.
    for (const float h : { 500.0f, 5000.0f }) {
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(0.0f, h, 0.0f), Vec3(0.0f, h, -100.0f));
        c.fovDegrees = kFovDeg;
        c.farClip = 40000.0f;
        r.v->setCamera(c);
        r.s->setSky(atmosphere(45.0f, 180.0f, 1.0f, 10.0f));
        ImageF img;
        shoot(r, img, 4);
        const double dip = std::acos(6360.0 / (6360.0 + h * 0.001)) * 180.0 / kPi;
        unsigned best = 0;
        double bestStep = -1.0;
        for (unsigned y = 100; y + 1 < kSize - 10; ++y) {
            const double st = std::fabs(rowMean(img, y) - rowMean(img, y + 1));
            if (st > bestStep) { bestStep = st; best = y; }
        }
        const double t = std::tan(0.5 * kFovDeg * kPi / 180.0);
        const double edge = std::atan((1.0 - 2.0 * (best + 1.0) / kSize) * t) * 180.0 / kPi;
        const AtmosphereStatus st = r.s->atmosphereStatus();
        std::printf("    %5.0f m: the horizon between rows %u|%u at %+.3f deg against the dip %.3f deg; "
                    "observer %.0f m (%u band rebuilds)\n", double(h), best, best + 1, edge, -dip,
                    double(st.observerAltitudeM), st.observerRebuilds);
        CHECK_MSG(std::fabs(edge + dip) <= 0.078 + 1e-6,
                  "(i) at %.0f m the horizon dips %.3f degrees, within a pixel of the geometric %.3f",
                  double(h), -edge, dip);
        // the level surface
        PbrParams p;
        p.albedo = Colour(0, 0, 0);
        p.metalness = 1.0f;
        p.roughness = 1.0f;
        const MaterialId mat = r.s->createPbrMaterial(p);
        const NodeId box = r.s->createNode();
        r.s->attachMesh(box, r.s->createMesh(enginetest::unitCubeMesh()), mat);
        enginetest::poseRegistry()[r.s][box] = enginetest::NodePose{};
        const float d = 2000.0f, size = 0.1f * d;
        enginetest::setNodeScale(r.s, box, Vec3(size, size, size));
        enginetest::setNodePosition(r.s, box, Vec3(0.0f, h, -(d + 0.5f * size)));
        ImageF a, b;
        PbrParams q = p;
        q.emissive = Colour(0.2f, 0.2f, 0.2f); r.s->setPbrMaterial(mat, q); shoot(r, a);
        q.emissive = Colour(0.8f, 0.8f, 0.8f); r.s->setPbrMaterial(mat, q); shoot(r, b);
        const double tGot = (double(centreOf(b).g) - double(centreOf(a).g)) / 0.6;
        const double hk = h * 0.001, dk = d * 0.001;
        const double sR[3] = { 5.802e-3, 13.558e-3, 33.1e-3 };
        const double dO = std::max(0.0, 1.0 - std::fabs(hk - 25.0) / 15.0);
        const double oz[3] = { 0.650e-3, 1.881e-3, 0.085e-3 };
        double tRef = 0.0;
        for (int k = 0; k < 3; ++k)
            tRef += std::exp(-(sR[k] * std::exp(-hk / 8.0) + 4.44e-3 * 10.0 * std::exp(-hk / 1.2) +
                               oz[k] * dO) * dk);
        tRef /= 3.0;
        CHECK_MSG(std::fabs(tGot - tRef) <= 0.005,
                  "(i) at %.0f m a level surface 2 km away keeps %.4f of itself — the air at that "
                  "altitude says %.4f", double(h), tGot, tRef);
        r.s->setNodeVisible(box, false);
    }
    r.v->setCamera(level);
    r.s->setSky(atmosphere(45.0f, 180.0f));

    // ---- THE COST AT 1080p, ON THE GPU (JAH_ATMO_COST=1; a measurement, never
    // asserted — run it under scripts/gpu-exclusive.sh with the clocks locked).
    // The four jobs' own timestamp pairs (the frame monitor's cache rows,
    // CacheKind::Atmosphere, CacheWork::gpuMs) in three regimes — a still frame,
    // the sun moving every frame, a dial dragged every frame — and the sky pass
    // as paired arms in this one process on a 1920x1080 view holding a floor:
    // the atmosphere against a one-texel image sky (both a full-screen quad at
    // queue 0), the frame's GPU time differenced.
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
        const auto collect = [&](int frames, const std::function<void(int)> &perFrame,
                                 double &frameMs, int &frameN, double jobMs[4], int jobN[4]) {
            r.e->setFrameMonitor(MonitorLevel::Review);
            std::vector<FrameRecord> drop;
            r.e->takeFrameRecords(drop);
            for (int i = 0; i < frames; ++i) { perFrame(i); r.e->renderOneFrame(); }
            r.e->setFrameMonitor(MonitorLevel::Off);
            std::vector<FrameRecord> recs;
            r.e->takeFrameRecords(recs);
            static const char *kJobs[4] = { "Jahshaka/AtmoTransmittance", "Jahshaka/AtmoMultiScatter",
                                            "Jahshaka/AtmoSkyView", "Jahshaka/AtmoAerial" };
            for (const FrameRecord &f : recs) {
                if (f.gpuMs > 0.0f) { frameMs += f.gpuMs; ++frameN; }
                for (const CacheWork &w : f.cacheWork)
                    if (w.cache == CacheKind::Atmosphere && w.gpuMs >= 0.0f)
                        for (int k = 0; k < 4; ++k)
                            if (w.detail == kJobs[k]) { jobMs[k] += w.gpuMs; ++jobN[k]; }
            }
        };
        const auto report = [](const char *what, double frameMs, int frameN, const double jobMs[4],
                               const int jobN[4]) {
            std::printf("    COST %-12s frame GPU %.4f ms (%d frames) | per dispatch: transmittance %.4f (%d) "
                        "multi-scatter %.4f (%d) sky view %.4f (%d) aerial %.4f (%d) ms\n", what,
                        frameN ? frameMs / frameN : -1.0, frameN,
                        jobN[0] ? jobMs[0] / jobN[0] : -1.0, jobN[0], jobN[1] ? jobMs[1] / jobN[1] : -1.0,
                        jobN[1], jobN[2] ? jobMs[2] / jobN[2] : -1.0, jobN[2],
                        jobN[3] ? jobMs[3] / jobN[3] : -1.0, jobN[3]);
        };
        double sumImg = 0.0, sumAtmo = 0.0;
        int nImg = 0, nAtmo = 0;
        for (int round = 0; round < 6; ++round)
            for (int arm = 0; arm < 2; ++arm) {
                cs->setSky(arm ? atmosphere(45.0f, 150.0f) : image);
                for (int i = 0; i < 20; ++i) r.e->renderOneFrame();
                double jm[4] = { 0, 0, 0, 0 };
                int jn[4] = { 0, 0, 0, 0 };
                collect(60, [](int) {}, arm ? sumAtmo : sumImg, arm ? nAtmo : nImg, jm, jn);
            }
        std::printf("    COST 1080p still frame GPU: image sky %.4f ms (%d), atmosphere %.4f ms (%d), the "
                    "sky pass difference %.4f ms\n", nImg ? sumImg / nImg : -1.0, nImg,
                    nAtmo ? sumAtmo / nAtmo : -1.0, nAtmo,
                    (nAtmo && nImg) ? sumAtmo / nAtmo - sumImg / nImg : 0.0);
        for (int regime = 0; regime < 2; ++regime) {
            double fm2 = 0.0, jm[4] = { 0, 0, 0, 0 };
            int fn = 0, jn[4] = { 0, 0, 0, 0 };
            cs->setSky(atmosphere(45.0f, 150.0f));
            for (int i = 0; i < 10; ++i) r.e->renderOneFrame();
            collect(60, [&](int i) {
                if (regime == 0) cs->setSky(atmosphere(30.0f + 0.25f * float(i), 150.0f));      // the sun
                else cs->setSky(atmosphere(45.0f, 150.0f, 0.0f, 5.0f + 0.25f * float(i)));     // a dial
            }, fm2, fn, jm, jn);
            report(regime == 0 ? "sun move" : "dial drag", fm2, fn, jm, jn);
        }
        r.v->setEnabled(true);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok (%d failures)\n", failures);
    return failures ? 1 : 0;
}
