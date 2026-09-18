// engine.lod_hysteresis — THE LOD SWITCH BAND BELONGS TO THE PASS
// (ogre-patch 0075 as amended by lane ATOM-3-FIX; the render audit's A7, and
// the Fable read of ATOM-3, ledger §664 finding 1).
//
// THE CLAIM UNDER TEST, in one sentence: the switch band holds a level against
// a small move of the WATCHED view and cannot be moved by any OTHER pass that
// renders the same scene in the same frame.
//
// WHY THAT SECOND HALF NEEDS A SUITE. `LodStrategy::lodSet` writes
// `MovableObject::mCurrentMeshLod`, and EVERY pass that updates LOD lists
// writes it: a planar reflector's mirrored camera, a picture-in-picture inset,
// a reflection-probe cube face, a thumbnail. Patch 0075's first version made
// the band process-wide and used that one slot as its direction state, so the
// view's band was measured against another camera's level — it could hold a
// level the view never chose, and its memory of the direction of travel was
// erased by every sibling pass. Nothing measured it, because the lane's
// fixture had one mesh, one camera and one workspace. This suite has two.
//
// HOW IT MEASURES A LEVEL WITHOUT MOVING THE CAMERA. `Scene::setLodBias`
// divides every threshold (OgreScene::applyLodValues), so sweeping the bias at
// a FIXED pose walks the object's value across a switch exactly as a dolly
// would — and the picture then differs by the LEVEL ONLY, which makes an exact
// pixel comparison the observable. Nothing here waits on wall-clock time: every
// step is one frame, and the band's own direction dependence means the sweep
// must be MONOTONE (a bisection would measure its own path).
//
// THE CASES:
//   1. THE BAND IS A LOOP, and the picture says how wide: sweeping the bias UP
//      switches at (1 + h) x the exact threshold, sweeping back DOWN switches
//      at (1 - h) x it, so the ratio of the two measured biases gives h — which
//      must be the 0.10 the engine asks for.
//   1b. AND THE HELD LEVEL IS A VISIBLE CHOICE: at one parked bias inside the
//      band the picture approached from below (held fine) and the picture
//      approached from above (held coarse) are different pictures. Without this
//      case 2's equality would be satisfied by a fixture that cannot tell the
//      levels apart at all.
//   2. A SIBLING PASS CANNOT MOVE THE VIEW'S LEVEL. With the view parked inside
//      the band, a PiP inset — a second scene pass on the SAME scene, with its
//      own camera far away, rendering every frame — is switched on: the view's
//      picture outside the inset must not move by one bit, while the inset
//      region must (or the sibling never rendered and the case proves nothing).
//      FAILS BEFORE: with the band on every pass and one shared state slot, the
//      inset's coarse level reaches the view and 1,760 pixels move (the whole level-0/level-1 difference at 192²; measured).
//   3. THE BAND IS BOUNDED: a bias genuinely past it switches, and the way back
//      to the finest level is exact.
//
// THE OFFSCREEN OPT-IN. A band is only ever given to a view a person watches
// over time (OgreView::chainDesc), and `View::readPixels` refuses an on-screen
// view — its target is a swapchain. So the one kind of view whose pixels a test
// can read is the one kind that has no band, and
// `View::setLodHysteresisOffscreen(true)` exists for this suite: it grants the
// band to THIS view, right after it is created (LOD-LATCH-1, 2026-09-18 — it
// used to be the process-wide env latch JAHSHAKA_LOD_HYSTERESIS_OFFSCREEN, now
// deleted). Nothing else in the tree asks for it — which is why every
// thumbnail, preview, screenshot and pixel suite keeps taking the exact level
// its own value asks for.
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

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

static const unsigned kSize = 192;
static const float kBand = 0.10f;   // what OgreMesh.cpp's kLodHysteresis asks for

