// gi.gather_reference — THE ANALYTIC GATE
// (SPECS/SCREEN_PROBE_GATHER_SPEC.md section 7 phase 1; the lead's decision,
// ledger sections 937 and 938.)
//
// WHY THIS SUITE EXISTS. Every diffuse-GI estimator this renderer has ever
// shipped has been measured against ANOTHER ESTIMATOR: the cones against the
// field, the field against the cones, the gather against both (GATHER-0's
// sheet: the floor's red excess under an emissive panel reads 0.0135 for the
// cones and 0.0860 for the gather, a factor of 6.4, with nothing to say which
// of them is right). A rectangular Lambertian emitter over a plane has a
// CLOSED FORM, so this is the measurement that settles it.
//
// THE PHYSICS, exactly. A Lambertian emitter of radiance L delivers, at a point
// p with normal n, the irradiance
//
//     E(p) = L * OMEGA_proj(p),
//
// where OMEGA_proj is the emitter's PROJECTED solid angle — Lambert's formula
// for a planar polygon, exact for any orientation:
//
//     OMEGA_proj = 1/2 * SUM_i  acos( dot(v_i, v_i+1) ) * dot( n, norm(v_i x v_i+1) )
//
// over the vertex directions from p. HlmsPbs consumes the diffuse GI as
// `envColourD` = E/pi and multiplies it by the albedo (kD * pi), so a MATTE
// surface with no other light source renders, in linear radiance,
//
//     pixel = albedo * E / pi.
//
// MATTE is not a wish here, it is authored: the floor uses the Specular
// workflow at ior 1.0 with a black specular colour — F0 = 0, no environment
// specular at all — which is the same recipe the default ground uses (GF1), so
// the pixel carries the DIFFUSE term and nothing else. And with no area light
// in the scene the pin's `envBRDF` is the identity (1,0,1), so the arithmetic
// above is the whole shader.
//
// WHAT IS CALIBRATED RATHER THAN ASSUMED. Three things, all measured inside the
// suite and all printed:
//   1. THE READBACK — the view's scene RADIANCE in float (HDR-READBACK-1),
//      checked against a ramp of EMISSIVE patches of known radiance, whose
//      pixels are that radiance.
//   2. THE BRDF PATH — a directional light of known intensity on the same
//      floor renders `albedo * intensity` (the engine's powerScale = I*pi
//      against HlmsPbs's kD = albedo/pi), which is an independent check of
//      every factor above.
//   3. THE PIXEL/WORLD MAPPING — the emitter's own silhouette in the picture is
//      compared with where the mapping says it should be.
// A gate whose currency is not calibrated is a number, not a measurement.
//
// AND WHAT IS COMPARED. At N floor points, in one process and at one pose: the
// GATHER, the CONES and the FIELD against the closed form, each as a RATIO. The
// ratio's VALUE is the magnitude; the ratio's SPREAD across the points is the
// stronger statement, because an estimator that is right in shape and wrong by
// a constant is a calibration and one that varies with the geometry is wrong.
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

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

/// The diffuse lobe's energy factor at perceptual roughness 1 (every floor here
/// is roughness 1): what the environment lobe multiplies envColourD by since
/// PHOTON-ENV-1, the same factor as the direct lobe's.
static const double kEnergyFactor1 = 1.0 / 1.51;

static const unsigned kSize = 512u;
/// HALF the vertical extent of the orthographic camera, in world units. An
/// ORTHOGRAPHIC top-down camera is what makes a world point and a pixel the
/// same statement: the mapping is affine and exact, with no projection to
/// invert and no perspective foreshortening to argue about.
static const float kOrthoHalf = 8.0f;
/// Where the camera sits. Low enough that the emitter is inside cascade 0 (the
/// cascade chain is camera-centred, 10 m across at High), high enough to see
/// the floor points.
static const float kCamY = 6.0f;

// ---------------------------------------------------------------------------
/// LAMBERT'S FORMULA: the projected solid angle of a planar polygon, seen from
/// `p` by a surface whose normal is `n`. Exact — no approximation, no series,
/// no form-factor table — for any polygon entirely on the positive side of the
/// receiver's plane, which the fixture asserts.
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

/// The camera: straight down, orthographic. Built by hand because the lookAt
/// helper's +Y up is degenerate for a view that looks along -Y.
static CameraDesc topDownCamera()
{
    CameraDesc c;
    c.position = Vec3(0.0f, kCamY, 0.0f);
    // A -90 degree rotation about X: the default forward (-Z) turns onto -Y,
    // so screen right is world +X and screen DOWN is world +Z.
    c.orientation = Quat(-0.70710678f, 0.0f, 0.0f, 0.70710678f);
    c.orthographic = true;
    c.orthoSize = kOrthoHalf;
    c.farClip = 200.0f;
    return c;
}

/// World (x, z) on the floor -> pixel, under that camera. Verified against the
/// emitter's own silhouette before any number below it is believed.
static void worldToPixel(float wx, float wz, double &px, double &py)
{
    px = (double(wx) / kOrthoHalf * 0.5 + 0.5) * kSize;
    py = (double(wz) / kOrthoHalf * 0.5 + 0.5) * kSize;
}

/// The mean of a block of pixels, per channel, in the picture's own units.
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

static int traceMain(Engine *e);

