// gi.volume_edge — THE AMBIENT MUST NOT STEP AT THE EDGE OF THE LIT VOLUME
// (the owner's "black rectangle", ledger 177 defect A).
//
// WHAT THE OWNER SAW. A 48 m slab laid across the Grand Showroom 2, whose GI
// volume was pinned to +-12, showed a hard-edged rectangle on it: the volume's
// own footprint, printed on a continuous surface by nothing but the ambient
// term. It is not a probe, not the sky and not a stale solve — it is a UNIT
// MISMATCH, and it is present in every scene whose ambient is a FLAT colour and
// whose GI volume does not cover everything the camera can see.
//
// THE MECHANISM, in the two lines that disagreed (OgreScene::setAmbient).
// HlmsPbs' own two ambient paths are a factor of pi apart — AmbientHemisphere
// feeds envColourD, which the BRDF multiplies by pi against a kD that already
// carries 1/pi (Main/200.BRDFs_piece_ps.any:305), i.e. a mean RADIANCE, while
// AmbientFixed does `finalColour += ambient * kD` with no pi, i.e. pi times
// darker for the same numbers. This engine forces AmbientSh (the radiance
// convention), so to keep a FLAT ambient looking the way it always had,
// setAmbient scales it by 1/pi on its way into the SH coefficients. That part
// is right and it stays.
//
// What was wrong is that the same function then handed VctLighting the
// UNSCALED pair, on the argument that "VctLighting has no such split". It does
// not need one: the value it takes is used in exactly the same radiance
// convention the SH arm is now in (`light.xyz += ambient * light.w` in
// Vct_piece_ps.any, added to envColourD), so the two arms were carrying the
// same scene ambient a factor of pi apart. And the two arms meet AT THE EDGE OF
// THE VOLUME: with a field bound, a pixel inside the field is lit by the SH
// value (JahIfd_piece_ps.any's sky-visibility term, `irradianceSH(...)`) and a
// pixel outside it by the VCT pair (the same file's fallback, which is the term
// VctDisableDiffuse deleted). Same surface, same setting, pi apart — measured
// here at 3.4x before the fix, which is pi over the sky-visibility fraction.
//
// WHAT THIS SUITE ASSERTS, on a 96 m unlit slab crossing an automatically
// fitted 24 m volume, lit by NOTHING but a flat ambient (so every number is a
// measurement of the ambient term itself):
//   1. the step across the volume boundary is at most 5% — it was 340%;
//   2. the flat ambient renders the same brightness with GI on as with GI off,
//      which is the invariant that says WHICH of the two conventions is the
//      right one to keep (the whole tree's pixel suites, the selftest hash and
//      every authored scene are the GI-off one);
//   3. a HEMISPHERE ambient (upper != lower) is untouched by all of this —
//      it never took the 1/pi branch and its edge was already flat.
//
// Its own binary like every GI suite: the field and the voxel lighting bind
// process-wide to HlmsPbs.
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
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

/// gi.volume_edge_spec_target (PHOTON-ENV-1, audit F4): the SAME cases on a slab
/// WITH the default 4 % specular. The specular environment steps at the volume's
/// face: outside it the voxel specular cone escapes whole and reads the one
/// environment, inside it the cone's escape rides the raw directional composite,
/// which reads the slab's own voxels at a coarse mip and over-states occlusion
/// (jah_voxel_march.glsl's age-carry note). OWNED BY PHOTON-PCC-1 / REFLECT-1:
/// three reflection sources (rays, probes, the cone) make the cone's step
/// irrelevant at the ray tiers; that lane REMOVES `photon-target` from the row.
/// In the target row only the two STEP assertions gate (printed as target lines
/// in either row's arithmetic); the GI-on-against-GI-off brightness checks are
/// the diffuse subject's and are printed, not gated, there.
static bool gTarget = false;
#define STEP_CHECK(step, msg)                                                   \
    do {                                                                        \
        const double e_ = std::fabs(double(step) - 1.0);                        \
        if (gTarget) {                                                          \
            std::printf("target: %.4f (bar 0.0500) %s%s\n", e_, msg,             \
                        e_ < 0.05 ? " -- MET" : "");                            \
            if (e_ < 0.05) std::printf("ok: %s\n", msg);                        \
            else { std::printf("FAIL: %s\n", msg); ++failures; }                \
        } else CHECK(e_ < 0.05, msg);                                           \
    } while (0)
