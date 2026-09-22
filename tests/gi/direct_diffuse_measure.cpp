// DIRECT-DIFFUSE-1 (lane MEASURE-1a) — a TOOL, not a suite. It answers ONE
// question with numbers: WHY does a directional light's DIRECT diffuse term on
// a matte floor render ~66 % of `albedo * P / pi`, while the environment path
// on the same floor in the same frame is exact?
//
// (The finding it chases: `spikes/gather-1a/FINDINGS.md` §1b, printed by
// gi.gather_reference — "the direct term, shadows on : 0.0471 against
// albedo * P / pi = 0.0716, 66 %". Ledger §955/§956.)
//
// THE CLOSED FORM IS STATED BEFORE EVERY MEASUREMENT, from the pin's own
// shader source, so an arm either confirms the arithmetic or falsifies it:
//
//   irisgl/thirdparty/ogre-next/Samples/Media/Hlms/Pbs/Any/Main/
//       200.BRDFs_piece_ps.any:144-230   — BRDF_Default (this is the answer)
//       200.BRDFs_piece_ps.any:74-140    — BRDF_CookTorrance (no energyFactor)
//       800.PixelShader_piece_ps.any:294 — pixelData.diffuse *= material.kD
//       800.PixelShader_piece_ps.any:330 — metallic workflow F0 = lerp(0.04, ..)
//       800.PixelShader_piece_ps.any:362 — roughness = perceptualRoughness^2
//   Components/Hlms/Pbs/src/OgreHlmsPbsDatablock.cpp:492-497 — kD = albedo / pi
//   irisgl/engine/src/OgreScene.cpp:1202 — powerScale = intensity * pi
//
// BRDF_Default's diffuse lobe is the *normalized* Disney diffuse of "Moving
// Frostbite to Physically Based Rendering" (Lagarde & de Rousiers):
//
//   energyBias   = 0.5 * perceptualRoughness
//   energyFactor = lerp( 1.0, 1.0/1.51, perceptualRoughness )     <-- 0.6623 at r = 1
//   fd90         = energyBias + 2 * VdotH^2 * perceptualRoughness
//   lightScatter = 1 + (fd90 - 1) * (1 - NdotL)^5
//   viewScatter  = 1 + (fd90 - 1) * (1 - NdotV)^5
//   Rd           = lightScatter * viewScatter * energyFactor * fresnelD * (albedo/pi)
//   pixel        = NdotL * (Rs * lightSpecular + Rd * lightDiffuse)
//
// so `albedo * P / pi` is NOT the closed form of this BRDF's direct diffuse at
// any geometry, and at normal incidence with roughness 1 it overstates it by
// exactly 1/0.66225 = 1.51. The arms below vary one thing at a time and check
// that prediction against the picture.
//
// THE PICTURE'S CURRENCY. The view is plain: `PostFxDesc::hdr` false, no
// bloom/SSAO/SSR/looks, so the scene renders straight into the offscreen
// RTT at PFG_RGBA8_UNORM (irisgl/engine/src/OgreView.cpp:1621 and
// OgreChain.cpp:973) — LINEAR, no sRGB encode, and NOT dithered (the dither
// rides the tonemap quad only: OgreView.cpp:645 `if (chainDesc().hdr)`). So
// the quantisation is a hard +-0.5/255 and the tool works around it by
// BISECTING the light power until the code steps: that pins a ratio to about
// 0.1 % instead of the 4 % an 8-bit read of a dim floor can carry. Arm D
// measures the transfer from an emissive ramp rather than assuming it.
//
// Build: `cmake --build <build> --target direct_diffuse_measure`; run it on a
// rig display (a Vulkan engine cannot boot without one).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static const unsigned kSize = 512u;
static const double PI = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// The surface, in the three shapes this tool needs. They differ ONLY in the
// specular/fresnel inputs, so a pair of arms isolates one factor each.
enum class Surf {
    NoSpec,      ///< Specular workflow, ior 1.0 => F0 = 0, kS black => Rs = 0
    NoSpecF04,   ///< Specular workflow, F0 = 0.04 explicit, kS black => Rs = 0
                 ///  (the only way to see fresnelD with no specular leak)
    Metallic     ///< our shipped default: Metallic workflow, metalness 0
                 ///  => F0 = 0.04, kS white => the specular lobe is live
};

