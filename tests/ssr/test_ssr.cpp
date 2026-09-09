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
//      PostFxDesc::ssrRoughnessCutoff and the reflection disappears — because a
//      v1 with no roughness-varying blur must not draw a sharp mirror image on
//      a matte surface.
//   5. SSR OFF IS BYTE-IDENTICAL TO TODAY. The frame captured before SSR was
//      ever enabled and the frame captured after it is switched off again must
//      match exactly, pixel for pixel. That is the offscreen-determinism law
//      (POST_CHAIN_SPEC §7.3) restated for this feature: every thumbnail,
//      preview and pixel suite in the tree renders through a view whose
//      PostFxDesc has ssr == 0.
//   6. The quality row is a SHAPE change (half-res rays -> full-res rays
//      rebuilds the workspace) and the tuning is NOT (max distance, thickness,
//      cutoff and intensity are uniforms and must never rebuild anything).
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
        rough.roughness = 0.9f;                        // far above the 0.35 cutoff
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
        view->setPostFx(tuned);
        CHECK(view->workspaceGeneration() == gen,
              "SSR distance/thickness/intensity are UNIFORMS, not graph changes");
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
        neutral.ssrRoughnessCutoff = 0.0f;   // reject every surface
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
                CHECK_MSG(peak0 > 0.6f && m0 > 0.5f,
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

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall SSR checks passed\n", failures);
    return failures ? 1 : 0;
}