#define DIFFUSE_CHECK(cond, msg)                                                \
    do {                                                                        \
        if (gTarget) { std::printf("(printed in the target row) %s: %s\n", msg, \
                                   (cond) ? "holds" : "does not hold"); break; } \
        CHECK(cond, msg);                                                       \
    } while (0)

static void render(Engine *e, int frames = 6)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

// The view is 128x128 with an ORTHOGRAPHIC camera looking straight down, so a
// world X maps to a pixel column by arithmetic and no projection guesswork:
// x in [-kOrthoHalf, +kOrthoHalf] fills the width.
static const unsigned kSize = 128;
static const float    kOrthoHalf = 24.0f;      // half the vertical AND horizontal extent (square)
static const float    kVolumeMax = 24.0f;      // GiParams::autoBoundsMax -> the box is +-12

static unsigned columnForX(float x)
{
    const float t = (x / kOrthoHalf) * 0.5f + 0.5f;
    const int   px = int(t * float(kSize) + 0.5f);
    return unsigned(std::min(std::max(px, 0), int(kSize) - 1));
}

/// The mean luminance of a band of columns on the middle row — a band rather
/// than a pixel so a one-texel dither or an SSAO tap cannot decide a verdict.
static float bandLum(const Image &img, unsigned x0, unsigned x1)
{
    float sum = 0.0f; unsigned n = 0;
    for (unsigned y = kSize / 2 - 3; y <= kSize / 2 + 3; ++y)
        for (unsigned x = x0; x <= x1; ++x) { sum += lum(img.at(x, y)); ++n; }
    return n ? sum / float(n) : 0.0f;
}

struct Slab {
    View  *view = nullptr;
    Scene *scene = nullptr;
};

