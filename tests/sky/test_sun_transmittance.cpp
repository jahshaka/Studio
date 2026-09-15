// THE SUN'S COLOUR IS THE AIR ON ITS OWN RAY, AND NOTHING ELSE (lane
// SKY-DENSITY-1) — headless, framework-free, links JahshakaEngine only (a
// reachable DISPLAY and a Vulkan driver or lavapipe, like tests/engine).
//
// THE DEFECT THIS SUITE IS THE GATE ON. `Ogre::AtmosphereNpr`'s single
// `density` parameter used to drive BOTH the sky dome's look and the
// transmittance applied to the SUNLIGHT: `Scene::atmosphereSunTint` read the
// component's own `lightDensity = densityCoeff / sunHeight^0.75` absorption
// term. Two different physical quantities on one dial — the sky's radiance is
// an integral of scattering over a whole view ray (and AtmosphereNpr is a
// non-physical model of it, so its dial is artistic), while the sun's colour is
// the extinction along the ONE ray to the sun, which is Beer-Lambert. Tuning
// the sky for its look moved the sunlight's colour (SKY-TUNE-1 had to ship a
// compromise inside the joint optimum of the two), and a sunlight tune would
// have moved the sky.
//
// THE MODEL, since this lane: tint = exp(-tau * m(elevation)) / exp(-tau * m(90)),
// tau = Rayleigh + Angstrom aerosol + ozone at 600/550/450 nm (Preetham et al.
// 1999 A.2, at the turbidity the SKY's defaults were fitted to), m =
// Kasten-Young airmass; `AtmosphereSky::sunHaze` (the turbidity) is its only
// input. OgreSky.cpp carries the derivation.
//
// WHAT IS ASSERTED
//   1. the tint at the zenith is exactly (1,1,1) — the picked colour IS the
//      noon colour, the contract every authored sun intensity rests on;
//   2. at 60/45/30/20/15/10/5/2 degrees it matches a reference computed OUTSIDE
//      this tree — the same model integrated SPECTRALLY (5 nm from 380 to 750
//      through the CIE 1931 observer into linear sRGB, spikes/skyd/model.py) —
//      within the deviation three channels can achieve, stated per row;
//   3. THE SEPARATION, both ways, which is the lane: moving the sky's `density`
//      leaves the tint BIT-IDENTICAL, and moving `sunHaze` leaves every sky
//      PIXEL bit-identical;
//   4. the dial is monotone and physical (more air = dimmer and redder), and
//      below a purely molecular atmosphere it is held rather than amplifying;
//   5. the Earth's occlusion still ends sunlight at the horizon (SUN-DISC-1).
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

