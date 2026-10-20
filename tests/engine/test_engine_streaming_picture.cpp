// frame.streaming_picture — A FRAME WITH A HOLE IS PRESENTED: THE STREAMING PICTURE, NEVER A HELD PLATE (BUGS-7 stage 2).
// The held frame (ASYNC-SHADERS-1's AsyncHoldListener, deleted) kept the LAST PRESENTED picture on screen while a frame
// drew a hole — a draw with neither its shader nor a placeholder (an unlit draw: unlit has none, on purpose), a deferred
// compute permutation, a lighting arm owing its rebuild. On a first open the last presented picture is the empty view's
// clear colour, so the window showed that plate while the world streamed in (Bistro: 26 frames held in a row at today's
// base, 18-36 s before U4; spikes/bugs-7/stage0). Now every frame is presented: a waiting PBS draw shows its placeholder,
// the unlit draw is absent until its permutation lands.
//
// THE SCRIPT, on a WINDOW view on the rig's display (the hold only ever withheld a swap) with the background compiler on
// and NO persistent shader cache (the subject is a cold permutation):
//  1. the placeholders: scene A (a sun, a PBS cube, an unlit cube) drawn with placeholders-only until nothing pends —
//     the passes' placeholders are built, the cube's own PBS permutation and the unlit one are NOT;
//  2. the reveal's plate: an EMPTY scene B bound and presented (the photograph of the window: the view's clear colour);
//  3. the stream-in: B gets the same three things; each next frame is photographed from the X root.
// THE LAWS, IN FRAMES: a hole existed (an unlit draw skipped while pending) and placeholders drew; the first photograph
// showing geometry is within kFirstGeometryFrames of the stream-in (the swap's own latency); no later photograph is the
// plate. RED on the held frame: the plate stays on screen until every permutation of the hole has landed.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

namespace streamingpicture {
bool openWindow(unsigned w, unsigned h, void **display, unsigned long *window);
bool grab(void *display, unsigned w, unsigned h, std::vector<unsigned char> &rgb);
void closeWindow(void *display, unsigned long window);
}

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

static constexpr unsigned kW = 256, kH = 256;
/// The frames a presented image takes to reach the X server's root (the swapchain's own queue) — the photograph of
/// frame k may be of frame k-2.
static constexpr int kFirstGeometryFrames = 3;
/// The frames watched after the stream-in begins.
static constexpr int kWatchFrames = 40;
/// A pixel is the plate's when every channel is within this of the plate's colour (8-bit codes: the dither of a
/// presented clear is 0).
static constexpr int kPlateCodes = 3;
/// The photograph shows GEOMETRY when this share of its centre region is not the plate (the cube covers ~30 %).
static constexpr double kGeometryShare = 0.05;

struct Rgb { int r = 0, g = 0, b = 0; };

/// The photograph, kept beside the row's log (a PPM: the evidence a red is read from).
static void dump(const std::vector<unsigned char> &rgb, const char *name) {
    if (FILE *f = std::fopen(name, "wb")) {
        std::fprintf(f, "P6\n%u %u\n255\n", kW, kH);
        std::fwrite(rgb.data(), 1, rgb.size(), f);
        std::fclose(f);
    }
}

static double nonPlateShare(const std::vector<unsigned char> &rgb, const Rgb &plate) {
    // the centre half of the window (the cube's region)
    unsigned n = 0, off = 0;
    for (unsigned y = kH / 4; y < 3 * kH / 4; ++y)
        for (unsigned x = kW / 4; x < 3 * kW / 4; ++x) {
            const unsigned char *p = &rgb[(size_t(y) * kW + x) * 3u];
            ++n;
            if (std::abs(p[0] - plate.r) > kPlateCodes || std::abs(p[1] - plate.g) > kPlateCodes ||
                std::abs(p[2] - plate.b) > kPlateCodes)
                ++off;
        }
    return n ? double(off) / double(n) : 0.0;
}

/// The stream-in's world: a sun, a PBS cube whose permutation was never built (its emissive term - a piece of its
/// own - is not phase 1's), and an unlit cube (unlit has no placeholder: its draw is a hole until its permutation lands).
static void addWorld(Scene *s, MaterialId *unlitOut) {
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    const NodeId c = s->createNode();
    const MeshId cm = c ? s->createMesh(enginetest::unitCubeMesh()) : 0;
    PbrParams p;
    p.albedo = Colour(0.85f, 0.2f, 0.15f);
    p.roughness = 0.5f;
    p.emissive = Colour(0.3f, 0.0f, 0.0f);
    const MaterialId cmat = cm ? s->createPbrMaterial(p) : 0;
    if (cmat) s->attachMesh(c, cm, cmat);
    const NodeId u = s->createNode();
    const MeshId m = u ? s->createMesh(enginetest::unitCubeMesh()) : 0;
    const MaterialId mat = m ? s->createUnlitMaterial(Colour(0.1f, 0.9f, 0.2f), true) : 0;
    if (mat && s->attachMesh(u, m, mat)) enginetest::setNodePosition(s, u, Vec3(1.4f, 0.0f, -0.6f));
    if (unlitOut) *unlitOut = mat;
}

static void printStats(Engine *e, const char *when) {
    e->latchSnapshot();
    const AsyncShaderStats s = e->published().shaders;
    std::printf("    [%s] async: pending %u, completed %llu, placeholder draws %llu, pending skips %llu\n", when, s.pending,
                s.completed, s.placeholderDraws, s.pendingSkips);
}

