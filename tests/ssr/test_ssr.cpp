// Screen-space reflections, pixel-asserted (POST_CHAIN_SPEC.md §4.1 row "SSR"
// and §8 phase 6).
//
// THE SCENE, and why it is shaped exactly like this — deliberately the same
// fixture the planar-reflection suite uses (tests/planar/test_planar.cpp), so
// the two reflection techniques are measured with one ruler:
//
//   * A glossy floor plate (a unit cube scaled to 12 x 0.2 x 12) with its top
//     face at y = 0. Metal, roughness ~0: what it shows IS a reflection.
//   * A bright red EMISSIVE cube floating above it. Emissive so its colour does
//     not depend on lighting, shadows or ambient: any red on the floor came
//     from the reflection and from nothing else.
//   * A camera low and in front, so the cube sits in the upper half of the
//     frame and its reflection lands in the LOWER half. Every measurement scans
//     the lower half only.
//
// WHAT EACH ASSERTION PROVES
//
//   1. SSR off -> on: red appears on the floor. The whole chain — prepass,
//      march, resolve, and HlmsPbs' own `hlms_use_ssr` lerp into the specular
//      environment term — working end to end.
//   2. Move the emitter out of the world: the red goes. This is what makes (1)
//      a statement about a REFLECTION rather than about some constant the flag
//      switches on.
//   3. THE ONE THING A PROBE CANNOT DO. Move the cube sideways and render TWO
//      frames; the reflection moves with it, by the same sign and a comparable
//      distance. No capture, no re-bake, no cadence — the hit coordinates come
//      from this frame's depth buffer. A parallax-corrected probe would need a
//      six-face re-render to notice; a planar reflector would need its own
//      extra scene pass. This assertion is the reason the feature exists.
//   4. THE ROUGHNESS CUTOFF IS HONOURED. Raise the floor's roughness above
//      PostFxDesc::rayReflectRoughness — the World panel's "Roughness Cutoff"
//      row, the ONE number both the march and the traced ray gate on since lane
//      SSR-3 — and the reflection disappears, because a v1 with no
//      roughness-varying blur must not draw a sharp mirror image on a matte
//      surface. Section 13 is the sharp version of the same statement: it pins
//      what the cutoff is MEASURED AGAINST to within 0.29..0.31 on a floor
//      authored at perceptual 0.30.
//   5. SSR OFF IS BYTE-IDENTICAL TO TODAY. The frame captured before SSR was
//      ever enabled and the frame captured after it is switched off again must
//      match exactly, pixel for pixel. That is the offscreen-determinism law
//      (POST_CHAIN_SPEC §7.3) restated for this feature: every thumbnail,
//      preview and pixel suite in the tree renders through a view whose
//      PostFxDesc has ssr == 0.
//   6. The quality row is a SHAPE change (half-res rays -> full-res rays
//      rebuilds the workspace) and the tuning is NOT (max distance, thickness,
//      cutoff and intensity are uniforms and must never rebuild anything) —
//      dragging the Roughness Cutoff row must not rebuild a workspace, which
//      section 13 drags through five values to say again.
//   7. Full-resolution rays still produce the reflection.
//   8. SHADOWS SURVIVE THE PREPASS, on their own fixture. This is the thing
//      `use_prepass` changes that has nothing to do with reflections: the main
//      pass stops sampling the shadow maps and reads the directional shadow
//      term out of the G-buffer instead. If that attachment never arrives, the
//      whole world silently loses its shadows while SSR is on, with no error
//      anywhere. The fixture is a MATTE floor SSR cannot touch, so the only
//      thing that can move a pixel is the shadow.
//   9. THE PREPASS RESTRUCTURE IS SHADING-NEUTRAL. With the roughness cutoff at
//      zero the reflection is empty everywhere, but the whole prepass shape is
//      still in the graph — and the frame comes back within 1/255 of the
//      SSR-off frame on 129 of 65536 silhouette pixels, which is the normals
//      G-buffer's R10G10B10A2 quantization and nothing else.
//  10. SSR AND REFRACTIVE GLASS COMPOSE. POST_CHAIN_SPEC §13 item 7 called this
//      "the one combination phases 6 and 7 must prove jointly". As built the
//      question does not arise — refractives render in their own later pass and
//      only the opaque pass takes setUseDepthPrePass — but a claim about a
//      graph is not a frame, so both switches go on and the reflection has to
//      still be there.
//
//  11. NO FIREFLIES (lane-whitedots, 2026-09-09). On a mirror floor under a
//      line of bright silhouettes — the shape that produced the Grand
//      Showroom's crawling white dots — no pixel of the floor is an ISOLATED
//      SPIKE: no pixel is 2.5x brighter than the BRIGHTEST of its eight
//      neighbours. That is a statement about SPARKS rather than about
//      contrast — a reflection edge, and the one-pixel-wide streak a thin
//      object's reflection legitimately is, both have something comparable
//      next to them and pass.
//
//  12. IT IS CORRECT UNDER AN ORTHOGRAPHIC CAMERA - the owner report
//      ("when I am in top view and move the camera the light reflections
//      change - it should not"). A pan of an ortho camera cannot change any
//      view-dependent shading, because every pixel of an ortho frame looks the
//      same way; the frame must therefore be the SAME PICTURE, TRANSLATED. The
//      section measures the effect's own footprint (on minus off at one pose,
//      so everything that is not the effect cancels) at two poses 8 px apart
//      and asserts it translated by exactly that - measured with the fix in:
//      centroid 113.05 -> 105.05 for an 8 px pan, mass 2655.8 -> 2656.0, and a
//      shifted mismatch of 0.000. Without it, SSR under an ortho camera is not
//      merely wrong on this fixture, it is DEAD: footprint 0.0, four of the
//      five assertions red.
//
//      SSAO gets the same treatment because it reconstructs a position from
//      depth the same way and had the same defect (ogre-patch 0019) - PLUS a
//      magnitude band, because the pan alone does not catch it. Measured
//      against the pre-patch code: it translates perfectly well (centroid
//      8.04 px, mismatch 0.005) while being seven times too strong, so the
//      only assertion that separates the two is how big the occlusion is.
//
//      AND THE HIGHLIGHTS (riders lane, ogre-patch 0024). SSR and SSAO were the
//      screen-space half of the owner report; the other half was HlmsPbs
//      itself, whose viewDir = normalize( -inPs.pos ) assumes a pinhole and so
//      slides every view-dependent term (NdotV, Fresnel, the half vector, the
//      probe lookup) with the fragment's screen position under an ortho pan.
//      A glossy sphere's specular highlight is the visible symptom: measured
//      pre-patch on this fixture the whole shaded sphere fails to translate
//      (shifted mismatch of the sphere window well above the noise floor)
//      while with the patch it is a pure translation. The section asserts the
//      highlight centroid moves by exactly the pan and the sphere window is
//      the same picture translated.
//
// TWO ENV-GATED EXTRAS, off in the gate and on when a human needs evidence:
//   JAH_SSR_DUMP=1   writes ssr-{off,half,hq}.ppm, ssr-firefly-{half,hq}.ppm, the
//                    shadow fixture's two frames and the four orthographic footprints
//                    beside the binary. A number in a log is not pixel
//                    evidence; these are.
//   JAH_SSR_BENCH=1  times 400 steady-state frames of an offscreen view (1080p
//                    by default; JAH_SSR_BENCH_W/H override it) with SSR off /
//                    half-res / full-res, over the fixture plus sixty extra
//                    cubes so the prepass has something to traverse, and prints
//                    the per-frame milliseconds. Never asserted — it is an
//                    absolute measurement and has no business failing a build
//                    on somebody else's GPU.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static bool envOn(const char *name)
{
    const char *v = std::getenv(name);
    return v && *v && *v != '0';
}

/// A plain binary PPM — no dependencies, and every image viewer reads it.
static void writePpm(const Image &img, const char *path)
{
    FILE *f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const unsigned char rgb[3] = {
                (unsigned char)(c.r < 0 ? 0 : (c.r > 1 ? 255 : c.r * 255.0f + 0.5f)),
                (unsigned char)(c.g < 0 ? 0 : (c.g > 1 ? 255 : c.g * 255.0f + 0.5f)),
                (unsigned char)(c.b < 0 ? 0 : (c.b > 1 ? 255 : c.b * 255.0f + 0.5f)) };
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    std::printf("    wrote %s\n", path);
}

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf(cond ? "ok: " : "FAIL: ");                                  \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

/// A unit UV sphere (radius 0.5, like unitCubeMesh's extents): the specular
/// highlight fixture of section 12's PBS half needs a curved surface.
static MeshData unitSphereMesh(unsigned rings = 24, unsigned segments = 48)
{
    MeshData d;
    const float kPi = 3.14159265358979f;
    for (unsigned r = 0; r <= rings; ++r) {
        const float v = float(r) / float(rings);
        const float phi = v * kPi;
        for (unsigned s = 0; s <= segments; ++s) {
            const float u = float(s) / float(segments);
            const float theta = u * 2.0f * kPi;
            const float nx = std::sin(phi) * std::cos(theta);
            const float ny = std::cos(phi);
            const float nz = std::sin(phi) * std::sin(theta);
            d.positions.insert(d.positions.end(), { 0.5f * nx, 0.5f * ny, 0.5f * nz });
            d.normals.insert(d.normals.end(), { nx, ny, nz });
            d.uvs.insert(d.uvs.end(), { u, v });
        }
    }
    const unsigned stride = segments + 1u;
    for (unsigned r = 0; r < rings; ++r)
        for (unsigned s = 0; s < segments; ++s) {
            const unsigned a = r * stride + s, b = a + stride;
            d.indices.insert(d.indices.end(), { a, b, a + 1u, a + 1u, b, b + 1u });
        }
    return d;
}

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// The strongest "this pixel is red and its neighbours are not" signal in the
/// bottom half of the frame: max over pixels of (r - max(g, b)). A neutral
/// floor scores ~0 whatever its brightness, so the measure is immune to
/// exposure, ambient and the floor's own albedo.
static float maxRedExcessLowerHalf(const Image &img, int *outX = nullptr, int *outY = nullptr)
{
    float best = 0.0f;
    for (unsigned y = img.height / 2; y < img.height; ++y) {
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const float e = c.r - (c.g > c.b ? c.g : c.b);
            if (e > best) { best = e; if (outX) *outX = int(x); if (outY) *outY = int(y); }
        }
    }
    return best;
}

/// Where the red is, horizontally: the red-excess-weighted centroid of the
/// lower half. Assertion 3 watches this move, not the peak, because a centroid
/// survives the half-resolution ray buffer's blockiness.
static float redCentroidX(const Image &img, float *outMass = nullptr)
{
    double sum = 0.0, weighted = 0.0;
    for (unsigned y = img.height / 2; y < img.height; ++y) {
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            float e = c.r - (c.g > c.b ? c.g : c.b);
            if (e < 0.05f) continue;                 // ignore the neutral floor
            sum += e;
            weighted += double(x) * e;
        }
    }
    if (outMass) *outMass = float(sum);
    return sum > 0.0 ? float(weighted / sum) : -1.0f;
}

static bool identical(const Image &a, const Image &b, unsigned *outDiff = nullptr)
{
    if (a.width != b.width || a.height != b.height) return false;
    unsigned diff = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            if (p.r != q.r || p.g != q.g || p.b != q.b) ++diff;
        }
    if (outDiff) *outDiff = diff;
    return diff == 0;
}

