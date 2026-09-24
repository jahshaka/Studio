// gi.field_follows_energy — THE BOUNCE AS ENERGY, AGAINST A CLOSED FORM.
// A TARGET TEST (label `photon-target`; PHOTON phase A, A1 section 0 / 1.2 —
// lane FENCE-1).
//
// ===========================================================================
// WHY THIS SUITE REPLACES AN EXISTENCE BAR
// ===========================================================================
// `gi.field_follows` case 5 used to assert `chainBounce > 0.012` — "there is
// SOME red on the ground beyond cascade 0". That number was 0.02 and was
// re-anchored DOWN to 0.012 when ogre-patch 0065 changed the answer, with
// thirty lines explaining why the new, smaller number was acceptable. That is
// what a fence around today's picture always becomes. An existence bar also
// cannot fail for the reason that matters: a transport that delivers a tenth of
// the energy it should still puts SOME red on the floor.
//
// So the claim here is ENERGY, and it has a closed form.
//
// WHY IT IS STILL RED AFTER THE SOURCE WAS MADE RIGHT (PHOTON-WRITER-1,
// 2026-09-23). The voxels used to hold the floor as a LAMBERTIAN emitter,
// rho E / pi, while the floor renders the normalised Disney diffuse — 1.5x
// brighter than its own picture at roughness 1 (1.694x / 1.591x / 0.912x here).
// Now the material store carries the roughness and the light injection stores
// what the surface renders (the lobe's directional albedo at the lamp's angle,
// jahDiffuseAlbedo), and the wall's environment lobe reflects with the same
// function at its view angle instead of the constant energy factor (equation (4)
// below says so): 1.168x / 1.100x / 0.627x. The mean fell by the source's own
// factor; what is left is THE SHAPE, which the source never touched (2.870 before,
// 2.877 after, against the transfer's 1.545): the wall stands 7 m from the camera,
// OUTSIDE cascade 0, so it is lit by the chain's cone march, and the cones read
// the floor too bright at 0.4 m (the first coarse samples sit on it) and too dark
// at 2.8 m (the aperture's over-occlusion over the long grazing path). That is
// the cone transport's, not the source's — the row stays a target for it.
//
// ===========================================================================
// THE PHYSICS, WRITTEN OUT
// ===========================================================================
// THE FIXTURE. A matte floor rectangle of albedo rho_f, lit face-on by a
// directional light (straight down) of power P, and a matte WALL standing on
// its near edge, perpendicular to it, facing the floor. The light is vertical,
// so the wall's own face receives cos(theta) = 0 of it: EVERY photon leaving
// the wall arrived there off the floor. There is no sky and no ambient.
//
// 1. THE FLOOR'S RADIANCE IS **NOT** rho_f * P / pi, and that is MEASURE-1a's
//    finding, not a defect (`tests/gi/direct_diffuse_measure.cpp`, ledger:
//    DIRECT-DIFFUSE-1). HlmsPbs BRDF_Default's diffuse lobe is the NORMALIZED
//    Disney diffuse of "Moving Frostbite to Physically Based Rendering", and its
//    renormalisation constant is
//
//        energyFactor = lerp( 1.0, 1.0/1.51, perceptualRoughness ),
//
//    which is 0.66225 at roughness 1 — the whole of the "direct term reads
//    66-69 %". The lobe is also VIEW-DEPENDENT:
//
//        energyBias   = 0.5 * rp
//        fd90         = energyBias + 2 * VdotH^2 * rp
//        lightScatter = 1 + (fd90 - 1) * (1 - NdotL)^5
//        viewScatter  = 1 + (fd90 - 1) * (1 - NdotV)^5
//        L_floor(V)   = NdotL * lightScatter * viewScatter * energyFactor
//                       * fresnelD * rho_f * P / pi.                      (1)
//
//    `pbsDirect()` below is exactly that, transcribed from
//    200.BRDFs_piece_ps.any:144-230 with the same file:line anchors MEASURE-1a
//    used. fresnelD is exactly 1 because PbsBrdf::Default carries no diffuse
//    fresnel, and the sun is straight down so NdotL = 1 and lightScatter = 1.
//
//    A CONSEQUENCE WORTH STATING, because it is the one place this suite departs
//    from the design's wording: **a view-dependent emitter is not Lambertian, so
//    no form factor alone describes the transfer.** The form factor of a finite
//    rectangle answers "how much of the hemisphere does the floor fill, cosine
//    weighted"; it silently assumes the floor emits the same radiance in every
//    direction, and with (1) it does not (viewScatter varies with the elevation
//    of the receiving point as seen from each floor element). So the closed form
//    is used two ways below and BOTH are printed: as the verified LAMBERTIAN
//    reference, and as the kernel of the exact transfer integral with (1) inside
//    it. The asserted bar is the exact one.
//
// 2. THE IRRADIANCE ON THE WALL. For a point p with normal n and a uniform
//    Lambertian emitter of radiance L occupying a planar patch A,
//
//        E(p) = L * INTEGRAL_A cos(theta_p) cos(theta_A) / r^2 dA
//             = L * Omega_proj(p),                                        (2)
//
//    where Omega_proj is the emitter's PROJECTED SOLID ANGLE at p. The
//    differential-area-to-finite-rectangle form factor is exactly
//    F = Omega_proj / pi, so (2) is the form-factor relation E = pi * L * F
//    written without the pi cancelling twice.
//
// 3. THE CLOSED FORM — the differential area to a finite rectangle, for ANY
//    relative orientation, is Lambert's polygon formula:
//
//        Omega_proj(p, n) = | 1/2 * SUM_i  Theta_i * ( n . u_i ) |         (3)
//
//    over the rectangle's four edges (v_i -> v_i+1) as seen from p, where
//    Theta_i = acos( a_i . b_i ) is the angle the edge subtends at p
//    (a_i, b_i the unit directions to its two ends) and u_i = norm(a_i x b_i)
//    is the unit normal of the triangle (p, v_i, v_i+1). It is EXACT — no
//    series, no table, no small-angle assumption — for a polygon wholly on the
//    positive side of the receiver's plane, which this fixture is by
//    construction. Evaluated below in DOUBLE precision, and cross-checked in
//    this file against a 512 x 512 midpoint quadrature of the defining
//    integral in (2) so the closed form cannot be silently mis-transcribed.
//
// 4. WHAT THE WALL RENDERS. The wall's light is an ENVIRONMENT term, and
//    BRDF_EnvMap (200.BRDFs_piece_ps.any) carries no scatter terms and — since
//    PHOTON-ENV-1 — the diffuse lobe's energy factor, the direct lobe's own
//    (`jahDiffuseEnergyFactor`, 1/1.51 on this roughness-1 wall; MEASURE-1a
//    decision b). HlmsPbs consumes a diffuse-GI irradiance as `envColourD` =
//    E/pi and multiplies it by the albedo and that factor, so with no other
//    light on the wall the pixel is, in linear radiance,
//
//        L_wall(h) = energyFactor(1) * rho_w * E(h) / pi,                  (4)
//
//    with E(h) the transfer integral of (2) carrying (1) as its source:
//
//        E(h) = INTEGRAL_floor  L_floor(dir from dA to p)
//                               * cos(theta_p) cos(theta_A) / r^2  dA.     (4a)
//
//    In the Lambertian limit (viewScatter == 1) (4a) collapses to
//    L_floor * Omega_proj(h) and (4) becomes
//    rho_w * rho_f * P * energyFactor * Omega_proj(h) / pi^2 — which is the
//    closed form, printed beside the exact number so the size of the Disney
//    lobe's contribution is visible rather than buried.
//
//    MATTE is authored, not hoped for: the Specular workflow at ior 1.0 with a
//    black specular colour gives F0 = 0 — no environment specular term at all
//    (the default ground's own recipe, GF1, and what `gi.gather_reference`
//    uses). With no area light in the scene the pin's envBRDF is the identity,
//    so (4) is the whole shader.
//
// THREE HEIGHTS, because a single height can be matched by a constant and the
// SHAPE is the stronger statement: Omega_proj falls off with h in a way no
// calibration can fake. h = 0.4, 1.2 and 2.8 m.
//
// ===========================================================================
// THE BAR, AND WHAT TURNS IT GREEN
// ===========================================================================
// +-15 %% of (4) at each of the three heights, on the RAYS-OFF (field + cones)
// path, which is the path `gi.field_follows` is about.
//
// TODAY'S MEASURED VALUES are printed by every run as `target:` lines; the
// numbers this lane measured on this tree (2026-09-22, RTX 4080 SUPER) are in
// the run log and in ~/Developer/spikes/fence-1/.
//
// THREE NUMBERS ARE PRINTED AND ONE IS ASSERTED, and the reason is attribution.
// The asserted one is the EXACT transfer (4)+(4a) from the AUTHORED rho_f,
// rho_w and P. Beside it go (a) the LAMBERTIAN closed form, so the Disney lobe's
// share is visible, and (b) the same claim with the floor's MEASURED radiance
// substituted for (1), which removes the direct term from the comparison
// altogether. The pair (exact, measured-floor) says which half of any miss is
// the SOURCE and which is the TRANSPORT — a lane fixing one of them can see it
// move, which a single ratio could never show.
//
// GREEN AFTER: PHOTON P4 — VOXEL-CLIP-1 (phase A) for the source the voxels
// hold, and the field's integral in P4 for the transport. DIRECT-DIFFUSE-1 is
// already ANSWERED (MEASURE-1a: the 66 %% is the BRDF, not a defect) and is in
// the reference above rather than owed by it. The lane that lands the rest
// DELETES the `photon-target` label from this suite's CMake row; that deletion
// IS the part's acceptance.
//
// Its own binary and its own process like every GI suite: the voxel lighting
// and the field bind process-wide to HlmsPbs.
// PHOTON-GATHER-1d: THE GATHER PINNED OFF. Since 1d the screen-probe gather is
// the diffuse at every ray tier (GiToggle::Auto resolves on at Medium and above);
// this suite measures the voxel chain / the field / the cones / the probes, which
// it pins, so its numbers stay about them. The gather has its own suites
// (gi.gather_*).
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