static SkyDesc atmosphereSky(float density, float haze, float elevationDeg)
{
    SkyDesc d;
    d.mode = SkyMode::Atmosphere;
    d.atmosphere.density = density;
    d.atmosphere.sunHaze = haze;
    d.atmosphere.hasSun = true;
    const Vec3 toSun = towardsSun(elevationDeg);
    d.atmosphere.sunDir[0] = toSun.x;
    d.atmosphere.sunDir[1] = toSun.y;
    d.atmosphere.sunDir[2] = toSun.z;
    return d;
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

    const float kDefaultDensity = 0.25f, kDefaultHaze = 2.5f;

    // ---- 1. THE ZENITH IS THE ANCHOR ---------------------------------------
    if (!s->setSky(atmosphereSky(kDefaultDensity, kDefaultHaze, 90.0f))) {
        std::printf("FAIL: the analytic sky applies: %s\n", engine->lastError().c_str());
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

    // ---- 2. AGAINST THE SPECTRAL REFERENCE ---------------------------------
    // ref = the same atmosphere (turbidity 2.5) integrated at 5 nm from 380 to
    // 750 nm through the CIE 1931 observer into linear sRGB. `tol` is in STOPS
    // and is what three wavelengths can do against that integral: the band
    // average of a varying tau is not an exponential, and the gap opens as the
    // airmass does. A channel whose reference is under 0.02 is asserted
    // absolutely instead (there the sRGB primaries no longer contain the beam).
    struct Row { float elev; float ref[3]; float tol; };
    const Row rows[] = {
        { 60.0f, { 0.9666f, 0.9571f, 0.9335f }, 0.10f },
        { 45.0f, { 0.9131f, 0.8893f, 0.8318f }, 0.10f },
        { 30.0f, { 0.8035f, 0.7534f, 0.6415f }, 0.12f },
        { 20.0f, { 0.6581f, 0.5809f, 0.4269f }, 0.16f },
        { 15.0f, { 0.5391f, 0.4473f, 0.2834f }, 0.22f },
        { 10.0f, { 0.3654f, 0.2681f, 0.1264f }, 0.32f },
        {  5.0f, { 0.1296f, 0.0679f, 0.0127f }, 0.70f },
        {  2.0f, { 0.0175f, 0.0046f, 0.0000f }, 1.00f },
    };
    std::printf("== the sun's transmittance at haze %.1f, against the spectral reference ==\n",
                double(kDefaultHaze));
    for (const Row &r : rows) {
        if (!s->setSky(atmosphereSky(kDefaultDensity, kDefaultHaze, r.elev))) {
            std::printf("FAIL: sky at %.0f degrees: %s\n", double(r.elev), engine->lastError().c_str());
            ++failures;
            continue;
        }
        const Colour t = s->atmosphereSunTint(towardsSun(r.elev));
        const float got[3] = { t.r, t.g, t.b };
        bool ok = true;
        float worst = 0.0f;
        for (int c = 0; c < 3; ++c) {
            if (r.ref[c] < 0.02f) { ok = ok && std::fabs(got[c] - r.ref[c]) < 0.03f; continue; }
            const float stops = std::fabs(std::log2(std::max(got[c], 1e-6f) / r.ref[c]));
            worst = std::max(worst, stops);
            ok = ok && stops <= r.tol;
        }
        CHECK_MSG(ok, "%4.0f deg: %.4f %.4f %.4f  (reference %.4f %.4f %.4f, worst %.3f of %.2f stops)",
                  double(r.elev), got[0], got[1], got[2],
                  double(r.ref[0]), double(r.ref[1]), double(r.ref[2]), double(worst), double(r.tol));
        // ...and a low sun is RED-SHIFTED and DIMMER, which is the look half.
        if (r.elev <= 15.0f)
            CHECK_MSG(got[0] > got[2] * 1.5f && got[0] < 1.0f,
                      "%4.0f deg: reddened and dimmed (r %.4f vs b %.4f)",
                      double(r.elev), got[0], got[2]);
    }

    // ---- 3a. THE SKY'S DIAL DOES NOT TOUCH THE SUN -------------------------
    // THE LANE, in one assertion: the same sun through four very different
    // skies, bit-identical. Before this lane, density 0.10 against 0.60 moved
    // the 10-degree tint from (0.94,0.90,0.82) to (0.70,0.53,0.31).
    {
        const float elev = 10.0f;
        Colour first(0, 0, 0, 0);
        bool identical = true;
        const float densities[] = { 0.10f, 0.25f, 0.47f, 0.90f };
        for (int i = 0; i < 4; ++i) {
            if (!s->setSky(atmosphereSky(densities[i], kDefaultHaze, elev))) { ++failures; continue; }
            engine->renderOneFrame();
            const Colour t = s->atmosphereSunTint(towardsSun(elev));
            std::printf("    density %.2f -> tint %.6f %.6f %.6f\n", double(densities[i]), t.r, t.g, t.b);
            if (i == 0) first = t;
            else identical = identical && t.r == first.r && t.g == first.g && t.b == first.b;
        }
        CHECK_MSG(identical,
                  "the SKY's density moves the sun's colour by NOTHING — four skies, one tint");
    }

    // ---- 3b. ...AND THE SUN'S DIAL DOES NOT TOUCH THE SKY -------------------
    // The other half, on PIXELS: the sky dome is the picture, and `sunHaze` may
    // not move one bit of it. (It also must not re-capture the environment or
    // stale the probe grid — Scene::setSky applies it as its own piece — but
    // pixels are what a user sees.)
    {
        const float elev = 12.0f;
        auto shoot = [&](float haze, Image &out) {
            if (!s->setSky(atmosphereSky(kDefaultDensity, haze, elev))) return false;
            engine->renderOneFrame();
            engine->renderOneFrame();
            return view->readPixels(out);
        };
        Image a, b, c;
        const bool got = shoot(1.0f, a) && shoot(kDefaultHaze, b) && shoot(6.0f, c);
        CHECK_MSG(got, "the sky renders at three haze values");
        if (got) {
            size_t diffAB = 0, diffAC = 0;
            for (size_t i = 0; i < a.rgba.size(); ++i) {
                if (a.rgba[i] != b.rgba[i]) ++diffAB;
                if (a.rgba[i] != c.rgba[i]) ++diffAC;
            }
            CHECK_MSG(diffAB == 0 && diffAC == 0,
                      "the SUN's haze moves the sky by NOTHING (%zu / %zu of %zu bytes differ "
                      "at haze 2.5 / 6.0 against 1.0)", diffAB, diffAC, a.rgba.size());
            // ...while the tint it is there to move DID move, at the same
            // elevation — otherwise the case above proves only that nothing works.
            if (!s->setSky(atmosphereSky(kDefaultDensity, 1.0f, elev))) ++failures;
            const Colour clear = s->atmosphereSunTint(towardsSun(elev));
            if (!s->setSky(atmosphereSky(kDefaultDensity, 6.0f, elev))) ++failures;
            const Colour hazy = s->atmosphereSunTint(towardsSun(elev));
            CHECK_MSG(hazy.g < clear.g * 0.5f,
                      "...while the SUN moved: green %.4f at haze 1 against %.4f at haze 6",
                      clear.g, hazy.g);
        }
    }

    // ---- 4. THE DIAL IS PHYSICAL -------------------------------------------
    {
        const float elev = 10.0f;
        const float hazes[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 6.0f, 10.0f };
        float prevG = 2.0f, prevRatio = 0.0f;
        bool monotone = true, redder = true;
        for (float h : hazes) {
            if (!s->setSky(atmosphereSky(kDefaultDensity, h, elev))) { ++failures; continue; }
            const Colour t = s->atmosphereSunTint(towardsSun(elev));
            const float ratio = t.r / std::max(t.b, 1e-9f);
            std::printf("    haze %5.1f -> %.4f %.4f %.4f   (r/b %.2f)\n", double(h), t.r, t.g, t.b, double(ratio));
            monotone = monotone && t.g < prevG;
            redder = redder && ratio > prevRatio;
            prevG = t.g; prevRatio = ratio;
        }
        CHECK_MSG(monotone, "more air is strictly DIMMER at a 10-degree sun");
        CHECK_MSG(redder, "...and strictly REDDER (r/b climbs with every step)");

        // Below a purely molecular atmosphere the Angstrom term would go
        // negative and AMPLIFY the beam. It is held at 1 instead.
        if (!s->setSky(atmosphereSky(kDefaultDensity, 1.0f, elev))) ++failures;
        const Colour molecular = s->atmosphereSunTint(towardsSun(elev));
        if (!s->setSky(atmosphereSky(kDefaultDensity, 0.0f, elev))) ++failures;
        const Colour under = s->atmosphereSunTint(towardsSun(elev));
        CHECK_MSG(under.r == molecular.r && under.g == molecular.g && under.b == molecular.b,
                  "a haze under 1 is held at the molecular atmosphere (%.4f %.4f %.4f)",
                  under.r, under.g, under.b);
        CHECK_MSG(molecular.r <= 1.0f && molecular.g < 1.0f,
                  "...which still absorbs (Rayleigh and ozone do not switch off): %.4f %.4f %.4f",
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
            if (!s->setSky(atmosphereSky(kDefaultDensity, kDefaultHaze, b.elev))) { ++failures; continue; }
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