// ---------------------------------------------------------------------------
// THE FIXTURE: an 8x8 m sine relief, 64x64 quads, plus three coarser index
// lists over THE SAME VERTICES (32x32, 16x16, 8x8) — the shape ATOM stage 1's
// bake produces, and the reason a level change costs no draw call. The relief's
// wavelength (1.33 m) is resolved by level 0 and aliased by every level below
// it, and the normals are analytic, so a level change moves both the silhouette
// and the shading: the levels are different PICTURES, which is what this suite
// reads. The errors are stated rather than derived, so the switch arithmetic is
// exact: the strategy's value is a world-space error and these ARE the
// thresholds (divided by the bias).
static const float kErr1 = 0.20f, kErr2 = 0.80f, kErr3 = 3.20f;

static MeshData reliefMesh()
{
    const int N = 64;
    const float span = 8.0f, amp = 0.6f, waves = 6.0f;
    const float k = waves * 3.14159265f / span;     // radians per metre
    MeshData d;
    for (int z = 0; z <= N; ++z) {
        for (int x = 0; x <= N; ++x) {
            const float px = (float(x) / float(N) - 0.5f) * span;
            const float pz = (float(z) / float(N) - 0.5f) * span;
            d.positions.insert(d.positions.end(),
                               { px, amp * std::sin(k * px) * std::cos(k * pz), pz });
            // The analytic normal of that surface, normalised: dy/dx and dy/dz.
            const float nx = -amp * k * std::cos(k * px) * std::cos(k * pz);
            const float nz = amp * k * std::sin(k * px) * std::sin(k * pz);
            const float len = std::sqrt(nx * nx + 1.0f + nz * nz);
            d.normals.insert(d.normals.end(), { nx / len, 1.0f / len, nz / len });
        }
    }
    auto build = [&](int step) {
        std::vector<unsigned> idx;
        for (int z = 0; z < N; z += step)
            for (int x = 0; x < N; x += step) {
                const unsigned a = unsigned(z * (N + 1) + x);
                const unsigned b = unsigned(z * (N + 1) + x + step);
                const unsigned c = unsigned((z + step) * (N + 1) + x + step);
                const unsigned e = unsigned((z + step) * (N + 1) + x);
                idx.insert(idx.end(), { a, b, c, a, c, e });
            }
        return idx;
    };
    d.indices = build(1);
    d.lodIndices.push_back(build(2));
    d.lodIndices.push_back(build(4));
    d.lodIndices.push_back(build(8));
    d.lodErrors = { kErr1, kErr2, kErr3 };
    return d;
}

// ---- pictures -------------------------------------------------------------
static Engine *gEngine = nullptr;
static View   *gView = nullptr;

static Image shot(int frames = 1)
{
    for (int i = 0; i < frames; ++i) gEngine->renderOneFrame();
    Image img;
    if (!gView->readPixels(img)) { std::printf("FAIL: readPixels\n"); ++failures; }
    return img;
}

/// Pixels that differ, exactly, optionally skipping a rectangle in normalised
/// coordinates (the PiP inset, which is a second picture and not the subject).
static size_t differing(const Image &a, const Image &b,
                        float skipL = 2.0f, float skipT = 2.0f, float skipR = 2.0f, float skipB = 2.0f)
{
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) return size_t(-1);
    const unsigned l = unsigned(skipL * float(a.width)), r = unsigned(skipR * float(a.width));
    const unsigned t = unsigned(skipT * float(a.height)), bo = unsigned(skipB * float(a.height));
    size_t n = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            if (x >= l && x < r && y >= t && y < bo) continue;
            const size_t i = (size_t(y) * a.width + x) * 4u;
            if (a.rgba[i] != b.rgba[i] || a.rgba[i+1] != b.rgba[i+1] ||
                a.rgba[i+2] != b.rgba[i+2] || a.rgba[i+3] != b.rgba[i+3]) ++n;
        }
    return n;
}