// ---------------------------------------------------------------------------
int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = std::getenv("JAH_GATHER_TRACE") ? "test-gi-gather-trace-ogre.log"
                                                  : "test-gi-gather-reference-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    if (std::getenv("JAH_GATHER_TRACE")) return traceMain(e);

    View *view = e->createOffscreenView("ref", kSize, kSize, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: view: %s\n", e->lastError().c_str()); return 1; }
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_reference is about the "
                    "gather and skips cleanly\n");
        return 0;
    }
    // THE CHAIN, WITH NO SSR ROW AT ALL. The gather's own row is what brings the
    // prepass now (`ChainDesc::probeGather`), and this fixture is where that is
    // proved: `ssr = 0`, so nothing in the graph exists for the reflection's
    // sake, and the probes still find their surfaces.
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdrReadback = true;   // every number below is read as RADIANCE (HDR-READBACK-1)
    view->setPostFx(fx);
    view->setShadows(true);

    Scene *s = e->createScene("ref");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    view->setCamera(topDownCamera());

    // ---- THE FIXTURE ------------------------------------------------------
    // A matte floor, a rectangular emitter above it, and nothing else: no sky,
    // no ambient, no light. Every photon that reaches the floor left the
    // emitter.
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    // A BRIGHT floor, and it costs nothing here: the emitter's albedo is zero
    // and the floor is planar, so there is no second bounce to speak of (the
    // arm at the end of this file measures exactly that and finds 0.3 %).
    const float kFloorAlbedo = 0.9f;
    const auto makeFloor = [&](float albedo) {
        PbrParams p;
        p.albedo = Colour(albedo, albedo, albedo);
        p.roughness = 1.0f;
        // MATTE, by the ground's own recipe (GF1): the Specular workflow at
        // ior 1.0 with a black specular colour is F0 = 0 — no environment
        // specular term at all, so the pixel is the diffuse GI and nothing
        // else. Without it the cone specular would ride along in every arm.
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        return s->createPbrMaterial(p);
    };
    const NodeId floorNode = s->createNode();
    MaterialId floorMat = makeFloor(kFloorAlbedo);
    CHECK(floorNode && floorMat && s->attachMesh(floorNode, cube, floorMat),
          "the matte floor exists (Specular workflow, ior 1.0, black kS — no specular term)");
    s->setNodeTransform(floorNode, Vec3(0.0f, -0.25f, 0.0f), Quat(), Vec3(60.0f, 0.5f, 60.0f));

    // THE EMITTER: 4 x 4 m, 0.2 m thick (two and a half cascade-0 cells, so the
    // voxeliser holds it), its BOTTOM face at y = 3.
    const float kEmitHalf = 2.0f;
    const float kEmitBottom = 3.0f;
    // Its radiance is 0.9 for continuity with every number this suite has
    // printed since it existed — not for a ceiling any more: the emissive voxel
    // store is float since ogre-patch 0087 and the picture is read as radiance
    // (HDR-READBACK-1), so nothing between the emitter and the number clips.
    const float kEmitRadiance = 0.9f;
    {
        const NodeId n = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.0f, 0.0f, 0.0f);       // it must not bounce anything back
        p.emissive = Colour(kEmitRadiance, kEmitRadiance, kEmitRadiance);
        p.roughness = 1.0f;
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        const MaterialId m = s->createPbrMaterial(p);
        CHECK(n && m && s->attachMesh(n, cube, m), "the rectangular emitter exists");
        s->setNodeTransform(n, Vec3(0.0f, kEmitBottom + 0.1f, 0.0f), Quat(),
                            Vec3(kEmitHalf * 2.0f, 0.2f, kEmitHalf * 2.0f));
    }

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::Off;
    gi.numBounces = 1;
    gi.cascades = true;
    CHECK(s->setGlobalIllumination(gi), "the cascade chain builds over the fixture");
    render(e, 30);

    // ---- CALIBRATION 1: THE READBACK --------------------------------------
    // Four emissive patches of known radiance, read where they are. An
    // emissive surface that reflects nothing (F0 = 0, black albedo — the
    // floor's own recipe) renders its own radiance, so each pixel must BE the
    // number it was authored at. It was the transfer's identification while
    // the picture was 8 bits; read as radiance it is the instrument's check.
    const double kRamp[4] = { 0.05, 0.12, 0.30, 0.60 };
    {
        for (int i = 0; i < 4; ++i) {
            const NodeId n = s->createNode();
            PbrParams p;
            p.albedo = Colour(0.0f, 0.0f, 0.0f);
            p.emissive = Colour(float(kRamp[i]), float(kRamp[i]), float(kRamp[i]));
            p.roughness = 1.0f;
            p.workflow = PbrParams::Workflow::Specular;
            p.ior = 1.0f;
            p.specularColour = Colour(0.0f, 0.0f, 0.0f);
            const MaterialId m = s->createPbrMaterial(p);
            if (!n || !m || !s->attachMesh(n, cube, m)) { std::printf("FAIL: ramp\n"); ++failures; }
            // On the floor, out along -Z where no measurement is taken.
            s->setNodeTransform(n, Vec3(-6.0f + 3.0f * float(i), 0.05f, -6.0f), Quat(),
                                Vec3(2.0f, 0.1f, 2.0f));
        }
        s->refreshGlobalIllumination();
        render(e, 30);
        ImageF img;
        CHECK(view->readPixelsHdr(img), "the view reads its radiance back");
        double worst = 0.0;
        std::printf("\n   THE READBACK, from a ramp of emissive patches:\n");
        for (int i = 0; i < 4; ++i) {
            double px, py;
            worldToPixel(-6.0f + 3.0f * float(i), -6.0f, px, py);
            double m[3];
            blockMean(img, px, py, 6, m);
            const double rel = std::fabs(m[0] - kRamp[i]) / kRamp[i];
            worst = std::max(worst, rel);
            std::printf("     radiance %.2f -> %.5f (%.2f %% off)\n", kRamp[i], m[0], 100.0 * rel);
        }
        CHECK_MSG(worst < 0.01,
                  "THE READBACK IS THE RADIANCE (worst %.2f %%, bar 1 %%) — the currency every "
                  "number below is stated in", 100.0 * worst);
    }

    // ---- CALIBRATION 2: THE MAPPING ---------------------------------------
    // The emitter's silhouette, found in the picture, against where the mapping
    // says it is. Nothing below means anything if a "floor point" is not the
    // pixel it claims to be.
    {
        ImageF img;
        view->readPixelsHdr(img);
        int minX = int(kSize), maxX = -1, minY = int(kSize), maxY = -1;
        for (unsigned y = 0; y < img.height; ++y)
            for (unsigned x = 0; x < img.width; ++x) {
                // The emitter is the brightest thing in the picture by a wide
                // margin (its own radiance against a floor under a fifth of
                // it), and the ramp patches are out at -Z beyond this window.
                // ...and the search window excludes the calibration ramp,
                // whose brightest patch is out at -Z (the top of the picture).
                if (img.at(x, y).r > 0.75f * float(kEmitRadiance) && y > kSize / 4u &&
                    y < kSize * 3u / 4u) {
                    minX = std::min(minX, int(x)); maxX = std::max(maxX, int(x));
                    minY = std::min(minY, int(y)); maxY = std::max(maxY, int(y));
                }
            }
        double x0, y0, x1, y1;
        worldToPixel(-kEmitHalf, -kEmitHalf, x0, y0);
        worldToPixel(kEmitHalf, kEmitHalf, x1, y1);
        std::printf("   THE MAPPING: the emitter's silhouette is [%d..%d]x[%d..%d] px; the "
                    "mapping predicts [%.0f..%.0f]x[%.0f..%.0f]\n", minX, maxX, minY, maxY, x0, x1,
                    y0, y1);
        CHECK(maxX > minX && std::fabs(minX - x0) <= 3.0 && std::fabs(maxX - x1) <= 3.0 &&
                  std::fabs(minY - y0) <= 3.0 && std::fabs(maxY - y1) <= 3.0,
              "THE PIXEL/WORLD MAPPING IS VERIFIED against the emitter's own silhouette "
              "(within 3 px on every edge)");
    }

    // ---- WHAT THE VOXEL CACHE HOLDS, which is where a loss would be ------
    // Both the gather and the cones read the hit's radiance out of the SAME
    // voxel volume, so a volume that does not hold the emitter's radiance makes
    // both of them dark by the same factor — and the ratios below could not
    // tell that from an estimator that is wrong. PHOTON-M3's readback is the
    // instrument: `peak` is the brightest voxel of the lit volume in scene
    // units, and the emitter's own radiance is the number it should be near.
    {
        const GiVoxelStats vs = s->giVoxelStats(0);
        std::printf("   THE VOXEL CACHE (cascade 0, %s %dx%dx%d, multiplier %.4f): peak %.4f, "
                    "peak direct %.4f, mean over %lld lit voxels %.4f — the emitter's own "
                    "radiance is %.2f\n", vs.format.c_str(), vs.width, vs.height, vs.depth,
                    double(vs.multiplier), double(vs.peak), double(vs.peakDirect),
                    (long long)vs.voxelsLit, vs.meanLit, double(kEmitRadiance));
    }

    // ---- CALIBRATION 3: THE ENVIRONMENT PATH, which is the one that matters
    // A FLAT AMBIENT of known radiance, with GI off, arrives at the pixel by
    // exactly the route the gather's answer does: it is written into
    // `envColourD` and multiplied by the albedo (kD * pi) AND BY THE DIFFUSE
    // LOBE'S ENERGY FACTOR (PHOTON-ENV-1: `jahDiffuseEnergyFactor`,
    // lerp( 1, 1/1.51, roughness ) — 1/1.51 on this roughness-1 floor, the
    // factor the direct lobe has always carried; MEASURE-1a decision b). So a
    // floor under an ambient of A renders `albedo * energyFactor * A`, and
    // measuring that calibrates the whole currency — the kD, the pi, the energy
    // factor and the float readback — in one number, on the path the
    // measurement below actually uses; every reading below is divided by the
    // same albedo * energyFactor to come back to E / pi.
    //
    // AND THE MEASURED FACTOR IS THE CURRENCY (PHOTON-GATHER-1b). The ratio
    // columns below used to divide by `albedo * kEnergyFactor1` — the constant
    // lobe factor the environment path carried before PHOTON-WRITER-1 replaced
    // it with the lobe's own directional albedo (0.688 at normal view against
    // 1/1.51 = 0.662) — so every ratio carried this calibration's residual
    // (+7.6 % at this tree) as if it were the estimator's. The factor measured
    // here on the SAME floor at the SAME view is what a pixel of envColourD is
    // worth; the gate divides by it.
    double envPathScale = 1.0;
    // The DIRECT path is printed beside it, not gated: a directional light of
    // power P on a horizontal matte floor renders MEASURE-1a's pbsDirect, the
    // normalised Disney lobe — energyFactor(1) = 0.662 of `albedo * P / pi` at
    // normal incidence, less the shadow arm's own loss. Nothing this lane
    // measures goes through it — the gather's answer is an environment term.
    {
        GiParams off = gi;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        const float kAmbient = 0.25f;
        s->setAmbient(Colour(kAmbient, kAmbient, kAmbient), Colour(kAmbient, kAmbient, kAmbient));
        render(e, 20);
        ImageF img;
        view->readPixelsHdr(img);
        double px, py, m[3];
        worldToPixel(5.0f, 5.0f, px, py);
        blockMean(img, px, py, 8, m);
        const double lit = m[0];
        // THE ENGINE'S AMBIENT IS AN IRRADIANCE, not a radiance: `setAmbient`
        // hands HlmsPbs the same 1/pi pair the voxel path uses (ENGINE-4 item
        // 1, "one ambient convention inside and outside the volume"), so what
        // reaches `envColourD` is A/pi and the floor renders albedo * A / pi.
        // Measured here rather than asserted from the header, which is the
        // whole point of a calibration.
        const double expected = double(kFloorAlbedo) * kEnergyFactor1 * double(kAmbient) /
                                3.14159265358979323846;
        std::printf("   THE ENVIRONMENT PATH: a %.2f-albedo floor under a flat ambient of %.2f "
                    "renders %.4f; the arithmetic says %.4f (%.1f %%)\n", double(kFloorAlbedo),
                    double(kAmbient), lit, expected, 100.0 * (lit / expected - 1.0));
        envPathScale = lit / expected;
        CHECK_MSG(std::fabs(lit / expected - 1.0) < 0.08,
                  "THE ENVIRONMENT PATH IS CALIBRATED: %.4f against %.4f, within 8 %% — a pixel "
                  "of this floor IS albedo * energyFactor(1) * envColourD", lit, expected);

        // ...and the direct term, both shadow arms, printed.
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        const NodeId sun = enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f), 0.25f);
        for (int arm = 0; arm < 2; ++arm) {
            view->setShadows(arm == 0);
            render(e, 20);
            view->readPixelsHdr(img);
            blockMean(img, px, py, 8, m);
            const double d = m[0];
            std::printf("   (the direct term, shadows %s: %.4f against albedo * P / pi = "
                        "%.4f, %.0f %%)\n", arm == 0 ? "on " : "off", d,
                        double(kFloorAlbedo) * 0.25 / 3.14159265358979323846,
                        100.0 * d / (double(kFloorAlbedo) * 0.25 / 3.14159265358979323846));
        }
        view->setShadows(true);
        s->removeNode(sun);
        s->setGlobalIllumination(gi);
        render(e, 20);
    }

    // ---- THE MEASUREMENT --------------------------------------------------
    // Seven floor points along +x, clear of the emitter's footprint and inside
    // the camera's window. At each one the analytic irradiance is averaged over
    // the PROBE CELL, not taken at its centre: the probe is jittered inside its
    // cell every frame, so what an average over frames converges to is the
    // average over the cell — and that is what the closed form is asked for.
    struct Point { float x, z; double analytic; double bottomOnly; };
    std::vector<Point> points;
    // THE COMPLETE FORM (PHOTON-GATHER-1b fix round, audit F1): EVERY face of the
    // 4 x 4 x 0.2 m emitter emits 0.9, not only its bottom, and a floor point
    // beyond x = 2 sees the +X side strip as well — +2 % of the bottom's
    // irradiance at 2.6 m rising to +15 % at 5.6 m (the same rising shape the
    // first cut attributed to the hit read). Each face counts where it FACES the
    // point (Lambert's routine returns |the projected solid angle| whichever
    // side is seen, so a back face must be excluded by its normal, not by the
    // formula); the top faces the sky and never reaches the floor.
    struct Face { double v[4][3]; double n[3]; };
    const double b0 = double(kEmitBottom), b1 = double(kEmitBottom) + 0.2, h = double(kEmitHalf);
    const Face faces[5] = {
        { { { -h, b0, -h }, { h, b0, -h }, { h, b0, h }, { -h, b0, h } }, { 0, -1, 0 } },   // bottom
        { { { h, b0, -h }, { h, b1, -h }, { h, b1, h }, { h, b0, h } }, { 1, 0, 0 } },       // +X
        { { { -h, b0, -h }, { -h, b1, -h }, { -h, b1, h }, { -h, b0, h } }, { -1, 0, 0 } },  // -X
        { { { -h, b0, h }, { h, b0, h }, { h, b1, h }, { -h, b1, h } }, { 0, 0, 1 } },       // +Z
        { { { -h, b0, -h }, { h, b0, -h }, { h, b1, -h }, { -h, b1, -h } }, { 0, 0, -1 } },  // -Z
    };
    const auto faceSees = [](const Face &f, const double p[3]) {
        double d = 0.0;
        for (int k = 0; k < 3; ++k) d += f.n[k] * (p[k] - f.v[0][k]);
        return d > 0.0;
    };
    const double up[3] = { 0.0, 1.0, 0.0 };
    const double cellWorld = 16.0 * (2.0 * kOrthoHalf) / double(kSize);   // one probe cell
    for (int i = 0; i < 7; ++i) {
        Point pt;
        pt.x = 2.6f + 0.5f * float(i);
        pt.z = 0.0f;
        double acc = 0.0, accBottom = 0.0;
        int n = 0;
        for (int sy = 0; sy < 8; ++sy)
            for (int sx = 0; sx < 8; ++sx) {
                const double ox = (double(sx) + 0.5) / 8.0 - 0.5, oz = (double(sy) + 0.5) / 8.0 - 0.5;
                const double p[3] = { double(pt.x) + ox * cellWorld, 0.0,
                                      double(pt.z) + oz * cellWorld };
                for (int f = 0; f < 5; ++f) {
                    if (!faceSees(faces[f], p)) continue;
                    const double e = projectedSolidAngle(p, up, faces[f].v, 4) * double(kEmitRadiance);
                    acc += e;
                    if (f == 0) accBottom += e;
                }
                ++n;
            }
        pt.analytic = acc / n / 3.14159265358979323846;   // E/pi, what envColourD is
        pt.bottomOnly = accBottom / n / 3.14159265358979323846;
        points.push_back(pt);
    }

    // THE THREE ARMS, each ONE estimator: the gather (the cones compiled out by
    // the listener, the field's cage stood down by ogre-patch 0086), the cones
    // (no field, no gather), the field (no gather).
    std::vector<double> sem;   // the gather arm's standard error per point, relative
    const auto measure = [&](const char *what, GiToggle ddgi, bool gather,
                             std::vector<double> &out) {
        GiParams g = gi;
        g.ddgi = ddgi;
        g.gather = gather ? GiToggle::On : GiToggle::Off;
        s->setGlobalIllumination(g);
        GatherTuning t;
        s->setGatherTuning(t);
        s->refreshGlobalIllumination();
        render(e, 40);
        // AVERAGED OVER FRAMES, IN LINEAR UNITS. The gather's estimate moves
        // frame to frame by construction (the probe's position and its 64 ray
        // directions are keyed on the frame index), so one frame is one sample
        // of a random variable whose MEAN is the quantity. Forty-eight frames
        // of a still scene is the measurement (a FLOAT read since REFLECT-1 —
        // no 8-bit quantisation to dither).
        //
        // ...AND THE GATHER'S ARM TAKES 192 (PHOTON-GATHER-1b). At the grazing
        // points the emitter covers two or three of a probe's 64 texels and the
        // 11 x 11 block sits inside one or two probe cells, so a frame's reading
        // there is a coin toss per texel: its standard error over 48 frames is
        // 3-4 % of the value — the size of the per-point bar — and it is
        // PRINTED below so the bar is never read inside the instrument's noise.
        // (The arms are deterministic: the sequence is the frame index's, so a
        // run repeats its own reading exactly — which is not the same as the
        // reading being the mean.)
        std::vector<double> acc(points.size(), 0.0), acc2(points.size(), 0.0);
        const int kFrames = gather ? 192 : 48;
        for (int f = 0; f < kFrames; ++f) {
            e->renderOneFrame();
            ImageF img;
            view->readPixelsHdr(img);
            for (size_t i = 0; i < points.size(); ++i) {
                double px, py, m[3];
                worldToPixel(points[i].x, points[i].z, px, py);
                blockMean(img, px, py, 5, m);
                const double v = m[0];   // the float read: scene-referred, no decode
                acc[i] += v;
                acc2[i] += v * v;
            }
        }
        out.assign(points.size(), 0.0);
        if (gather) sem.assign(points.size(), 0.0);
        for (size_t i = 0; i < points.size(); ++i) {
            out[i] = acc[i] / kFrames /
                     (double(kFloorAlbedo) * kEnergyFactor1 * envPathScale);   // back to E/pi
            if (gather) {
                const double mean = acc[i] / kFrames;
                const double var = std::max(0.0, acc2[i] / kFrames - mean * mean);
                // The standard error of the mean, as a fraction of it.
                sem[i] = mean > 0.0 ? std::sqrt(var / kFrames) / mean : 0.0;
            }
        }
        (void)what;
    };
    std::vector<double> gatherE, conesE, fieldE;
    measure("gather", GiToggle::Off, true, gatherE);
    const bool gatherRan = s->giStatus().gather.running;
    measure("cones", GiToggle::Off, false, conesE);
    measure("field", GiToggle::On, false, fieldE);
    // ...and whether the FIELD arm was a field at all. It is reported rather
    // than assumed because a column that silently repeats the one beside it is
    // worse than an absent one.
    const bool fieldBound = s->giStatus().ifdBound;
    CHECK(gatherRan, "the gather arm above really ran the gather (giStatus().gather.running)");

    std::printf("\n THE FIRST MEASUREMENT OF THIS RENDERER'S DIFFUSE GI AGAINST A REFERENCE\n");
    std::printf(" (a %.0f x %.0f m Lambertian emitter of radiance %.1f, its bottom face %.1f m "
                "over a matte floor; every number is E/pi, i.e. envColourD)\n\n",
                double(kEmitHalf * 2), double(kEmitHalf * 2), double(kEmitRadiance),
                double(kEmitBottom));
    std::printf("   x (m)   CLOSED FORM (bottom only)     GATHER  ratio      CONES  ratio      FIELD  ratio\n");
    double gSum = 0.0, gMin = 1e30, gMax = -1e30;
    for (size_t i = 0; i < points.size(); ++i) {
        const double a = points[i].analytic;
        const double rg = a > 0 ? gatherE[i] / a : 0.0;
        const double rc = a > 0 ? conesE[i] / a : 0.0;
        const double rf = a > 0 ? fieldE[i] / a : 0.0;
        std::printf("   %5.2f   %11.5f (%9.5f) %10.5f  %5.3f %10.5f  %5.3f %10.5f  %5.3f\n",
                    double(points[i].x), a, points[i].bottomOnly, gatherE[i], rg, conesE[i], rc,
                    fieldE[i], rf);
        gSum += rg;
        gMin = std::min(gMin, rg);
        gMax = std::max(gMax, rg);
    }
    const double gMean = gSum / double(points.size());
    const double spread = gMean > 0 ? (gMax - gMin) / gMean : 1e30;
    std::printf("\n   THE GATHER: mean ratio %.3f, spread %.1f %% of the mean (min %.3f, max "
                "%.3f)\n", gMean, 100.0 * spread, gMin, gMax);

    // THE TWO ASSERTIONS, and they say different things.
    //
    // THE SHAPE is the strong one: an estimator that is right in shape and
    // wrong by a constant is a CALIBRATION, and one whose error varies with the
    // geometry is wrong about the integral. The gather's ratio must therefore
    // be flat across seven points spanning a factor of several in irradiance.
    CHECK_MSG(spread < 0.25,
              "THE SHAPE IS RIGHT: the gather's ratio to the closed form varies by %.1f %% "
              "across the seven points (bar: 25 %%)", 100.0 * spread);
    // THE MAGNITUDE is the one nobody has ever measured. The bar is wide on
    // purpose and it is stated, not hidden: the gather reads the hit's radiance
    // out of the VOXEL CACHE, whose texel is 0.078 m here and whose store is
    // quantised, so a few per cent of loss is expected and is the cache's, not
    // the estimator's; an error of a FACTOR would be the estimator's.
    CHECK_MSG(gMean > 0.70 && gMean < 1.30,
              "THE MAGNITUDE IS RIGHT: the gather reads %.3f of the closed-form irradiance "
              "(bar: 0.70 to 1.30)", gMean);
    // ...AND AT EVERY POINT, against the COMPLETE form (PHOTON-GATHER-1b and its
    // fix round, audit F1). Measured at 192 frames, the standard errors printed:
    //   base (a single-probe read, GATHER-1a's estimator)  0.958 0.947 0.928 0.922 0.912 0.912 0.912
    //   this lane (filter, SH9 anchored, 4-probe + twin)    0.977 0.963 0.949 0.944 0.944 0.947 0.944
    // A residual survives — flat at -5 % past 3.6 m — and its cause, BY
    // EXCLUSION: each probe's value IS GATHER-1a's exact ratio estimator (the SH
    // is anchored to it at the probe's normal, and the floor's normal is the
    // probe's); the filter moves no mean (its off-arm read the same to 0.1 %);
    // the four-probe interpolation's blur of this convex falloff is +0.4..1.3 %
    // (the WRONG sign, g1b_blur.py); the currency is the measured environment
    // factor; a miss is exact (gi.gather_sky, 1.004-1.016 against a band-limited
    // reference). What is left is the radiance the rays bring back from their
    // HITS — the emitter read out of the voxel cache (its opacity-divided,
    // mip-footprint sample of a 0.2 m slab). GA-1e's card read at the hit is
    // texel-exact inside its footprint gate and is predicted to close it.
    // THE GATING BARS, from the arithmetic: the worst reading (0.944) less two
    // standard errors (2 x 1.6 %) is 0.912, so the floor is 0.90; nothing in the
    // chain adds light but the blur (+1.3 %) and two standard errors (+3.2 %),
    // so the ceiling is 1.05. The brief's 1.00 +- 0.05 at every point is the
    // TARGET row (gi.gather_reference_target, label photon-target).
    const bool targetRow = std::getenv("JAH_GATHER_REFERENCE_TARGET") != nullptr;
    for (size_t i = 0; i < points.size(); ++i) {
        const double a = points[i].analytic;
        const double rg = a > 0 ? gatherE[i] / a : 0.0;
        const double lo = targetRow ? 0.95 : 0.90, hi = 1.05;
        const double se = i < sem.size() ? sem[i] : 0.0;
        if (targetRow)
            std::printf("target: %.3f at x = %.2f m (bar 1.00 +- 0.05)\n", rg, double(points[i].x));
        CHECK_MSG(rg >= lo && rg <= hi,
                  "THE ANALYTIC GATE AT x = %.2f m: the gather reads %.3f of the complete closed "
                  "form (bar %.2f..%.2f; the reading's standard error %.1f %%)",
                  double(points[i].x), rg, lo, hi, 100.0 * se);
    }

    // ...and the other two estimators are PRINTED, never gated: this lane does
    // not own the cones or the field, and a bar on them here would be a bar
    // nobody agreed to. The numbers are the finding.
    {
        double cSum = 0.0, fSum = 0.0, cMin = 1e30, cMax = -1e30, fMin = 1e30, fMax = -1e30;
        for (size_t i = 0; i < points.size(); ++i) {
            const double a = points[i].analytic;
            const double rc = a > 0 ? conesE[i] / a : 0.0, rf = a > 0 ? fieldE[i] / a : 0.0;
            cSum += rc; fSum += rf;
            cMin = std::min(cMin, rc); cMax = std::max(cMax, rc);
            fMin = std::min(fMin, rf); fMax = std::max(fMax, rf);
        }
        const double cMean = cSum / double(points.size()), fMean = fSum / double(points.size());
        std::printf("   THE CONES:  mean ratio %.3f, spread %.1f %% (min %.3f, max %.3f)\n",
                    cMean, cMean > 0 ? 100.0 * (cMax - cMin) / cMean : 0.0, cMin, cMax);
        std::printf("   THE FIELD:  mean ratio %.3f, spread %.1f %% (min %.3f, max %.3f)%s\n",
                    fMean, fMean > 0 ? 100.0 * (fMax - fMin) / fMean : 0.0, fMin, fMax,
                    fieldBound ? "" : "  [NO FIELD WAS BOUND in this fixture — the column is "
                                      "the cones' again]");
        std::printf("   (printed, not gated: this lane owns the gather)\n");
    }

    // ---- THE SECOND BOUNCE, priced rather than assumed away ---------------
    // The floor is not black, so some of what it receives goes back up, hits
    // the emitter's underside and comes down again — light the closed form does
    // not contain. If it mattered, the ratio would RISE with the floor's
    // albedo. Half the albedo, measure again: a ratio that does not move says
    // the second bounce is inside the noise.
    {
        PbrParams p;
        p.albedo = Colour(kFloorAlbedo * 0.5f, kFloorAlbedo * 0.5f, kFloorAlbedo * 0.5f);
        p.roughness = 1.0f;
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        const MaterialId dim = s->createPbrMaterial(p);
        s->setNodeMaterial(floorNode, dim);
        std::vector<double> dimE;
        // `measure` divides by kFloorAlbedo; this arm's floor is half of it, so
        // its E/pi comes back at half and is doubled here.
        measure("gather-dim", GiToggle::Off, true, dimE);
        double rSum = 0.0;
        for (size_t i = 0; i < points.size(); ++i)
            rSum += points[i].analytic > 0 ? (dimE[i] * 2.0) / points[i].analytic : 0.0;
        const double dimMean = rSum / double(points.size());
        std::printf("   THE SECOND BOUNCE: the same measurement at HALF the floor albedo gives a "
                    "ratio of %.3f against %.3f — a difference of %.1f %%\n", dimMean, gMean,
                    100.0 * (gMean - dimMean) / std::max(gMean, 1e-6));
        CHECK_MSG(std::fabs(gMean - dimMean) < 0.12,
                  "the floor's own second bounce is not what is being measured (%.3f against "
                  "%.3f at half the albedo)", dimMean, gMean);
        s->setNodeMaterial(floorNode, floorMat);
    }

    // ---- ONE DIFFUSE TERM, WITH THE FIELD BOUND (ogre-patch 0086) ----------
    //
    // THE ACCOUNTING RULE, and the only arm of any gather suite that tests it:
    // every other arm measures with `ddgi` OFF, which is what makes the three
    // estimators comparable and also what hides this. With a field BOUND and
    // the gather on, a covered pixel must read exactly what it reads with no
    // field at all — the cage declines (the patch), and the field's own
    // FALLBACK terms decline with it (this lane's half, in JahIfd's resolve).
    //
    // WHAT IT DOES NOT GUARD, said here because a reader will come looking. The
    // confidence divisor in JahIfd's resolve (`ifdFrontWeight / sumIfdWeight`)
    // must be SCALE-FREE rather than floored -- a crushed cage carries the same
    // tiny scale in both terms, so the ratio is still one and the field fades to
    // nothing deliberately, while a 1e-6 floor on the denominator alone would
    // collapse the confidence and hand the pixel the whole fallback. THREE
    // fixtures were built to catch that and NONE of them discriminates: the
    // outer wall of a sealed box and its roof (the case that code's own comment
    // names) both have `ifdFrontWeight` EXACTLY zero -- every probe behind the
    // surface -- where the two spellings agree; and this arm's declined cage
    // never reaches the divide, because the resolve pins the confidence to one
    // when the cage did not run. The discriminating input is a cage that DID run
    // whose front share is non-zero and whose every weight was crushed by the
    // visibility test, and no scene was found that produces it. What stands
    // instead of a fixture is that the shipped arithmetic is unchanged for every
    // non-zero divisor, which is a property of the line rather than of a
    // picture.
    {
        std::vector<double> fieldOnE;
        measure("gather+field", GiToggle::On, true, fieldOnE);
        {
            const GiStatus st2 = s->giStatus();
            std::printf("   (the arm really is both: gather running %d, field bound %d; E/pi at "
                        "x = 2.60 m reads %.5f with the field against %.5f without it)\n",
                        int(st2.gather.running), int(st2.ifdBound), fieldOnE[0], gatherE[0]);
        }
        // Seeded at the NEUTRAL ratio, not at zero: seeded at zero nothing is ever
        // "worse than" it and the loop reports its own initialiser.
        double worst = 1.0;
        size_t worstAt = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            const double base = gatherE[i] > 1e-9 ? gatherE[i] : 1e-9;
            const double r = fieldOnE[i] / base;
            if (std::fabs(r - 1.0) > std::fabs(worst - 1.0)) { worst = r; worstAt = i; }
        }
        std::printf("\n   ONE DIFFUSE TERM WITH THE FIELD BOUND: the gather's answer at each "
                    "point with ddgi ON against the same point with it OFF — worst ratio %.3f "
                    "at x = %.2f m\n", worst, double(points[worstAt].x));
        CHECK_MSG(worst > 0.90 && worst < 1.10,
                  "A COVERED PIXEL GETS ONE DIFFUSE TERM: with a field bound the gather's answer "
                  "is %.3f of the field-off answer (bar 0.90 to 1.10) — the cage declines "
                  "(ogre-patch 0086) and its fallback declines with it", worst);
    }

    e->destroyScene(s);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