int main()
{
    std::printf("== frame.streaming_picture: a frame with a hole is presented - the streaming picture, never a held plate\n");
    void *display = nullptr;
    unsigned long window = 0;
    if (!streamingpicture::openWindow(kW, kH, &display, &window)) {
        std::printf("FAIL: no X display (DISPLAY must name the rig's Xvfb)\n");
        return 1;
    }
    EngineConfig cfg = enginetest::testEngineConfig(
        "test-streaming-picture-ogre.log",
        enginetest::EngineOverrides()
            .display(static_cast<NativeDisplayHandle>(reinterpret_cast<unsigned long long>(display)))
            .shaderCacheOff());
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createView("streaming", static_cast<NativeWindowHandle>(window), kW, kH, Colour(0.10f, 0.11f, 0.14f));
    if (!view) { std::printf("FAIL: createView: %s\n", e->lastError().c_str()); return 1; }
    view->setAsyncShaders(true);
    const CameraDesc cam = enginetest::testCameraDescLookAt(Vec3(1.8f, 1.4f, 2.4f), Vec3(0.0f, 0.25f, 0.0f));

    // ---- 1. the placeholders, built (a sun and a plain PBS cube, placeholders only) -----------------------------
    Scene *a = e->createScene("placeholders");
    enginetest::addDirectionalLight(a, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    enginetest::addTestCube(a, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.6f);
    view->setScene(a);
    view->setCamera(cam);
    e->setAsyncShaderPlaceholdersOnly(true);
    for (int i = 0; i < 3; ++i) { enginetest::frames(e, 2); e->waitForAsyncShaders(); }
    e->setAsyncShaderPlaceholdersOnly(false);
    printStats(e, "placeholders built");

    // ---- 2. the reveal's plate: an empty scene, presented -------------------------------------------------------
    // The plate is the view's clear colour: a corner of the window (no scene ever draws there from this camera).
    Scene *b = e->createScene("streaming");
    view->setScene(nullptr);   // a view shows one scene: detached first
    CHECK(view->setScene(b), "the empty scene bound to the window");
    view->setCamera(cam);
    std::vector<unsigned char> rgb;
    Rgb plate;
    double emptyShare = 1.0;
    int plateFrames = 0;
    for (; plateFrames < 120 && emptyShare >= 0.001; ++plateFrames) {
        enginetest::frames(e, 1);
        if (plateFrames % 10 == 9) e->waitForAsyncShaders();
        if (!streamingpicture::grab(display, kW, kH, rgb)) { std::printf("FAIL: grab\n"); return 1; }
        plate = Rgb{ rgb[0], rgb[1], rgb[2] };
        emptyShare = nonPlateShare(rgb, plate);
    }
    dump(rgb, "streaming_picture_plate.ppm");
    std::printf("    the empty scene's picture: the plate (%d, %d, %d), %.4f of the centre off it, after %d frames\n",
                plate.r, plate.g, plate.b, emptyShare, plateFrames);
    printStats(e, "the plate");
    CHECK(emptyShare < 0.001, "the empty scene's plate reached the screen (the picture the stream-in must leave at once)");

    // ---- 3. the stream-in -------------------------------------------------------------------------------------------
    e->latchSnapshot();
    const AsyncShaderStats s0 = e->published().shaders;
    MaterialId unlit = 0;
    addWorld(b, &unlit);
    int firstGeometry = -1, platesAfter = 0;
    std::string seq;
    for (int k = 1; k <= kWatchFrames; ++k) {
        enginetest::frames(e, 1);
        if (!streamingpicture::grab(display, kW, kH, rgb)) { std::printf("FAIL: grab\n"); return 1; }
        const double share = nonPlateShare(rgb, plate);
        const bool geometry = share >= kGeometryShare;
        if (geometry && firstGeometry < 0) firstGeometry = k;
        if (!geometry && k > kFirstGeometryFrames) ++platesAfter;
        seq += geometry ? 'G' : 'p';
        if (k == 1) dump(rgb, "streaming_picture_frame1.ppm");
        if (k == kWatchFrames) dump(rgb, "streaming_picture_last.ppm");
    }
    e->latchSnapshot();
    const AsyncShaderStats s1 = e->published().shaders;
    std::printf("    the photographs, frame by frame after the stream-in (G geometry, p the plate): %s\n", seq.c_str());
    std::printf("    pending skips %llu, placeholder draws %llu, pending now %u\n", s1.pendingSkips - s0.pendingSkips,
                s1.placeholderDraws - s0.placeholderDraws, s1.pending);
    CHECK(unlit != 0, "the unlit cube (the hole: unlit has no placeholder) was made");
    CHECK(s1.pendingSkips > s0.pendingSkips, "a HOLE existed: a draw skipped while its permutation built (%llu)",
          s1.pendingSkips - s0.pendingSkips);
    CHECK(s1.placeholderDraws > s0.placeholderDraws, "the waiting PBS cube drew its placeholder (%llu draws)",
          s1.placeholderDraws - s0.placeholderDraws);
    CHECK(firstGeometry >= 1 && firstGeometry <= kFirstGeometryFrames,
          "the first photograph showing geometry is frame %d of the stream-in (bar %d)", firstGeometry,
          kFirstGeometryFrames);
    CHECK(platesAfter == 0, "no photograph after frame %d is the plate (%d were)", kFirstGeometryFrames, platesAfter);

    view->setScene(nullptr);
    e->destroyScene(a);
    e->destroyScene(b);
    e->destroyView(view);
    enginetest::frames(e, 1);
    engine.reset();
    streamingpicture::closeWindow(display, window);
    std::printf(failures ? "frame.streaming_picture: FAILED\n" : "frame.streaming_picture: PASS\n");
    return failures ? 1 : 0;
}
