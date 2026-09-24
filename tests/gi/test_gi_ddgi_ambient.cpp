// gi.ddgi_ambient — THE SKY INSIDE THE IRRADIANCE FIELD, end to end.
//
// SINCE PHOTON-ENV-1 THE FIELD CARRIES THE SKY ITSELF: every probe ray that
// escapes the voxels reads the one environment in its direction, and the
// cosine integration puts it in the irradiance atlas beside the bounce. The
// read-time "ambient x sky visibility" term this suite was written for, and its
// dial (ddgiAmbient, whose 0 was "DDGI before the fix"), are deleted, so the
// A/Bs below compare the field against the cone reference and the ANALYTIC sky
// visibility, and the sealed-room invariance is stated as "the sky on against
// the sky off" (setAmbient black), which is the physics the dial stood in for.
// The history below is kept because it is why each case exists.
//
// WHAT WAS WRONG, in one paragraph, because the mechanism is three upstream
// facts stacked on each other (SPECS/OGRE_UPSTREAM_ISSUES.md; GI_UNIFIED_SPEC.md
// ADDENDUM CORRECTION):
//
//   1. Every PBS ambient term is wrapped in `if( vctSpecular.w == 0 )` —
//      "only use ambient lighting if the object is outside any VCT probe"
//      (AmbientLighting_piece_ps.any:63,84,123);
//   2. the volume test that would set `vctSpecular.w` to zero is COMMENTED OUT
//      upstream (`float blend = 1.0f;` in both branches,
//      Vct_piece_ps.any:325-331), so the gate never fires and PBS ambient is
//      dead scene-wide in any VCT scene;
//   3. the only live ambient was therefore the cone-traced diffuse's own
//      `light.xyz += ambient * light.w` — ambient times the cones' ESCAPE
//      FRACTION, i.e. ambient weighted by visible sky — and binding an
//      irradiance field sets `VctDisableDiffuse`, which deletes that branch.
//
// So with DDGI on, ambient light inside the lit volume came from NOWHERE. It
// reads as flatness, not as breakage: sky-facing mid-ground loses 15-25%, and a
// sealed room — which has no sky to see — loses nothing at all.
//
// THE FIX, and what this suite exists to prove about it: the field's DEPTH
// atlas already stores, per probe and per octahedral direction, how far the
// generation rays travelled — as a cosine-lobe MEAN and MEAN-SQUARE over the
// hemisphere (upstream's Integration/Depth job convolves it; the first build
// of this fix believed a texel was one ray, and its single tap over-brightened
// a wall foot by +17 points for exactly that reason). Rays that hit nothing
// left the march at its limit, so the escape distance is known per direction,
// and a Chebyshev test at that distance (the DDGI paper's own visibility test
// shape) turns the moment pair into an escape fraction per cage probe, and
// ambient times the cage-weighted fraction is the term the cone diffuse used
// to add (media/Hlms/Jahshaka/JahIfd_piece_ps.any). No Ogre patch.
//
// THE TWO ASSERTIONS THAT MATTER, and they are a PAIR — either alone would pass
// for a wrong fix:
//   * RECOVERY (case 1): on an open scene, DDGI + the fix lands within 10% of
//     the ambient reading VCT gave before DDGI replaced it. A fix that simply
//     ungated PBS ambient (the option this lane did NOT take) would also pass
//     this.
//   * SEALED-ROOM INVARIANCE (case 3): inside a closed room, every pixel moves
//     by at most 1/255 between the fix off and the fix on. This is the one that
//     separates a visibility-weighted term from a blanket ambient — ungating
//     PBS ambient would flood a sealed room with light that has no way in.
//
// Plus: the bounce must survive the fix (case 3), and THE CORNER (case 4) —
// a floor patch at the foot of a wall must be darkened as the fixture's ANALYTIC
// says (sky-only: this engine injects no ambient into the voxels), within a bar
// derived from the voxel envelope and the field's converged error. It used to be
// compared with the cone reference, which over-reads a corner (+11.7 points): the
// cone's corner is the target row gi.cone_corner_target (PHOTON-FIELD-ROTATE-1).
//
// Its own binary, like every GI suite here. Determinism discipline is gi.ddgi's: fixed frame delta, no
// wall clock, and a rebuild-determinism control before any A/B is believed.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 4)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