struct Cfg {
    double rho = 0.8;             ///< linear albedo (grey)
    double cosTheta = 1.0;        ///< NdotL: the light's tilt from straight down
    double viewElevDeg = 90.0;    ///< camera elevation above the floor
    double roughness = 1.0;       ///< perceptual roughness, as authored
    Surf   surf = Surf::NoSpec;
    const char *brdf = "Default";
    bool   shadows = true;
};

// ---------------------------------------------------------------------------
// THE CLOSED FORM, transcribed from the pieces named in the header.
struct Pred {
    double rd = 0.0;      ///< diffuse, per unit lightDiffuse, already * NdotL
    double rs = 0.0;      ///< specular, per unit lightSpecular, already * NdotL
    double total = 0.0;   ///< (rd + rs), i.e. the pixel at P = 1
    double lambert = 0.0; ///< rho * NdotL / pi — what the 66 % claim compares to
    bool   known = true;
};

static double dot3(const double a[3], const double b[3])
{ return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static Pred predict(const Cfg &c)
{
    Pred p;
    const double sinT = std::sqrt(std::max(0.0, 1.0 - c.cosTheta * c.cosTheta));
    // N up; L toward the light; V toward the camera (elevation in the +Z half).
    const double N[3] = { 0.0, 1.0, 0.0 };
    const double L[3] = { -sinT, c.cosTheta, 0.0 };
    const double e = c.viewElevDeg * PI / 180.0;
    const double V[3] = { 0.0, std::sin(e), std::cos(e) };
    double H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
    const double hl = std::sqrt(dot3(H, H));
    for (int k = 0; k < 3; ++k) H[k] /= hl;

    const double NdotL = std::max(0.0, dot3(N, L));
    const double NdotV = std::max(0.0, dot3(N, V));
    const double NdotH = std::max(0.0, dot3(N, H));
    const double VdotH = std::max(0.0, dot3(V, H));

    // OgreMaterials.cpp:150 clamps the authored roughness at 1e-4.
    const double rp = std::max(c.roughness, 1e-4);
    // 800.PixelShader_piece_ps.any:362 (perceptual_roughness is on).
    const double rough = std::max(rp * rp, 0.001);
    const double sqR = rough * rough;

    const double F0 = (c.surf == Surf::NoSpec) ? 0.0 : 0.04;
    const double kS = (c.surf == Surf::Metallic) ? 1.0 : 0.0;

    const std::string brdf = c.brdf;
    const bool isDefault = (brdf == "Default" || brdf == "DefaultSeparateDiffuseFresnel");
    const bool sepDiffuseFresnel = (brdf == "DefaultSeparateDiffuseFresnel");
    if (!isDefault && brdf != "CookTorrance") { p.known = false; return p; }
    // CookTorrance's specular lobe is a different pair of terms; this tool only
    // ever runs it with kS = 0, where the lobe is identically zero.
    if (brdf == "CookTorrance" && kS != 0.0) { p.known = false; return p; }

    // fresnelD. PbsBrdf::Default carries NO FLAG_HAS_DIFFUSE_FRESNEL
    // (OgreHlmsPbsDatablock.h:75,113 — "the new Default BRDF in 3.0 does not
    // include diffuse fresnel"), so the shipped material's fresnelD is exactly
    // 1; "DefaultSeparateDiffuseFresnel" turns the term on.
    const double fresnelD = sepDiffuseFresnel
        ? (1.0 - F0 + std::pow(1.0 - NdotL, 5.0) * F0)
        : 1.0;

    if (brdf == "CookTorrance") {
        // 200.BRDFs_piece_ps.any:139 — no energy factor, no scatter terms.
        p.rd = NdotL * fresnelD * (c.rho / PI);
    } else {
        const double energyBias   = 0.5 * rp;
        const double energyFactor = 1.0 + (1.0 / 1.51 - 1.0) * rp;
        const double fd90         = energyBias + 2.0 * VdotH * VdotH * rp;
        const double lightScatter = 1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotL, 5.0);
        const double viewScatter  = 1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotV, 5.0);
        p.rd = NdotL * lightScatter * viewScatter * energyFactor * fresnelD * (c.rho / PI);

        if (kS != 0.0) {
            // GGX + Smith height-correlated, full32 branch (:157-186). The 1/pi
            // of the specular lobe is folded into G.
            const double lamV = NdotL * std::sqrt((-NdotV * sqR + NdotV) * NdotV + sqR);
            const double lamL = NdotV * std::sqrt((-NdotL * sqR + NdotL) * NdotL + sqR);
            const double G = 0.5 / ((lamV + lamL + 1e-6) * PI);
            const double f = (NdotH * sqR - NdotH) * NdotH + 1.0;
            const double R = sqR / (f * f);
            const double fresnelS = F0 + std::pow(1.0 - VdotH, 5.0) * (1.0 - F0);
            p.rs = NdotL * fresnelS * R * G * kS;
        }
    }
    p.total = p.rd + p.rs;
    p.lambert = c.rho * NdotL / PI;
    return p;
}