static const double kPi = 3.14159265358979323846;

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

// ---------------------------------------------------------------------------
// THE FIXTURE'S NUMBERS, all in metres and all used by the closed form.
// ---------------------------------------------------------------------------
static const unsigned kSize = 512u;
/// The emitting floor rectangle: 12 m across, 12 m deep, its NEAR edge at z = 0
/// where the wall stands. Finite and small on purpose — it fits inside the
/// innermost cascade (10 m across at High) plus its neighbour, so the renderer
/// has every voxel the closed form integrates over. A 60 m floor would make the
/// reference include light from beyond the outermost cascade, and a target no
/// part of PHOTON promises to reach is not a target, it is a wish.
static const double kFloorHalfX = 6.0;
static const double kFloorDepth = 12.0;
static const double kFloorAlbedo = 0.85;
static const double kWallAlbedo = 0.85;
/// The directional light's `power` in the helper's units: irradiance at normal
/// incidence. 2.2 puts the floor's radiance near 0.6, which is comfortably
/// inside an 8-bit picture at both ends (the wall reads a third of it).
static const double kSunPower = 2.2;
/// Half the vertical extent of the orthographic camera. The wall is 4 m tall;
/// 3.0 puts y in [-1, 5] on screen, so the floor's near strip is visible below
/// the wall's base and the wall's top edge is inside the frame.
static const double kOrthoHalf = 3.0;
static const double kCamY = 2.0;
static const double kCamZ = -7.0;
/// The three heights on the wall.
static const double kHeights[3] = { 0.4, 1.2, 2.8 };