static void show(const char *what, const Colour &c)
{
    std::printf("   %-34s r=%.4f g=%.4f b=%.4f  (lum %.4f)\n", what, c.r, c.g, c.b, lum(c));
}

/// The largest per-channel difference between two frames, in 8-bit steps —
/// the unit the sealed-room invariance assertion is stated in.
static float maxChannelDelta(const Image &a, const Image &b, unsigned *outX = nullptr,
                             unsigned *outY = nullptr)
{
    float worst = 0.0f;
    if (a.width != b.width || a.height != b.height) return 1e9f;
    for (unsigned y = 0; y < a.height; ++y) {
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            const float d = std::max(std::max(std::fabs(ca.r - cb.r), std::fabs(ca.g - cb.g)),
                                     std::fabs(ca.b - cb.b));
            if (d > worst) { worst = d; if (outX) *outX = x; if (outY) *outY = y; }
        }
    }
    return worst;
}

/// One wall/floor slab, the shape every scene here is built out of.
static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

// The ambient every scene here uses: a genuine hemisphere pair (upper != lower)
// in RADIANCE units, which is the non-degenerate case both the SH path and the
// VCT path are written for (the flat case takes Scene::setAmbient's 1/pi
// branch, which would compare two different conventions instead of two
// techniques).
static const Colour kAmbientUpper(0.40f, 0.40f, 0.44f);
static const Colour kAmbientLower(0.10f, 0.10f, 0.12f);

/// The VCT parameters every case starts from — gi.ddgi's, so the numbers in
/// this file and the ones in that one are about comparable pictures.
static GiParams vctBase()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;      // 64^3 voxels
    gi.numBounces = 2;
    gi.testBoundsMin = Vec3(-9.0f, -1.5f, -9.0f);
    gi.testBoundsMax = Vec3(9.0f, 7.5f, 9.0f);
    return gi;
}


// ---------------------------------------------------------------------------
// THE OPEN SCENE, shared by cases 1 and 2: a big floor with one wall standing
// on it, an ambient hemisphere pair, and NO LIGHTS AT ALL. With nothing but
// ambient in the scene, every number below is a measurement of the ambient term
// rather than a comparison of two whole lighting solutions (gi.ambient's
// premise), and with no light injected the field's own irradiance is zero — so
// whatever the fix adds is unambiguously the recovered ambient.
//
// The wall earns its place twice: it darkens its own corner in the cone-traced
// reference (the cones see it), which is the ONLY thing case 1's corner
// measurement can be compared against.
struct OpenScene {
    View  *view = nullptr;
    Scene *scene = nullptr;
};
static const unsigned kOpenX = 64, kOpenY = 118;      // floor, well clear of the wall
static const unsigned kCornerX = 64, kCornerY = 74;   // floor, at the wall's foot
static const Vec3 kCamPos(0.0f, 3.0f, 7.0f), kCamTarget(0.0f, 0.0f, -1.0f);

