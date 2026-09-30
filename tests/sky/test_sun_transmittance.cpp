// THE SUN'S COLOUR IS THE AIR ON ITS OWN RAY (SKY-ATMOSPHERE-1; first lane
// SKY-DENSITY-1) — headless, framework-free, links JahshakaEngine only (a
// reachable DISPLAY and a Vulkan driver or lavapipe, like tests/engine).
//
// THE MODEL: `Scene::atmosphereSunTint` is the transmittance from the top of
// the planet's atmosphere to the observer along the ray to the sun, divided by
// its value at the zenith — Beer-Lambert over the optical depth of the three
// species of Hillaire's Earth (Rayleigh (5.802, 13.558, 33.1)e-6 per metre over
// an 8 km scale height; Mie extinction 4.44e-6 per metre times the haze over
// 1.2 km; ozone (0.650, 1.881, 0.085)e-6 in a 30 km tent at 25 km), the SAME
// model the sky is drawn with. (Until this lane it was Preetham's clear-sky
// terms at Kasten-Young airmass beside a non-physical sky — a separate model,
// and a separate dial: rows 2 and 3 of this suite said so. One atmosphere now.)
//
// WHAT IS ASSERTED
//   1. the tint at the zenith is exactly (1,1,1) — the picked colour IS the
//      noon colour, the contract every authored sun intensity rests on;
//   2. at 60/45/30/20/15/10/5/2 degrees it matches the SAME integral computed
//      here, independently (a 4000-step march on a sphere written out below),
//      within 2e-3 per channel;
//   3. ONE AIR: the haze moves the sky's pixels AND the sun (it is not a
//      sun-only dial any more — the sky, the sun and the aerial perspective are
//      one atmosphere);
//   4. the dial is monotone and physical (more haze = dimmer, and grey — the
//      aerosol does not redden), and no haze at all still absorbs (Rayleigh
//      and ozone do not switch off);
//   5. the Earth's occlusion still ends sunlight at the horizon (SUN-DISC-1);
//   6. a picture sky knows nothing about the air: white.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                               std::printf("\n"); ++failures; } \
                               else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const float kPi = 3.14159265358979323846f;

static Vec3 towardsSun(float elevationDeg, float azimuthDeg = 0.0f)
{
    const float e = elevationDeg * kPi / 180.0f, a = azimuthDeg * kPi / 180.0f;
    return Vec3(std::cos(e) * std::sin(a), std::sin(e), -std::cos(e) * std::cos(a));
}

static SkyDesc atmosphereSky(float haze, float elevationDeg)
{
    SkyDesc d;
    d.mode = SkyMode::Atmosphere;
    d.atmosphere.sunHaze = haze;
    d.atmosphere.hasSun = true;
    d.atmosphere.sunIlluminance = Colour(kPi, kPi, kPi, 1.0f);
    const Vec3 toSun = towardsSun(elevationDeg);
    d.atmosphere.sunDir[0] = toSun.x;
    d.atmosphere.sunDir[1] = toSun.y;
    d.atmosphere.sunDir[2] = toSun.z;
    return d;
}

