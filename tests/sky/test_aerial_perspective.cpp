// sky.aerial_perspective — THE AIR IS A MEDIUM UNDER THE REALISTIC SKY (lane
// FOG-ATMO-1) — headless, framework-free, links JahshakaEngine only (a reachable
// DISPLAY and a Vulkan driver or lavapipe, like tests/engine).
//
// THE CHANGE THIS SUITE IS THE GATE ON. Until FOG-ATMO-1 the analytic sky
// ZEROED the atmosphere component's fog, so with the World fog off a surface
// 2 km away rendered exactly like one 2 m away, and the 2 km horizon plane met
// the sky in a hard line. The owner asked for Unreal's "sky atmosphere
// affecting height fog": a real horizon. The engine now keeps the air's own
// aerial perspective live under that sky, at the atmosphere's ONE density —
// the sea-level extinction of the same turbidity that tints the sun
// (OgreSky.cpp, airFogDensity; SKY-DENSITY-1's optical depths):
//
//     sigma(T) = tau_R(550)/8 km + beta(T) * tau_A/beta(550)/1.2 km
//     T 2.5:  0.10013/8000 + 0.06934*2.17535/1200 = 1.3822e-4 per metre
//
// and every fog layer fogs towards the sky's own scattering colour.
//
// THE INSTRUMENT. A black, rough box with an EMISSIVE face, read back in float
// (HDR-READBACK-1: the linear scene radiance before any grade), rendered twice
// at each distance with two emissive levels. Whatever else reaches the pixel —
// the fog colour, the ambient, the specular — is the same in both frames, so
//
//     T(d) = (pixel(E2) - pixel(E1)) / (E2 - E1)
//
// is the transmittance and nothing else. A single-colour sky is the control:
// there T must be exactly 1 at every distance, which proves the instrument has
// unit gain.
//
// WHAT IS ASSERTED
//   1. the control: no atmosphere, T == 1 at 125 m and at 2 km;
//   2. THE PHYSICS BAR: under the realistic sky with the fog OFF, T(d) matches
//      exp(-sigma d) within 0.003 at 125/250/500/1000/2000 m — 75.8 % at 2 km;
//   3. the colour it fogs towards is the SKY's: a black far surface moves
//      towards the sky just above it as the distance grows, and it is blue at
//      noon;
//   4. ONE ATMOSPHERE, ONE DENSITY: the turbidity dial moves T(2 km) by the
//      same law (T 1 -> 97.5 %, T 6 -> 42.3 %) and moves NO sky pixel;
//  4b. THE HAZE SWITCH (AIR-HAZE-TOGGLE-1): atmosphereHaze off reads T = 1 —
//      zero air extinction — at 125 m and 2 km, paired against the same sky
//      with it on, and moves no sky pixel;
//   5. the World fog ADDS: fog density D on top gives exp(-sigma d) * 2^(-D d);
//      and its breakthrough never bends the AIR (a bright surface at 2 km);
//   6. THE HEIGHT LAYER IS SKY-COLOURED under the realistic sky — a magenta
//      authored colour shows green — and its colour at sunset differs from
//      noon (redder); under the single-colour sky it is the authored colour;
//   7. THE HORIZON HAS NO LINE: a two-triangle 8 km ground under a sunset,
//      fully fogged, reads the sky just above it in every column.
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
// THE BAR ON A TRANSMITTANCE: 0.003 (the fix round's F8). The instrument's
// noise is 0.0006 (measured, every arm); 0.01 at 2 km was a 4.8 % error in
// sigma, a bar a wrong density could pass.
const double kTolT = 0.003;
// The reference, computed here from the stated model rather than read from the
// engine (the engine's number is what is being tested).
double airSigma(double haze)
{
    const double T = haze < 1.0 ? 1.0 : haze;
    const double beta = std::max(0.0, 0.04608 * T - 0.04586);
    return 0.10013 / 8000.0 + beta * 2.17535 / 1200.0;
}

SkyDesc realisticSky(float elevationDeg, float azimuthDeg, float haze)
{
    SkyDesc d;
    d.mode = SkyMode::Atmosphere;
    d.atmosphere.sunHaze = haze;
    d.atmosphere.hasSun = true;
    const float e = elevationDeg * kPi / 180.0f, a = azimuthDeg * kPi / 180.0f;
    d.atmosphere.sunDir[0] = std::cos(e) * std::sin(a);
    d.atmosphere.sunDir[1] = std::sin(e);
    d.atmosphere.sunDir[2] = -std::cos(e) * std::cos(a);
    return d;
}