// ---------------------------------------------------------------------------
struct Rig {
    Engine *e = nullptr;
    Scene  *s = nullptr;
    View   *v = nullptr;
    MeshId  cube = 0;
    NodeId  floorNode = 0;
    NodeId  sun = 0;
    Cfg     applied;
    bool    haveApplied = false;
};

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

/// The camera: orthographic, at `elev` degrees above the floor, aimed at the
/// origin. Built by hand — the lookAt helper's +Y up is degenerate at 90.
static CameraDesc orthoCameraAt(double elevDeg)
{
    const double e = elevDeg * PI / 180.0;
    CameraDesc c;
    const double R = 40.0;
    c.position = Vec3(0.0f, float(R * std::sin(e)), float(R * std::cos(e)));
    // A rotation about X by -elev takes the default forward (-Z) onto
    // (0, -sin e, -cos e), i.e. down at the origin.
    c.orientation = Quat(float(std::sin(-e * 0.5)), 0.0f, 0.0f, float(std::cos(-e * 0.5)));
    c.orthographic = true;
    c.orthoSize = 6.0f;
    c.nearClip = 0.1f;
    c.farClip = 400.0f;
    return c;
}

static void applySurface(Rig &r, const Cfg &c)
{
    PbrParams p;
    p.albedo = Colour(float(c.rho), float(c.rho), float(c.rho));
    p.roughness = float(c.roughness);
    p.brdf = c.brdf;
    switch (c.surf) {
    case Surf::NoSpec:
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;                                  // F0 = ((1-ior)/(1+ior))^2 = 0
        p.specularColour = Colour(0.f, 0.f, 0.f);      // kS = 0 => Rs = 0
        break;
    case Surf::NoSpecF04:
        p.workflow = PbrParams::Workflow::Specular;
        p.useFresnelColour = true;
        p.fresnelColour = Colour(0.04f, 0.04f, 0.04f);
        p.specularColour = Colour(0.f, 0.f, 0.f);
        break;
    case Surf::Metallic:
        p.workflow = PbrParams::Workflow::Metallic;
        p.metalness = 0.0f;                            // F0 = 0.04
        p.specularColour = Colour(1.f, 1.f, 1.f);      // kS = 1: the lobe is live
        break;
    }
    const MaterialId m = r.s->createPbrMaterial(p);
    r.s->attachMesh(r.floorNode, r.cube, m);
}

/// The light, at power P (the IRRADIANCE on a perpendicular surface): the
/// helper divides by pi because OgreScene.cpp:1202 multiplies by it.
static void applyLight(Rig &r, const Cfg &c, double P)
{
    if (r.sun) r.s->removeNode(r.sun);
    const double sinT = std::sqrt(std::max(0.0, 1.0 - c.cosTheta * c.cosTheta));
    r.sun = enginetest::addDirectionalLight(
        r.s, Vec3(float(sinT), float(-c.cosTheta), 0.0f), float(P));
}

/// The centre block's mean code (0..255) in the red channel, plus its spread.
static double centreCode(const Image &img, double *spreadOut = nullptr)
{
    const int cx = int(img.width) / 2, cy = int(img.height) / 2, half = 20;
    double sum = 0.0; int n = 0; double lo = 1e9, hi = -1e9;
    for (int y = cy - half; y <= cy + half; ++y)
        for (int x = cx - half; x <= cx + half; ++x) {
            const double v = double(img.at(unsigned(x), unsigned(y)).r) * 255.0;
            sum += v; ++n; lo = std::min(lo, v); hi = std::max(hi, v);
        }
    if (spreadOut) *spreadOut = hi - lo;
    return n ? sum / double(n) : 0.0;
}