// ---------------------------------------------------------------------------
/// LAMBERT'S FORMULA — equation (3) of the header. The projected solid angle of
/// a planar polygon at `p` for a receiver whose normal is `n`. The same function
/// `gi.gather_reference` carries; the two suites must agree, and the quadrature
/// check below is what makes that checkable rather than assumed.
static double projectedSolidAngle(const double p[3], const double n[3],
                                  const double verts[][3], int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        double a[3], b[3];
        for (int k = 0; k < 3; ++k) { a[k] = verts[i][k] - p[k]; b[k] = verts[j][k] - p[k]; }
        const double la = std::sqrt(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
        const double lb = std::sqrt(b[0]*b[0] + b[1]*b[1] + b[2]*b[2]);
        if (la < 1e-9 || lb < 1e-9) continue;
        for (int k = 0; k < 3; ++k) { a[k] /= la; b[k] /= lb; }
        double c = a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
        c = std::max(-1.0, std::min(1.0, c));
        const double theta = std::acos(c);
        double cr[3] = { a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0] };
        const double lc = std::sqrt(cr[0]*cr[0] + cr[1]*cr[1] + cr[2]*cr[2]);
        if (lc < 1e-9) continue;
        for (int k = 0; k < 3; ++k) cr[k] /= lc;
        sum += theta * (n[0]*cr[0] + n[1]*cr[1] + n[2]*cr[2]);
    }
    return std::fabs(0.5 * sum);
}

/// THE FLOOR'S OUTGOING RADIANCE, from HlmsPbs BRDF_Default's own arithmetic —
/// equation (1) of the header. Transcribed from
/// `irisgl/thirdparty/ogre-next/Samples/Media/Hlms/Pbs/Any/Main/
///  200.BRDFs_piece_ps.any:144-230` with the same anchors MEASURE-1a used
/// (`tests/gi/direct_diffuse_measure.cpp`), which is the tool that MEASURED this
/// prediction against the picture and found it exact.
///
/// `V` is the unit direction from the floor element TOWARD the receiver. N and L
/// are both straight up here (a horizontal floor under a vertical sun), so
/// NdotL = 1 and lightScatter = 1; the view dependence is real and is what makes
/// the emitter non-Lambertian.
///
/// fresnelD = 1: PbsBrdf::Default carries no FLAG_HAS_DIFFUSE_FRESNEL
/// (OgreHlmsPbsDatablock.h:75,113). The specular lobe is identically zero on
/// this material (Specular workflow, ior 1.0, black kS -> F0 = 0, kS = 0).
static double pbsDirect(double rho, double power, double perceptualRoughness,
                        const double V[3])
{
    const double N[3] = { 0.0, 1.0, 0.0 };
    const double L[3] = { 0.0, 1.0, 0.0 };
    double H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
    const double hl = std::sqrt(H[0]*H[0] + H[1]*H[1] + H[2]*H[2]);
    if (hl < 1e-12) return 0.0;
    for (int k = 0; k < 3; ++k) H[k] /= hl;
    const double NdotL = std::max(0.0, N[0]*L[0] + N[1]*L[1] + N[2]*L[2]);
    const double NdotV = std::max(0.0, N[0]*V[0] + N[1]*V[1] + N[2]*V[2]);
    const double VdotH = std::max(0.0, V[0]*H[0] + V[1]*H[1] + V[2]*H[2]);
    // OgreMaterials.cpp:150 clamps the authored roughness at 1e-4.
    const double rp = std::max(perceptualRoughness, 1e-4);
    const double energyBias   = 0.5 * rp;
    const double energyFactor = 1.0 + (1.0 / 1.51 - 1.0) * rp;
    const double fd90         = energyBias + 2.0 * VdotH * VdotH * rp;
    const double lightScatter = 1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotL, 5.0);
    const double viewScatter  = 1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotV, 5.0);
    return NdotL * lightScatter * viewScatter * energyFactor * (rho * power / kPi);
}