SkyDesc colourSky(Scene *s)
{
    // A 1x1 grey equirect: a sky with no atmosphere to ask.
    const unsigned char px[4] = { 110, 130, 160, 255 };
    SkyDesc d;
    d.mode = SkyMode::Equirectangular;
    d.equirect = s->createTexture(1, 1, px, true);
    return d;
}

struct Rig {
    Engine *e = nullptr;
    View *v = nullptr;
    Scene *s = nullptr;
    NodeId box = 0;
    MaterialId mat = 0;
    const float eyeY = 2.0f;
};

// Places the box so its near face is `d` metres straight ahead and fills the
// centre of the frame (a tenth of the distance across: ~4 degrees of a 45).
void placeBox(Rig &r, float d)
{
    const float size = 0.1f * d;
    enginetest::setNodeScale(r.s, r.box, Vec3(size, size, size));
    enginetest::setNodePosition(r.s, r.box, Vec3(0.0f, r.eyeY, -(d + 0.5f * size)));
}

void setEmissive(Rig &r, float e, const Colour &albedo = Colour(0, 0, 0))
{
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 1.0f;
    p.emissive = Colour(e, e, e);
    r.s->setPbrMaterial(r.mat, p);
}

bool shoot(Rig &r, ImageF &out)
{
    for (int i = 0; i < 3; ++i) r.e->renderOneFrame();
    return r.v->readPixelsHdr(out);
}

Colour centreOf(const ImageF &img)
{
    // a 4x4 mean at the centre: the box face, well inside its edges
    double c[3] = { 0, 0, 0 };
    int n = 0;
    for (unsigned y = img.height / 2 - 2; y < img.height / 2 + 2; ++y)
        for (unsigned x = img.width / 2 - 2; x < img.width / 2 + 2; ++x) {
            const Colour p = img.at(x, y);
            c[0] += p.r; c[1] += p.g; c[2] += p.b; ++n;
        }
    return Colour(float(c[0] / n), float(c[1] / n), float(c[2] / n), 1.0f);
}