/// One measurement: the linear pixel value of the lit floor at power P.
static double measureAt(Rig &r, const Cfg &c, double P, int frames = 8,
                        double *spreadOut = nullptr)
{
    if (!r.haveApplied || r.applied.rho != c.rho || r.applied.roughness != c.roughness ||
        r.applied.surf != c.surf || std::string(r.applied.brdf) != std::string(c.brdf)) {
        applySurface(r, c);
        r.applied = c;
        r.haveApplied = true;
    }
    r.applied.rho = c.rho; r.applied.roughness = c.roughness;
    r.applied.surf = c.surf; r.applied.brdf = c.brdf;
    r.v->setShadows(c.shadows);
    r.v->setCamera(orthoCameraAt(c.viewElevDeg));
    applyLight(r, c, P);
    render(r.e, frames);
    Image img;
    r.v->readPixels(img);
    return centreCode(img, spreadOut) / 255.0;
}

/// THE PRECISE READ. The picture is 8-bit and undithered, so a single read of a
/// dim floor carries +-0.5/255. Instead: bisect the light power until the code
/// STEPS from `target` to `target+1`. At that power the rendered value is
/// exactly (target + 0.5)/255 (a UNORM write rounds to nearest), so the ratio
/// the arm is after is pinned to about 0.1 % regardless of how dim the arm is.
/// Returns `pixel / P` — the arm's transfer, independent of the power used.
static double measureSlope(Rig &r, const Cfg &c, int target = 200)
{
    const double want = (double(target) + 0.5) / 255.0;
    // A coarse read to get into the right decade.
    double P = 1.0;
    double v = measureAt(r, c, P, 8);
    if (v <= 1e-6) return 0.0;
    P *= want / v;
    // Bracket.
    double lo = P * 0.8, hi = P * 1.25;
    for (int guard = 0; guard < 8; ++guard) {
        const double cl = std::floor(measureAt(r, c, lo, 6) * 255.0 + 0.25);
        if (cl <= double(target) - 0.5) break;
        lo *= 0.8;
    }
    for (int guard = 0; guard < 8; ++guard) {
        const double ch = std::floor(measureAt(r, c, hi, 6) * 255.0 + 0.25);
        if (ch >= double(target) + 0.5) break;
        hi *= 1.25;
    }
    for (int it = 0; it < 16; ++it) {
        const double mid = 0.5 * (lo + hi);
        const double code = measureAt(r, c, mid, 6) * 255.0;
        if (code <= double(target) + 0.25) lo = mid; else hi = mid;
    }
    const double Pstar = 0.5 * (lo + hi);
    return want / Pstar;
}

// ---------------------------------------------------------------------------
static void row(const char *name, const Cfg &c, double slope)
{
    const Pred p = predict(c);
    const double lam = p.lambert;
    if (!p.known) {
        std::printf("  %-34s  %6.3f %5.2f %5.1f  %-10s    (no closed form)   meas/lambert "
                    "%7.4f\n", name, c.rho, c.roughness, c.viewElevDeg, c.brdf, slope / lam);
        return;
    }
    std::printf("  %-34s  rho %.2f r %.2f cos %.2f view %4.0f  pred %.5f (d %.5f + s %.5f)  "
                "meas %.5f  meas/pred %6.4f   pred/lam %6.4f  meas/lam %6.4f\n",
                name, c.rho, c.roughness, c.cosTheta, c.viewElevDeg,
                p.total, p.rd, p.rs, slope, slope / p.total, p.total / lam, slope / lam);
}