/// THE ANALYTIC CORNER (PHOTON-FIELD-ROTATE-1, F2): the irradiance an upward floor
/// point receives from the hemisphere ambient with the wall in the way, as a
/// fraction of the same point's with nothing in the way... computed for the two
/// pixels and returned as corner / open. Cosine-weighted directions over the upper
/// hemisphere, the ambient's radiance lerp( lower, upper, 0.5 + 0.5 y ) in
/// luminance, and a ray that meets the wall reads NOTHING: this engine injects no
/// ambient into the voxels (the environment is read only where a ray escapes), so a
/// bounce off the wall is not part of the physics being rendered here - the
/// "wall bounce" brackets (0.875-0.926) describe a picture this engine does not
/// make. `grow` inflates the wall by that many metres on every face (the voxel
/// envelope's one cell).
static double analyticCorner(float grow)
{
    const enginetest::AnalyticBox wall{ Vec3(-6.0f - grow, 0.0f - grow, -2.2f - grow),
                                        Vec3(6.0f + grow, 4.0f + grow, -1.8f + grow) };
    const auto irr = [&](const Vec3 &p) {
        const int N = 360;
        double sum = 0.0;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                const double u = (i + 0.5) / N, v = (j + 0.5) / N;
                const double r = std::sqrt(u), phi = 2.0 * M_PI * v;
                const Vec3 d(float(r * std::cos(phi)), float(std::sqrt(1.0 - u)), float(r * std::sin(phi)));
                if (enginetest::rayHitsBox(p, d, wall)) continue;
                const double t = 0.5 + 0.5 * d.y;
                sum += (1.0 - t) * lum(kAmbientLower) + t * lum(kAmbientUpper);
            }
        return sum / double(N * N);
    };
    const Vec3 corner = enginetest::groundPointForPixel(kCamPos, kCamTarget, kCornerX, kCornerY, 128);
    const Vec3 open = enginetest::groundPointForPixel(kCamPos, kCamTarget, kOpenX, kOpenY, 128);
    return irr(corner) / irr(open);
}

static OpenScene buildOpenScene(Engine *e, const char *name)
{
    OpenScene o;
    o.view = e->createOffscreenView(name, 128, 128, Colour(0, 0, 0));
    o.scene = e->createScene(name);
    o.view->setScene(o.scene);
    o.scene->setAmbient(kAmbientUpper, kAmbientLower);
    addSlab(o.scene, Colour(0.9f, 0.9f, 0.9f), Vec3(0.0f, -0.05f, 0.0f), Vec3(16.0f, 0.1f, 16.0f));
    addSlab(o.scene, Colour(0.9f, 0.9f, 0.9f), Vec3(0.0f, 2.0f, -2.0f), Vec3(12.0f, 4.0f, 0.4f));
    enginetest::testCameraLookAt(o.view, kCamPos, kCamTarget);
    return o;
}