/// The LAMBERTIAN limit of (1) — what `rho * P / pi` would give if the lobe were
/// view-independent, times the one factor that survives at normal incidence.
/// Printed beside the exact numbers so the Disney lobe's share is never hidden.
static double pbsDirectLambertLimit(double rho, double power, double perceptualRoughness)
{
    const double rp = std::max(perceptualRoughness, 1e-4);
    const double energyFactor = 1.0 + (1.0 / 1.51 - 1.0) * rp;
    return energyFactor * rho * power / kPi;
}

/// THE TRANSFER INTEGRAL (2)/(4a) BY QUADRATURE, over the floor rectangle, with
/// a per-element source radiance. Two uses:
///
///   * with `lambertian` true the source is constant, and the result must equal
///     `projectedSolidAngle` * that constant — the check that the closed form
///     really solves the integral it claims to (a reference nobody verified is
///     the most expensive kind of wrong);
///   * with `lambertian` false it carries (1) per element, which is the EXACT
///     transfer and the number the bar is stated against. No closed form exists
///     for it: the emitter is view-dependent, so the finite-rectangle form
///     factor is the kernel and not the answer.
static double transferIntegral(const double p[3], const double n[3], int n1d, bool lambertian,
                               double rho, double power, double roughness)
{
    const double dx = 2.0 * kFloorHalfX / double(n1d);
    const double dz = kFloorDepth / double(n1d);
    const double dA = dx * dz;
    double sum = 0.0;
    for (int iz = 0; iz < n1d; ++iz) {
        const double z = -kFloorDepth + (double(iz) + 0.5) * dz;
        for (int ix = 0; ix < n1d; ++ix) {
            const double x = -kFloorHalfX + (double(ix) + 0.5) * dx;
            const double d[3] = { p[0] - x, p[1] - 0.0, p[2] - z };   // element -> receiver
            const double r2 = d[0]*d[0] + d[1]*d[1] + d[2]*d[2];
            const double r = std::sqrt(r2);
            if (r < 1e-9) continue;
            const double V[3] = { d[0] / r, d[1] / r, d[2] / r };
            const double cosA = V[1];                                  // floor normal is +Y
            const double cosP = -(n[0]*V[0] + n[1]*V[1] + n[2]*V[2]);  // receiver faces -V
            if (cosP <= 0.0 || cosA <= 0.0) continue;
            const double L = lambertian ? 1.0 : pbsDirect(rho, power, roughness, V);
            sum += L * cosP * cosA / r2 * dA;
        }
    }
    return sum;
}

/// The camera: horizontal, orthographic, looking along +z at the wall. Built by
/// hand rather than through the lookAt helper so the pixel mapping below is an
/// identity in x and a flip in y, with nothing to invert.
static CameraDesc wallCamera()
{
    CameraDesc c;
    c.position = Vec3(0.0f, float(kCamY), float(kCamZ));
    c.orientation = Quat{ 0.0f, 1.0f, 0.0f, 0.0f };   // 180 degrees about Y: -Z forward -> +Z
    c.orthographic = true;
    c.orthoSize = float(kOrthoHalf);
    c.farClip = 200.0f;
    return c;
}

/// World (x, y) on the wall plane -> pixel. Screen right is world -X after the
/// 180-degree turn; screen down is world -Y.
static void wallToPixel(double wx, double wy, double &px, double &py)
{
    px = (-wx / kOrthoHalf * 0.5 + 0.5) * kSize;
    py = (-(wy - kCamY) / kOrthoHalf * 0.5 + 0.5) * kSize;
}

/// A SECOND POSE FOR THE SAME VIEW: straight down over the floor, orthographic.
/// The floor is what lights the wall, and its radiance has to be read from
/// ABOVE — a horizontal camera sees a horizontal plane EDGE-ON, with zero extent
/// on screen, so the wall pose cannot see the floor at all. One view, two poses,
/// one workspace: a second View would mean a second workspace inside the same
/// process, which is what every GI suite in this directory avoids.
static const double kFloorCamY = 8.0;
static const double kFloorOrthoHalf = 6.0;
static CameraDesc floorCamera()
{
    CameraDesc c;
    c.position = Vec3(0.0f, float(kFloorCamY), float(-kFloorDepth * 0.5));
    c.orientation = Quat{ -0.70710678f, 0.0f, 0.0f, 0.70710678f };   // straight down
    c.orthographic = true;
    c.orthoSize = float(kFloorOrthoHalf);
    c.farClip = 200.0f;
    return c;
}