// THE REFERENCE: the transmittance from an observer 2 m above a 6360 km planet
// to the top of a 100 km atmosphere, straight numerical integration of the
// stated coefficients (km units), written out here rather than read from the
// engine.
static void referenceTransmittance(double mu, double haze, double out[3])
{
    const double Rb = 6360.0, Rt = 6460.0, r = Rb + 0.002;
    const double sR[3] = { 5.802e-3, 13.558e-3, 33.1e-3 };
    const double oz[3] = { 0.650e-3, 1.881e-3, 0.085e-3 };
    const double mieExt = 4.440e-3 * haze;
    const double b = r * mu, c = r * r - Rt * Rt;
    const double tTop = -b + std::sqrt(b * b - c);
    const int n = 4000;
    const double dt = tTop / n, sinT = std::sqrt(std::max(0.0, 1.0 - mu * mu));
    double depth[3] = { 0, 0, 0 };
    for (int i = 0; i < n; ++i) {
        const double t = (i + 0.5) * dt;
        const double x = sinT * t, y = r + mu * t;
        const double h = std::max(0.0, std::sqrt(x * x + y * y) - Rb);
        const double dO = std::max(0.0, 1.0 - std::fabs(h - 25.0) / 15.0);
        for (int k = 0; k < 3; ++k)
            depth[k] += (sR[k] * std::exp(-h / 8.0) + mieExt * std::exp(-h / 1.2) + oz[k] * dO) * dt;
    }
    for (int k = 0; k < 3; ++k) out[k] = std::exp(-depth[k]);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-sun-transmittance-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("sky", 256, 256, Colour(0, 0, 0));
    Scene *s = engine->createScene("sky");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    // A camera looking at the horizon: the frame is then half sky dome, which is
    // what case 3b compares. Nothing else is in the scene — no geometry, no sun
    // disc (SkyDesc::sun is disabled by default) — so every pixel is the sky.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, -10.0f));

    const float kDefaultHaze = 1.0f;

    // ---- 1. THE ZENITH IS THE ANCHOR ---------------------------------------
    if (!s->setSky(atmosphereSky(kDefaultHaze, 90.0f))) {
        std::printf("FAIL: the atmosphere applies: %s\n", engine->lastError().c_str());
        return 1;
    }
    engine->renderOneFrame();
    {
        const Colour noon = s->atmosphereSunTint(towardsSun(90.0f));
        CHECK_MSG(std::fabs(noon.r - 1.0f) < 1e-4f && std::fabs(noon.g - 1.0f) < 1e-4f &&
                      std::fabs(noon.b - 1.0f) < 1e-4f,
                  "the zenith tint is 1,1,1 (%.5f %.5f %.5f) — the picked colour IS the noon colour",
                  noon.r, noon.g, noon.b);
    }

    // ---- 2. AGAINST THE MODEL'S OWN INTEGRAL ---------------------------------
    std::printf("== the sun's transmittance at haze %.1f, against the independent integral ==\n",
                double(kDefaultHaze));
    const float elevs[] = { 60.0f, 45.0f, 30.0f, 20.0f, 15.0f, 10.0f, 5.0f, 2.0f };
    double zen[3];
    referenceTransmittance(1.0, kDefaultHaze, zen);
    for (float e : elevs) {
        if (!s->setSky(atmosphereSky(kDefaultHaze, e))) { ++failures; continue; }
        const Colour t = s->atmosphereSunTint(towardsSun(e));
        double ref[3];
        referenceTransmittance(std::sin(double(e) * kPi / 180.0), kDefaultHaze, ref);
        for (int k = 0; k < 3; ++k) ref[k] /= zen[k];
        const float got[3] = { t.r, t.g, t.b };
        double worst = 0.0;
        for (int k = 0; k < 3; ++k) worst = std::max(worst, std::fabs(double(got[k]) - ref[k]));
        CHECK_MSG(worst < 2e-3, "%4.0f deg: %.4f %.4f %.4f  (reference %.4f %.4f %.4f, worst %.5f)",
                  double(e), got[0], got[1], got[2], ref[0], ref[1], ref[2], worst);
        if (e <= 15.0f)
            CHECK_MSG(got[0] > got[2] * 1.5f && got[0] < 1.0f,
                      "%4.0f deg: reddened and dimmed (r %.4f vs b %.4f)", double(e), got[0], got[2]);
    }

    // ---- 3. ONE AIR: THE HAZE MOVES THE SKY AND THE SUN ----------------------
    {
        const float elev = 12.0f;
        auto shoot = [&](float haze, Image &out) {
            if (!s->setSky(atmosphereSky(haze, elev))) return false;
            engine->renderOneFrame();
            engine->renderOneFrame();
            return view->readPixels(out);
        };
        Image a, c;
        const bool got = shoot(1.0f, a) && shoot(40.0f, c);
        CHECK_MSG(got, "the sky renders at two haze values");
        if (got) {
            size_t diff = 0;
            for (size_t i = 0; i < a.rgba.size(); ++i)
                if (a.rgba[i] != c.rgba[i]) ++diff;
            CHECK_MSG(diff > a.rgba.size() / 4,
                      "the haze moves the SKY (%zu of %zu bytes differ at haze 40 against 1)", diff,
                      a.rgba.size());
            if (!s->setSky(atmosphereSky(1.0f, elev))) ++failures;
            const Colour clear = s->atmosphereSunTint(towardsSun(elev));
            if (!s->setSky(atmosphereSky(40.0f, elev))) ++failures;
            const Colour hazy = s->atmosphereSunTint(towardsSun(elev));
            CHECK_MSG(hazy.g < clear.g * 0.8f,
                      "...and the SUN: green %.4f at haze 1 against %.4f at haze 40", clear.g, hazy.g);
        }
    }

    // ---- 4. THE DIAL IS PHYSICAL -------------------------------------------
    {
        const float elev = 10.0f;
        const float hazes[] = { 0.0f, 1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f };
        float prevG = 2.0f, firstRatio = -1.0f, worstRatio = 0.0f;
        bool monotone = true;
        for (float h : hazes) {
            if (!s->setSky(atmosphereSky(h, elev))) { ++failures; continue; }
            const Colour t = s->atmosphereSunTint(towardsSun(elev));
            const float ratio = t.r / std::max(t.b, 1e-9f);
            std::printf("    haze %5.1f -> %.4f %.4f %.4f   (r/b %.2f)\n", double(h), t.r, t.g, t.b, double(ratio));
            monotone = monotone && t.g < prevG;
            if (firstRatio < 0.0f) firstRatio = ratio;
            worstRatio = std::max(worstRatio, std::fabs(ratio / firstRatio - 1.0f));
            prevG = t.g;
        }
        CHECK_MSG(monotone, "more haze is strictly DIMMER at a 10-degree sun");
        // THE AEROSOL IS GREY in this model (Hillaire's Mie extinction has one
        // value for every wavelength), so it dims the beam without reddening
        // it: what reddens a low sun is the molecular air and its path length,
        // which the elevation carries (row 2). The retired model's aerosol had
        // an Angstrom wavelength exponent and did redden; this is the physics
        // verdict on that row.
        CHECK_MSG(worstRatio < 0.01f,
                  "...and GREY: the haze leaves r/b where the molecular air put it (worst %.4f)",
                  double(worstRatio));
        if (!s->setSky(atmosphereSky(0.0f, elev))) ++failures;
        const Colour molecular = s->atmosphereSunTint(towardsSun(elev));
        CHECK_MSG(molecular.r < 1.0f && molecular.g < molecular.r && molecular.b < molecular.g,
                  "no haze at all still absorbs, blue most (Rayleigh and ozone): %.4f %.4f %.4f",
                  molecular.r, molecular.g, molecular.b);
    }

    // ---- 5. THE EARTH STILL ENDS IT ----------------------------------------
    // SUN-DISC-1's occlusion band runs -0.305 to -0.835 degrees (the sun's own
    // disc setting through a refraction-lifted horizon). The light, the disc and
    // the shadow ride this one number to zero.
    {
        struct { float elev; const char *what; } band[] = {
            {  0.00f, "at the horizon the beam is still admitted" },
            { -0.55f, "mid-band it is part-occluded" },
            { -1.00f, "a set sun delivers nothing" },
            { -30.0f, "and a sun 30 degrees under the ground delivers nothing" },
        };
        float prev = 1.0f;
        for (const auto &b : band) {
            if (!s->setSky(atmosphereSky(kDefaultHaze, b.elev))) { ++failures; continue; }
            const Colour t = s->atmosphereSunTint(towardsSun(b.elev));
            const float m = std::max(std::max(t.r, t.g), t.b);
            std::printf("    %6.2f deg -> %.3e  (%s)\n", double(b.elev), double(m), b.what);
            if (b.elev <= -1.0f) CHECK_MSG(m == 0.0f, "%s (%.3e)", b.what, double(m));
            else CHECK_MSG(m <= prev, "%s (%.3e)", b.what, double(m));
            prev = m;
        }
    }

    // ---- 6. NO ANALYTIC SKY, NO OPINION ------------------------------------
    {
        SkyDesc none;
        CHECK_MSG(s->setSky(none), "the sky goes away: %s", engine->lastError().c_str());
        const Colour off = s->atmosphereSunTint(towardsSun(5.0f));
        CHECK_MSG(off.r == 1.0f && off.g == 1.0f && off.b == 1.0f,
                  "a picture sky knows nothing about the air: the tint is white");
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok (%d failures)\n", failures);
    return failures ? 1 : 0;
}