/// gi.gather_trace — THE TRACE'S OWN BAR, in R5's form.
///
/// A probe a metre from a LIT RED WALL must read most of that wall's radiance
/// back. It is the same question the reflection tier's own suite asks of a ray
/// ("does the hit carry the colour of the thing it hit?"), asked of a gather
/// whose 64 rays see the wall over a large part of their hemisphere — so the
/// quantity is the wall's radiance times the fraction of the hemisphere it
/// covers, which is the closed form above, and the bar is 0.6 of it.
///
/// WHAT MAKES IT CLEAN: the light travels HORIZONTALLY (straight into the
/// wall's face), so the floor receives no direct light at all — every photon
/// on the floor bounced off the wall — and the wall's own radiance is READ from
/// the picture rather than derived, which takes the injection, the voxel store
/// and the shadow term out of the argument.
static int traceMain(Engine *e)
{
    View *view = e->createOffscreenView("trace", kSize, kSize, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: view: %s\n", e->lastError().c_str()); return 1; }
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_trace skips cleanly\n");
        return 0;
    }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdrReadback = true;   // every number below is read as RADIANCE (HDR-READBACK-1)
    view->setPostFx(fx);
    view->setShadows(true);

    Scene *s = e->createScene("trace");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const float kFloorAlbedo = 0.5f;
    const auto matte = [&](Colour albedo, Colour emissive) {
        PbrParams p;
        p.albedo = albedo;
        p.emissive = emissive;
        p.roughness = 1.0f;
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        return s->createPbrMaterial(p);
    };
    {
        const NodeId n = s->createNode();
        s->attachMesh(n, cube, matte(Colour(kFloorAlbedo, kFloorAlbedo, kFloorAlbedo),
                                     Colour(0, 0, 0)));
        s->setNodeTransform(n, Vec3(0.0f, -0.25f, 0.0f), Quat(), Vec3(40.0f, 0.5f, 40.0f));
    }
    // THE WALL: 6 x 4 m at z = -4, its lit face towards +z.
    const float kWallHalfX = 3.0f, kWallTop = 4.0f, kWallZ = -4.0f;
    const Colour kWallAlbedo(0.9f, 0.06f, 0.06f);
    {
        const NodeId n = s->createNode();
        s->attachMesh(n, cube, matte(kWallAlbedo, Colour(0, 0, 0)));
        s->setNodeTransform(n, Vec3(0.0f, kWallTop * 0.5f, kWallZ - 0.1f), Quat(),
                            Vec3(kWallHalfX * 2.0f, kWallTop, 0.2f));
    }
    // ONE emissive patch, the readback's check (the reference suite's
    // calibration, in its smallest form).
    const double kRampRadiance = 0.40;
    {
        const NodeId n = s->createNode();
        s->attachMesh(n, cube, matte(Colour(0, 0, 0),
                                     Colour(float(kRampRadiance), float(kRampRadiance),
                                            float(kRampRadiance))));
        s->setNodeTransform(n, Vec3(-6.0f, 0.05f, 5.0f), Quat(), Vec3(2.0f, 0.1f, 2.0f));
    }
    // THE LIGHT TRAVELS HORIZONTALLY, into the wall's face: the floor's own
    // NdotL is zero, so every photon it shows bounced.
    enginetest::addDirectionalLight(s, Vec3(0.0f, 0.0f, -1.0f), 1.0f);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;
    gi.numBounces = 1;
    gi.cascades = true;
    CHECK(s->setGlobalIllumination(gi), "the cascade chain builds over the wall fixture");
    view->setCamera(topDownCamera());
    render(e, 40);

    {
        ImageF img;
        view->readPixelsHdr(img);
        double px, py, m[3];
        worldToPixel(-6.0f, 5.0f, px, py);
        blockMean(img, px, py, 6, m);
        std::printf("   the readback: a patch of radiance %.2f reads %.5f\n", kRampRadiance, m[0]);
        CHECK_MSG(std::fabs(m[0] - kRampRadiance) < 0.01 * kRampRadiance,
                  "THE READBACK IS THE RADIANCE: %.5f against %.2f (bar 1 %%)", m[0],
                  kRampRadiance);
    }

    // ---- the wall's own radiance, read head-on ----------------------------
    double wallRed = 0.0;
    {
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 2.0f),
                                                        Vec3(0.0f, 2.0f, kWallZ));
        view->setCamera(c);
        render(e, 20);
        ImageF wall;
        view->readPixelsHdr(wall);
        double m[3];
        blockMean(wall, kSize * 0.5, kSize * 0.5, 40, m);
        wallRed = m[0];
        std::printf("   the wall renders red %.4f (authored albedo %.2f under a light of "
                    "radiance 1.0)\n", wallRed, double(kWallAlbedo.r));
        // A FLOOR, NOT A FORMULA: the bar only asks whether the wall is lit,
        // because the wall's radiance is MEASURED here and every number below
        // is stated against the measurement. It is deliberately well under
        // `albedo * P / pi` (0.286 for this wall) — the direct term on this pin
        // renders 66-69 % of that arithmetic, which gi.gather_reference prints
        // beside its own calibration and which nothing in this lane goes
        // through.
        CHECK_MSG(wallRed > 0.1, "the wall is lit at all (red %.4f)", wallRed);
        view->setCamera(topDownCamera());
        render(e, 20);
    }

    // ---- the floor a metre from it, under the gather ----------------------
    const double verts[4][3] = { { -double(kWallHalfX), 0.0, double(kWallZ) },
                                 {  double(kWallHalfX), 0.0, double(kWallZ) },
                                 {  double(kWallHalfX), double(kWallTop), double(kWallZ) },
                                 { -double(kWallHalfX), double(kWallTop), double(kWallZ) } };
    const double up[3] = { 0.0, 1.0, 0.0 };
    const float kAtZ = -3.0f;
    const double cellWorld = 16.0 * (2.0 * kOrthoHalf) / double(kSize);
    double analytic = 0.0;
    {
        double acc = 0.0;
        for (int sy = 0; sy < 8; ++sy)
            for (int sx = 0; sx < 8; ++sx) {
                const double ox = (double(sx) + 0.5) / 8.0 - 0.5, oz = (double(sy) + 0.5) / 8.0 - 0.5;
                const double p[3] = { ox * cellWorld, 0.0, double(kAtZ) + oz * cellWorld };
                acc += projectedSolidAngle(p, up, verts, 4);
            }
        analytic = wallRed * (acc / 64.0) / 3.14159265358979323846;
    }

    double measured = 0.0;
    const int kFrames = 48;
    for (int f = 0; f < kFrames; ++f) {
        e->renderOneFrame();
        ImageF shot;
        view->readPixelsHdr(shot);
        double px, py, m[3];
        worldToPixel(0.0f, kAtZ, px, py);
        blockMean(shot, px, py, 5, m);
        measured += m[0];
    }
    // Back to E/pi through the floor's albedo AND the environment lobe's energy
    // factor (roughness 1: 1/1.51 — PHOTON-ENV-1), as the reference arms.
    measured = measured / kFrames / (double(kFloorAlbedo) * kEnergyFactor1);
    std::printf("\n   THE PROBE ON THE LIT WALL: the floor a metre out reads E/pi = %.5f; the "
                "closed form for that wall at that point is %.5f (%.0f %% of it)\n", measured,
                analytic, 100.0 * measured / std::max(analytic, 1e-9));
    CHECK_MSG(measured >= 0.6 * analytic,
              "THE TRACE CARRIES THE WALL'S LIGHT: %.5f against the closed form's %.5f "
              "(R5's bar: 0.6 of it)", measured, analytic);
    CHECK_MSG(measured <= 1.4 * analytic,
              "...and does not invent any: %.5f against %.5f (a ceiling of 1.4)", measured,
              analytic);

    e->destroyScene(s);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