/// One 96 m slab, an ambient, and a camera straight above it. NO LIGHTS AT ALL.
static Slab buildSlab(Engine *e, const char *name, const Colour &upper, const Colour &lower)
{
    Slab o;
    o.view = e->createOffscreenView(name, kSize, kSize, Colour(0, 0, 0));
    o.scene = e->createScene(name);
    o.view->setScene(o.scene);
    o.scene->setAmbient(upper, lower);
    // MATTE (the ground's recipe, GF1: Specular workflow, ior 1.0, black kS —
    // F0 = 0), because the subject is the DIFFUSE ambient and nothing else.
    // With the default 4 % specular the slab also carried a specular
    // environment term, and that term is NOT continuous across the edge: outside
    // the voxel volume the specular cone escapes whole, inside it the cone's
    // directional alpha reads the slab's own voxels (the specular walk's escape
    // is the raw directional composite — jah_voxel_march.glsl, the age-carry
    // note), and GI off has no specular environment at all without a sky cube.
    // That step was ~3.7 % of this reading; once the environment lobe carries
    // the diffuse energy factor (PHOTON-ENV-1) the diffuse share is 0.69 of what
    // it was and the same step is 5.4 % — a finding about the specular escape,
    // measured here and moved out of this suite's subject.
    // (The target row, gi.volume_edge_spec_target, restores that 4 % specular.)
    NodeId slab = 0;
    {
        PbrParams p;
        p.albedo = Colour(0.8f, 0.8f, 0.8f);
        p.roughness = 0.9f;
        if (!gTarget) {
            p.workflow = PbrParams::Workflow::Specular;
            p.ior = 1.0f;
            p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        }
        slab = o.scene->createNode();
        o.scene->attachMesh(slab, o.scene->createMesh(enginetest::unitCubeMesh()),
                            o.scene->createPbrMaterial(p));
        enginetest::poseRegistry()[o.scene][slab] = enginetest::NodePose{};
    }
    enginetest::setNodePosition(o.scene, slab, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(o.scene, slab, Vec3(96.0f, 0.1f, 96.0f));
    // Straight down: a -90 degree pitch about X, which the lookAt helper cannot
    // express (its up vector is parallel to the forward one there).
    CameraDesc cam;
    cam.position = Vec3(0.0f, 40.0f, 0.0f);
    cam.orientation = Quat{ -0.70710678f, 0.0f, 0.0f, 0.70710678f };
    cam.orthographic = true;
    cam.orthoSize = kOrthoHalf;
    cam.farClip = 500.0f;
    o.view->setCamera(cam);
    return o;
}

static GiParams hybridDdgi()
{
    GiParams gi;
    gi.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::High;      // Epic in the UI: 128^3 voxels
    gi.numBounces = 1;
    gi.ddgi = GiToggle::On;
    gi.testAutoBoundsMax = kVolumeMax;      // the boundary this suite is about, fitted not pinned
    return gi;
}

/// inside / outside the volume edge, and the ratio between them.
static void measure(Engine *e, Slab &o, const char *what, float &inside, float &outside)
{
    Image img;
    render(e, 8);
    o.view->readPixels(img);
    const GiStatus st = o.scene->giStatus();
    // The edge the engine actually fitted, read back rather than assumed.
    const float edge = st.boundsMin.x;
    const unsigned edgeCol = columnForX(edge);
    // Two bands of equal width, one each side, starting 2 columns clear of the
    // edge itself (the boundary pixel is a blend of both by construction).
    outside = bandLum(img, edgeCol - 18u, edgeCol - 3u);
    inside  = bandLum(img, edgeCol + 3u, edgeCol + 18u);
    std::printf("   %-28s volume x %.2f .. %.2f (edge col %u)  outside %.4f  inside %.4f  "
                "step %.2fx  [ifd %d probes %d atRest %d]\n", what, st.boundsMin.x, st.boundsMax.x,
                edgeCol, outside, inside, outside > 1e-5f ? inside / outside : 0.0f,
                st.ifdBound ? 1 : 0, st.ifdProbes, st.giAtRest ? 1 : 0);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--target") gTarget = true;
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-volume-edge-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // =====================================================================
    // CASE 1 — A FLAT AMBIENT, hybrid + DDGI at Epic. The owner's case.
    // =====================================================================
    std::printf("\n== case 1: a flat ambient across the volume edge (hybrid + DDGI, Epic) ==\n");
    float flatGiOff = 0.0f;
    {
        Slab o = buildSlab(e, "edge_flat", Colour(0.376f, 0.376f, 0.376f),
                           Colour(0.376f, 0.376f, 0.376f));

        // The reference EVERY other reading is compared against: no GI at all,
        // which is how the whole tree's pixel suites and every authored scene
        // render a flat ambient.
        GiParams off; off.mode = GiMode::Off;
        CHECK(o.scene->setGlobalIllumination(off), "GI off");
        render(e, 6);
        Image img;
        o.view->readPixels(img);
        flatGiOff = bandLum(img, kSize / 2 - 8, kSize / 2 + 8);
        std::printf("   %-28s %.4f\n", "flat ambient, GI off", flatGiOff);
        CHECK(flatGiOff > 0.02f, "the slab really is lit by its ambient (nothing else can be)");

        CHECK(o.scene->setGlobalIllumination(hybridDdgi()), "hybrid + DDGI arms");
        const GiStatus st = o.scene->giStatus();
        CHECK(st.vctBound, "the voxel arm is bound");
        CHECK(st.boundsMax.x - st.boundsMin.x < 48.0f,
              "the fitted volume really is smaller than the slab (there IS an edge to cross)");

        float inside = 0.0f, outside = 0.0f;
        measure(e, o, "flat ambient, GI on", inside, outside);
        const float step = outside > 1e-5f ? inside / outside : 0.0f;

        // 1. THE STEP. 3.4x before the fix.
        STEP_CHECK(step, "the ambient does not step across the volume boundary (within 5%)");
        // 2. WHICH CONVENTION SURVIVED: the GI-off one, on both sides.
        //
        // RE-ANCHORED 5 % -> 10 % BY PHOTON-M2 (patch 0077), measured +7.9 %
        // (0.0941 GI off, 0.1015 GI on, i.e. +2/255 on a 24/255 band), and the
        // 2/255 is ATTRIBUTED: zeroing the specular cone's escape ambient in the
        // staged media brings this band back to 0.0941 EXACTLY, so the whole
        // difference is that one term. Patch 0077 removed the 0.31831 = 1/pi
        // upstream multiplied it by ("I'm not sure why is it even needed") --
        // an eye-tuned cancellation of the light injection's missing 1/pi, fixed
        // at the cause -- so the scene's one ambient now reaches the specular
        // slot in the same convention it reaches the diffuse one. The GI-OFF arm
        // has no specular ambient at all under this engine's AmbientSh (the
        // hemisphere piece is not compiled in), so the term has no counterpart
        // there and the two arms cannot agree to better than it: what this
        // assertion guards is the 3.4x convention error it was written for, and
        // 10 % still guards that with a factor of 30 to spare.
        DIFFUSE_CHECK(std::fabs(outside - flatGiOff) < 0.10f * flatGiOff,
                      "outside the volume, turning GI on does not change the flat ambient");
        DIFFUSE_CHECK(std::fabs(inside - flatGiOff) < 0.08f * flatGiOff,
                      "inside the volume, turning GI on does not change the flat ambient");

        // GI OFF before case 2 measures anything. (It used to be REQUIRED: the
        // VctLighting was bound process-wide, and a second scene with GI off
        // rendered through it with its ambient gated off. The binding is per
        // scene and per pass since PHOTON-SCENE-SWITCH-1; this is housekeeping.)
        CHECK(o.scene->setGlobalIllumination(off), "GI off again");
        render(e, 2);
        e->destroyView(o.view);
        e->destroyScene(o.scene);
    }

    // =====================================================================
    // CASE 2 — A HEMISPHERE ambient is untouched (it never took the 1/pi
    // branch). The control for case 1: if the fix had moved the radiance
    // convention instead of the flat one, this reading would have moved too.
    // =====================================================================
    std::printf("\n== case 2: a hemisphere ambient (the control) ==\n");
    {
        Slab o = buildSlab(e, "edge_hemi", Colour(0.40f, 0.40f, 0.44f), Colour(0.10f, 0.10f, 0.12f));
        GiParams off; off.mode = GiMode::Off;
        CHECK(o.scene->setGlobalIllumination(off), "GI off");
        render(e, 6);
        Image img;
        o.view->readPixels(img);
        const float giOff = bandLum(img, kSize / 2 - 8, kSize / 2 + 8);
        std::printf("   %-28s %.4f\n", "hemisphere ambient, GI off", giOff);
        CHECK(giOff > 0.02f, "the reference reading is a real one (nothing else holds the binding)");

        CHECK(o.scene->setGlobalIllumination(hybridDdgi()), "hybrid + DDGI arms");
        float inside = 0.0f, outside = 0.0f;
        measure(e, o, "hemisphere ambient, GI on", inside, outside);
        const float step = outside > 1e-5f ? inside / outside : 0.0f;
        STEP_CHECK(step, "a hemisphere ambient does not step across the boundary either");
        DIFFUSE_CHECK(std::fabs(outside - giOff) < 0.05f * giOff,
                      "and it renders at the same brightness with GI on as with GI off");
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