/// Pixels that differ INSIDE that rectangle.
static size_t differingInside(const Image &a, const Image &b,
                              float l0, float t0, float r0, float b0)
{
    if (a.width != b.width || a.height != b.height) return size_t(-1);
    const unsigned l = unsigned(l0 * float(a.width)), r = unsigned(r0 * float(a.width));
    const unsigned t = unsigned(t0 * float(a.height)), bo = unsigned(b0 * float(a.height));
    size_t n = 0;
    for (unsigned y = t; y < bo && y < a.height; ++y)
        for (unsigned x = l; x < r && x < a.width; ++x) {
            const size_t i = (size_t(y) * a.width + x) * 4u;
            if (a.rgba[i] != b.rgba[i] || a.rgba[i+1] != b.rgba[i+1] ||
                a.rgba[i+2] != b.rgba[i+2] || a.rgba[i+3] != b.rgba[i+3]) ++n;
        }
    return n;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-lod-hysteresis-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    gEngine = engine.get();

    gView = gEngine->createOffscreenView("lodband", kSize, kSize, Colour(0.02f, 0.02f, 0.03f));
    // See the file header: the band is a watched view's, and only an offscreen
    // view's pixels can be read. This view asks for it, and nothing else does.
    if (gView) gView->setLodHysteresisOffscreen(true);
    Scene *scene = gEngine->createScene("lodband");
    if (!gView || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    gView->setScene(scene);
    scene->setAmbient(Colour(0.25f, 0.25f, 0.3f), Colour(0.2f, 0.2f, 0.25f));
    enginetest::addDirectionalLight(scene, Vec3(-0.45f, -0.8f, -0.4f), 3.14159f);

    const MeshId mesh = scene->createMesh(reliefMesh());
    PbrParams p; p.albedo = Colour(0.8f, 0.55f, 0.35f); p.metalness = 0.0f; p.roughness = 0.55f;
    const MaterialId mat = scene->createPbrMaterial(p);
    const NodeId node = scene->createNode();
    CHECK(mesh && mat && node && scene->attachMesh(node, mesh, mat),
          "a four-level relief mesh is attached (8192 / 2048 / 512 / 128 triangles)");

    const Vec3 eye(0.0f, 6.0f, 14.0f), look(0.0f, 0.0f, 0.0f);
    enginetest::testCameraLookAt(gView, eye, look);

    scene->setLodBias(1.0f);
    const Image p0 = shot(3);
    CHECK(p0.width == kSize && p0.height == kSize, "the view reads back %ux%u", p0.width, p0.height);

    // =====================================================================
    // 1. THE BAND IS A LOOP, AND THE PICTURE MEASURES IT
    // =====================================================================
    // Every step is ONE frame at a fixed pose; only the thresholds move. The
    // sweep is monotone in both directions because a band makes the answer
    // depend on the path (ATOM-3's lesson: every LOD probe dollies outward).
    const float kStep = 1.005f, kLo = 3.0f, kHi = 8.0f;
    float up = 0.0f, down = 0.0f;
    for (float b = kLo; b <= kHi; b *= kStep) {
        scene->setLodBias(b);
        if (differing(shot(), p0) != 0) { up = b; break; }
    }
    CHECK(up > 0.0f, "sweeping the bias UP switches the level, at bias %.4f", up);

    scene->setLodBias(kHi);
    const Image pCoarse = shot(2);
    CHECK(differing(pCoarse, p0) > 0, "and the coarse level is a different picture (%zu px)",
          differing(pCoarse, p0));
    for (float b = kHi; b >= kLo; b /= kStep) {
        scene->setLodBias(b);
        if (differing(shot(), p0) == 0) { down = b; break; }
    }
    CHECK(down > 0.0f, "sweeping it back DOWN returns to the finest level, at bias %.4f", down);

    if (!(up > 0.0f && down > 0.0f)) {
        std::printf("FAIL: no switch found — the fixture cannot measure the band\n");
        return 1;
    }
    // up = (1+h) x the exact threshold's bias, down = (1-h) x it.
    const float ratio = up / down;
    const float measured = (ratio - 1.0f) / (ratio + 1.0f);
    std::printf("   up %.4f / down %.4f = %.4f -> h = %.4f (asked for %.2f)\n",
                up, down, ratio, measured, kBand);
    CHECK(up > down, "THE SWITCH IS DIRECTION-DEPENDENT: %.4f going out, %.4f coming back",
          up, down);
    CHECK(std::fabs(measured - kBand) < 0.012f,
          "and the width the pictures report IS the engine's band (%.4f vs %.2f)",
          measured, kBand);

    // The exact (unbanded) switch bias, and a park 4 % into the band above it.
    const float exact = up / (1.0f + kBand);
    const float park = exact * 1.04f;

    // =====================================================================
    // 1b. THE HELD LEVEL IS A VISIBLE CHOICE
    // =====================================================================
    scene->setLodBias(kHi);  shot(1);                 // state: coarse
    scene->setLodBias(park);
    const Image parkFromAbove = shot(2);
    scene->setLodBias(1.0f); shot(1);                 // state: finest
    scene->setLodBias(park);
    const Image parkFromBelow = shot(2);
    const size_t parkSpread = differing(parkFromAbove, parkFromBelow);
    CHECK(differing(parkFromBelow, p0) == 0,
          "at bias %.4f, approached from below, the band HOLDS the fine level (0 px from it)", park);
    CHECK(parkSpread > 100,
          "and approached from above it holds the coarse one — the same pose, two pictures (%zu px)",
          parkSpread);

    // =====================================================================
    // 2. A SIBLING PASS CANNOT MOVE THE VIEW'S LEVEL
    // =====================================================================
    // The state is now "fine, held at `park`". The PiP inset is a second
    // PASS_SCENE on the SAME scene with its own camera 60 m away and its own
    // 56-pixel-tall target, so its exact level is several steps coarser than
    // the view's — and it is built by `chain::buildPip`, which never carries a
    // band. Its picture lives in the inset rectangle; the VIEW is everything
    // outside it.
    ViewPipDesc pip;
    pip.enabled = true;
    pip.allowOffscreen = true;            // the same word as PostFxDesc's — suites only
    pip.left = 0.70f; pip.top = 0.70f; pip.width = 0.29f; pip.height = 0.29f;
    pip.background = Colour(0.0f, 0.0f, 0.0f, 1.0f);
    pip.camera = enginetest::testCameraDescLookAt(Vec3(0.0f, 20.0f, 60.0f), look);
    gView->setPip(pip);
    const Image withPip = shot(6);
    const float sl = pip.left - 0.01f, st = pip.top - 0.01f;     // one-pixel margin
    const size_t viewMoved = differing(withPip, parkFromBelow, sl, st, 1.0f, 1.0f);
    const size_t insetMoved = differingInside(withPip, parkFromBelow, pip.left, pip.top, 1.0f, 1.0f);
    CHECK(insetMoved > 100,
          "the sibling pass really is rendering — the inset region moved (%zu px)", insetMoved);
    CHECK(viewMoved == 0,
          "THE VIEW'S PICTURE DID NOT MOVE BY ONE BIT while another camera's pass rendered the "
          "same object at another level (%zu px outside the inset)", viewMoved);

    // ...and nothing lingers when the sibling goes away.
    ViewPipDesc off; off.enabled = false; off.allowOffscreen = true;
    gView->setPip(off);
    CHECK(differing(shot(4), parkFromBelow) == 0,
          "and with the inset gone the view is exactly the picture it was");

    // =====================================================================
    // 3. THE BAND IS BOUNDED
    // =====================================================================
    scene->setLodBias(exact * 1.5f);
    CHECK(differing(shot(2), p0) > 0,
          "a bias genuinely past the band switches (1.5x the exact threshold)");
    scene->setLodBias(1.0f);
    CHECK(differing(shot(2), p0) == 0,
          "and bias 1 is the finest level again, exactly");

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