static float measure(Engine *e, View *v, const char *what, int frames = 3, Image *out = nullptr)
{
    render(e, frames);
    Image img;
    if (!v->readPixels(img)) { std::printf("FAIL: readPixels (%s)\n", what); ++failures; return 0.0f; }
    int x = -1, y = -1;
    const float r = maxRedExcessLowerHalf(img, &x, &y);
    std::printf("   %s: max red excess (lower half) = %.3f at (%d,%d)\n", what, r, x, y);
    if (out) *out = img;
    return r;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-ssr-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("ssr", 256, 256, Colour(0, 0, 0));
    Scene *s = engine->createScene("ssr");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    // A little ambient so the floor is not pitch black without a reflection;
    // neutral, so it contributes nothing to the red-excess measure. It is also
    // what gives the shader an `envColourS` for SSR to LERP INTO — without any
    // environment source at all the composite is an add instead, which is a
    // different (and weaker) statement to be making.
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

    // The glossy floor. Its material id is kept: assertion 4 raises its
    // roughness through setPbrMaterial.
    const NodeId floor = s->createNode();
    PbrParams floorParams;
    floorParams.albedo = Colour(1.0f, 1.0f, 1.0f);
    floorParams.metalness = 1.0f;
    floorParams.roughness = 0.0f;              // clamped to 1e-4 by the backend
    const MaterialId floorMat = s->createPbrMaterial(floorParams);
    const MeshId floorMesh = s->createMesh(enginetest::unitCubeMesh());
    CHECK(floor && floorMat && floorMesh && s->attachMesh(floor, floorMesh, floorMat),
          "the glossy floor plate exists");
    enginetest::setNodeScale(s, floor, Vec3(12.0f, 0.2f, 12.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));

    // The emitter: bright red, emissive, floating above the floor.
    const NodeId emitter = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = Colour(3.0f, 0.0f, 0.0f);
        p.roughness = 0.5f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        CHECK(emitter && mat && mesh && s->attachMesh(emitter, mesh, mat),
              "the emissive cube exists");
    }
    enginetest::setNodeScale(s, emitter, Vec3(1.5f, 1.5f, 1.5f));
    enginetest::setNodePosition(s, emitter, Vec3(0.0f, 2.2f, 0.0f));

    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);

    // Low and in front: the cube is up-frame, its reflection is down-frame.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));

    // ---- 5a. the reference frame, with no post chain at all ----------------
    Image plain;
    const float redOff = measure(engine.get(), view, "ssr off", 3, &plain);
    CHECK(redOff < 0.06f, "no red on the floor before SSR exists");
    if (envOn("JAH_SSR_DUMP")) writePpm(plain, "ssr-off.ppm");
    const unsigned genPlain = view->workspaceGeneration();

    // ---- 1. off -> on ------------------------------------------------------
    PostFxDesc fx;
    fx.allowOffscreen = true;      // the ONE door through the offscreen guarantee
    fx.ssr = 1;                    // half-resolution rays
    view->setPostFx(fx);
    CHECK(view->workspaceGeneration() > genPlain, "enabling SSR rebuilds the chain");
    // Four frames: the resolve reads the PREVIOUS frame's colour, so the first
    // frame after a rebuild reflects the seeded black history.
    Image ssrOnImg;
    const float redOn = measure(engine.get(), view, "ssr on (half-res rays)", 4, &ssrOnImg);
    if (!engine->lastError().empty())
        std::printf("    lastError after enabling SSR: %s\n", engine->lastError().c_str());
    CHECK(redOn > redOff + 0.10f,
          "the emissive cube is REFLECTED in the glossy floor (red appears)");
    if (envOn("JAH_SSR_DUMP")) writePpm(ssrOnImg, "ssr-half.ppm");

    // ---- 2. move the emitter away: the reflection must go -------------------
    enginetest::setNodePosition(s, emitter, Vec3(0.0f, 2.2f, -400.0f));
    const float redAway = measure(engine.get(), view, "emitter moved away", 4);
    CHECK(redAway < redOn - 0.10f,
          "moving the emitter removes the reflection (it really is a reflection)");
    enginetest::setNodePosition(s, emitter, Vec3(0.0f, 2.2f, 0.0f));
    const float redBack = measure(engine.get(), view, "emitter back", 4);
    CHECK(redBack > redAway + 0.10f, "bringing it back restores the reflection");

    // ---- 3. THE REALTIME PROPERTY: the reflection moves with the object -----
    // Two frames only. The hit coordinates are recomputed from THIS frame's
    // depth and normals, so the reflection has already moved; the colour it
    // samples is one frame old, which is why two and not one.
    {
        Image before;
        render(engine.get(), 3);
        if (!view->readPixels(before)) { std::printf("FAIL: readPixels (move/before)\n"); ++failures; }
        float massBefore = 0.0f;
        const float xBefore = redCentroidX(before, &massBefore);

        const float dx = 1.6f;
        enginetest::setNodePosition(s, emitter, Vec3(dx, 2.2f, 0.0f));
        render(engine.get(), 2);                       // TWO frames, no settling
        Image after;
        if (!view->readPixels(after)) { std::printf("FAIL: readPixels (move/after)\n"); ++failures; }
        float massAfter = 0.0f;
        const float xAfter = redCentroidX(after, &massAfter);
        std::printf("    reflection centroid x: %.1f (mass %.1f) -> %.1f (mass %.1f) "
                    "after moving the cube +%.1f in x, 2 frames\n",
                    xBefore, massBefore, xAfter, massAfter, dx);
        CHECK(xBefore > 0.0f && xAfter > 0.0f, "the reflection is measurable before and after the move");
        CHECK(xAfter > xBefore + 6.0f,
              "the reflection MOVED with the object, within two frames and with no re-capture");
        enginetest::setNodePosition(s, emitter, Vec3(0.0f, 2.2f, 0.0f));
        render(engine.get(), 3);
    }

    // ---- 4. the roughness cutoff -------------------------------------------
    {
        PbrParams rough = floorParams;
        rough.roughness = 0.9f;                        // far above the 0.40 cutoff
        CHECK(s->setPbrMaterial(floorMat, rough), "the floor accepts a rough material");
        const float redRough = measure(engine.get(), view, "rough floor, ssr on", 4);
        CHECK(redRough < 0.06f, "a ROUGH floor shows no screen-space reflection (cutoff honoured)");
        CHECK(s->setPbrMaterial(floorMat, floorParams), "the floor goes back to glossy");
        const float redGlossy = measure(engine.get(), view, "glossy floor again", 4);
        CHECK(redGlossy > redRough + 0.10f, "and the reflection comes back with it");
    }

    // ---- 6. shape vs tuning -------------------------------------------------
    {
        const unsigned gen = view->workspaceGeneration();
        PostFxDesc tuned = fx;
        tuned.ssrMaxDistance = 40.0f;
        tuned.ssrThickness = 0.8f;
        tuned.ssrIntensity = 0.9f;
        tuned.rayReflectRoughness = 0.55f;   // the World row, mid-drag
        view->setPostFx(tuned);
        CHECK(view->workspaceGeneration() == gen,
              "SSR distance/thickness/intensity/roughness-cutoff are UNIFORMS, "
              "not graph changes");
        render(engine.get(), 3);

        PostFxDesc hq = fx;
        hq.ssr = 2;                                    // full-resolution rays
        view->setPostFx(hq);
        CHECK(view->workspaceGeneration() > gen, "the quality row IS a graph change");
        // ---- 7. full-resolution rays still reflect --------------------------
        Image hqImg;
        const float redHq = measure(engine.get(), view, "ssr on (full-res rays)", 4, &hqImg);
        CHECK(redHq > redOff + 0.10f, "full-resolution rays reflect the cube too");
        if (envOn("JAH_SSR_DUMP")) writePpm(hqImg, "ssr-hq.ppm");
    }

    // ---- the cost, at 1080p, on whatever GPU is running this ---------------
    //
    // HOW THIS IS TIMED, because a naive loop measures nothing: renderOneFrame
    // returns as soon as the command buffer is submitted, so a block of N of
    // them only reports GPU time once the GPU falls far enough behind for the
    // in-flight limit to block the CPU. The block therefore ends with a
    // readPixels, which drains the pipeline, and N is large enough that the one
    // readback is noise. Absolute numbers are still this machine's; the DELTAS
    // are the answer.
    if (envOn("JAH_SSR_BENCH")) {
        // 1080p unless asked otherwise: JAH_SSR_BENCH_W/H exist because the
        // interesting question — "is the ray march or the extra traversal the
        // cost?" — is answered by watching the delta against RESOLUTION, and a
        // rebuild to ask it would be silly.
        const unsigned bw = std::getenv("JAH_SSR_BENCH_W")
                                ? unsigned(std::atoi(std::getenv("JAH_SSR_BENCH_W"))) : 1920u;
        const unsigned bh = std::getenv("JAH_SSR_BENCH_H")
                                ? unsigned(std::atoi(std::getenv("JAH_SSR_BENCH_H"))) : 1080u;
        View *big = engine->createOffscreenView("ssr-bench", bw, bh, Colour(0, 0, 0));
        if (big) {
            big->setScene(s);
            enginetest::testCameraLookAt(big, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));
            view->setEnabled(false);       // measure ONE view's frame, not two
            // The fixture is two objects, which would make the PREPASS look
            // free — it is a second full traversal, and a traversal of nothing
            // costs nothing. Sixty more cubes, spread across the floor and all
            // in frame, so the prepass has geometry to re-submit and the number
            // below is a cost somebody could recognise.
            // JAH_SSR_BENCH_N: the prepass is a SECOND DRAW SUBMISSION of the
            // whole scene, and this renderer is CPU-bound long before it is
            // pixel-bound, so "how many objects" is the other axis worth
            // sweeping. 60 by default.
            const int fillerCount = std::getenv("JAH_SSR_BENCH_N")
                                        ? std::atoi(std::getenv("JAH_SSR_BENCH_N")) : 60;
            std::vector<NodeId> filler;
            for (int i = 0; i < fillerCount; ++i) {
                const NodeId c = enginetest::addTestCube(s, Colour(0.6f, 0.6f, 0.65f),
                                                         0.0f, 0.4f);
                if (!c) break;
                const float a = float(i) * 0.9f;
                enginetest::setNodePosition(s, c, Vec3(std::cos(a) * (1.5f + 0.06f * i),
                                                       0.4f,
                                                       -0.5f + std::sin(a) * (1.5f + 0.06f * i)));
                enginetest::setNodeScale(s, c, Vec3(0.6f, 0.8f, 0.6f));
                filler.push_back(c);
            }
            std::printf("    BENCH scene: %zu filler cubes + the floor and emitter\n",
                        filler.size());
            // Three rows. The baseline is the PASSTHROUGH shape, because that is
            // what a user turning the row on is actually paying the difference
            // against — and because the chain has no "SSR off" shape of its own:
            // with every other switch off, ssr == 0 IS the passthrough graph.
            // Part of the delta is therefore the chain's own offscreen target
            // and final composite quad, shared with every other effect; the rest
            // is the prepass, the march and the resolve.
            struct Case { const char *name; int ssr; };
            const Case cases[] = { { "passthrough  ", 0 },
                                   { "ssr half-res ", 1 },
                                   { "ssr full-res ", 2 } };
            double baseline = 0.0;
            for (const Case &c : cases) {
                PostFxDesc d;
                if (c.ssr > 0) { d.allowOffscreen = true; d.ssr = c.ssr; }
                big->setPostFx(d);
                Image drain;
                for (int i = 0; i < 60; ++i) engine->renderOneFrame();     // settle
                big->readPixels(drain);
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < 400; ++i) engine->renderOneFrame();
                big->readPixels(drain);                                    // drain
                const auto t1 = std::chrono::steady_clock::now();
                const double ms =
                    std::chrono::duration<double, std::milli>(t1 - t0).count() / 400.0;
                if (c.ssr == 0) baseline = ms;
                std::printf("    BENCH %ux%u  %s  %6.3f ms/frame   (delta %+.3f)\n",
                            bw, bh, c.name, ms, ms - baseline);
            }
            big->setPostFx(PostFxDesc());
            engine->destroyView(big);
            view->setEnabled(true);
        }
    }

    // ---- 10. SSR AND REFRACTION COMPOSE ------------------------------------
    //
    // POST_CHAIN_SPEC §13 item 7 flagged "can one scene pass be both
    // use_prepass and use_refractions?" as the combination phases 6 and 7 had
    // to prove jointly. As BUILT the question does not arise — refractive items
    // render in their own later pass (RQ 200) and only the opaque pass takes
    // setUseDepthPrePass, so no pass in the graph is ever both — but "does not
    // arise" is a claim about a graph, and this is the claim about a frame:
    // both switches on, the chain builds, nothing throws, and the reflection is
    // still there.
    {
        const NodeId pane = enginetest::addTestCube(s, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.05f);
        {
            PbrParams gp;
            gp.albedo = Colour(0.9f, 0.9f, 0.9f);
            gp.roughness = 0.05f;
            gp.alphaMode = PbrAlphaMode::Refractive;
            gp.refractionStrength = 0.3f;
            const MaterialId gm = s->createPbrMaterial(gp);
            const MeshId gmesh = s->createMesh(enginetest::unitCubeMesh());
            CHECK(pane && gm && gmesh && s->attachMesh(pane, gmesh, gm),
                  "a refractive pane exists");
        }
        enginetest::setNodeScale(s, pane, Vec3(3.0f, 2.0f, 0.1f));
        enginetest::setNodePosition(s, pane, Vec3(-3.5f, 1.2f, 3.0f));

        PostFxDesc both = fx;
        both.ssr = 1;
        both.refractions = true;
        view->setPostFx(both);
        const float redBoth = measure(engine.get(), view, "ssr + refraction", 5);
        CHECK(engine->lastError().empty() || engine->lastError().find("refract") == std::string::npos,
              "no engine error with SSR and refraction in the same chain");
        CHECK(redBoth > redOff + 0.10f,
              "SSR and refractive glass compose: the reflection survives the extra pass");
        s->removeNode(pane);
        view->setPostFx(fx);
        render(engine.get(), 3);
    }

    // ---- 9. THE PREPASS RESTRUCTURE IS SHADING-NEUTRAL ---------------------
    //
    // The strongest statement this suite can make, and the one that answers
    // "what ELSE did use_prepass change?": with the roughness cutoff at zero
    // every ray is rejected before it is cast, so the reflection buffer is zero
    // everywhere and HlmsPbs' lerp is a no-op — but the whole prepass shape is
    // still in the graph, the main pass still shades from the G-buffer, and its
    // depth is still the prepass'. If the frame is not the frame SSR-off
    // produces, something about `use_prepass` moved the picture: normals,
    // shadows, roughness or depth. It is not; the pixels match exactly.
    {
        PostFxDesc neutral;
        neutral.allowOffscreen = true;
        neutral.ssr = 1;
        neutral.rayReflectRoughness = 0.0f;  // reject every surface
        view->setPostFx(neutral);
        render(engine.get(), 4);
        Image flat;
        CHECK(view->readPixels(flat), "readPixels with SSR structurally on and zero confidence");
        unsigned diff = 0, big = 0;
        float worst = 0.0f;
        identical(plain, flat, &diff);
        for (unsigned y = 0; y < plain.height; ++y)
            for (unsigned x = 0; x < plain.width; ++x) {
                const Colour a = plain.at(x, y), b = flat.at(x, y);
                const float d = std::max(std::max(std::fabs(a.r - b.r), std::fabs(a.g - b.g)),
                                         std::fabs(a.b - b.b));
                if (d > worst) worst = d;
                if (d > 4.0f / 255.0f) ++big;
            }
        std::printf("    pixels differing from the SSR-off frame: %u of %u "
                    "(%u by more than 4/255; worst channel delta %.1f/255)\n",
                    diff, plain.width * plain.height, big, worst * 255.0f);
        // MEASURED, not guessed: 129 of 65536 pixels move, every one of them by
        // exactly 1/255, and every one of them on a silhouette. That is the
        // R10G10B10A2 normals G-buffer quantizing a normal the forward path
        // interpolates at full precision — inherent to shading from a G-buffer,
        // and the reason this assertion has a tolerance instead of demanding
        // byte equality (which the SSR-OFF assertion, further down, does).
        CHECK_MSG(worst <= 4.0f / 255.0f && diff * 200u <= plain.width * plain.height,
                  "the SSR PREPASS alone is shading-neutral to within the G-buffer's precision: "
                  "%u of %u pixels moved, worst channel delta %.1f/255",
                  diff, plain.width * plain.height, worst * 255.0f);
    }

    // ---- 8. SHADOWS SURVIVE THE PREPASS ------------------------------------
    //
    // The one thing `use_prepass` changes that has nothing to do with
    // reflections: the main pass stops sampling the shadow maps and reads the
    // directional shadow term out of the G-buffer instead
    // (800.PixelShader_piece_ps.any:876 — `midf fShadow = shadowRoughness.x`).
    // If the prepass' second attachment does not arrive, every surface reads
    // "fully lit" and the whole world loses its shadows while SSR is on. That
    // is a silent, total regression with no error anywhere, so it gets its own
    // scene and its own assertion.
    //
    // The scene is deliberately one SSR CANNOT TOUCH: a matte floor, roughness
    // 0.8, far above the cutoff, so the reflection is zero everywhere and the
    // ONLY thing that can move a pixel is the shadow term.
    {
        Scene *sh = engine->createScene("ssr-shadow");
        View *shv = engine->createOffscreenView("ssr-shadow", 192, 192, Colour(0, 0, 0));
        if (sh && shv) {
            shv->setScene(sh);
            shv->setShadows(true);
            sh->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));
            const NodeId floor2 = enginetest::addTestCube(sh, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.8f);
            enginetest::setNodeScale(sh, floor2, Vec3(14.0f, 0.2f, 14.0f));
            enginetest::setNodePosition(sh, floor2, Vec3(0.0f, -0.1f, 0.0f));
            const NodeId caster = enginetest::addTestCube(sh, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.8f);
            enginetest::setNodeScale(sh, caster, Vec3(2.0f, 2.0f, 2.0f));
            enginetest::setNodePosition(sh, caster, Vec3(0.0f, 3.0f, 0.0f));
            enginetest::addDirectionalLight(sh, Vec3(0.0f, -1.0f, -0.15f), 4.0f);
            // Looking down at the floor from in front: the cube is high in the
            // frame, its shadow is a dark patch on the floor below it.
            enginetest::testCameraLookAt(shv, Vec3(0.0f, 5.0f, 9.0f), Vec3(0.0f, 0.0f, -0.5f));

            // The shadow's contrast, measured as a ratio so it cannot be faked
            // by the whole frame getting brighter: the darkest floor pixel in
            // the lower third over the brightest one.
            auto contrast = [&](const char *what) {
                render(engine.get(), 4);
                Image img;
                if (!shv->readPixels(img)) { CHECK_MSG(false, "readPixels (%s)", what); return 1.0f; }
                if (envOn("JAH_SSR_DUMP")) {
                    std::string p = std::string("ssr-shadow-") + what + ".ppm";
                    for (char &c : p) if (c == ' ' || c == ',') c = '_';
                    writePpm(img, p.c_str());
                }
                float lo = 1e9f, hi = 0.0f;
                for (unsigned y = img.height / 2u; y < img.height; ++y)
                    for (unsigned x = img.width / 6u; x < img.width * 5u / 6u; ++x) {
                        const Colour c = img.at(x, y);
                        const float l = (c.r + c.g + c.b) / 3.0f;
                        if (l < lo) lo = l;
                        if (l > hi) hi = l;
                    }
                const float r = hi > 0.0f ? lo / hi : 1.0f;
                std::printf("   %s: floor darkest/brightest = %.3f (lo %.3f hi %.3f)\n",
                            what, r, lo, hi);
                return r;
            };

            const float plainContrast = contrast("shadows, ssr off");
            CHECK_MSG(plainContrast < 0.7f,
                      "the fixture really casts a shadow (ratio %.3f)", plainContrast);

            PostFxDesc shfx;
            shfx.allowOffscreen = true;
            shfx.ssr = 1;
            shv->setPostFx(shfx);
            const float ssrContrast = contrast("shadows, ssr on");
            CHECK_MSG(ssrContrast < plainContrast * 1.25f + 0.02f,
                      "the shadow SURVIVES the SSR prepass: %.3f -> %.3f",
                      plainContrast, ssrContrast);

            shv->setPostFx(PostFxDesc());
            render(engine.get(), 2);
            engine->destroyView(shv);
            engine->destroyScene(sh);
        } else {
            CHECK_MSG(false, "could not build the shadow fixture");
        }
    }

    // ---- 11. THE FIREFLY GATE (lane-whitedots) ------------------------------
    //
    // "Random bright white dots on the floor that move with the camera" — the
    // Grand Showroom on Ultra, which is the only tier that turns FULL-RESOLUTION
    // rays on. The cause was in the resolve: its 3x3 filter averaged the hit
    // COORDINATES of the neighbouring taps, and two taps on opposite sides of a
    // silhouette hit two unrelated places, so their mean pointed at a third
    // place neither ray ever touched. Land that on something bright in the
    // colour history and the pixel comes back as a lone spark; move the camera
    // and it lands somewhere else, which is why the dots crawl.
    //
    // THE FIXTURE IS SILHOUETTES. A mirror floor and a picket line of narrow,
    // bright emissive slabs at three different depths, viewed from close to
    // floor level so the reflected rays travel far across the screen and cross
    // as many depth discontinuities as possible. This is the shape that
    // produced the dots; a single cube over a floor (the fixture above) barely
    // does.
    //
    // WHAT THIS GATE PROVES, EXACTLY — measured against the pre-fix shaders,
    // because a gate whose bite is assumed is worth nothing:
    //   * It is a REGRESSION GUARD, not a reproduction. With the old resolve
    //     staged, this fixture at 256x256 does NOT produce isolated sparks:
    //     what it produces is a bright bleed into the lower end of each dark
    //     slab reflection and one extra row of the reflection boundary's
    //     documented dither (617 of 65536 pixels differ at full-res rays, 552
    //     at half-res, up to 168/255 apiece — the fix's effect is real and
    //     large, it simply is not shaped like a dot at this size). The dots
    //     the report is about are that same mechanism at 1080p over textured
    //     geometry; the pixel evidence for THEM is a before/after of the
    //     Grand Showroom, not this fixture.
    //   * What it does guard is the failure this shader is one edit away from
    //     at any time: a resolve that hands back a coordinate no tap reported
    //     lands somewhere arbitrary, and somewhere arbitrary in a scene with
    //     any bright object in it is a spark. If that ever comes back with a
    //     spark's shape, on a floor whose reflections cross nine silhouettes,
    //     this assertion is what says so.
    {
        Scene *ff = engine->createScene("ssr-firefly");
        View *ffv = engine->createOffscreenView("ssr-firefly", 256, 256, Colour(0, 0, 0));
        if (ff && ffv) {
            ffv->setScene(ff);
            ff->setAmbient(Colour(0.10f, 0.10f, 0.12f), Colour(0.08f, 0.08f, 0.10f));

            // The mirror.
            {
                const NodeId plate = ff->createNode();
                PbrParams p;
                p.albedo = Colour(1.0f, 1.0f, 1.0f);
                p.metalness = 1.0f;
                p.roughness = 0.0f;
                const MaterialId m = ff->createPbrMaterial(p);
                const MeshId mesh = ff->createMesh(enginetest::unitCubeMesh());
                CHECK(plate && m && mesh && ff->attachMesh(plate, mesh, m),
                      "firefly fixture: the mirror floor exists");
                enginetest::setNodeScale(ff, plate, Vec3(24.0f, 0.2f, 24.0f));
                enginetest::setNodePosition(ff, plate, Vec3(0.0f, -0.1f, 0.0f));
            }

            // THE BRIGHT WALL BEHIND EVERYTHING. The contrast has to be this
            // way round and it is the whole reason the first version of this
            // fixture caught nothing: the defect INVENTS a coordinate between
            // two neighbouring hits, so it only shows when the invented place
            // is BRIGHTER than the two real ones. Bright objects on a black
            // background invent a coordinate in the black and produce nothing;
            // dark objects in front of a bright wall invent one ON THE WALL.
            {
                const NodeId wall = ff->createNode();
                PbrParams p;
                p.albedo = Colour(0.02f, 0.02f, 0.02f);
                p.emissive = Colour(6.0f, 6.0f, 6.0f);
                p.roughness = 0.5f;
                const MaterialId m = ff->createPbrMaterial(p);
                const MeshId mesh = ff->createMesh(enginetest::unitCubeMesh());
                CHECK(wall && m && mesh && ff->attachMesh(wall, mesh, m),
                      "firefly fixture: the bright backdrop exists");
                enginetest::setNodeScale(ff, wall, Vec3(26.0f, 7.0f, 0.2f));
                enginetest::setNodePosition(ff, wall, Vec3(0.0f, 3.0f, -9.0f));
            }

            // The picket line: narrow, DARK, and at three depths, so a ray
            // that just catches one slab and its neighbour that just catches
            // the slab behind it hit two places with a slice of bright wall
            // between them. That is the exact geometry the resolve used to
            // average across.
            for (int i = 0; i < 9; ++i) {
                const NodeId slab = ff->createNode();
                PbrParams p;
                p.albedo = Colour(0.015f, 0.015f, 0.015f);
                p.roughness = 0.55f;
                const MaterialId m = ff->createPbrMaterial(p);
                const MeshId mesh = ff->createMesh(enginetest::unitCubeMesh());
                if (!(slab && m && mesh && ff->attachMesh(slab, mesh, m))) break;
                enginetest::setNodeScale(ff, slab, Vec3(0.30f, 1.6f, 0.30f));
                // ENTIRELY ABOVE EYE LEVEL (the camera sits at y = 0.45), so
                // every direct slab pixel is above the horizon and the probe
                // region below it contains floor and reflections ONLY. With
                // the slabs standing ON the floor their thin, one-pixel-wide
                // bases fall inside the probe and read as sparks that have
                // nothing to do with SSR — measured, and the reason for this
                // geometry.
                enginetest::setNodePosition(ff, slab,
                                            Vec3(-3.6f + float(i) * 0.9f, 1.65f,
                                                 -1.2f + float(i % 3) * 1.5f));
            }
            enginetest::addDirectionalLight(ff, Vec3(-0.3f, -1.0f, -0.4f), 2.0f);
            // Close to floor level: long, grazing reflected rays.
            enginetest::testCameraLookAt(ffv, Vec3(0.0f, 0.45f, 6.0f), Vec3(0.0f, 0.35f, -2.0f));

            // A pixel is a FIREFLY when its luminance exceeds the median of its
            // eight neighbours by `ratio`, in a region bright enough for the
            // ratio to mean anything.
            // WHAT COUNTS AS A FIREFLY. Strict isolation: brighter than the
            // BRIGHTEST of its eight neighbours by `ratio`. Everything softer
            // than that was tried and is a false positive on this fixture —
            // "brighter than the median" flags every pixel of the one-pixel
            // -wide streak a thin object's reflection legitimately is, and
            // "dark on both sides in all four directions" still flags the
            // corner of a steep reflection edge (measured: a 201-156-51-6-0
            // ramp). A spark has nothing comparable next to it in any
            // direction and this is the only test that says exactly that; the
            // price is that a two-pixel spark is not seen, which is the right
            // side to err on for a gate that must never cry wolf.
            auto fireflies = [](const Image &img, float ratio, float &outWorst,
                                int *outX, int *outY) {
                const unsigned y0 = img.height * 55u / 100u;
                int count = 0;
                outWorst = 0.0f;
                auto lum = [&](unsigned x, unsigned y) {
                    const Colour c = img.at(x, y);
                    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                };
                for (unsigned y = y0 + 1; y + 1 < img.height; ++y)
                    for (unsigned x = 1; x + 1 < img.width; ++x) {
                        const float l = lum(x, y);
                        // Below this the frame is near-black and a "ratio" is
                        // quantization noise, not a spark.
                        if (l < 0.12f) continue;
                        float ref = 0.0f;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                                if (dx || dy) ref = std::max(ref, lum(x + dx, y + dy));
                        if (l > ref * ratio + 0.04f) {
                            ++count;
                            const float r = ref > 1e-4f ? l / ref : 999.0f;
                            if (r > outWorst) { outWorst = r; if (outX) *outX = int(x); if (outY) *outY = int(y); }
                        }
                    }
                return count;
            };

            auto probe = [&](int ssrMode, const char *what) {
                PostFxDesc d;
                d.allowOffscreen = true;
                d.ssr = ssrMode;
                d.ssrMaxDistance = 30.0f;
                ffv->setPostFx(d);
                render(engine.get(), 6);
                Image img;
                if (!ffv->readPixels(img)) { CHECK_MSG(false, "readPixels (%s)", what); return -1; }
                if (envOn("JAH_SSR_DUMP")) {
                    std::string p = std::string("ssr-firefly-") + what + ".ppm";
                    for (char &c : p) if (c == ' ') c = '_';
                    writePpm(img, p.c_str());
                }
                float worst = 0.0f;
                int fx = -1, fy = -1;
                const int n = fireflies(img, 2.5f, worst, &fx, &fy);
                std::printf("   %s: %d firefly pixel(s) in the floor probe, worst ratio %.2f"
                            " at (%d,%d)\n", what, n, worst, fx, fy);
                return n;
            };

            // The half-resolution row first: it was always clean (its nine taps
            // come from four times fewer, far more coherent rays), and it is
            // the row every other pixel suite in the tree exercises — so a
            // regression here would be the fix having moved something it must
            // not.
            const int halfCount = probe(1, "half");
            CHECK_MSG(halfCount == 0,
                      "half-resolution rays leave no fireflies on the mirror floor (%d)",
                      halfCount);

            // And the row the report came from.
            const int hqCount = probe(2, "hq");
            CHECK_MSG(hqCount == 0,
                      "FULL-resolution rays leave no fireflies on the mirror floor (%d) "
                      "- the resolve never averages hit coordinates across a silhouette",
                      hqCount);

            // The gate would be vacuous if the fixture showed no reflection at
            // all, so: with SSR on, the floor is measurably brighter than with
            // it off. (Emissive slabs, mirror floor: the only source of light
            // down there IS the reflection.)
            auto floorEnergy = [&](int ssrMode) {
                PostFxDesc d;
                if (ssrMode > 0) { d.allowOffscreen = true; d.ssr = ssrMode; d.ssrMaxDistance = 30.0f; }
                ffv->setPostFx(d);
                render(engine.get(), 6);
                Image img;
                if (!ffv->readPixels(img)) return -1.0;
                double sum = 0.0;
                unsigned n = 0;
                for (unsigned y = img.height * 55u / 100u; y < img.height; ++y)
                    for (unsigned x = 0; x < img.width; ++x) {
                        const Colour c = img.at(x, y);
                        sum += (c.r + c.g + c.b) / 3.0;
                        ++n;
                    }
                return n ? sum / n : -1.0;
            };
            const double eOff = floorEnergy(0);
            const double eHq = floorEnergy(2);
            std::printf("    floor mean luminance: ssr off %.4f -> full-res %.4f\n", eOff, eHq);
            CHECK_MSG(eHq > eOff * 1.15,
                      "the firefly fixture really reflects (mean floor luminance %.4f -> %.4f)",
                      eOff, eHq);

            ffv->setPostFx(PostFxDesc());
            render(engine.get(), 2);
            engine->destroyView(ffv);
            engine->destroyScene(ff);
        } else {
            CHECK_MSG(false, "could not build the firefly fixture");
        }
    }

    // ---- 5b. SSR off is BYTE-IDENTICAL to before it was ever on -------------
    {
        view->setPostFx(PostFxDesc());
        render(engine.get(), 3);
        Image restored;
        CHECK(view->readPixels(restored), "readPixels after switching SSR off");
        unsigned diff = 0;
        const bool same = identical(plain, restored, &diff);
        std::printf("    pixels differing from the pre-SSR frame: %u of %u\n",
                    diff, plain.width * plain.height);
        CHECK(same, "switching SSR off restores BYTE-IDENTICAL pixels");
    }

    // ---- 11. THE ORTHOGRAPHIC PAN (orthoview lane) -------------------------
    // THE OWNER REPORT THIS SECTION EXISTS FOR: "when I am in top view and move
    // the camera the light reflections change - it should not".
    //
    // Under an ORTHOGRAPHIC camera every pixel looks the same way, so a pure
    // PAN (a translation with no rotation) cannot change any view-dependent
    // shading: the same surface, seen along the same direction, must reflect
    // the same thing. The picture as a whole obviously moves - that is what a
    // pan is - so the assertion is not "the frame is identical" but "the frame
    // is the SAME PICTURE, translated".
    //
    // WHY THE MEASURE IS A DIFFERENCE OF TWO FRAMES. The effect is measured as
    // (SSR on) minus (SSR off) at the SAME pose. Everything that is not the
    // reflection - direct light, ambient, the emitter's own colour, and the
    // HlmsPbs `viewDir = normalize(-inPs.pos)` that is perspective-only in this
    // pin and is a separate, owner-facing decision - is bit-identical between
    // the two and cancels. What is left is the SSR term and nothing else, which
    // is exactly the thing the ortho branch in JahSsrRayMarch_ps.glsl fixes.
    //
    // THE PAN IS AN EXACT PIXEL SHIFT, by construction: a 256x256 view with
    // orthoSize 4 shows 8 world units across, i.e. 32 px per unit, so moving
    // the camera 0.25 units along its right axis moves the image exactly 8 px
    // left. Both effects' screen-space noise survives that shift unchanged (the
    // march's checkerboard jitter keys on pixel parity and 8 is even; SSAO's
    // rotation tile is 2x2 and wraps every 2 px), so the comparison is a plain
    // translation with no resampling anywhere.
    //
    // BEFORE THE FIX this section fails on its first assertion: the ray origin
    // is reconstructed as `cameraDir * distance`, and an ortho frustum's
    // normalized far corner is not a ray (OgreFrustum.cpp:884 takes ratio = 1),
    // so the origin collapses toward the view axis and the "reflection" is a
    // smear that MOVES with the camera instead of with the scene.
    {
        Scene *os = engine->createScene("ssr-ortho");
        View  *ov = engine->createOffscreenView("ssr-ortho", 256, 256, Colour(0, 0, 0));
        if (!os || !ov) {
            CHECK_MSG(false, "could not build the orthographic fixture");
        } else {
        ov->setScene(os);
        os->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

        // The same fixture as the perspective one: a mirror floor and a red
        // emissive cube above it. Measured with one ruler, deliberately.
        {
            PbrParams p;
            p.albedo = Colour(1.0f, 1.0f, 1.0f);
            p.metalness = 1.0f;
            p.roughness = 0.0f;
            const NodeId n = os->createNode();
            os->attachMesh(n, os->createMesh(enginetest::unitCubeMesh()),
                           os->createPbrMaterial(p));
            enginetest::setNodeScale(os, n, Vec3(12.0f, 0.2f, 12.0f));
            enginetest::setNodePosition(os, n, Vec3(0.0f, -0.1f, 0.0f));
        }
        {
            PbrParams p;
            p.albedo = Colour(0.05f, 0.05f, 0.05f);
            p.emissive = Colour(3.0f, 0.0f, 0.0f);
            p.roughness = 0.5f;
            const NodeId n = os->createNode();
            os->attachMesh(n, os->createMesh(enginetest::unitCubeMesh()),
                           os->createPbrMaterial(p));
            enginetest::setNodeScale(os, n, Vec3(1.5f, 1.5f, 1.5f));
            enginetest::setNodePosition(os, n, Vec3(0.0f, 2.2f, 0.0f));
        }
        // A matte box RESTING on the floor: the AO half of this section needs a
        // contact corner, and a rough box cannot itself be reflected sharply.
        {
            PbrParams p;
            p.albedo = Colour(0.6f, 0.6f, 0.6f);
            p.roughness = 0.9f;
            const NodeId n = os->createNode();
            os->attachMesh(n, os->createMesh(enginetest::unitCubeMesh()),
                           os->createPbrMaterial(p));
            enginetest::setNodeScale(os, n, Vec3(1.6f, 1.6f, 1.6f));
            enginetest::setNodePosition(os, n, Vec3(-2.6f, 0.8f, 1.0f));
        }
        enginetest::addDirectionalLight(os, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);

        // 32 px per world unit; the pan is 0.25 units = exactly 8 px.
        const float kOrthoSize = 4.0f;
        const float kPanWorld  = 0.25f;
        const int   kPanPixels = 8;

        // A FAR CLIP THAT IS TRIED AND REJECTED, recorded so nobody spends the
        // afternoon again: rendering the same pose at two far planes looks like
        // an exact test of the reconstruction (the correct one is far-plane
        // independent), and it is not - the perspective-only form's error under
        // an ortho camera lands almost entirely in xy, which the ortho
        // projection then divides straight back out, so BOTH reconstructions
        // are far-clip invariant to within 0.5%. Measured, both ways round.
        const float kFarA = 1000.0f;      // the CameraDesc default
        auto poseAt = [&](float dx, float farClip) {
            CameraDesc c = enginetest::testCameraDescLookAt(Vec3(dx, 5.0f, 7.0f),
                                                            Vec3(dx, 0.0f, 0.0f));
            c.orthographic = true;
            c.orthoSize    = kOrthoSize;
            c.farClip      = farClip;
            return c;
        };

        // The effect's own footprint, in FLOAT and not in an 8-bit Image: these
        // are differences of two frames, and quantising a difference to 1/255
        // throws away most of what is being measured.
        const unsigned kW = 256u, kH = 256u;
        auto capture = [&](const CameraDesc &c, const PostFxDesc &fxOn,
                           std::vector<float> &out) {
            ov->setCamera(c);
            // The resolve pass reads the PREVIOUS frame's colour, so the history
            // has to settle at the new pose before the frame is read.
            ov->setPostFx(PostFxDesc());
            render(engine.get(), 4);
            Image off;
            if (!ov->readPixels(off)) return false;
            ov->setPostFx(fxOn);
            render(engine.get(), 4);
            Image on;
            if (!ov->readPixels(on)) return false;
            if (on.width != kW || on.height != kH) return false;
            out.assign(size_t(kW) * kH, 0.0f);
            for (unsigned y = 0; y < kH; ++y)
                for (unsigned x = 0; x < kW; ++x) {
                    const Colour a = on.at(x, y), b = off.at(x, y);
                    out[size_t(y) * kW + x] =
                        std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b);
                }
            return true;
        };

        // Total footprint and its horizontal centroid over a WINDOW, ignoring
        // the noise floor (one 8-bit level on one channel is 0.004; 0.02 is
        // five of them).
        //
        // THE WINDOW IS NOT DECORATION. A pan moves content OUT of one edge of
        // the frame and brings new content IN at the other, so a measure taken
        // over the whole frame is reading the clipping as much as the
        // translation — measured, on the first run of this section: SSR's
        // footprint covers the entire glossy floor and its full-frame centroid
        // moved 5.89 px for an 8 px pan. The two poses are compared over the
        // region they actually share, and only there (kWinLo/kWinHi below).
        auto footprint = [&](const std::vector<float> &d, int x0, int x1,
                             float *outMass) {
            double sum = 0.0, weighted = 0.0;
            for (unsigned y = 0; y < kH; ++y)
                for (int x = x0; x < x1; ++x) {
                    const float v = d[size_t(y) * kW + unsigned(x)];
                    if (v < 0.02f) continue;
                    sum += v;
                    weighted += double(x) * v;
                }
            if (outMass) *outMass = float(sum);
            return sum > 0.0 ? float(weighted / sum) : -1.0f;
        };
        // The shared region, in each frame's own coordinates: the camera moved
        // +x, so the picture moved LEFT, and what frame 1 shows at x is what
        // frame 0 showed at x + kPanPixels.
        //
        // THE 40 px MARGIN IS SSR's OWN SCREEN EDGE FADE, and it is the one
        // part of the frame that is ENTITLED not to translate. The marcher
        // fades a hit out over `smoothstep( 0.0, 0.12, ... )` of the frame —
        // 30.7 px at this width — because the information genuinely stops at
        // the border, and that ramp is anchored to the SCREEN rather than to
        // the scene (JahSsrRayMarch_ps.glsl says so where it is written).
        // Measured, on this fixture: inside a 16 px margin the comparison reads
        // 6.70 px of an 8 px pan and a 0.019 mismatch; outside the ramp, at 40,
        // it reads 8.00 and 0.0004 with the mass identical to the last digit.
        // The margin is therefore excluding a known screen-space term, not
        // hiding a residue.
        const int kWinLo = 40;
        const int kWinHi = int(kW) - 40 - kPanPixels;

        // Sum |D1(x) - D0(x + shift)| over the interior as a FRACTION of the
        // effect's own magnitude there: 0 is a perfect translation, 1 is "the
        // two frames have nothing to do with each other".
        auto shiftedMismatch = [&](const std::vector<float> &d0,
                                   const std::vector<float> &d1, int shift,
                                   int x0, int x1) {
            double err = 0.0, mag = 0.0;
            for (unsigned y = 0; y < kH; ++y)
                for (int x = x0; x < x1; ++x) {
                    const float a = d1[size_t(y) * kW + unsigned(x)];
                    const float b = d0[size_t(y) * kW + unsigned(x + shift)];
                    err += std::fabs(a - b);
                    mag += (a > b ? a : b);
                }
            return mag > 0.0 ? float(err / mag) : 0.0f;
        };

        // Only for the human-facing dump: the footprint as a viewable image.
        auto deltaPpm = [&](const std::vector<float> &d, const char *path) {
            Image img;
            img.width = kW; img.height = kH;
            img.rgba.assign(size_t(kW) * kH * 4u, 255u);
            for (size_t i = 0; i < size_t(kW) * kH; ++i) {
                const float v = d[i] / 3.0f;
                const unsigned char g =
                    (unsigned char)(v <= 0.0f ? 0 : (v >= 1.0f ? 255 : v * 255.0f + 0.5f));
                img.rgba[i * 4u] = img.rgba[i * 4u + 1] = img.rgba[i * 4u + 2] = g;
            }
            writePpm(img, path);
        };

        // ---- SSR ----------------------------------------------------------
        {
            PostFxDesc ofx;
            ofx.allowOffscreen = true;
            ofx.ssr = 1;
            std::vector<float> d0, d1;
            const bool got = capture(poseAt(0.0f, kFarA), ofx, d0) &&
                             capture(poseAt(kPanWorld, kFarA), ofx, d1);
            CHECK(got, "ortho SSR: both poses rendered and read back");
            if (got) {
                float m0 = 0.0f, m1 = 0.0f;
                const float c0 = footprint(d0, kWinLo + kPanPixels, kWinHi + kPanPixels, &m0);
                const float c1 = footprint(d1, kWinLo, kWinHi, &m1);
                const float mism = shiftedMismatch(d0, d1, kPanPixels, kWinLo, kWinHi);
                std::printf("   ortho SSR: mass %.1f -> %.1f, centroid %.2f -> %.2f "
                            "(expected %.2f), shifted mismatch %.3f\n",
                            m0, m1, c0, c1, c0 - float(kPanPixels), mism);
                if (envOn("JAH_SSR_DUMP")) {
                    deltaPpm(d0, "ssr-ortho-delta-0.ppm");
                    deltaPpm(d1, "ssr-ortho-delta-1.ppm");
                }
                CHECK_MSG(m0 > 5.0f,
                          "SSR produces a reflection under an ORTHOGRAPHIC camera "
                          "at all (footprint %.1f)", m0);
                CHECK_MSG(c0 > 0.0f && std::fabs(c1 - (c0 - float(kPanPixels))) < 0.5f,
                          "the reflection PANS WITH THE SCENE: centroid %.2f -> %.2f, "
                          "expected %.2f", c0, c1, c0 - float(kPanPixels));
                CHECK_MSG(m0 > 0.0f && std::fabs(m1 - m0) / m0 < 0.05f,
                          "the reflection keeps its strength across the pan "
                          "(%.1f -> %.1f)", m0, m1);
                CHECK_MSG(mism < 0.02f,
                          "the panned frame IS the first one translated: shifted "
                          "mismatch %.3f", mism);
            }
        }

        // ---- SSAO ---------------------------------------------------------
        // The same statement for the other effect that reconstructs a position
        // from depth (ogre-patch 0019). Its buffer is kept at full resolution so
        // the 8 px pan is 8 px there too.
        {
            PostFxDesc ofx;
            ofx.allowOffscreen = true;
            ofx.ssao = true;
            ofx.ssaoScale = 1.0f;
            std::vector<float> d0, d1;
            const bool got = capture(poseAt(0.0f, kFarA), ofx, d0) &&
                             capture(poseAt(kPanWorld, kFarA), ofx, d1);
            CHECK(got, "ortho SSAO: both poses rendered and read back");
            if (got) {
                float m0 = 0.0f, m1 = 0.0f;
                const float c0 = footprint(d0, kWinLo + kPanPixels, kWinHi + kPanPixels, &m0);
                const float c1 = footprint(d1, kWinLo, kWinHi, &m1);
                const float mism = shiftedMismatch(d0, d1, kPanPixels, kWinLo, kWinHi);
                std::printf("   ortho SSAO: mass %.1f -> %.1f, centroid %.2f -> %.2f "
                            "(expected %.2f), shifted mismatch %.3f\n",
                            m0, m1, c0, c1, c0 - float(kPanPixels), mism);
                if (envOn("JAH_SSR_DUMP")) {
                    deltaPpm(d0, "ssao-ortho-delta-0.ppm");
                    deltaPpm(d1, "ssao-ortho-delta-1.ppm");
                }
                CHECK_MSG(m0 > 5.0f,
                          "SSAO occludes something under an ORTHOGRAPHIC camera "
                          "at all (footprint %.1f)", m0);
                CHECK_MSG(c0 > 0.0f && std::fabs(c1 - (c0 - float(kPanPixels))) < 0.5f,
                          "the occlusion PANS WITH THE SCENE: centroid %.2f -> %.2f, "
                          "expected %.2f", c0, c1, c0 - float(kPanPixels));
                CHECK_MSG(mism < 0.02f,
                          "the panned AO frame IS the first one translated: shifted "
                          "mismatch %.3f", mism);

                // AND THE ASSERTION THAT ACTUALLY CATCHES THE SSAO DEFECT,
                // which the pan does NOT. Measured against the perspective-only
                // reconstruction (the code before ogre-patch 0019, run on this
                // exact fixture): it translates correctly - centroid 8.04 px
                // for an 8 px pan, mismatch 0.005 - because it is wrong by a
                // factor that is itself a function of the surface, so it MOVES
                // with the scene and reads as a plausible occlusion. What it
                // cannot fake is the SIZE of it: pre-0019 the shader
                // reconstructs a view-space z of -1/t where metres are wanted,
                // the kernel then swamps the depth comparison, and the
                // occlusion comes out roughly seven times too strong and twice
                // as wide (window mass 1702, 10.4% of the frame covered,
                // against 243 and 5.6% once the branch is right).
                //
                // A magnitude band is a blunter instrument than the rest of
                // this section and it is deliberate: the fixture is offscreen,
                // fixed-geometry and deterministic, and the two values are 7x
                // apart, so a 3x band is unambiguous in both directions.
                CHECK_MSG(m0 > 80.0f && m0 < 800.0f,
                          "the occlusion is the SIZE an ortho reconstruction "
                          "gives (window mass %.1f; 243 correct, 1702 with the "
                          "perspective-only form)", m0);
            }
        }

        // ---- THE HIGHLIGHTS (ogre-patch 0024) -----------------------------
        // The same statement for HlmsPbs' OWN view-dependent shading, measured
        // on plain frames (no post chain at all): a glossy dielectric sphere
        // under the directional light, seen by the ortho camera at the two
        // poses. With the pinhole viewDir the highlight sits at a different
        // place on the sphere at each pose (the eye direction differs per
        // pixel, so the half vector does), and the whole sphere's Fresnel
        // shading changes too; with the patch every fragment looks the same
        // way and the panned frame is the first one translated.
        //
        // The camera is brought CLOSE for this: the pinhole error grows with
        // the fragment's view-space xy over its distance, and an ortho camera
        // parked 8.6 units out (the pose above) makes a 0.25-unit pan a
        // fraction of a degree, which a 48 px sphere cannot resolve.
        {
            const NodeId sphere = os->createNode();
            {
                PbrParams p;
                p.albedo = Colour(0.8f, 0.8f, 0.8f);
                p.metalness = 0.0f;
                p.roughness = 0.25f;
                os->attachMesh(sphere, os->createMesh(unitSphereMesh()),
                               os->createPbrMaterial(p));
                enginetest::setNodeScale(os, sphere, Vec3(3.0f, 3.0f, 3.0f));
                enginetest::setNodePosition(os, sphere, Vec3(2.2f, 1.6f, -1.0f));
            }
            auto closePoseAt = [&](float dx) {
                CameraDesc c = enginetest::testCameraDescLookAt(Vec3(dx, 3.0f, 3.5f),
                                                                Vec3(dx, 0.0f, 0.0f));
                c.orthographic = true;
                c.orthoSize    = kOrthoSize;
                c.farClip      = kFarA;
                return c;
            };
            auto plain = [&](const CameraDesc &c, std::vector<float> &lum, Image &img) {
                ov->setCamera(c);
                ov->setPostFx(PostFxDesc());
                render(engine.get(), 4);
                if (!ov->readPixels(img) || img.width != kW || img.height != kH) return false;
                lum.assign(size_t(kW) * kH, 0.0f);
                for (unsigned y = 0; y < kH; ++y)
                    for (unsigned x = 0; x < kW; ++x) {
                        const Colour c4 = img.at(x, y);
                        lum[size_t(y) * kW + x] = 0.2126f * c4.r + 0.7152f * c4.g + 0.0722f * c4.b;
                    }
                return true;
            };
            std::vector<float> l0, l1;
            Image i0, i1;
            const bool got = plain(closePoseAt(0.0f), l0, i0) &&
                             plain(closePoseAt(kPanWorld), l1, i1);
            CHECK(got, "ortho highlight: both plain poses rendered and read back");
            if (got) {
                if (envOn("JAH_SSR_DUMP")) {
                    writePpm(i0, "pbs-ortho-plain-0.ppm");
                    writePpm(i1, "pbs-ortho-plain-1.ppm");
                }
                // The highlight CORE: pixels within 90% of the window's peak
                // luminance. Its centroid must move by the pan and nothing else.
                auto core = [&](const std::vector<float> &l, int x0, int x1,
                                float &cx, float &cy, float &mass) {
                    float peak = 0.0f;
                    for (unsigned y = 0; y < kH; ++y)
                        for (int x = x0; x < x1; ++x)
                            peak = std::max(peak, l[size_t(y) * kW + unsigned(x)]);
                    double sum = 0.0, wx = 0.0, wy = 0.0;
                    for (unsigned y = 0; y < kH; ++y)
                        for (int x = x0; x < x1; ++x) {
                            const float v = l[size_t(y) * kW + unsigned(x)];
                            if (v < 0.9f * peak) continue;
                            sum += v; wx += double(x) * v; wy += double(y) * v;
                        }
                    mass = float(sum);
                    cx = sum > 0.0 ? float(wx / sum) : -1.0f;
                    cy = sum > 0.0 ? float(wy / sum) : -1.0f;
                    return peak;
                };
                float cx0, cy0, m0, cx1, cy1, m1;
                const float peak0 = core(l0, kWinLo + kPanPixels, kWinHi + kPanPixels, cx0, cy0, m0);
                const float peak1 = core(l1, kWinLo, kWinHi, cx1, cy1, m1);
                const float mism = shiftedMismatch(l0, l1, kPanPixels, kWinLo, kWinHi);
                std::printf("   ortho highlight: peak %.3f -> %.3f, core mass %.2f -> %.2f, "
                            "centroid (%.2f, %.2f) -> (%.2f, %.2f) (expected x %.2f), "
                            "shifted mismatch %.4f\n",
                            peak0, peak1, m0, m1, cx0, cy0, cx1, cy1, cx0 - float(kPanPixels), mism);
                // Measured 0.463 / 346 on this fixture (a dielectric at
                // roughness 0.25 under a 3.0 directional); the floor sits
                // well under it and well above the sphere's diffuse body.
                CHECK_MSG(peak0 > 0.3f && m0 > 0.5f,
                          "the sphere has a specular highlight at all (peak %.3f, core mass %.2f)",
                          peak0, m0);
                CHECK_MSG(cx0 > 0.0f && std::fabs(cx1 - (cx0 - float(kPanPixels))) < 0.5f &&
                          std::fabs(cy1 - cy0) < 0.5f,
                          "the HIGHLIGHT PANS WITH THE SCENE: centroid (%.2f, %.2f) -> "
                          "(%.2f, %.2f), expected (%.2f, %.2f)",
                          cx0, cy0, cx1, cy1, cx0 - float(kPanPixels), cy0);
                CHECK_MSG(m0 > 0.0f && std::fabs(m1 - m0) / m0 < 0.10f,
                          "the highlight keeps its size across the pan (core mass %.2f -> %.2f)",
                          m0, m1);
                CHECK_MSG(mism < 0.01f,
                          "the panned PBS frame IS the first one translated (every "
                          "view-dependent term, not just the highlight): shifted mismatch %.4f",
                          mism);
            }
            os->removeNode(sphere);
        }

        // ---- THE FALLBACK SWITCH ------------------------------------------
        // JAH_ORTHO_POSTFX=off restores the plain gate (OgreView::chainDesc):
        // an axis view then gets no SSR and no SSAO at all, which is the safe
        // answer if the branch above ever misbehaves on a driver. The switch is
        // read once per process, so a suite cannot flip it mid-run - this only
        // records where it lives.
        std::printf("   (fallback: JAH_ORTHO_POSTFX=off gates both effects off "
                    "under an orthographic camera)\n");

        ov->setPostFx(PostFxDesc());
        render(engine.get(), 2);
        engine->destroyView(ov);
        engine->destroyScene(os);
        }
    }


    // ---- 13. THE COLOUR HISTORY CANNOT RUN AWAY (SMOKE-ENGINE-1 item 1) ----
    //
    // The owner's "black holes in the textures when I move the sun" and the
    // player's "all white, stuck exposure" are ONE defect: the SSR chain closes
    // a loop — scene colour -> the kSsrPrev history -> this shader's `prevFrame`
    // -> jahSsrReflection -> HlmsPbs' envColourS -> scene colour — and before
    // this section the loop had no finite guard anywhere in it. The history is
    // RGBA16_FLOAT, so a radiance above 65504 is stored as +Inf; the firefly
    // clamp then computes `reflected *= ceiling / lum` with lum == Inf, which is
    // Inf * 0 == NaN, and a NaN in the scene colour is a BLACK PIXEL after the
    // tonemap. It circulates for as long as the workspace lives, and the HDR
    // luminance reduction averages it into a 1x1 keep_content history that never
    // recovers — which is the stuck exposure.
    //
    // The fixture seeds the loop the way the engine really does it: a mirror
    // floor under an emitter far brighter than the half-float format can hold.
    // Nothing here is exotic — an emissive of 1e6 is what a sun disc or a
    // runaway feedback loop arrives at after a handful of frames of gain > 1.
    //
    // THREE THINGS ARE ASSERTED, and the third is the one that matters most:
    //   a. the picture is not shot through with black holes while the bright
    //      source is present;
    //   b. the unrepresentable thing itself develops as WHITE, which is what an
    //      over-bright pixel IS, rather than as a hole;
    //   c. REMOVING IT RECOVERS. A latched NaN never leaves — neither the
    //      history nor the exposure — so a run that cannot come back is the
    //      defect, whatever the picture looked like at its worst.
    //
    // WHAT THIS SECTION DOES NOT DO, measured rather than assumed, because a
    // green run here must not be read as proof of the guard:
    //
    // IT PASSES ON THE BINARY BEFORE THE GUARD. Run against the round-1 BASE
    // media (irisgl c620eee's resolve, a pass-through history copy, the
    // pristine HDR luminance end) every assertion below is green, with the same
    // numbers to three decimal places. It was never red, and no amount of
    // tuning made it red, for a reason worth writing down:
    //
    //   THIS GPU DOES NOT OVERFLOW A HALF-FLOAT TARGET TO INFINITY. Vulkan
    //   leaves the narrowing conversion implementation-defined — "values with
    //   magnitude greater than the maximum representable value may be converted
    //   to either infinity or the maximum representable value" — and on the
    //   RTX 4080 (driver 595.84) it CLAMPS. An emitter at 1e6 and an emitter at
    //   1e30 produce the identical frame, mean 0.4204 both times, with not one
    //   black pixel anywhere: the fixture has nothing to overflow. A non-finite
    //   value in this renderer is therefore made by ARITHMETIC — the loop's own
    //   Inf * 0 — and not by a store, and a 256x256 view two hundred frames
    //   long does not reach the loop gain that takes it there.
    //
    // The pixel proof of the guard is consequently the LIVE editor measurement
    // in spikes/smoke-engine-1/FINDINGS.md: on Ogre's ShadowMapFromCode port at
    // Full-Res Rays, 4.142 % of the viewport pure black before the guard,
    // 0.149 % after, against 0.166 % for the same frame with SSR switched off.
    //
    // What this section IS worth: it is the regression guard for the three
    // invariants around that fix — a very bright source does not hole the
    // picture, it develops as white, and removing it recovers — on a fixture
    // that runs in the gate in seconds. That is the honest claim.
    {
        const auto meanOf = [](const Image &img) {
            double s = 0.0;
            for (unsigned y = 0; y < img.height; ++y)
                for (unsigned x = 0; x < img.width; ++x) {
                    const Colour c = img.at(x, y);
                    s += (c.r + c.g + c.b) / 3.0;
                }
            return float(s / double(img.width * img.height));
        };
        // A GREY BACKDROP AND A GREY FLOOR, on purpose: in this fixture NOTHING
        // is legitimately black, so "a black pixel" means "a NaN reached the
        // tonemapper" with no heuristic in between.
        Scene *hs = engine->createScene("ssr-history");
        View  *hv = engine->createOffscreenView("ssr-history", 256, 256, Colour(0.25f, 0.25f, 0.30f));
        CHECK(hs && hv, "the history fixture's scene and view exist");
        if (hs && hv) {
        hv->setScene(hs);
        hs->setAmbient(Colour(0.35f, 0.35f, 0.40f), Colour(0.30f, 0.30f, 0.32f));

        PbrParams fp;
        fp.albedo = Colour(0.60f, 0.60f, 0.62f);
        fp.metalness = 0.85f;
        fp.roughness = 0.08f;
        const MaterialId hFloorMat = hs->createPbrMaterial(fp);
        const NodeId hFloor = hs->createNode();
        hs->attachMesh(hFloor, hs->createMesh(enginetest::unitCubeMesh()), hFloorMat);
        enginetest::setNodeScale(hs, hFloor, Vec3(12.0f, 0.2f, 12.0f));
        enginetest::setNodePosition(hs, hFloor, Vec3(0.0f, -0.1f, 0.0f));

        // The emitter. It starts ORDINARY so the fixture has a clean baseline,
        // and is cranked past the half-float ceiling below.
        PbrParams ep;
        ep.albedo = Colour(0.05f, 0.05f, 0.05f);
        ep.emissive = Colour(3.0f, 0.0f, 0.0f);
        ep.roughness = 0.5f;
        const MaterialId hEmitMat = hs->createPbrMaterial(ep);
        const NodeId hEmit = hs->createNode();
        hs->attachMesh(hEmit, hs->createMesh(enginetest::unitCubeMesh()), hEmitMat);
        enginetest::setNodeScale(hs, hEmit, Vec3(1.5f, 1.5f, 1.5f));
        enginetest::setNodePosition(hs, hEmit, Vec3(0.0f, 2.2f, 0.0f));

        enginetest::addDirectionalLight(hs, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
        enginetest::testCameraLookAt(hv, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));

        PostFxDesc hfx;
        hfx.allowOffscreen = true;
        hfx.hdr = true;                 // the half-float target is the whole point
        hfx.ssr = 2;                    // full-resolution rays, the owner's row
        // A CONSTANT GRADE, and this is what makes the section discriminating.
        // With the AUTO exposure the fixture proved nothing: a scene holding
        // something a million times brighter than the rest meters dark, that is
        // a camera working rather than a bug, and the black it produces swamps
        // the black the defect produces. tonemapFixed replaces the whole
        // luminance reduction with a per-frame clear of the 1x1 exposure
        // texture, so the grade cannot move and a black pixel in the measured
        // region can only be a NaN. exposureScale is pushed rather than derived
        // from the grey card so the floor reads mid-bright.
        hfx.tonemapFixed = true;
        hfx.exposureScale = 3.0f;
        hv->setPostFx(hfx);

        // "A black hole": a pixel that is pure black with a bright neighbour.
        // A NaN tonemaps to 0 while everything around it stays lit, which is
        // exactly what the owner photographed.
        // A BLACK HOLE, defined so the AUTO-EXPOSURE cannot fake one: a pixel
        // that is pure black while one of its eight neighbours is bright. A
        // scene holding something a million times brighter than the rest SHOULD
        // meter dark — that is a camera working, not a bug — and every pixel of
        // a frame the exposure has stopped down has dark neighbours too. A NaN
        // does not: it sits inside the lit region it came from.
        // THE MEASURED REGION IS THE LOWER HALF, i.e. the FLOOR, and the
        // emitter sits in the upper half. That is not tidiness, it is the whole
        // discrimination: an emitter whose own radiance is +Inf is a NaN in the
        // TONEMAPPER (FilmicTonemap(Inf) is Inf/Inf) on every binary ever
        // built, fixed or not, so its own pixels say nothing about this guard.
        // The floor is lit by the ambient and by one thing else — the SSR
        // reflection — so a black pixel down there IS the loop, and nothing
        // else in this fixture can write one.
        auto blackHoles = [](const Image &img) {
            unsigned n = 0;
            for (unsigned y = img.height / 2u; y < img.height; ++y)
                for (unsigned x = 0; x < img.width; ++x) {
                    const Colour c = img.at(x, y);
                    if (c.r <= 0.004f && c.g <= 0.004f && c.b <= 0.004f) ++n;
                }
            return n;
        };

        // The BASELINE the two later measurements are read against.
        Image cold;
        render(engine.get(), 60);
        CHECK(hv->readPixels(cold), "readPixels before the overflow source exists");
        const unsigned holesCold = blackHoles(cold);
        std::printf("   baseline (ordinary emitter): black pixels %u/%u, mean %.4f\n",
                    holesCold, cold.width * cold.height, meanOf(cold));
        CHECK_MSG(holesCold == 0u,
                  "no holes in the fixture before anything goes over-bright (%u px)", holesCold);

        // THE OVER-BRIGHT SOURCE. 1e6 is fifteen times the half-float target's
        // largest value, so this is as far past the format as a scene can
        // meaningfully go — on hardware that overflows to infinity it IS +Inf,
        // and on this one it clamps (see the note at the top of the section).
        ep.emissive = Colour(1.0e6f, 1.0e6f, 1.0e6f);
        CHECK(hs->setPbrMaterial(hEmitMat, ep),
              "the emitter goes far past what the history's format can hold");
        Image hot;
        render(engine.get(), 120);
        CHECK(hv->readPixels(hot), "readPixels with the overflow source present");
        const unsigned holesHot = blackHoles(hot);
        const float meanHot = meanOf(hot);
        std::printf("   overflow source present: black holes %u/%u, mean %.4f\n",
                    holesHot, hot.width * hot.height, meanHot);
        if (envOn("JAH_SSR_DUMP")) writePpm(hot, "ssr-history-hot.ppm");
        // ATTRIBUTION: the same overflow with SSR OFF. Whatever black is left
        // in THAT frame is the tonemapper's answer to an Inf that never went
        // near the reflection chain; the difference is SSR's own contribution.
        {
            PostFxDesc noSsr = hfx; noSsr.ssr = 0;
            hv->setPostFx(noSsr);
            Image hotOff;
            render(engine.get(), 60);
            hv->readPixels(hotOff);
            std::printf("   overflow source, SSR OFF: black %u/%u, mean %.4f\n",
                        blackHoles(hotOff), hotOff.width * hotOff.height, meanOf(hotOff));
            if (envOn("JAH_SSR_DUMP")) writePpm(hotOff, "ssr-history-hot-ssroff.ppm");
            hv->setPostFx(hfx);
            render(engine.get(), 60);
        }
        CHECK_MSG(holesHot == 0u,
                  "no black holes on the FLOOR while a source far brighter than the history's "
                  "format can hold is on screen (%u px)", holesHot);
        // ...and the unrepresentable thing itself develops as WHITE. A hole and
        // a blown highlight are both "not the right colour"; only one of them
        // is what an over-bright pixel IS.
        {
            float brightest = 0.0f;
            for (unsigned y = 0; y < hot.height; ++y)
                for (unsigned x = 0; x < hot.width; ++x) {
                    const Colour c = hot.at(x, y);
                    brightest = std::max(brightest, (c.r + c.g + c.b) / 3.0f);
                }
            CHECK_MSG(brightest > 0.90f,
                      "the over-bright source develops as WHITE, not as a hole (%.3f)", brightest);
        }

        // c. RECOVERY. Take the source back to an ordinary brightness; the
        //    history and the exposure must come back within a second of frames.
        ep.emissive = Colour(3.0f, 0.0f, 0.0f);
        CHECK(hs->setPbrMaterial(hEmitMat, ep), "the overflow source goes back to an ordinary one");
        (void)holesCold;
        Image cool;
        render(engine.get(), 120);
        CHECK(hv->readPixels(cool), "readPixels after the overflow source is gone");
        const unsigned holesCool = blackHoles(cool);
        const float meanCool = meanOf(cool);
        std::printf("   overflow source removed: black holes %u/%u, mean %.4f\n",
                    holesCool, cool.width * cool.height, meanCool);
        if (envOn("JAH_SSR_DUMP")) writePpm(cool, "ssr-history-cool.ppm");
        CHECK_MSG(holesCool == 0u,
                  "the black holes are GONE two seconds after the source is (%u px)", holesCool);
        CHECK_MSG(meanCool > 0.02f && meanCool < 0.95f,
                  "the auto-exposure RECOVERED rather than latching (mean %.4f)", meanCool);

        hv->setPostFx(PostFxDesc());
        render(engine.get(), 2);
        engine->destroyView(hv);
        engine->destroyScene(hs);
        }

    }

    // ---- 14. A CURVED MIRROR FADES OUT WHERE THE TRACE CANNOT BE TRUSTED ---
    //
    // The owner's push #34 smoke, defect A: the Mirror Room's chrome sphere
    // reflected the teapot as green shards and dots while SSR was on, and the
    // same sphere with SSR OFF showed a clean parallax-corrected probe
    // reflection. A screen-space trace has nothing useful to say about most of
    // a sphere — the rays leave in every direction, most of them ask about
    // geometry that is off screen, behind something, or seen edge-on — and the
    // march used to hand every hit it got back at full confidence.
    //
    // THE FIXTURE is the smallest thing that reproduces it: a mirror SPHERE on
    // a floor with three coloured emissive blocks around it, so the sphere's
    // rays have both something to find and plenty of directions in which to
    // find nothing. The measurement is the sphere's OWN disc, compared between
    // SSR off and SSR on:
    //
    //   footprint   pixels where the two differ by more than 0.25 (of 1). This
    //               is the shards: on a curved mirror the honest answer is the
    //               probe/sky the surface already had, so a big footprint means
    //               the screen overruled it with something it could not verify.
    //   isolated    of those, the ones whose eight neighbours did NOT move.
    //               A coherent reflection appearing is a patch; a shard is a
    //               dot. This is the number the owner photographed.
    //
    // Both are asserted against the SAME scene rendered with SSR off, so the
    // tolerance is not a picture taste — it is "how much of the sphere did the
    // screen-space trace overrule".
    //
    // MEASURED ON THE BASE MEDIA (irisgl c4f7af5, the SSR-1 lane's base), this
    // section is RED: **37.996 %** of the sphere overruled. With the lane's
    // confidence terms (arrival angle, thickness margin, ray coherence, the
    // borrow quorum — JahSsrRayMarch_ps.glsl and JahSsrResolve_ps.glsl) it is
    // green at 0.000 %. The live proof at editor resolution is in
    // spikes/ssr-1/FINDINGS.md: on the Mirror Room's sphere, 5.70 % of the
    // region differed from the SSR-off picture by more than 32/255 before and
    // 0.50 % after, with the gold torus going 1.61 % -> 0.001 %.
    {
        Scene *cs = engine->createScene("ssr-curved");
        View  *cv = engine->createOffscreenView("ssr-curved", 256, 256, Colour(0.05f, 0.05f, 0.08f));
        CHECK(cs && cv, "the curved-mirror fixture's scene and view exist");
        if (cs && cv) {
        cv->setScene(cs);
        // AN ENVIRONMENT FOR THE SPHERE TO FALL BACK ON, and it has to be a real
        // one: without it "SSR faded out" and "SSR was never there" are the same
        // picture and the section measures nothing. The scene ambient is not
        // enough — it is added AFTER the SSR composite (upstream's
        // DoAmbientLighting) and a mirror with no reflection source renders
        // BLACK, measured. The analytic sky is one line and binds a real IBL
        // cubemap, which is exactly what the Mirror Room's sphere falls back to.
        cs->setAmbient(Colour(0.30f, 0.30f, 0.35f), Colour(0.18f, 0.18f, 0.22f));
        {
            SkyDesc sky;
            sky.mode = SkyMode::Atmosphere;
            CHECK(cs->setSky(sky), "the curved fixture has a sky to fall back to");
        }

        PbrParams gp;
        gp.albedo = Colour(0.55f, 0.55f, 0.55f);
        gp.metalness = 0.1f;
        gp.roughness = 0.6f;                      // matte: SSR cannot touch the floor itself
        const NodeId gfloor = cs->createNode();
        cs->attachMesh(gfloor, cs->createMesh(enginetest::unitCubeMesh()), cs->createPbrMaterial(gp));
        enginetest::setNodeScale(cs, gfloor, Vec3(16.0f, 0.2f, 16.0f));
        enginetest::setNodePosition(cs, gfloor, Vec3(0.0f, -0.1f, 0.0f));

        PbrParams mp;
        mp.albedo = Colour(1.0f, 1.0f, 1.0f);
        mp.metalness = 1.0f;
        mp.roughness = 0.03f;                     // the Mirror Room's own chrome ball
        const NodeId ball = cs->createNode();
        cs->attachMesh(ball, cs->createMesh(unitSphereMesh()), cs->createPbrMaterial(mp));
        enginetest::setNodeScale(cs, ball, Vec3(3.0f, 3.0f, 3.0f));
        enginetest::setNodePosition(cs, ball, Vec3(0.0f, 1.5f, 0.0f));

        const Vec3 blockPos[3] = { Vec3(-2.6f, 0.8f, 2.2f), Vec3(2.6f, 0.9f, 1.4f),
                                   Vec3(0.2f, 0.6f, 3.4f) };
        const Colour blockCol[3] = { Colour(0.0f, 2.5f, 0.0f), Colour(2.5f, 0.0f, 0.0f),
                                     Colour(0.0f, 0.0f, 2.5f) };
        for (int i = 0; i < 3; ++i) {
            PbrParams bp;
            bp.albedo = Colour(0.05f, 0.05f, 0.05f);
            bp.emissive = blockCol[i];
            bp.roughness = 0.5f;
            const NodeId b = cs->createNode();
            cs->attachMesh(b, cs->createMesh(enginetest::unitCubeMesh()), cs->createPbrMaterial(bp));
            enginetest::setNodeScale(cs, b, Vec3(1.1f, 1.1f, 1.1f));
            enginetest::setNodePosition(cs, b, blockPos[i]);
        }
        enginetest::addDirectionalLight(cs, Vec3(-0.4f, -1.0f, -0.3f), 2.5f);
        enginetest::testCameraLookAt(cv, Vec3(0.0f, 2.4f, 6.5f), Vec3(0.0f, 1.5f, 0.0f));

        Image curvedOff, curvedOn;
        PostFxDesc cfx;
        cfx.allowOffscreen = true;
        cv->setPostFx(cfx);                        // ssr == 0
        render(engine.get(), 6);
        CHECK(cv->readPixels(curvedOff), "curved fixture renders with SSR off");
        cfx.ssr = 2;                               // full-resolution rays, the owner's row
        cv->setPostFx(cfx);
        render(engine.get(), 8);                   // the history needs a frame; give it several
        CHECK(cv->readPixels(curvedOn), "curved fixture renders with SSR on");
        if (envOn("JAH_SSR_DUMP")) {
            writePpm(curvedOff, "ssr-curved-off.ppm");
            writePpm(curvedOn, "ssr-curved-on.ppm");
        }

        // THE SPHERE'S DISC, in pixels, from where it was placed: centre (0,1.5,0)
        // at radius 1.5 seen from (0,2.4,6.5). Measured rather than derived —
        // the assertions below only need a window that is sphere and not floor.
        const unsigned sx0 = 78, sx1 = 178, sy0 = 74, sy1 = 174;
        unsigned moved = 0, isolated = 0, total = 0;
        const auto delta = [&](unsigned x, unsigned y) {
            const Colour a = curvedOff.at(x, y), b = curvedOn.at(x, y);
            return std::max(std::max(std::fabs(a.r - b.r), std::fabs(a.g - b.g)),
                            std::fabs(a.b - b.b));
        };
        for (unsigned y = sy0; y <= sy1; ++y)
            for (unsigned x = sx0; x <= sx1; ++x) {
                ++total;
                if (delta(x, y) <= 0.25f) continue;
                ++moved;
                float nbr = 0.0f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (dx || dy) nbr = std::max(nbr, delta(x + dx, y + dy));
                if (nbr <= 0.10f) ++isolated;       // a dot, not a patch
            }
        const float footprint = 100.0f * float(moved) / float(total);
        const float isoPct    = 100.0f * float(isolated) / float(total);
        std::printf("   curved mirror: footprint %.3f %% (%u px), isolated %.3f %% (%u px)\n",
                    footprint, moved, isoPct, isolated);
        CHECK_MSG(footprint < 1.5f,
                  "a curved mirror keeps its probe reflection where the trace cannot be "
                  "trusted (SSR overruled %.3f %% of the sphere, budget 1.5 %%)", footprint);
        CHECK_MSG(isoPct < 0.10f,
                  "no shards: SSR paints no isolated dots on a curved mirror "
                  "(%.3f %%, budget 0.10 %%)", isoPct);

        cv->setPostFx(PostFxDesc());
        render(engine.get(), 2);
        engine->destroyView(cv);
        engine->destroyScene(cs);
        }
    }

    // ---- 15. THE REFLECTION REPLACES THE ENVIRONMENT TERM, NEVER ADDS ------
    //
    // Defect C of the same smoke: "SSR roughly DOUBLES a mirror floor's metered
    // brightness". The mechanism is upstream's composite, which chooses between
    // two spellings on a DATABLOCK property (800.PixelShader_piece_ps.any,
    // `hlms_use_ssr`):
    //
    //     @property( use_envprobe_map )   envColourS = lerp( envColourS, ssr, w )
    //     @else                           envColourS += ssr * w
    //
    // The lerp is the physics — the probe and the screen are two estimates of
    // ONE integral and `w` says how much of it the screen answered — and the
    // add is a second copy of the same lobe. `use_envprobe_map` is raised only
    // by a reflection cubemap or parallax-corrected probes, while the voxel
    // cone's specular, the irradiance field and irradiance volumes all write
    // envColourS without raising it. ogre-patch 0036 makes the composite the
    // lerp unconditionally.
    //
    // THE FIXTURE is exactly that configuration: VCT global illumination, NO
    // sky and NO probe grid, so the mirror floor's environment term is the
    // voxel cone's specular and the material takes the second branch on
    // unpatched media.
    //
    // THE MEASUREMENT IS A FOUR-WAY A/B, because no single picture can tell
    // "the reflection arrived" from "the reflection arrived twice". Render the
    // floor with VCT off and on, and each of those with SSR off and on:
    //
    //     vctAlone    = mean(VCT on,  SSR off) - mean(VCT off, SSR off)
    //     vctWithSsr  = mean(VCT on,  SSR on)  - mean(VCT off, SSR on)
    //
    // `vctAlone` is what the voxel cone puts on the mirror. `vctWithSsr` is what
    // it still puts there once the screen has answered for the same directions.
    // REPLACE means the second is a fraction of the first — the part of the
    // integral the screen could not answer, (1 - w). ADD means they are EQUAL,
    // because the add branch lays the whole cone term on top of the reflection
    // whatever the confidence was. So the assertion is a ratio, and it needs no
    // knowledge of w, of the tonemap or of the fixture's absolute brightness.
    //
    // MEASURED on this fixture: 0.314 with patch 0036 against 0.753 without it
    // — and the budget is halfway between them. The unpatched figure is not the
    // 1.0 the arithmetic of `+=` implies because the numbers are read off the
    // view's 8-BIT sRGB-ENCODED readback (this fixture has HDR off, so there is
    // no film curve): the encode compresses the brighter (doubled) pixel more
    // than the dimmer one and the result clips at 1.0. The raw radiance ratio is
    // 1.0 by construction.
    {
        Scene *vs = engine->createScene("ssr-energy");
        View  *vv = engine->createOffscreenView("ssr-energy", 256, 256, Colour(0.0f, 0.0f, 0.0f));
        CHECK(vs && vv, "the energy fixture's scene and view exist");
        if (vs && vv) {
        vv->setScene(vs);
        // NO SKY, deliberately: a sky binds an IBL cubemap, which raises
        // use_envprobe_map and puts the material on the lerp branch — the very
        // thing this section exists to measure the absence of.
        vs->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));

        PbrParams fp;
        fp.albedo = Colour(1.0f, 1.0f, 1.0f);
        fp.metalness = 1.0f;
        fp.roughness = 0.0f;
        const NodeId efloor = vs->createNode();
        vs->attachMesh(efloor, vs->createMesh(enginetest::unitCubeMesh()), vs->createPbrMaterial(fp));
        enginetest::setNodeScale(vs, efloor, Vec3(12.0f, 0.2f, 12.0f));
        enginetest::setNodePosition(vs, efloor, Vec3(0.0f, -0.1f, 0.0f));

        PbrParams ep;
        ep.albedo = Colour(0.05f, 0.05f, 0.05f);
        ep.emissive = Colour(4.0f, 0.0f, 0.0f);
        ep.roughness = 0.5f;
        const NodeId eemit = vs->createNode();
        vs->attachMesh(eemit, vs->createMesh(enginetest::unitCubeMesh()), vs->createPbrMaterial(ep));
        enginetest::setNodeScale(vs, eemit, Vec3(1.5f, 1.5f, 1.5f));
        enginetest::setNodePosition(vs, eemit, Vec3(0.0f, 2.2f, 0.0f));

        enginetest::addDirectionalLight(vs, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
        enginetest::testCameraLookAt(vv, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));

        GiParams vct;
        vct.mode = GiMode::Vct;
        vct.quality = GiQuality::Medium;
        vct.numBounces = 1;
        const GiParams giOff;                      // mode Off

        const auto shoot = [&](bool withVct, bool withSsr, Image &out, const char *what) {
            CHECK(vs->setGlobalIllumination(withVct ? vct : giOff), what);
            PostFxDesc d;
            d.allowOffscreen = true;
            d.ssr = withSsr ? 2 : 0;               // full-resolution rays, the owner's row
            vv->setPostFx(d);
            render(engine.get(), 10);              // the history needs several frames
            CHECK(vv->readPixels(out), "readPixels");
        };
        Image noVctNoSsr, vctNoSsr, noVctSsr, vctSsr;
        shoot(false, false, noVctNoSsr, "GI off, SSR off");
        shoot(true,  false, vctNoSsr,   "VCT on, SSR off");
        shoot(false, true,  noVctSsr,   "GI off, SSR on");
        shoot(true,  true,  vctSsr,     "VCT on, SSR on");
        if (envOn("JAH_SSR_DUMP")) {
            writePpm(vctNoSsr, "ssr-energy-vct.ppm");
            writePpm(vctSsr, "ssr-energy-vct-ssr.ppm");
        }

        // THE POPULATION: the floor pixels the SCREEN's reflection reached (the
        // SSR-on/GI-off picture's own red footprint). Measuring anywhere else
        // would be asking about pixels where SSR has no opinion and the two
        // composites agree by construction.
        double vctAlone = 0.0, vctWithSsr = 0.0;
        unsigned n = 0;
        for (unsigned y = noVctSsr.height / 2; y < noVctSsr.height; ++y)
            for (unsigned x = 0; x < noVctSsr.width; ++x) {
                const Colour a = noVctSsr.at(x, y), b = noVctNoSsr.at(x, y);
                const float ssrRed = (a.r - std::max(a.g, a.b)) - (b.r - std::max(b.g, b.b));
                if (ssrRed < 0.08f) continue;      // the screen reflects nothing here
                const auto lum = [](const Colour &c) { return (c.r + c.g + c.b) / 3.0f; };
                vctAlone   += lum(vctNoSsr.at(x, y)) - lum(noVctNoSsr.at(x, y));
                vctWithSsr += lum(vctSsr.at(x, y))   - lum(noVctSsr.at(x, y));
                ++n;
            }
        CHECK_MSG(n > 200u, "the screen's reflection has a measurable footprint (%u px)", n);
        if (n > 200u) {
            const float alone = float(vctAlone / n), with = float(vctWithSsr / n);
            const float ratio = float(with / std::max(alone, 1e-6f));
            std::printf("   energy: %u px; the voxel cone puts %.4f on the mirror alone, "
                        "%.4f once SSR answered -> ratio %.3f\n", n, alone, with, ratio);
            CHECK_MSG(alone > 0.01f,
                      "the voxel cone really does light this mirror (%.4f), so the ratio "
                      "below measures something", alone);
            CHECK_MSG(ratio < 0.50f,
                      "the reflection REPLACES the voxel-cone environment term rather than "
                      "adding to it (%.3f of the cone's term survives where the screen "
                      "answered; unpatched media measures 0.753 here)", ratio);
        }

        CHECK(vs->setGlobalIllumination(giOff), "GI off again for the teardown");
        vv->setPostFx(PostFxDesc());
        render(engine.get(), 2);
        engine->destroyView(vv);
        engine->destroyScene(vs);
        }
    }

    // ---- 16. ON A MIRROR THE WEIGHT IS A DECISION, NOT A FRACTION ----------
    //
    // Lane SSR-2, the owner's DUAL IMAGE on the Mirror Room's chrome sphere
    // (rig evidence spikes/smoke-2026-09-15/reportB, ledger §321): with SSR on,
    // the sphere showed a stippled ghost of the teapot painted OVER the probe's
    // smooth image; with SSR off, one image. Section 14 above is about SSR
    // painting things it cannot verify; this one is about it painting a thing
    // it CAN verify at a FRACTION, which is a different defect with a different
    // cure.
    //
    // WHY A FRACTION IS WRONG ON A MIRROR, in one line: the probe's image and
    // the screen's image are the same objects in two PLACES (the probe is
    // parallax-corrected onto a box and captured from a grid point; the trace
    // is at the true position and from this frame), so any weight strictly
    // between 0 and 1 shows BOTH of them, and a weight that changes from pixel
    // to pixel — which is what a per-ray confidence does — shows the screen's
    // copy as a dither. Below a roughness threshold the confidence therefore
    // decides VALID vs NONE and stops scaling the blend
    // (JahSsrResolve_ps.glsl, "THE RULE ON A MIRROR"); above it the lerp stays,
    // because a wide enough lobe blurs the two sources into one answer.
    //
    // THE FIXTURE MAKES THE WEIGHT ITSELF READABLE, which takes three
    // deliberate choices and is worth spelling out because each one removes a
    // term that would otherwise be mistaken for the weight:
    //
    //  1. NO SKY, NO AMBIENT, NO LIGHT. With no environment source at all the
    //     composite is lerp( 0, ssr, w ) = w * ssr — the mirror is BLACK where
    //     the screen has no answer — so the picture IS the weight field, scaled
    //     by the reflected radiance.
    //  2. A WHITE METAL MIRROR. F0 = albedo = 1 makes the Fresnel term 1 at
    //     every angle, so that scale does not drift across the floor. (The
    //     first cut of this fixture used a coloured sky and a lit metal and
    //     measured the Fresnel ramp instead of the weight.)
    //  3. AN EMISSIVE CEILING, low and wide. Emissive so what the screen finds
    //     does not depend on lighting or GI; low so a grazing reflected ray
    //     still reaches it; and horizontal so the reflected ray's vertical
    //     component IS the arrival angle at it, sweeping from face-on near the
    //     camera to grazing towards the horizon — straight through the march's
    //     `faceFade` ramp. That is the confidence term with the widest, most
    //     even footprint, which is what makes a blend and a decision differ
    //     over a whole region of the picture instead of a few pixels.
    //     `ssrMaxDistance` is moved far past the scene for the same reason: the
    //     DISTANCE fade is an envelope the rule keeps as a fraction on purpose,
    //     and on this geometry it sweeps the same pixels the arrival angle does.
    //
    // The measurement is then the HISTOGRAM of the floor's LINEAR green, in
    // bands of its own maximum:
    //
    //     full   d >= 0.75 M            the screen answered and won
    //     middle 0.20 M <= d < 0.75 M   both images are on the pixel at once
    //
    // and the assertion is the middle band as a fraction of the two. A blend
    // spreads its pixels across the middle; a decision puts them at the ends
    // and keeps in the middle only the mask's own FEATHER — about one
    // ray-buffer texel of ramp at the hit region's edge, a boundary and not a
    // ghost.
    //
    // WHAT THIS SECTION IS AND IS NOT, said plainly because the number it
    // prints is the same on both media: 0.089 on the base resolve and 0.092
    // with the rule. It is a GUARD, not the proof of the fix. A well-sampled
    // mirror is FULLY confident under both — that is the design, and it is why
    // the flat-floor sections above do not move either — so the two only differ
    // where the trace is marginal, and there the probe's answer has to DISAGREE
    // with the screen's for the difference to be visible at all. Nothing in
    // this suite's fixtures disagrees: they fall back to the analytic sky,
    // which is the same sky the trace is looking at. It takes a
    // parallax-corrected PROBE GRID — a captured photograph of a room, from a
    // point that is not this pixel — for the two sources to be the same objects
    // in two places, and that is the Mirror Room, not a 256x256 fixture. The
    // lane's proof is there (SSR on vs off over the chrome sphere's limb:
    // footprint 7.58 % and mean delta 4.34 before, 0.47 % and 1.07 after).
    //
    // What this section DOES pin is the other half of the rule, the half a
    // future change could quietly break: where the screen DOES answer on a
    // mirror, the answer must arrive as a decision — full strength across the
    // region, with only the mask's feather in between — rather than as a field
    // of fractions.
    {
        Scene *ms = engine->createScene("ssr-mirror-rule");
        View  *mv = engine->createOffscreenView("ssr-mirror-rule", 256, 256,
                                                Colour(0.0f, 0.0f, 0.0f));
        CHECK(ms && mv, "the mirror-rule fixture's scene and view exist");
        if (ms && mv) {
        mv->setScene(ms);
        ms->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));

        PbrParams fp2;
        fp2.albedo = Colour(1.0f, 1.0f, 1.0f);
        fp2.metalness = 1.0f;
        fp2.roughness = 0.02f;                    // mirror class: the rule's own range
        const NodeId mfloor = ms->createNode();
        ms->attachMesh(mfloor, ms->createMesh(enginetest::unitCubeMesh()),
                       ms->createPbrMaterial(fp2));
        enginetest::setNodeScale(ms, mfloor, Vec3(60.0f, 0.2f, 60.0f));
        enginetest::setNodePosition(ms, mfloor, Vec3(0.0f, -0.2f, 0.0f));

        PbrParams cp;
        cp.albedo   = Colour(0.0f, 0.0f, 0.0f);
        cp.emissive = Colour(0.0f, 1.0f, 0.0f);
        cp.roughness = 0.9f;
        const NodeId ceiling = ms->createNode();
        ms->attachMesh(ceiling, ms->createMesh(enginetest::unitCubeMesh()),
                       ms->createPbrMaterial(cp));
        enginetest::setNodeScale(ms, ceiling, Vec3(60.0f, 0.1f, 60.0f));
        enginetest::setNodePosition(ms, ceiling, Vec3(0.0f, 0.55f, 0.0f));
        enginetest::testCameraLookAt(mv, Vec3(0.0f, 0.16f, 6.0f), Vec3(0.0f, 0.13f, -6.0f));

        Image mirrOff, mirrOn;
        PostFxDesc mfx;
        mfx.allowOffscreen = true;
        mv->setPostFx(mfx);                        // ssr == 0
        render(engine.get(), 6);
        CHECK(mv->readPixels(mirrOff), "mirror-rule fixture renders with SSR off");
        mfx.ssr = 2;                               // full-resolution rays
        mv->setPostFx(mfx);
        render(engine.get(), 8);
        CHECK(mv->readPixels(mirrOn), "mirror-rule fixture renders with SSR on");
        if (envOn("JAH_SSR_DUMP")) {
            writePpm(mirrOff, "ssr-mirror-rule-off.ppm");
            writePpm(mirrOn, "ssr-mirror-rule-on.ppm");
        }

        // A window of FLOOR: below the horizon, inset from the frame's edges so
        // the march's own screen-edge ramp is not what is being measured.
        const unsigned mx0 = 40, mx1 = 215, my0 = 140, my1 = 250;
        // The readback is 8-bit sRGB-ENCODED (this fixture has HDR off, so
        // there is no film curve); the weight is linear, so the bands are
        // measured after decoding. A monotone curve would not change WHICH
        // pixels are extreme, but it would move the band edges.
        const auto linear = [](float v) {
            return v <= 0.04045f ? v / 12.92f
                                 : std::pow((v + 0.055f) / 1.055f, 2.4f);
        };
        const auto weightAt = [&](unsigned x, unsigned y) {
            return linear(mirrOn.at(x, y).g) - linear(mirrOff.at(x, y).g);
        };
        // NORMALISED PER ROW, which is what makes this a measurement of the
        // CONFIDENCE and not of the envelope. The distance fade, the arrival
        // angle and the reflected radiance are all functions of the reflected
        // ray's elevation — i.e. of the screen ROW — and are constant along a
        // row; the mask is not. So each row is measured against its own
        // brightest pixel, and a row is either answered (its pixels sit at its
        // own full strength), unanswered (nothing above the floor of the
        // measurement), or DITHERED — which is the defect, and the only thing
        // that puts pixels in the middle band.
        float peak = 0.0f;
        for (unsigned y = my0; y <= my1; ++y)
            for (unsigned x = mx0; x <= mx1; ++x)
                peak = std::max(peak, weightAt(x, y));
        unsigned full = 0, middle = 0;
        if (peak > 0.02f)
            for (unsigned y = my0; y <= my1; ++y) {
                float rowPeak = 0.0f;
                for (unsigned x = mx0; x <= mx1; ++x)
                    rowPeak = std::max(rowPeak, weightAt(x, y));
                if (rowPeak < 0.10f * peak) continue;      // an unanswered row
                for (unsigned x = mx0; x <= mx1; ++x) {
                    const float d = weightAt(x, y);
                    if (d >= 0.75f * rowPeak)      ++full;
                    else if (d >= 0.20f * rowPeak) ++middle;
                }
            }
        const float midFrac = (full + middle) > 0u
                                  ? float(middle) / float(full + middle)
                                  : 1.0f;
        std::printf("   mirror rule: peak reflected green %.3f; full %u px, middle %u px "
                    "-> middle fraction %.3f\n", peak, full, middle, midFrac);
        CHECK_MSG(peak > 0.02f,
                  "the screen's answer really is on this mirror (%.3f), so the bands "
                  "below measure something", peak);
        CHECK_MSG(full > 400u,
                  "...and it WINS over a real region of it (%u px at full strength)", full);
        CHECK_MSG(midFrac < 0.35f,
                  "on a mirror the composite is a decision, not a blend: only %.3f of the "
                  "answered pixels carry both images at once (budget 0.35; measured 0.09 "
                  "on both the base resolve and the rule — see the note above)", midFrac);

        mv->setPostFx(PostFxDesc());
        render(engine.get(), 2);
        engine->destroyView(mv);
        engine->destroyScene(ms);
        }
    }

    // ---- teardown with the chain live --------------------------------------
    // The ASan copy of this suite is what would catch a texture or node
    // definition the SSR shape leaks across a rebuild.
    {
        PostFxDesc live = fx;
        view->setPostFx(live);
        render(engine.get(), 2);
        engine->destroyView(view);
        engine->destroyScene(s);
        std::printf("ok: destroyed the view and scene with the SSR chain live\n");
    }

    // ---- 13. THE PREPASS MUST HAND BACK THE ROUGHNESS IT WROTE -------------
    //          (lane HDR-1, ogre-patch 0043)
    //
    // Assertion 9 above says the prepass RESTRUCTURE is shading-neutral: with the
    // roughness cutoff at zero the reflection is empty everywhere, the whole
    // prepass shape is still in the graph, and the frame comes back within the
    // normals G-buffer's quantization of the SSR-off frame. It says that on an
    // UNTEXTURED, fairly rough material. It was NOT true of a MIRROR-SMOOTH one,
    // and the reason was a real picture defect rather than a test gap.
    //
    // In PrePassUse mode the shading pass takes the roughness back out of the
    // G-buffer, where the prepass packed it as `(alpha - 0.02) * 1.0204` — a
    // range that starts at 0.02 while the shader's own alpha floor
    // (SampleRoughnessMap) is 0.001. A material smoother than perceptual 0.141
    // therefore came back WIDENED, over its whole lit area, purely because SSR
    // was switched on. ogre-patch 0043 packs over the range the shader can
    // actually reach, at the same 16 bits.
    //
    // THE SAME LINE also decides whether the readback happens at all, and
    // upstream gates it on the material carrying a roughness MAP. Jahshaka patch
    // 0022 broke that premise — it folds the screen-space variance of the shading
    // normal into the GGX alpha, so on any NORMAL-MAPPED surface the roughness is
    // a per-pixel quantity the material constant does not carry, and a
    // normal-mapped material with a constant roughness lost its anti-aliasing for
    // as long as SSR was on (lane SSR-1 round 2 measured that half at 1.48 % of
    // the picture, peak 161, on the real ShadowMapFromCode port). 0043 widens the
    // gate to `roughness_map || normal_map_tex`. This fixture carries BOTH maps,
    // so it measures the encoding directly and holds the gate open by
    // construction; the gate's own half is not separable on a synthetic fixture
    // because patch 0022's kernel is ~0 wherever the normal map is well behaved.
    //
    // THE MEASUREMENT is the one assertion 9 makes — the prepass at zero
    // confidence against no prepass at all, so everything that is not the prepass
    // cancels.
    {
        Scene *rs = engine->createScene("ssr-roughness-gate");
        View  *rv = engine->createOffscreenView("ssr-roughness-gate", 256, 256, Colour(0, 0, 0));
        if (rs && rv) {
            rv->setScene(rs);
            rs->setAmbient(Colour(0.35f, 0.36f, 0.40f), Colour(0.20f, 0.20f, 0.24f));

            // A tangent-space normal map with detail at the texel rate, unmipped
            // so the normal really does change fast from pixel to pixel - which is
            // both what patch 0022's anti-aliasing is for and what makes this
            // fixture's shading depend on the roughness the prepass carried.
            const unsigned kN = 256;
            std::vector<unsigned char> nm(kN * kN * 4);
            for (unsigned y = 0; y < kN; ++y)
                for (unsigned x = 0; x < kN; ++x) {
                    const float fx = std::sin(float(x) * 0.41f) * 0.7f;
                    const float fy = std::sin(float(y) * 0.47f) * 0.7f;
                    float nx = fx, ny = fy, nz = std::sqrt(std::max(0.0f, 1.0f - fx * fx - fy * fy));
                    unsigned char *p = &nm[(y * kN + x) * 4];
                    p[0] = (unsigned char)((nx * 0.5f + 0.5f) * 255.0f);
                    p[1] = (unsigned char)((ny * 0.5f + 0.5f) * 255.0f);
                    p[2] = (unsigned char)((nz * 0.5f + 0.5f) * 255.0f);
                    p[3] = 255;
                }
            const TextureId normalTex = rs->createTexture(kN, kN, nm.data(), /*srgb*/ false,
                                                          /*mipmaps*/ false);
            const NodeId plane = rs->createNode();
            PbrParams pp;
            pp.albedo = Colour(0.9f, 0.9f, 0.9f);
            pp.metalness = 1.0f;
            // MIRROR-SMOOTH: alpha = 0.12^2 = 0.0144, below the 0.02 the G-buffer
            // could say before ogre-patch 0043. This is the number the case turns on.
            pp.roughness = 0.12f;
            pp.uvScale[0] = 40.0f; pp.uvScale[1] = 40.0f;   // texel rate ~ pixel rate mid-frame
            const MaterialId planeMat = rs->createPbrMaterial(pp);
            // ...and a WHITE roughness map, so the material's own roughness is what
            // the shader sees (SampleRoughnessMap multiplies material.kS.w by the
            // texture) while the readback happens on UPSTREAM's own gate - i.e.
            // this case fails on an unpatched tree for the encoding alone, with no
            // argument about patch 0022 needed.
            std::vector<unsigned char> rm(4 * 4 * 4, 255);
            const TextureId roughTex = rs->createTexture(4, 4, rm.data(), false, false);
            CHECK(normalTex && roughTex && planeMat &&
                  rs->setPbrTexture(planeMat, PbrTextureSlot::Normal, normalTex) &&
                  rs->setPbrTexture(planeMat, PbrTextureSlot::Roughness, roughTex),
                  "the normal-mapped, mirror-smooth fixture exists");
            rs->attachMesh(plane, rs->createMesh(enginetest::unitCubeMesh()), planeMat);
            enginetest::setNodeScale(rs, plane, Vec3(60.0f, 0.2f, 60.0f));
            enginetest::setNodePosition(rs, plane, Vec3(0.0f, -0.1f, 0.0f));
            // A POINT light over the plane: a metal surface has no diffuse, so the
            // only thing in the frame IS the specular lobe - and the lobe's WIDTH is
            // the quantity patch 0022 changes, which is what makes this measurable.
            {
                const NodeId lamp = rs->createNode();
                LightDesc l;
                l.type = LightType::Point;
                l.colour = Colour(1.0f, 0.97f, 0.92f);
                l.intensity = 400.0f;
                l.range = 60.0f;
                rs->setLight(lamp, l);
                enginetest::setNodePosition(rs, lamp, Vec3(0.0f, 3.0f, -8.0f));
            }
            // Low and looking down the plane: it runs away from the camera, so the
            // normal map's screen-space derivative sweeps the whole range and the
            // lobe is seen at every angle from near-normal to grazing.
            enginetest::testCameraLookAt(rv, Vec3(0.0f, 2.2f, 7.0f), Vec3(0.0f, 0.0f, -14.0f));

            Image noPrepass, withPrepass;
            rv->setPostFx(PostFxDesc());
            render(engine.get(), 4);
            CHECK(rv->readPixels(noPrepass), "readPixels (no prepass)");
            PostFxDesc pfx;
            pfx.allowOffscreen = true;
            pfx.ssr = 2;                     // full-res rays, so nothing is half-res
            pfx.rayReflectRoughness = 0.0f;  // ...and the reflection is empty everywhere
            rv->setPostFx(pfx);
            render(engine.get(), 5);
            CHECK(rv->readPixels(withPrepass), "readPixels (prepass, zero confidence)");

            if (envOn("JAH_SSR_DUMP")) {
                writePpm(noPrepass, "ssr-roughgate-off.ppm");
                writePpm(withPrepass, "ssr-roughgate-on.ppm");
            }

            unsigned moved = 0, big = 0, peak = 0;
            for (unsigned y = 0; y < noPrepass.height; ++y)
                for (unsigned x = 0; x < noPrepass.width; ++x) {
                    const Colour a = noPrepass.at(x, y), b = withPrepass.at(x, y);
                    const float d = std::max(std::max(std::fabs(a.r - b.r), std::fabs(a.g - b.g)),
                                             std::fabs(a.b - b.b));
                    const unsigned lv = unsigned(d * 255.0f + 0.5f);
                    if (lv > 1) ++moved;
                    if (lv > 8) ++big;
                    if (lv > peak) peak = lv;
                }
            const unsigned total = noPrepass.width * noPrepass.height;
            std::printf("    prepass vs no prepass on a normal-mapped, mirror-smooth "
                        "surface: %u of %u px move by >1/255 (%.2f %%), %u by >8/255, peak %u\n",
                        moved, total, 100.0f * float(moved) / float(total), big, peak);
            // THE MEASURE IS THE POPULATION THAT MOVES FAR, not the one that moves
            // at all: a mirror-smooth lobe is a near-delta, and the prepass' own
            // R10G10B10A2 normals reposition it by a pixel here and there whatever
            // the roughness says (1.5 % of the frame at 1/255, four of them at the
            // full 255 on the highlight's core). A roughness the prepass could not
            // SAY is a different order of magnitude, and it is flat wrong over the
            // whole lit area. Measured on this fixture: 5424 of 65536 px past 8/255
            // without ogre-patch 0043, 5 with it.
            CHECK_MSG(big * 200u <= total,
                      "the prepass hands back the roughness it wrote "
                      "(%u of %u px differ by more than 8/255 = %.2f %%, budget 0.50 %%; "
                      "%u move at all, peak %u)",
                      big, total, 100.0f * float(big) / float(total), moved, peak);

            rv->setPostFx(PostFxDesc());
            render(engine.get(), 2);
            engine->destroyView(rv);
            engine->destroyScene(rs);
        } else {
            CHECK_MSG(false, "could not build the roughness-gate fixture");
        }
    }

    // ---- 13. WHAT THE CUTOFF IS MEASURED AGAINST ---------------------------
    //
    // THE DEFECT THIS SECTION EXISTS FOR (lane SSR-3; found by the PHOTON-R5
    // readers, ledger §432/§440). The march decoded the prepass G-buffer's
    // roughness channel with the PRE-ogre-patch-0043 range (`.y * 0.98 + 0.02`)
    // and then compared the result to its cutoff as though it were a perceptual
    // roughness. Both halves were wrong: patch 0043 packs the GGX ALPHA over
    // [0.001, 1], and the alpha is the perceptual roughness SQUARED
    // (`mPerceptualRoughness` is true on this pin). The band the frame actually
    // applied for a cutoff of 0.35 was therefore
    //
    //     0.980981 * alpha + 0.019019 > 0.35   ->   alpha > 0.3374
    //                                          ->   PERCEPTUAL > 0.581
    //
    // — half again the number anyone could read, and in a unit nothing in the
    // product is stated in. Nothing failed: the reflection appeared and
    // disappeared, just at the wrong roughness.
    //
    // HOW A PIXEL SUITE PINS A DECODE. The shader hands out no numbers, so the
    // cutoff itself is the probe. The resolve's roughness ramp is
    //
    //     roughFade = 1 - smoothstep( cutoff - feather, cutoff, roughness )
    //
    // with `feather` = kRayReflectFeather = 0.1, the one the traced half fades
    // over (EnginePrivate.h). The march is skipped outright above the cutoff, so
    // for a floor authored at a known roughness r the screen's contribution is
    // EXACTLY FULL while `cutoff - 0.1 >= r`, EXACTLY ZERO while `cutoff <= r`,
    // and in between it is a known fraction of full. That is three different
    // shapes of evidence about one number:
    //
    //     the floor is authored at   r          = 0.300 (perceptual)
    //     a correct read gives                    0.300
    //     reading the raw packed alpha gives      0.090
    //     the pre-0043 decode of it gives         0.107
    //
    //   * `cutoff = 0.29` — a correct read marches NOTHING. Both wrong reads sit
    //     below `cutoff - 0.1` and would march at FULL strength. So r >= 0.29.
    //   * `cutoff = 0.41` — the ramp's full-strength edge lands at 0.31, so a
    //     correct read must match the wide-open shot exactly. So r <= 0.31.
    //   * `cutoff = 0.35` — the floor sits at the ramp's MIDPOINT (0.25..0.35),
    //     where smoothstep is exactly 0.5, so the reflection must be HALF the
    //     wide-open one. This is the sharp one: r = 0.29 would score 0.65 of
    //     full and r = 0.31 would score 0.35 of it, so a 5 % tolerance on the
    //     half pins the value the shader read to about 0.300 +/- 0.007.
    //
    // ...and `cutoff = 0.40`, the shipped default, must leave this floor's
    // reflection WHOLE: 0.30 is a feather below the cutoff. That is the lane's
    // round-2 change in one assertion — with the old `cutoff/2` ramp the same
    // surface kept only half of it (measured, 0.424 against 0.847).
    //
    // AND THE ROW IS A UNIFORM. The five shots below run through five cutoffs
    // on one workspace; a drag of the World panel's slider must not rebuild a
    // compositor graph, and this is that assertion with a real drag in it.
    {
        Scene *cs = engine->createScene("ssr-cutoff");
        View  *cv = engine->createOffscreenView("ssr-cutoff", 256, 256, Colour(0, 0, 0));
        if (cs && cv) {
            cv->setScene(cs);
            cs->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

            const float kFloorRoughness = 0.30f;
            {
                PbrParams p;
                p.albedo = Colour(1.0f, 1.0f, 1.0f);
                p.metalness = 1.0f;
                p.roughness = kFloorRoughness;   // PERCEPTUAL: alpha 0.09
                const NodeId n = cs->createNode();
                CHECK(n && cs->attachMesh(n, cs->createMesh(enginetest::unitCubeMesh()),
                                          cs->createPbrMaterial(p)),
                      "cutoff fixture: a floor authored at perceptual roughness 0.30");
                enginetest::setNodeScale(cs, n, Vec3(12.0f, 0.2f, 12.0f));
                enginetest::setNodePosition(cs, n, Vec3(0.0f, -0.1f, 0.0f));
            }
            {
                PbrParams p;
                p.albedo = Colour(0.05f, 0.05f, 0.05f);
                p.emissive = Colour(3.0f, 0.0f, 0.0f);
                p.roughness = 0.5f;
                const NodeId n = cs->createNode();
                CHECK(n && cs->attachMesh(n, cs->createMesh(enginetest::unitCubeMesh()),
                                          cs->createPbrMaterial(p)),
                      "cutoff fixture: the emissive cube");
                enginetest::setNodeScale(cs, n, Vec3(1.5f, 1.5f, 1.5f));
                enginetest::setNodePosition(cs, n, Vec3(0.0f, 2.2f, 0.0f));
            }
            enginetest::addDirectionalLight(cs, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
            enginetest::testCameraLookAt(cv, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));

            PostFxDesc base;
            base.allowOffscreen = true;
            base.ssr = 2;                      // full-res rays: no half-res blockiness
            base.rayReflectRoughness = 1.0f;
            cv->setPostFx(base);
            render(engine.get(), 5);
            const unsigned genCut = cv->workspaceGeneration();

            auto atCutoff = [&](float cutoff, const char *what) {
                PostFxDesc p = base;
                p.rayReflectRoughness = cutoff;
                cv->setPostFx(p);
                return measure(engine.get(), cv, what, 5);
            };

            const float wide  = atCutoff(1.00f, "cutoff 1.00, wide open");
            const float full  = atCutoff(0.41f, "cutoff 0.41, the feather's full-strength edge at 0.31");
            const float shut  = atCutoff(0.29f, "cutoff 0.29, just below the floor's roughness");
            const float mid   = atCutoff(0.35f, "cutoff 0.35, the floor at the feather's midpoint");
            const float dflt  = atCutoff(0.40f, "cutoff 0.40, the shipped default");

            CHECK_MSG(wide > 0.10f,
                      "the 0.30-roughness floor reflects the cube at all (red excess %.3f)", wide);
            // THE UPPER BRACKET.
            CHECK_MSG(std::fabs(full - wide) <= 0.01f,
                      "at cutoff 0.41 the feather's full-strength edge is 0.31, so the frame "
                      "matches the wide-open one: the roughness the shader read is at most 0.31 "
                      "(%.3f vs %.3f)", full, wide);
            // THE LOWER BRACKET, and the one that kills both wrong decodes.
            CHECK_MSG(shut < 0.06f,
                      "at cutoff 0.29 NOTHING is marched: the roughness the shader read is at "
                      "least 0.29, so it is neither the raw packed alpha (0.090) nor the "
                      "pre-0043 decode of it (0.107) — either would have marched at full "
                      "strength here (red excess %.3f)", shut);
            // THE SHARP ONE: the floor sits at the ramp's midpoint, where
            // smoothstep is exactly 0.5 whatever the fixture's brightness is.
            CHECK_MSG(std::fabs(mid - 0.5f * wide) <= 0.05f * wide,
                      "at cutoff 0.35 the floor sits at the feather's MIDPOINT and scores half "
                      "the wide-open reflection: %.3f against %.3f/2 = %.3f, which pins the "
                      "roughness the shader read to 0.300 +/- 0.007 (0.29 would score %.3f, "
                      "0.31 would score %.3f)",
                      mid, wide, 0.5f * wide, 0.648f * wide, 0.352f * wide);
            // AND THE DEFAULT COSTS THIS SURFACE NOTHING, which is the whole
            // point of sharing the ray tier's feather instead of halving the
            // cutoff: 0.30 is a feather below 0.40.
            CHECK_MSG(std::fabs(dflt - wide) <= 0.01f,
                      "at the SHIPPED DEFAULT of 0.40 a satin floor at 0.30 keeps its screen "
                      "reflection WHOLE (%.3f vs %.3f) - with the old cutoff/2 ramp the same "
                      "surface kept half of it (0.424 of 0.847, measured)", dflt, wide);
            // THE DRAG.
            CHECK_MSG(cv->workspaceGeneration() == genCut,
                      "five cutoffs on one workspace: dragging the Roughness Cutoff row is a "
                      "UNIFORM and never rebuilds the chain (generation %u throughout)", genCut);

            if (envOn("JAH_SSR_DUMP")) {
                PostFxDesc p = base;
                p.rayReflectRoughness = 1.0f;
                cv->setPostFx(p);
                render(engine.get(), 5);
                Image img;
                if (cv->readPixels(img)) writePpm(img, "ssr-cutoff-wide.ppm");
                p.rayReflectRoughness = 0.29f;
                cv->setPostFx(p);
                render(engine.get(), 5);
                if (cv->readPixels(img)) writePpm(img, "ssr-cutoff-shut.ppm");
            }

            cv->setPostFx(PostFxDesc());
            render(engine.get(), 2);
            engine->destroyView(cv);
            engine->destroyScene(cs);
        } else {
            CHECK_MSG(false, "could not build the cutoff fixture");
        }
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall SSR checks passed\n", failures);
    return failures ? 1 : 0;
}