/// The transmittance at distance d, per the two-emissive instrument (green;
/// the fog weight is a scalar, so every channel carries the same number).
double transmittance(Rig &r, float d, Colour *blackPixel = nullptr,
                     float e1 = 0.15f, float e2 = 0.60f)
{
    placeBox(r, d);
    ImageF a, b;
    setEmissive(r, e1);
    if (!shoot(r, a)) return -1.0;
    setEmissive(r, e2);
    if (!shoot(r, b)) return -1.0;
    if (blackPixel) {
        ImageF k;
        setEmissive(r, 0.0f);
        if (!shoot(r, k)) return -1.0;
        *blackPixel = centreOf(k);
    }
    return (double(centreOf(b).g) - double(centreOf(a).g)) / double(e2 - e1);
}
}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-aerial-perspective-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);

    Rig r;
    r.e = engine.get();
    r.v = r.e->createOffscreenView("aerial", 128, 128, Colour(0, 0, 0));
    r.s = r.e->createScene("aerial");
    if (!r.v || !r.s) { std::printf("FAIL: view/scene\n"); return 1; }
    r.v->setScene(r.s);
    PostFxDesc fx;
    fx.hdrReadback = true;      // the linear radiance, before any grade
    r.v->setPostFx(fx);
    {
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(0.0f, r.eyeY, 0.0f),
                                                        Vec3(0.0f, r.eyeY, -100.0f));
        c.farClip = 8000.0f;    // the 2 km box, and its 200 m depth, inside the frustum
        r.v->setCamera(c);
    }
    {
        // The box with a material this suite owns (setEmissive rewrites it).
        PbrParams p;
        p.albedo = Colour(0, 0, 0);
        p.roughness = 1.0f;
        r.mat = r.s->createPbrMaterial(p);
        r.box = r.s->createNode();
        const MeshId mesh = r.s->createMesh(enginetest::unitCubeMesh());
        if (!r.mat || !r.box || !mesh || !r.s->attachMesh(r.box, mesh, r.mat)) {
            std::printf("FAIL: the box\n");
            return 1;
        }
        enginetest::poseRegistry()[r.s][r.box] = enginetest::NodePose{};
    }
    const float dists[] = { 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f };

    // ---- 1. THE CONTROL: NO ATMOSPHERE, UNIT GAIN --------------------------
    {
        CHECK_MSG(r.s->setSky(colourSky(r.s)), "the single-colour (image) sky applies: %s",
                  r.e->lastError().c_str());
        const double tNear = transmittance(r, 125.0f), tFar = transmittance(r, 2000.0f);
        std::printf("    single-colour sky, fog off: T(125 m) %.5f  T(2 km) %.5f\n", tNear, tFar);
        CHECK_MSG(std::fabs(tNear - 1.0) < 2e-3 && std::fabs(tFar - 1.0) < 2e-3,
                  "no atmosphere, no air: the instrument reads T = 1 at 125 m and at 2 km "
                  "(%.5f, %.5f) — unit gain, and the single-colour sky is unchanged", tNear, tFar);
    }

    // ---- 2. THE PHYSICS BAR ------------------------------------------------
    // The sun behind the camera, 45 degrees up: noon-ish, and no glow in view.
    CHECK_MSG(r.s->setSky(realisticSky(45.0f, 180.0f, 2.5f)), "the realistic sky applies: %s",
              r.e->lastError().c_str());
    std::printf("== the air at turbidity 2.5, the World fog OFF (sigma %.4e per metre) ==\n",
                airSigma(2.5));
    for (float d : dists) {
        const double t = transmittance(r, d);
        const double ref = std::exp(-airSigma(2.5) * d);
        CHECK_MSG(std::fabs(t - ref) <= kTolT,
                  "T(%5.0f m) = %.4f against exp(-sigma d) = %.4f (|diff| %.4f <= 0.003)",
                  double(d), t, ref, std::fabs(t - ref));
    }

    // ---- 3. IT FOGS TOWARDS THE SKY ----------------------------------------
    {
        Colour kNear, kFar;
        transmittance(r, 125.0f, &kNear);
        transmittance(r, 2000.0f, &kFar);
        // The sky itself, just above the box's top edge at 2 km (the box spans
        // about 12 of 128 rows; row 50 is sky above it).
        ImageF sky;
        setEmissive(r, 0.0f);
        shoot(r, sky);
        const Colour sk = sky.at(sky.width / 2, 50);
        auto dist = [&](const Colour &a) {
            return std::sqrt(double(a.r - sk.r) * (a.r - sk.r) + double(a.g - sk.g) * (a.g - sk.g) +
                             double(a.b - sk.b) * (a.b - sk.b));
        };
        std::printf("    black face: 125 m %.4f %.4f %.4f | 2 km %.4f %.4f %.4f | sky above %.4f %.4f %.4f\n",
                    kNear.r, kNear.g, kNear.b, kFar.r, kFar.g, kFar.b, sk.r, sk.g, sk.b);
        CHECK_MSG(dist(kFar) < dist(kNear) * 0.85,
                  "a far surface moves TOWARDS the sky behind it (distance to the sky %.4f at 2 km "
                  "against %.4f at 125 m)", dist(kFar), dist(kNear));
        CHECK_MSG(kFar.b > kFar.r && sk.b > sk.r,
                  "...and the haze is the sky's own blue at a high sun (b %.4f > r %.4f)",
                  kFar.b, kFar.r);
    }

    // ---- 4. ONE ATMOSPHERE, ONE DENSITY ------------------------------------
    {
        ImageF skyClear, skyHazy;
        r.s->setSky(realisticSky(45.0f, 180.0f, 1.0f));
        const double tClear = transmittance(r, 2000.0f);
        shoot(r, skyClear);
        r.s->setSky(realisticSky(45.0f, 180.0f, 6.0f));
        const double tHazy = transmittance(r, 2000.0f);
        shoot(r, skyHazy);
        const double refClear = std::exp(-airSigma(1.0) * 2000.0);
        const double refHazy = std::exp(-airSigma(6.0) * 2000.0);
        CHECK_MSG(std::fabs(tClear - refClear) <= kTolT,
                  "pure air (turbidity 1): T(2 km) %.4f against %.4f", tClear, refClear);
        CHECK_MSG(std::fabs(tHazy - refHazy) <= kTolT,
                  "a hazy day (turbidity 6): T(2 km) %.4f against %.4f", tHazy, refHazy);
        // The dome is the top rows (above the box): it may not move a bit.
        size_t differ = 0, total = 0;
        for (unsigned y = 0; y < 30; ++y)
            for (unsigned x = 0; x < skyClear.width; ++x, ++total) {
                const Colour a = skyClear.at(x, y), b = skyHazy.at(x, y);
                if (a.r != b.r || a.g != b.g || a.b != b.b) ++differ;
            }
        CHECK_MSG(differ == 0, "...and the turbidity moves NO sky pixel (%zu of %zu differ)",
                  differ, total);
        r.s->setSky(realisticSky(45.0f, 180.0f, 2.5f));
    }

    // ---- 4b. THE HAZE SWITCH (AIR-HAZE-TOGGLE-1) ----------------------------
    // `AtmosphereSky::atmosphereHaze` false takes the air's extinction off every
    // surface: the transmittance the instrument reads is exactly the NO-AIR one
    // (T = 1, the control's unit gain) at every distance, paired in this process
    // against the same sky with the switch on (exp(-sigma d)); and like the
    // turbidity it moves NO sky pixel. The sun's tint is untouched by design
    // (the switch is not an input to atmosphereSunTint).
    {
        const double tOn = transmittance(r, 2000.0f);
        ImageF domeOn, domeOff;
        shoot(r, domeOn);
        SkyDesc off = realisticSky(45.0f, 180.0f, 2.5f);
        off.atmosphere.atmosphereHaze = false;
        CHECK_MSG(r.s->setSky(off), "the realistic sky with its haze OFF applies: %s",
                  r.e->lastError().c_str());
        const double tOffNear = transmittance(r, 125.0f);
        const double tOffFar = transmittance(r, 2000.0f);
        shoot(r, domeOff);
        const double ref = std::exp(-airSigma(2.5) * 2000.0);
        std::printf("    haze ON: T(2 km) %.5f | haze OFF: T(125 m) %.5f  T(2 km) %.5f\n",
                    tOn, tOffNear, tOffFar);
        CHECK_MSG(std::fabs(tOn - ref) <= kTolT,
                  "with the switch ON the far surface is hazed: T(2 km) %.4f against exp(-sigma d) %.4f",
                  tOn, ref);
        CHECK_MSG(std::fabs(tOffNear - 1.0) < 2e-3 && std::fabs(tOffFar - 1.0) < 2e-3,
                  "with it OFF the air's extinction is ZERO: T = 1 at 125 m and at 2 km (%.5f, %.5f) "
                  "— the reading the no-air control gives", tOffNear, tOffFar);
        CHECK_MSG(tOffFar - tOn > 0.2,
                  "...a measurable difference at 2 km (%.4f against %.4f)", tOffFar, tOn);
        size_t differ = 0, total = 0;
        for (unsigned y = 0; y < 30; ++y)
            for (unsigned x = 0; x < domeOn.width; ++x, ++total) {
                const Colour a = domeOn.at(x, y), b = domeOff.at(x, y);
                if (a.r != b.r || a.g != b.g || a.b != b.b) ++differ;
            }
        CHECK_MSG(differ == 0, "...and the switch moves NO sky pixel (%zu of %zu differ)", differ, total);
        r.s->setSky(realisticSky(45.0f, 180.0f, 2.5f));
        const double tBack = transmittance(r, 2000.0f);
        CHECK_MSG(std::fabs(tBack - ref) <= kTolT,
                  "switched back ON, the haze returns: T(2 km) %.4f against %.4f", tBack, ref);
    }

    // ---- 5. THE WORLD FOG ADDS ---------------------------------------------
    {
        FogDesc fog;
        fog.enabled = true;
        fog.colour = Colour(1.0f, 0.0f, 1.0f);
        fog.density = 0.001f;          // exp2 per metre: half gone at 1 km on its own
        fog.breakFalloff = 0.0f;       // the law, not the breakthrough's bend of it
        r.s->setFog(fog);
        const double t = transmittance(r, 500.0f);
        const double ref = std::exp(-airSigma(2.5) * 500.0) * std::exp2(-0.001 * 500.0);
        CHECK_MSG(std::fabs(t - ref) <= kTolT,
                  "the World fog adds to the air: T(500 m) %.4f against exp(-sigma d) 2^(-D d) = %.4f",
                  t, ref);
        fog.enabled = false;
        r.s->setFog(fog);
        const double back = transmittance(r, 500.0f);
        const double air = std::exp(-airSigma(2.5) * 500.0);
        CHECK_MSG(std::fabs(back - air) <= kTolT,
                  "...and switching the World fog off leaves the air (%.4f against %.4f)", back, air);
    }

    // ---- 5b. THE BREAKTHROUGH BENDS THE WORLD FOG, NEVER THE AIR ------------
    // (the fix round's F4) The World fog ON at the constructor's breakthrough
    // pair (0.25 / 0.1) but with NO density of its own, and a box glowing at
    // luminance ~4 at 2 km. Before the fix the breakthrough weight multiplied
    // the WHOLE exponential — air included — so a bright surface kept a
    // quarter of the air's haze off. The air is pure extinction whatever the
    // pixel's brightness: T(2 km) is exp(-sigma d) at emissive 4.0 / 4.5 too.
    {
        FogDesc fog;
        fog.enabled = true;
        fog.colour = Colour(1.0f, 0.0f, 1.0f);
        fog.density = 0.0f;
        fog.breakMinBrightness = 0.25f;
        fog.breakFalloff = 0.1f;
        r.s->setFog(fog);
        const double t = transmittance(r, 2000.0f, nullptr, 4.0f, 4.5f);
        const double ref = std::exp(-airSigma(2.5) * 2000.0);
        CHECK_MSG(std::fabs(t - ref) <= kTolT,
                  "a surface at luminance ~4 under the World fog's breakthrough keeps the AIR's "
                  "haze: T(2 km) %.4f against exp(-sigma d) %.4f", t, ref);
        fog.enabled = false;
        r.s->setFog(fog);
    }

    // ---- 6. THE HEIGHT LAYER IS SKY-COLOURED -------------------------------
    // A height layer with no falloff and no distance fog: a uniform medium of
    // the height layer's own colour. The authored colour is magenta (no green),
    // so green in the fogged black face is the sky's.
    {
        FogDesc fog;
        fog.enabled = true;
        fog.colour = Colour(1.0f, 0.0f, 1.0f);
        fog.density = 0.0f;
        fog.heightDensity = 0.004f;
        fog.heightFalloff = 0.0f;
        fog.breakFalloff = 0.0f;
        r.s->setFog(fog);
        Colour noon, sunset, authored;
        r.s->setSky(realisticSky(60.0f, 0.0f, 2.5f));
        transmittance(r, 500.0f, &noon);
        r.s->setSky(realisticSky(3.0f, 0.0f, 2.5f));     // the sun low, straight ahead
        transmittance(r, 500.0f, &sunset);
        r.s->setSky(colourSky(r.s));
        transmittance(r, 500.0f, &authored);
        std::printf("    height layer at 500 m: noon %.4f %.4f %.4f | sunset %.4f %.4f %.4f | "
                    "single-colour sky %.4f %.4f %.4f\n",
                    noon.r, noon.g, noon.b, sunset.r, sunset.g, sunset.b,
                    authored.r, authored.g, authored.b);
        CHECK_MSG(noon.g > 0.02f && noon.g > authored.g + 0.02f,
                  "under the realistic sky the height layer is the SKY's colour, not the authored "
                  "magenta (green %.4f against %.4f)", noon.g, authored.g);
        CHECK_MSG(authored.g < 0.01f && authored.r > 0.1f,
                  "under the single-colour sky it is the authored colour (%.4f %.4f %.4f)",
                  authored.r, authored.g, authored.b);
        const double rbNoon = noon.r / std::max(noon.b, 1e-6f);
        const double rbSunset = sunset.r / std::max(sunset.b, 1e-6f);
        CHECK_MSG(rbSunset > rbNoon * 1.3,
                  "...and it follows the sun: the sunset layer is redder than the noon one "
                  "(r/b %.3f against %.3f)", rbSunset, rbNoon);
        fog.enabled = false;
        r.s->setFog(fog);
    }

    // ---- 7. THE HORIZON HAS NO LINE ----------------------------------------
    // THE OWNER'S CASE. A ground of two triangles 8 km across (the shape of the
    // mirror's horizon plane: four vertices, nothing in between), a camera two
    // metres up looking level, a SUNSET off to one side — where the sky's
    // horizon changes most from left to right — and a fog thick enough that
    // the ground just under the horizon is all fog. That ground must read what
    // the sky just above it reads, in EVERY column: the fog's colour is the
    // sky's own for the pixel's ray. (Measured before the per-pixel colour:
    // upstream's per-vertex colour, evaluated at the plane's four far corners,
    // painted one blend of them along the whole horizon — a bright band under
    // the dark half of the sky.)
    {
        r.s->setNodeVisible(r.box, false);
        const NodeId ground = r.s->createNode();
        MeshData plane;
        const float h = 4000.0f;
        plane.positions = { -h, 0.0f, -h,   h, 0.0f, -h,   h, 0.0f, h,   -h, 0.0f, h };
        plane.normals = { 0, 1, 0,  0, 1, 0,  0, 1, 0,  0, 1, 0 };
        plane.uvs = { 0, 0,  1, 0,  1, 1,  0, 1 };
        plane.indices = { 0, 2, 1,  0, 3, 2 };
        PbrParams gp;
        gp.albedo = Colour(0.2f, 0.2f, 0.2f);
        gp.roughness = 1.0f;
        const MaterialId gm = r.s->createPbrMaterial(gp);
        const MeshId gmesh = r.s->createMesh(plane);
        const bool built = ground && gm && gmesh && r.s->attachMesh(ground, gmesh, gm);
        CHECK_MSG(built, "the two-triangle ground is built");
        FogDesc fog;
        fog.enabled = true;
        fog.density = 0.1f;            // 2^-33 at the ground's nearest visible row
        fog.breakFalloff = 0.0f;
        r.s->setFog(fog);
        r.s->setSky(realisticSky(3.0f, 60.0f, 2.5f));
        ImageF img;
        const bool shot = built && shoot(r, img);
        CHECK_MSG(shot, "the horizon renders");
        if (shot) {
            // The horizon is the edge between the two centre rows of a level
            // camera: the row above it is sky (a sixth of a degree up), the row
            // below it is ground 650 m away and all fog (2^-65). Across that
            // edge the picture may step by no more than the sky's OWN step
            // between its last two rows — the gradient it has anyway — in any
            // column; a line is a step the sky does not have.
            const unsigned skyRow = img.height / 2 - 1, groundRow = img.height / 2;
            auto gap = [](const Colour &a, const Colour &b) {
                return std::max(std::fabs(double(a.r - b.r)),
                                std::max(std::fabs(double(a.g - b.g)), std::fabs(double(a.b - b.b))));
            };
            double worst = 0.0;
            float minG = 1e9f, maxG = 0.0f;
            for (unsigned x = 4; x < img.width - 4; x += 8) {
                const Colour above = img.at(x, skyRow - 1), sky = img.at(x, skyRow),
                             ground = img.at(x, groundRow);
                const double ratio = gap(sky, ground) / std::max(gap(above, sky), 1e-6);
                worst = std::max(worst, ratio);
                minG = std::min(minG, sky.g); maxG = std::max(maxG, sky.g);
            }
            const double skySpread = double(maxG) / std::max(double(minG), 1e-6);
            if (std::getenv("JAH_AERIAL_DUMP"))
                for (unsigned x : { 4u, 36u, 64u, 92u, 124u })
                    for (unsigned y = img.height / 2 - 4; y < img.height / 2 + 4; ++y) {
                        const Colour c = img.at(x, y);
                        std::printf("      x %3u y %3u  %.4f %.4f %.4f\n", x, y, c.r, c.g, c.b);
                    }
            std::printf("    horizon: the step across it is at worst %.2fx the sky's own last "
                        "row-to-row step; the sky's left-to-right spread %.2fx\n", worst, skySpread);
            CHECK_MSG(skySpread > 1.3,
                      "the fixture's horizon varies across the frame at this sunset (%.2fx), so the "
                      "next check can see an azimuth error", skySpread);
            CHECK_MSG(worst < 1.5,
                      "THE HORIZON HAS NO LINE: the fully fogged ground meets the sky with the sky's "
                      "own gradient in every column (worst step %.2fx the sky's, < 1.5x)", worst);
        }
        fog.enabled = false;
        r.s->setFog(fog);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok (%d failures)\n", failures);
    return failures ? 1 : 0;
}