/// World (x, z) on the floor -> pixel, under that pose. Screen right is world +X
/// and screen DOWN is world +Z (the -90 degree turn about X).
static void floorToPixel(double wx, double wz, double &px, double &py)
{
    px = (wx / kFloorOrthoHalf * 0.5 + 0.5) * kSize;
    py = ((wz + kFloorDepth * 0.5) / kFloorOrthoHalf * 0.5 + 0.5) * kSize;
}

static void blockMean(const ImageF &img, double cx, double cy, int half, double out[3])
{
    double s[3] = { 0, 0, 0 };
    int n = 0;
    for (int y = int(cy) - half; y <= int(cy) + half; ++y)
        for (int x = int(cx) - half; x <= int(cx) + half; ++x) {
            if (x < 0 || y < 0 || x >= int(img.width) || y >= int(img.height)) continue;
            const Colour c = img.at(unsigned(x), unsigned(y));
            s[0] += c.r; s[1] += c.g; s[2] += c.b;
            ++n;
        }
    for (int k = 0; k < 3; ++k) out[k] = n ? s[k] / n : 0.0;
}

// ---------------------------------------------------------------------------
int main()
{
    std::printf("== gi.field_follows_energy: A TARGET TEST (label photon-target) -- the bounce as "
                "ENERGY against a closed form; green after PHOTON P4 (DIRECT-DIFFUSE-1 + "
                "VOXEL-CLIP-1 + the field's integral)\n");

    // ---- 0. THE CLOSED FORM AGAINST ITS OWN DEFINITION --------------------
    // Before the engine is even started, because it costs nothing and because a
    // reference nobody checked is the most expensive kind of wrong.
    {
        const double n[3] = { 0.0, 0.0, -1.0 };
        const double verts[4][3] = { { -kFloorHalfX, 0.0, 0.0 },
                                     {  kFloorHalfX, 0.0, 0.0 },
                                     {  kFloorHalfX, 0.0, -kFloorDepth },
                                     { -kFloorHalfX, 0.0, -kFloorDepth } };
        double worst = 0.0;
        std::printf("\n   THE CLOSED FORM (3) AGAINST A 512x512 MIDPOINT QUADRATURE OF (2):\n");
        for (int i = 0; i < 3; ++i) {
            const double p[3] = { 0.0, kHeights[i], 0.0 };
            const double closed = projectedSolidAngle(p, n, verts, 4);
            const double quad = transferIntegral(p, n, 512, true, 0.0, 0.0, 0.0);
            const double rel = std::fabs(closed / quad - 1.0);
            worst = std::max(worst, rel);
            // ...and how much the Disney lobe moves the transfer at this height:
            // the exact integral against the same integral with a Lambertian
            // source of the lobe's normal-incidence value.
            const double exact = transferIntegral(p, n, 512, false, kFloorAlbedo, kSunPower, 1.0);
            const double lamb = closed * pbsDirectLambertLimit(kFloorAlbedo, kSunPower, 1.0);
            std::printf("     h = %.1f m: Lambert %.6f sr, quadrature %.6f sr (%.4f %%); "
                        "form factor F = %.4f; E exact %.5f vs Lambertian %.5f (%+.1f %%)\n",
                        kHeights[i], closed, quad, 100.0 * rel, closed / kPi, exact, lamb,
                        100.0 * (exact / lamb - 1.0));
        }
        CHECK_MSG(worst < 0.002,
                  "THE CLOSED FORM SOLVES THE INTEGRAL IT CLAIMS TO (worst %.4f %% against the "
                  "quadrature) — the reference is verified before anything is measured against it",
                  100.0 * worst);
        std::printf("   (the floor's own radiance at normal incidence: pbsDirect %.5f against "
                    "rho*P/pi %.5f — the 1/1.51 energyFactor, MEASURE-1a)\n",
                    pbsDirectLambertLimit(kFloorAlbedo, kSunPower, 1.0),
                    kFloorAlbedo * kSunPower / kPi);
    }

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-field-energy-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("fieldenergy", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("fieldenergy");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;                        // no screen-space reflection anywhere in this measurement
    // THE CURRENCY IS RADIANCE (HDR-READBACK-1): no tonemap, no exposure, and
    // the scene result read back in float — no 8-bit quantisation (it was a
    // hard +-0.5/255 on a wall reading 0.03-0.10) and no ceiling.
    fx.hdr = false;
    fx.hdrReadback = true;
    view->setPostFx(fx);
    view->setShadows(true);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));   // no sky, no ambient: the floor is the source
    view->setCamera(wallCamera());

    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const auto matteMaterial = [&](double albedo, double emissive) {
        PbrParams p;
        p.albedo = Colour(float(albedo), float(albedo), float(albedo));
        p.emissive = Colour(float(emissive), float(emissive), float(emissive));
        p.roughness = 1.0f;
        // F0 = 0: the Specular workflow at ior 1.0 with a black specular colour.
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        return s->createPbrMaterial(p);
    };

    // ---- THE FLOOR: the emitting rectangle, exactly as the closed form has it
    {
        const NodeId n = s->createNode();
        const MaterialId m = matteMaterial(kFloorAlbedo, 0.0);
        CHECK(n && m && s->attachMesh(n, cube, m),
              "the matte floor rectangle exists (Specular workflow, ior 1.0, black kS)");
        // Top face at y = 0, x in [-6, 6], z in [-12, 0].
        s->setNodeTransform(n, Vec3(0.0f, -0.15f, float(-kFloorDepth * 0.5)), Quat(),
                            Vec3(float(2.0 * kFloorHalfX), 0.3f, float(kFloorDepth)));
    }
    // ---- THE WALL: perpendicular, on the floor's near edge, facing the floor
    {
        const NodeId n = s->createNode();
        const MaterialId m = matteMaterial(kWallAlbedo, 0.0);
        CHECK(n && m && s->attachMesh(n, cube, m), "the matte wall exists");
        // 12 m x 4 m, 0.3 m thick (two cascade-0 cells, so the voxeliser holds
        // it), its FRONT face at z = 0 and its base at y = 0.
        s->setNodeTransform(n, Vec3(0.0f, 2.0f, 0.15f), Quat(),
                            Vec3(float(2.0 * kFloorHalfX), 4.0f, 0.3f));
    }
    // ---- THE SUN: straight down, so the wall gets NONE of it directly -------
    const NodeId sun = enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f),
                                                       float(kSunPower));
    CHECK(sun != 0, "the sun is straight down: cos(theta) = 0 on the wall, so every photon "
                    "leaving it arrived off the floor");

    // ---- the calibration ramp: emissive patches of known radiance ----------
    // ON THE WALL, high up, flat-on to the camera — and REMOVED AGAIN before a
    // single number is measured. They are emitters inside cascade 0, so leaving
    // them in place would put their own bounce into the wall's reading (measured
    // at ~16 %% of the h = 2.8 m value at these radiances), and the GI-off
    // control could not subtract it: emissive is not GI, so the control carries
    // the patches and not their bounce.
    const double kRamp[4] = { 0.05, 0.12, 0.30, 0.60 };
    const double kRampX[4] = { -2.2, -1.1, 1.1, 2.2 };
    const double kRampY = 3.6;
    NodeId rampNodes[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; ++i) {
        const NodeId n = s->createNode();
        const MaterialId m = matteMaterial(0.0, kRamp[i]);
        if (!n || !m || !s->attachMesh(n, cube, m)) { std::printf("FAIL: ramp\n"); ++failures; }
        rampNodes[i] = n;
        // Just in front of the wall's face (z = -0.06), 0.7 x 0.5 m.
        s->setNodeTransform(n, Vec3(float(kRampX[i]), float(kRampY), -0.06f), Quat(),
                            Vec3(0.7f, 0.5f, 0.06f));
    }

    GiParams gi;
    gi.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::On;            // the irradiance field: the term this suite is about
    gi.gather = GiToggle::Off;
    gi.numBounces = 1;
    gi.cascades = true;
    CHECK(s->setGlobalIllumination(gi), "the cascade chain with the field builds over the fixture");
    // RAYS OFF: `gi.field_follows` is about the field + cone path, and that is
    // the path this bar is stated for. The ray tier's own reflection answer is
    // gi.rt_reflect's subject.
    s->setRayTracing(RayTracingMode::Off);
    render(e, 40);

    // ---- CALIBRATION: the readback ----------------------------------------
    // Each patch is an emitter that reflects nothing (F0 = 0, black albedo), so
    // its pixel is its authored radiance — the check that every number below is
    // read in the units the closed form is written in.
    {
        ImageF img;
        CHECK(view->readPixelsHdr(img), "the view reads its radiance back");
        double worst = 0.0;
        std::printf("\n   THE READBACK, from a ramp of emissive patches:\n");
        for (int i = 0; i < 4; ++i) {
            double px, py, m[3];
            wallToPixel(kRampX[i], kRampY, px, py);
            blockMean(img, px, py, 5, m);
            const double rel = std::fabs(m[0] - kRamp[i]) / kRamp[i];
            worst = std::max(worst, rel);
            std::printf("     radiance %.2f -> %.5f (%.2f %% off)\n", kRamp[i], m[0], 100.0 * rel);
        }
        CHECK_MSG(worst < 0.01,
                  "THE READBACK IS THE RADIANCE (worst %.2f %%, bar 1 %%) — the currency every "
                  "number below is stated in", 100.0 * worst);
    }

    // ---- THE RAMP GOES, before anything is measured ------------------------
    for (int i = 0; i < 4; ++i) if (rampNodes[i]) s->removeNode(rampNodes[i]);
    s->refreshGlobalIllumination();
    render(e, 40);

    // ---- THE CONTROL, GI OFF: the wall goes dark AND the floor is pure direct
    // BOTH READINGS BELONG HERE, and the floor's is why. With GI on, the floor
    // receives its own bounce off the wall and off itself, so a floor read under
    // GI is not the DIRECT term and cannot be compared with (1): measured 0.4878
    // with GI on against pbsDirect's 0.3942, i.e. 124 %% — the extra 24 %% is
    // indirect light, not a BRDF error. With GI off it is the direct term and
    // nothing else, which is the number MEASURE-1a's closed form predicts.
    double wallNoGi[3] = { 0, 0, 0 };
    double floorMeasured = 0.0;
    const double floorPredicted = pbsDirectLambertLimit(kFloorAlbedo, kSunPower, 1.0);
    {
        GiParams off = gi;
        off.mode = GiMode::Off;
        CHECK(s->setGlobalIllumination(off), "...and the same picture with no GI at all");
        render(e, 20);
        ImageF img;
        view->readPixelsHdr(img);
        for (int i = 0; i < 3; ++i) {
            double px, py, m[3];
            wallToPixel(0.0, kHeights[i], px, py);
            blockMean(img, px, py, 8, m);
            wallNoGi[i] = m[0];
        }
        std::printf("\n   THE CONTROL (GI off): the wall reads %.4f / %.4f / %.4f at the three "
                    "heights — the sun is vertical, so this is the floor of the measurement\n",
                    wallNoGi[0], wallNoGi[1], wallNoGi[2]);
        CHECK(wallNoGi[0] < 0.01 && wallNoGi[1] < 0.01 && wallNoGi[2] < 0.01,
              "with no GI the wall is dark: nothing but indirect light can reach it");

        // ...and the floor, from ABOVE (a horizontal camera sees a horizontal
        // plane edge-on, with zero extent on screen). Mid-floor, 4 m from the
        // wall and 2 m off the axis.
        view->setCamera(floorCamera());
        render(e, 12);
        view->readPixelsHdr(img);
        double px, py, m[3];
        floorToPixel(2.0, -4.0, px, py);
        blockMean(img, px, py, 10, m);
        floorMeasured = m[0];
        view->setCamera(wallCamera());
        std::printf("   THE FLOOR (GI off, direct only): measured radiance %.4f; pbsDirect at "
                    "normal incidence %.4f (%.0f %%); rho_f * P / pi %.4f (%.0f %%)\n",
                    floorMeasured, floorPredicted, 100.0 * floorMeasured / floorPredicted,
                    kFloorAlbedo * kSunPower / kPi,
                    100.0 * floorMeasured / (kFloorAlbedo * kSunPower / kPi));
        std::printf("   (the second ratio IS MEASURE-1a's 1/1.51 energyFactor at roughness 1 — "
                    "the BRDF, not a defect; DIRECT-DIFFUSE-1 is answered and (1) carries it)\n");
        CHECK_MSG(floorMeasured > 0.02,
                  "the floor IS lit (%.4f) — without that there is nothing for the wall to "
                  "receive and every number below would be a division by noise", floorMeasured);
        CHECK_MSG(std::fabs(floorMeasured / floorPredicted - 1.0) < 0.06,
                  "THE SOURCE TERM IS CALIBRATED: the floor's direct radiance is pbsDirect to "
                  "within 6 %% (%.4f against %.4f) — the reference's source is measured, not "
                  "assumed", floorMeasured, floorPredicted);

        CHECK(s->setGlobalIllumination(gi), "...back to the chain with the field");
        render(e, 40);
    }

    // ---- THE MEASUREMENT -------------------------------------------------
    {
        ImageF img;
        view->readPixelsHdr(img);
        const double n[3] = { 0.0, 0.0, -1.0 };
        const double verts[4][3] = { { -kFloorHalfX, 0.0, 0.0 },
                                     {  kFloorHalfX, 0.0, 0.0 },
                                     {  kFloorHalfX, 0.0, -kFloorDepth },
                                     { -kFloorHalfX, 0.0, -kFloorDepth } };
        std::printf("\n   THE WALL AGAINST EQUATIONS (4)+(4a), three heights:\n");
        for (int i = 0; i < 3; ++i) {
            const double p[3] = { 0.0, kHeights[i], 0.0 };
            const double omega = projectedSolidAngle(p, n, verts, 4);
            // (4)+(4a) from the AUTHORED numbers, with pbsDirect inside the
            // integral — the asserted bar.
            const double eExact = transferIntegral(p, n, 512, false, kFloorAlbedo, kSunPower, 1.0);
            // THE WALL'S LOBE IS ITS DIRECTIONAL ALBEDO (PHOTON-WRITER-1:
            // BRDF_EnvMap reflects the environment with the normalised Disney
            // lobe's integral, jahDiffuseAlbedo). The camera looks straight at the
            // wall (orthographic, along its normal): cos theta_v = 1, roughness 1.
            const double kWallEnergyFactor = enginetest::disneyDiffuseAlbedo(1.0, 1.0);
            const double expected = kWallEnergyFactor * kWallAlbedo * eExact / kPi;
            const double expectedOldAccounting = kWallAlbedo * eExact / kPi;
            // ...the LAMBERTIAN closed form, so the lobe's share is visible...
            const double expectedLambert = kWallEnergyFactor * kWallAlbedo * omega *
                                           pbsDirectLambertLimit(kFloorAlbedo, kSunPower, 1.0) / kPi;
            // ...and the same claim with the floor's MEASURED radiance in place
            // of (1), which takes the source term out of the comparison.
            const double expectedFromFloor =
                kWallEnergyFactor * kWallAlbedo * floorMeasured * omega / kPi;
            double px, py, m[3];
            wallToPixel(0.0, kHeights[i], px, py);
            blockMean(img, px, py, 8, m);
            const double measured = m[0] - wallNoGi[i];
            const double ratio = expected > 0.0 ? measured / expected : 0.0;
            const double ratioFromFloor = expectedFromFloor > 0.0 ? measured / expectedFromFloor
                                                                  : 0.0;
            std::printf("     h = %.1f m (px %.0f,%.0f): F = %.4f  measured %.4f  |  EXACT (4a) "
                        "%.4f -> %.3fx  |  Lambertian %.4f  |  from the MEASURED floor %.4f -> "
                        "%.3fx  |  the wall Lambertian (the old accounting, not the claim) "
                        "%.4f -> %.3fx\n",
                        kHeights[i], px, py, omega / kPi, measured, expected, ratio,
                        expectedLambert, expectedFromFloor, ratioFromFloor,
                        expectedOldAccounting,
                        expectedOldAccounting > 0.0 ? measured / expectedOldAccounting : 0.0);
            const double err = std::fabs(ratio - 1.0);
            std::printf("target: %.4f (bar 0.1500) the wall's reflected radiance at h = %.1f m is "
                        "the floor's bounce through the finite-rectangle transfer: "
                        "|measured/(4a) - 1|%s\n", err, kHeights[i], err <= 0.15 ? " -- MET" : "");
            CHECK_MSG(err <= 0.15,
                      "h = %.1f m: the bounce carries the ENERGY the transfer integral says it "
                      "does (%.3fx of equations (4)+(4a), bar 0.85-1.15x; from the measured floor "
                      "%.3fx)", kHeights[i], ratio, ratioFromFloor);
        }
        // AND THE SHAPE, which no calibration can fake: the ratio of the two
        // extreme heights' radiances is the ratio of their form factors, whatever
        // the absolute scale is. Printed as a second target line, because an
        // estimator that is right in shape and wrong by a constant is a
        // calibration and one that varies with h is wrong (gi.gather_reference's
        // own reading of the same distinction).
        const double pLow[3] = { 0.0, kHeights[0], 0.0 };
        const double pHigh[3] = { 0.0, kHeights[2], 0.0 };
        const double fLow = transferIntegral(pLow, n, 512, false, kFloorAlbedo, kSunPower, 1.0);
        const double fHigh = transferIntegral(pHigh, n, 512, false, kFloorAlbedo, kSunPower, 1.0);
        double mLow[3], mHigh[3], px, py;
        wallToPixel(0.0, kHeights[0], px, py); blockMean(img, px, py, 8, mLow);
        wallToPixel(0.0, kHeights[2], px, py); blockMean(img, px, py, 8, mHigh);
        const double lowLin = mLow[0] - wallNoGi[0];
        const double highLin = mHigh[0] - wallNoGi[2];
        const double shapeMeasured = highLin > 0.0 ? lowLin / highLin : 0.0;
        const double shapeExpected = fLow / fHigh;
        const double shapeErr = shapeExpected > 0.0
                                    ? std::fabs(shapeMeasured / shapeExpected - 1.0) : 1.0;
        std::printf("\n     THE SHAPE: L(%.1f)/L(%.1f) measured %.3f, the transfer integrals say "
                    "%.3f (%.1f %%)\n", kHeights[0], kHeights[2], shapeMeasured, shapeExpected,
                    100.0 * shapeErr);
        std::printf("target: %.4f (bar 0.1500) the falloff with height IS the transfer's falloff "
                    "(a constant miss is a calibration; a varying one is wrong)%s\n",
                    shapeErr, shapeErr <= 0.15 ? " -- MET" : "");
        CHECK_MSG(shapeErr <= 0.15,
                  "THE SHAPE: the bounce falls off with height exactly as the transfer integral "
                  "does (%.3f against %.3f)", shapeMeasured, shapeExpected);
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