// ---------------------------------------------------------------------------
int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "direct-diffuse-measure-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);

    Rig r;
    r.e = engine.get();
    r.v = r.e->createOffscreenView("dd", kSize, kSize, Colour(0, 0, 0));
    r.s = r.e->createScene("dd");
    if (!r.v || !r.s) { std::printf("FAIL: view/scene: %s\n", r.e->lastError().c_str()); return 1; }
    r.v->setScene(r.s);

    // THE PLAIN GRADE: no HDR target, no tonemap, no bloom, no SSAO, no SSR, no
    // looks. The floor's radiance reaches the RTT as a linear code.
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.hdr = false;
    fx.bloom = false;
    fx.ssao = false;
    fx.ssr = 0;
    r.v->setPostFx(fx);

    // NO GI, NO SKY, NO AMBIENT: the only light in the picture is the sun.
    GiParams gi;
    gi.mode = GiMode::Off;
    r.s->setGlobalIllumination(gi);
    r.s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // The floor: one flat box, 120 x 120, its top face at y = 0.
    r.cube = r.s->createMesh(enginetest::unitCubeMesh());
    r.floorNode = r.s->createNode();
    Cfg base;
    applySurface(r, base);
    r.applied = base; r.haveApplied = true;
    r.s->setNodeTransform(r.floorNode, Vec3(0.0f, -0.25f, 0.0f), Quat(),
                          Vec3(120.0f, 0.5f, 120.0f));
    r.v->setCamera(orthoCameraAt(90.0));
    render(r.e, 10);

    std::printf("\n=== DIRECT-DIFFUSE-1 — the direct diffuse term against its closed form ===\n");
    std::printf("engine: ray queries %s; view %ux%u, plain (hdr off) => PFG_RGBA8_UNORM, "
                "linear, undithered\n", r.e->rayQueryAvailable() ? "yes" : "no", kSize, kSize);
    std::printf("`lam` below is albedo*P*cos/pi — the closed form the 66 %% claim compares "
                "against.\n");
    std::printf("`pred` is HlmsPbs BRDF_Default's own closed form (see the header), "
                "stated BEFORE the measurement.\n");
    std::printf("every `meas` is a BISECTED power (the code steps at 200/201), so it carries "
                "~0.1 %%, not 8-bit's ~2 %%.\n");

    // ---- ARM D FIRST: the transfer, measured, not assumed -----------------
    // An emissive ramp with zero albedo and no light: the pixel IS the authored
    // radiance if the path is linear.
    {
        std::printf("\n-- ARM D: the picture's transfer and the albedo's colour space --\n");
        if (r.sun) { r.s->removeNode(r.sun); r.sun = 0; }
        double maxLin = 0.0, maxSrgb = 0.0;
        const double ramp[] = { 0.05, 0.12, 0.30, 0.60, 0.80 };
        for (double e : ramp) {
            PbrParams p;
            p.albedo = Colour(0, 0, 0);
            p.emissive = Colour(float(e), float(e), float(e));
            p.roughness = 1.0f;
            const MaterialId m = r.s->createPbrMaterial(p);
            r.s->attachMesh(r.floorNode, r.cube, m);
            render(r.e, 8);
            Image img; r.v->readPixels(img);
            const double v = centreCode(img) / 255.0;
            const double srgb = e <= 0.0031308 ? e * 12.92 : 1.055 * std::pow(e, 1.0 / 2.4) - 0.055;
            std::printf("   emissive %.2f -> pixel %.4f   (linear would be %.4f, err %+.2f %%; "
                        "sRGB-encoded would be %.4f, err %+.1f %%)\n",
                        e, v, e, 100.0 * (v / e - 1.0), srgb, 100.0 * (v / srgb - 1.0));
            maxLin = std::max(maxLin, std::fabs(v / e - 1.0));
            maxSrgb = std::max(maxSrgb, std::fabs(v / srgb - 1.0));
        }
        std::printf("   => the transfer is %s (worst error: linear %.1f %%, sRGB %.1f %%). "
                    "The albedo constant is handed to setDiffuse as a LINEAR triple "
                    "(OgreMaterials.cpp:90) and is NOT colour-converted anywhere in the "
                    "engine.\n",
                    maxLin < maxSrgb ? "LINEAR" : "sRGB", 100.0 * maxLin, 100.0 * maxSrgb);
        r.haveApplied = false;   // the ramp replaced the floor's material
    }

    // ---- ARM A: is the claim reproduced, and what does it depend on? ------
    std::printf("\n-- ARM A: the geometry of the claim (roughness 1, no specular at all) --\n");
    {
        Cfg c; c.surf = Surf::NoSpec; c.roughness = 1.0; c.brdf = "Default";
        for (double rho : { 0.18, 0.50, 0.80 }) {
            c.rho = rho; c.cosTheta = 1.0; c.viewElevDeg = 90.0;
            row("albedo sweep", c, measureSlope(r, c));
        }
        c.rho = 0.8;
        for (double ct : { 1.0, 0.5 }) {
            c.cosTheta = ct; c.viewElevDeg = 90.0;
            row("light tilt (cos theta)", c, measureSlope(r, c));
        }
        c.cosTheta = 1.0;
        for (double ev : { 90.0, 60.0, 30.0, 15.0 }) {
            c.viewElevDeg = ev;
            row("VIEW elevation", c, measureSlope(r, c));
        }
        c.viewElevDeg = 90.0;
        c.shadows = false; row("shadows OFF", c, measureSlope(r, c));
        c.shadows = true;  row("shadows ON", c, measureSlope(r, c));
    }

    // ---- ARM A': power linearity, read at fixed powers (quantised) --------
    std::printf("\n-- ARM A': is the ratio power-dependent? (fixed powers, 8-bit reads) --\n");
    {
        Cfg c; c.surf = Surf::NoSpec; c.rho = 0.8; c.roughness = 1.0;
        for (double P : { 1.0, 2.0, PI }) {
            const Pred p = predict(c);
            const double v = measureAt(r, c, P);
            std::printf("   P %.4f: lam = rho*P/pi = %.5f, pred %.5f, meas %.5f "
                        "(meas/lam %6.4f, meas/pred %6.4f)\n",
                        P, c.rho * P / PI, p.total * P, v, v / (c.rho * P / PI), v / (p.total * P));
        }
    }

    // ---- ARM B: the BRDF, one factor removed at a time --------------------
    std::printf("\n-- ARM B: the BRDF's own factors --\n");
    {
        Cfg c; c.surf = Surf::NoSpec; c.rho = 0.8; c.brdf = "Default";
        for (double rg : { 1.0, 0.5, 0.25, 0.0 }) {
            c.roughness = rg;
            row("Default, energyFactor lerp(1,1/1.51,r)", c, measureSlope(r, c));
        }
        c.roughness = 1.0;
        c.brdf = "CookTorrance";
        row("CookTorrance (NO energyFactor)", c, measureSlope(r, c));
        c.brdf = "Default";
        c.surf = Surf::NoSpecF04;
        row("Default, F0=0.04 (fresnelD = 1)", c, measureSlope(r, c));
        c.brdf = "DefaultSeparateDiffuseFresnel";
        row("SeparateDiffuseFresnel (fresnelD on)", c, measureSlope(r, c));
    }

    // ---- ARM E: the specular leak on the SHIPPED material -----------------
    std::printf("\n-- ARM E: the shipped default surface (Metallic, metalness 0, kS white) --\n");
    {
        Cfg c; c.surf = Surf::Metallic; c.rho = 0.8; c.brdf = "Default";
        for (double rg : { 1.0, 0.5, 0.25 }) {
            c.roughness = rg;
            row("metallic m=0 (specular lobe live)", c, measureSlope(r, c));
        }
        c.roughness = 1.0; c.viewElevDeg = 60.0;
        row("metallic m=0, view 60 (off the peak)", c, measureSlope(r, c));
    }

    // ---- ARM C: the power convention, measured at the engine boundary -----
    std::printf("\n-- ARM C: the power convention (pi applied how many times?) --\n");
    {
        // LightDesc.intensity = I  =>  Ogre powerScale = I*pi (OgreScene.cpp:1202)
        // and kD = albedo/pi (OgreHlmsPbsDatablock.cpp:492-497). The two cancel,
        // so a floor's diffuse is albedo*I*NdotL*<brdf terms> with NO stray pi.
        Cfg c; c.surf = Surf::NoSpec; c.rho = 0.8; c.roughness = 1.0;
        const double slope = measureSlope(r, c);   // slope is per unit of P = I*pi
        const double perIntensity = slope * PI;    // per unit of LightDesc.intensity
        std::printf("   per unit LightDesc.intensity I: pixel = %.5f * I; "
                    "albedo*I = %.5f; ratio %6.4f\n", perIntensity, c.rho, perIntensity / c.rho);
        std::printf("   => pi appears EXACTLY TWICE and cancels: powerScale = I*pi "
                    "(irisgl/engine/src/OgreScene.cpp:1202) against kD = albedo/pi "
                    "(OgreHlmsPbsDatablock.cpp:492-497). The document path adds none: "
                    "SceneMirror copies intensity through unchanged "
                    "(irisgl/mirror/scenemirror.cpp:4962).\n");
    }

    std::printf("\n=== done ===\n");
    return 0;
}