int main(int argc, char **argv)
{
    // --cone-target: gi.cone_corner_target, the cone reference's corner against the
    // same analytic (a photon-target row: it prints and does not gate).
    const bool coneTarget = argc > 1 && std::string(argv[1]) == "--cone-target";
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ddgi-ambient-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // =====================================================================
    // CASE 1 — OPEN-SCENE RECOVERY, and CASE 4 — CORNER HONESTY, both on
    // GiQuality::Low.
    //
    // WHY LOW, and this is a finding in itself (case 2 pins it): Low is the
    // only quality whose voxel cone tracing is ISOTROPIC, and it is therefore
    // the only one whose cone-traced diffuse still carries a MEANINGFUL ambient
    // for this fix to be measured against. Above Low the engine builds
    // anisotropic voxels (OgreGi.cpp: `anisotropic = quality != Low`) and the
    // anisotropic march saturates its alpha almost immediately, so the escape
    // fraction that weights the ambient collapses to ~4% and there is no
    // reference left to compare with. Case 2 measures that separately and
    // fences it; the brief's "within 10% of the pre-DDGI VCT ambient reading"
    // is answered here, where the phrase means something.
    // =====================================================================
    std::printf("\n== case 1/4: open-scene recovery + corner honesty (isotropic VCT) ==\n");
    {
        OpenScene o = buildOpenScene(e, "ddgiamb_open");
        Scene *s = o.scene;
        Image img;

        // ---- what the ambient IS, with no GI at all: the PBS ambient term,
        //      unoccluded by anything (it has no occlusion model).
        GiParams off; off.mode = GiMode::Off;
        CHECK(s->setGlobalIllumination(off), "GI off");
        render(e, 6);
        o.view->readPixels(img);
        const Colour giOffOpen = img.at(kOpenX, kOpenY);
        const Colour giOffCorner = img.at(kCornerX, kCornerY);
        show("open   GI off (the raw ambient)", giOffOpen);
        show("corner GI off (the raw ambient)", giOffCorner);
        CHECK(lum(giOffOpen) > 0.05f, "the scene really is lit by its ambient (nothing else can)");

        // ---- THE REFERENCE: VCT with no field. This is where the ambient
        //      lives in a VCT scene — the cone-traced diffuse's own
        //      `ambient * escapeFraction` add.
        GiParams ref;
        ref.mode = GiMode::Vct;
        ref.quality = GiQuality::Low;        // isotropic: see the header above
        ref.numBounces = 2;
        ref.testBoundsMin = Vec3(-9.0f, -1.5f, -9.0f);
        ref.testBoundsMax = Vec3(9.0f, 7.5f, 9.0f);
        CHECK(s->setGlobalIllumination(ref), "VCT (isotropic) builds over the open scene");
        render(e, 6);
        o.view->readPixels(img);
        const Colour refOpen = img.at(kOpenX, kOpenY);
        const Colour refCorner = img.at(kCornerX, kCornerY);
        show("open   VCT (the reference)", refOpen);
        show("corner VCT (the reference)", refCorner);
        std::printf("   the cone reference keeps %.1f%% of the raw ambient on open floor "
                    "and %.1f%% in the corner\n",
                    100.0f * lum(refOpen) / lum(giOffOpen),
                    100.0f * lum(refCorner) / lum(giOffCorner));
        CHECK(lum(refCorner) < 0.95f * lum(refOpen),
              "the reference DARKENS the corner (its cones see the wall) — "
              "without that there would be nothing to measure the proxy against");

        // ---- THE FIELD. It carries the sky its probes' rays escape to.
        GiParams fix = ref;
        fix.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(fix), "DDGI binds over the open scene");
        render(e, 6);
        {
            // THE CONVERGED FIELD: the one settle predicate, in frames.
            int n = 0;
            for (; n < 4000 && !s->giStatus().giAtRest; ++n) render(e, 1);
            const GiStatus st = s->giStatus();
            CHECK(st.ifdBound && st.vctBound && st.giAtRest,
                  "the field is bound and GI at rest (every refinement paid)");
            std::printf("   (at rest after %d more frames)\n", n);
        }
        o.view->readPixels(img);
        const Colour fixOpen = img.at(kOpenX, kOpenY);
        const Colour fixCorner = img.at(kCornerX, kCornerY);
        show("open   DDGI", fixOpen);
        show("corner DDGI", fixCorner);
        // The field's sky carries the ambient's HUE, not a grey: the pair is
        // blue-tinted (0.44 blue against 0.40 red/green).
        CHECK(fixOpen.b > fixOpen.r * 1.02f,
              "the field's sky carries the AMBIENT's hue (blue-tinted, like the pair)");

        const float recovery = lum(fixOpen) / lum(refOpen);
        std::printf("   RECOVERY: open floor is %.1f%% of the cone-traced (VCT) reading "
                    "(and %.1f%% of the raw ambient)\n",
                    recovery * 100.0f, 100.0f * lum(fixOpen) / lum(giOffOpen));
        CHECK(recovery > 0.90f && recovery < 1.10f,
              "RECOVERY: the field's sky lands within 10% of the cones' — two integrals of one "
              "environment through one voxel reader");

        // ---- CASE 4: THE CORNER, AGAINST THE ANALYTIC (PHOTON-FIELD-ROTATE-1, F2).
        //      A floor patch at the foot of the wall has part of its sky bricked up;
        //      the corner's fraction of open floor is compared with the ANALYTIC
        //      fraction for this fixture - never with another estimator (the cone
        //      reference over-reads a corner and is its own target row,
        //      gi.cone_corner_target). THE BAR, derived: the wall as authored and the
        //      wall as the voxels hold it (grown by one cell on every face - the
        //      conservative raster's envelope) bracket the physics the field
        //      integrates, widened by twice the field's own converged standard error
        //      on an ordinary lit room (one sample's 2 % over sqrt(K) samples) and
        //      by the reader's half-code quantisation of the two pixels (0.5/255 over
        //      each reading).
        const float cell = s->giStatus().voxelMetres;
        const double anaAuthored = analyticCorner(0.0f);
        const double anaEnvelope = analyticCorner(cell);
        const double k = std::max(1u, s->giStatus().ifdTargetSamples);
        const double quant = 0.5 / 255.0 / lum(fixCorner) + 0.5 / 255.0 / lum(fixOpen);
        const double tol = 2.0 * 0.02 / std::sqrt(k) + quant;
        const double lo = std::min(anaAuthored, anaEnvelope) - tol, hi = std::max(anaAuthored, anaEnvelope) + tol;
        const float refCornerFrac = lum(refCorner) / lum(refOpen);
        const float fixCornerFrac = lum(fixCorner) / lum(fixOpen);
        std::printf("   CORNER: the analytic lights the wall foot at %.1f%% of open floor (the voxel "
                    "envelope, cell %.3f m: %.1f%%); the field %.1f%% (%+.1f points), the cone "
                    "reference %.1f%% (%+.1f points); bar %.1f-%.1f%%\n",
                    anaAuthored * 100.0, cell, anaEnvelope * 100.0, fixCornerFrac * 100.0f,
                    (fixCornerFrac - anaAuthored) * 100.0, refCornerFrac * 100.0f,
                    (refCornerFrac - anaAuthored) * 100.0, lo * 100.0, hi * 100.0);
        CHECK(fixCornerFrac < 0.95f,
              "the field DARKENS the wall foot against open floor");
        if (coneTarget) {
            CHECK(refCornerFrac >= lo && refCornerFrac <= hi,
                  "CONE CORNER (target): the cone reference's wall-foot darkening lands on the "
                  "analytic within the field's bar");
        } else {
            CHECK(fixCornerFrac >= lo && fixCornerFrac <= hi,
                  "CORNER: the field's wall-foot darkening lands on the ANALYTIC corner of this "
                  "fixture within the derived bar");
        }

        o.view->setScene(nullptr);
        e->destroyScene(s);
        e->destroyView(o.view);
    }

    if (coneTarget) {
        std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
        return failures ? 1 : 0;
    }

    // =====================================================================
    // CASE 2 — THE SHIPPED TIER, and the finding this lane owes the ledger:
    // AT EVERY QUALITY ABOVE LOW THE CONE-TRACED AMBIENT IS ALREADY GONE
    // BEFORE DDGI IS INVOLVED.
    //
    // `anisotropic = quality != GiQuality::Low` (OgreGi.cpp), and the
    // anisotropic branch of `voxelConeTraceDiff` (Vct_piece_ps.any) saturates
    // its alpha within a step or two on an open floor, so the escape fraction
    // that weights `light.xyz += ambient * light.w` collapses. Measured here on
    // the SAME scene as case 1: the isotropic reference keeps ~93% of the raw
    // ambient, the anisotropic one keeps ~4%.
    //
    // Consequence for the fix, and it is a GOOD one: on the tiers Photon
    // actually ships (medium and up), the ambient recovered here is not merely
    // a replacement for the term DDGI removed — it is the first correct ambient
    // those scenes have had. The assertion is therefore against the RAW
    // ambient, which is what the number should have been all along. (Patch
    // 0021, rayon2 S2, since restored 65% of it to the VCT-only tiers; the
    // fence below pins that.)
    // =====================================================================
    std::printf("\n== case 2: the shipped tier (anisotropic VCT) ==\n");
    {
        OpenScene o = buildOpenScene(e, "ddgiamb_aniso");
        Scene *s = o.scene;
        Image img;

        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        render(e, 6);
        o.view->readPixels(img);
        const Colour giOffOpen = img.at(kOpenX, kOpenY);
        show("open   GI off (the raw ambient)", giOffOpen);

        GiParams ref = vctBase();            // Medium == anisotropic
        CHECK(s->setGlobalIllumination(ref), "anisotropic VCT builds over the same scene");
        render(e, 6);
        o.view->readPixels(img);
        const Colour refOpen = img.at(kOpenX, kOpenY);
        show("open   anisotropic VCT", refOpen);
        const float kept = lum(refOpen) / lum(giOffOpen);
        std::printf("   FINDING: anisotropic VCT keeps %.1f%% of the raw ambient on open floor "
                    "(isotropic keeps ~93%%) — the ambient was already gone at this tier "
                    "BEFORE any irradiance field was bound\n", kept * 100.0f);
        // A FENCE, not a wish: this pins PATCH 0021's behaviour (rayon2 S2) so
        // that a pin bump, a dropped patch loop, or a future upstream fix tells
        // us instead of silently changing what "the ambient gap" means. The
        // unpatched march kept 4.3%; the patch's min3-one-mip-finer escape
        // keeps 65.2% (measured), deliberately short of the isotropic path's
        // 93% because the exact route floods sealed rooms (build record).
        // RE-ANCHORED 0.50-0.80 -> 0.70-0.90 (PHOTON-ENV-1), and the ESCAPE did
        // not move: what moved is the environment each cone reads. The cones
        // used to fill their escape with ONE value — the hemisphere pair's pole
        // at the normal (0.40 here) times the summed escape; each cone now reads
        // the environment in its own direction (jahEnvCone: the SH's radiance,
        // de-convolved), and the cone that escapes MOST — the zenith one — reads
        // the brightest sky (0.475 of this hemisphere; the 60-degree ones 0.36).
        // Measured 0.814 (the pole-value arithmetic gave 0.652). The fence still
        // rejects the unpatched march (4 %) and the exact route (~93 %).
        CHECK(kept > 0.70f && kept < 0.90f,
              "FENCE: with patch 0021 anisotropic voxel cone tracing keeps most of the "
              "ambient (65% measured; 4% unpatched; the exact 95% route is rejected)");

        GiParams fix = ref;
        fix.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(fix), "DDGI on the shipped tier");
        render(e, 6);
        o.view->readPixels(img);
        const Colour fixOpen = img.at(kOpenX, kOpenY);
        show("open   DDGI", fixOpen);
        const float vsRaw = lum(fixOpen) / lum(giOffOpen);
        std::printf("   the field lands at %.1f%% of the raw ambient (the cones alone %.1f%%)\n",
                    vsRaw * 100.0f, kept * 100.0f);
        // AGAINST THE FIXTURE'S OWN TRUTH, NOT THE RAW AMBIENT (PHOTON-READER-1): the
        // wall really hides part of this floor point's sky, so the correct answer is
        // its cosine-weighted sky visibility, computed here exactly. What the field
        // loses beyond that is voxel cone tracing's cone-vs-edge mechanism - a
        // widening footprint over coarse mips catching the wall's top edge a ray
        // passes over - plus the probe cage's below-floor layer; a 10 % allowance.
        const Vec3 openPoint = enginetest::groundPointForPixel(Vec3(0.0f, 3.0f, 7.0f),
                                                               Vec3(0.0f, 0.0f, -1.0f), kOpenX,
                                                               kOpenY, 128u);
        const float truth = enginetest::cosineSkyVisibilityUp(
            Vec3(openPoint.x, 1e-4f, openPoint.z),
            { { Vec3(-6.0f, 0.0f, -2.2f), Vec3(6.0f, 4.0f, -1.8f) } });
        std::printf("   ANALYTIC: the open floor point (%.2f, 0, %.2f) sees %.3f of the sky; the "
                    "field gives %.3f of the raw ambient = %.3f of the truth\n",
                    openPoint.x, openPoint.z, truth, vsRaw, truth > 0.0f ? vsRaw / truth : 0.0f);
        CHECK(vsRaw >= 0.90f * truth && vsRaw <= 1.05f,
              "on the shipped tier the field lands at >= 90% of the floor's ANALYTIC sky visibility");

        o.view->setScene(nullptr);
        e->destroyScene(s);
        e->destroyView(o.view);
    }

    // =====================================================================
    // CASE 3 — THE BOUNCE SURVIVES. gi.ddgi's lit room with ambient added: the
    // fix must add achromatic ambient WITHOUT eating the red bounce the field
    // is there to carry.
    // =====================================================================
    std::printf("\n== case 3: the bounce survives the sky ==\n");
    {
        View *view = e->createOffscreenView("ddgiamb_lit", 128, 128, Colour(0, 0, 0));
        Scene *s = e->createScene("ddgiamb_lit");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

        addSlab(s, Colour(1.0f, 1.0f, 1.0f), Vec3(0.0f, -0.05f, 0.0f), Vec3(14.0f, 0.1f, 14.0f));
        addSlab(s, Colour(1.0f, 0.05f, 0.05f), Vec3(0.0f, 3.0f, -3.0f), Vec3(12.0f, 6.0f, 0.9f));
        const NodeId light = s->createNode();
        const float half = 40.0f * 3.14159265f / 180.0f;      // 80 degrees about X
        const Quat atWall(std::sin(half), 0.0f, 0.0f, std::cos(half));
        s->setNodeTransform(light, Vec3(0, 6, 6), atWall, Vec3(1, 1, 1));
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1, 1, 1);
        l.intensity = 1.0f;
        l.castShadows = false;
        s->setLight(light, l);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 6.0f), Vec3(0.0f, 0.0f, -0.5f));
        const unsigned fx = 64, fy = 96;      // gi.ddgi's floor probe

        // THE SKY OFF (a black ambient: no environment for any reader), then ON.
        GiParams field = vctBase();
        field.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(field), "the lit room binds a field, no sky");
        render(e, 6);
        Image img; view->readPixels(img);
        const Colour litGap = img.at(fx, fy);
        show("lit floor  DDGI, no sky", litGap);

        s->setAmbient(kAmbientUpper, kAmbientLower);
        render(e, 30);          // the field re-integrates (its rays now see a sky)
        view->readPixels(img);
        const Colour litFix = img.at(fx, fy);
        show("lit floor  DDGI, sky", litFix);

        const float bounceGap = litGap.r - litGap.g;
        const float bounceFix = litFix.r - litFix.g;
        std::printf("   red bounce: %.4f without the sky, %.4f with it (%.1f%%)\n",
                    bounceGap, bounceFix, 100.0f * bounceFix / bounceGap);
        CHECK(bounceGap > 0.02f, "there is a red bounce to preserve");
        // The sky is achromatic here and enters the SAME atlas texel as the
        // bounce: it must add to it, never replace it. 35 % is the build
        // record's allowance (the sky also brightens the wall, whose red bounce
        // then rises a little — a second bounce of the sky, which is physics).
        CHECK(std::fabs(bounceFix - bounceGap) < 0.35f * bounceGap,
              "the sky preserves the red bounce (it adds light, it does not replace GI)");
        CHECK(lum(litFix) > lum(litGap), "and it does brighten the lit room's floor");

        view->setScene(nullptr);
        e->destroyScene(s);
        e->destroyView(view);
    }

    // =====================================================================
    // CASE 5 — SEALED-ROOM INVARIANCE. THE assertion that says the sky enters
    // the field by VISIBILITY and not as a blanket ambient: a closed room has no
    // sky, so turning the sky on must change NOTHING there. Stated over the WHOLE
    // FRAME, in 8-bit steps, because a per-pixel probe could sit on the one
    // surface that happens not to move — and guarded against the vacuous
    // version of itself (a black or a blown-out frame cannot move either).
    // =====================================================================
    std::printf("\n== case 5: sealed-room invariance ==\n");
    {
        View *view = e->createOffscreenView("ddgiamb_room", 128, 128, Colour(0, 0, 0));
        Scene *s = e->createScene("ddgiamb_room");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));      // the sky OFF first

        // gi.pcc_mirror's closed room: interior x,z in [-4,4], y in [0,5],
        // 0.4-thick shell, and no way in for sky.
        // Mid-grey rather than white, and a dim light: a white box around a
        // bright point light clips most of the frame, and a clipped pixel
        // cannot move — which would make the invariance assertion below
        // vacuous. The non-vacuity check a few lines down is what enforces it.
        const Colour white(0.45f, 0.45f, 0.45f);
        const Colour red(0.55f, 0.02f, 0.02f);
        addSlab(s, white, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));    // floor
        addSlab(s, white, Vec3(0.0f,  5.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));    // ceiling
        addSlab(s, white, Vec3(0.0f,  2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f));   // -Z wall
        addSlab(s, white, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));    // -X wall
        addSlab(s, white, Vec3( 4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));    // +X wall
        addSlab(s, red,   Vec3(0.0f,  2.5f, 4.2f), Vec3(8.8f, 5.0f, 0.4f));    // +Z wall
        // A point light INSIDE the room: a directional one injects nothing into
        // a sealed volume (OGRE_UPSTREAM_ISSUES, VCT light injection), which
        // would leave the field with nothing to carry.
        const NodeId light = s->createNode();
        s->setNodeTransform(light, Vec3(0.0f, 3.5f, 0.0f), Quat(), Vec3(1, 1, 1));
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1, 1, 1);
        l.intensity = 0.25f;
        l.range = 20.0f;
        l.castShadows = false;
        CHECK(s->setLight(light, l), "the room's point light arms");
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 3.4f), Vec3(0.0f, 1.6f, -1.0f));

        GiParams room = vctBase();
        room.testBoundsMin = Vec3(-5.0f, -1.0f, -5.0f);
        room.testBoundsMax = Vec3(5.0f, 6.0f, 5.0f);
        room.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(room), "the sealed room binds a field, no sky");
        render(e, 6);
        Image off1; view->readPixels(off1);

        // NOT VACUOUS: an invariance assertion over a black or a blown-out
        // frame proves nothing, so count the pixels that could actually have
        // moved before believing that none did.
        unsigned movable = 0;
        for (unsigned y = 0; y < off1.height; ++y)
            for (unsigned x = 0; x < off1.width; ++x) {
                const float v = lum(off1.at(x, y));
                if (v > 0.05f && v < 0.90f) ++movable;
            }
        const float movableFrac = float(movable) / float(off1.width * off1.height);
        std::printf("   %.1f%% of the frame is mid-tone (neither black nor clipped)\n",
                    movableFrac * 100.0f);
        CHECK(movableFrac > 0.20f,
              "the sealed room's frame has room to move (the invariance is not vacuous)");

        // THE CONTROL, before any A/B is believed: the same params pushed
        // again must reproduce the frame exactly. Without it, an invariance
        // failure could be rebuild noise rather than the fix leaking light.
        CHECK(s->setGlobalIllumination(room), "the same params rebuild");
        render(e, 6);
        Image off2; view->readPixels(off2);
        const float control = maxChannelDelta(off1, off2);
        std::printf("   control: an identical rebuild moves at most %.5f (%.2f/255)\n",
                    control, control * 255.0f);
        CHECK(control * 255.0f <= 1.0f, "CONTROL: an identical rebuild is stable to 1/255");

        s->setAmbient(kAmbientUpper, kAmbientLower);          // the sky ON
        render(e, 30);           // every probe re-integrated under the sky
        Image on1; view->readPixels(on1);
        unsigned wx = 0, wy = 0;
        const float delta = maxChannelDelta(off2, on1, &wx, &wy);
        std::printf("   INVARIANCE: worst pixel moves %.5f (%.2f/255) at (%u,%u)\n",
                    delta, delta * 255.0f, wx, wy);
        show("sealed room floor, no sky", off2.at(64, 104));
        show("sealed room floor, sky    ", on1.at(64, 104));
        // THE BAR IS ONE QUANTISATION STEP, NOT ZERO (PHOTON-READER-1, the lead's
        // verdict): the escape is what the voxel march measures, and the voxel
        // representation leaks a sliver of it through the anisotropic volumes'
        // coarse mips (a hit-based escape — cards or rays — is the true zero).
        CHECK(delta * 255.0f <= 1.0f + 1e-3f,
              "SEALED-ROOM INVARIANCE: no pixel moves more than 1/255 with the sky on");

        view->setScene(nullptr);
        e->destroyScene(s);
        e->destroyView(view);
    }

    engine.reset();
    std::printf(failures ? "\n%d FAILURES\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
