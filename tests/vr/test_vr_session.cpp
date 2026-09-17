// vr.session — the VR SESSION, in the engine (SPECS/VR_SPEC.md §6, phase 2).
//
// WHAT IT IS FOR. `vr.spike_1a` beside it proves the FLOOR: that an engine
// booted on a device an OpenXR runtime created renders the same picture as one
// on a device Ogre created (ogre-patch 0068). This suite proves the PHASE-2
// engine on top of it:
//
//   1. A session begins on the simulated runtime, walks the runtime's own
//      lifecycle to FOCUSED and submits frames the runtime ACCEPTS.
//   2. Both eyes are really rendered, into one target two eyes wide, by ONE
//      scene pass — instanced stereo, which is a first for this pin on Vulkan.
//   3. The engine's own picture is not disturbed by any of it: the desktop
//      view renders the same bytes before a session and after one, in the same
//      process.
//
// HOW (2) IS PROVED EXACTLY, and this is the part worth reading. "Both eyes
// drew" is easy to assert loosely and hard to assert exactly: the two halves of
// a stereo frame are SUPPOSED to differ, so no equality holds between them —
// and a right eye that never drew, or that drew the left eye's picture, or that
// drew at the wrong viewport, all fail different loose thresholds. The exact
// statement is a CONTROL: `VrConfig::worldScale` scales the eye offsets, so at
// worldScale ZERO the two eyes are at the same place and looking the same way,
// and their two pictures must be BYTE-IDENTICAL — that can only be true if both
// halves were rendered, by the same shader, through per-eye matrices that agree
// when the eyes agree, into exactly half the target each. At worldScale ONE the
// same two halves must DIFFER, which is the parallax. Two exact assertions, no
// threshold anywhere, and between them they catch every failure mode above.
//
// SKIPS (exit 77) with no monado-service, no manifest, or no display: a box
// without a VR stack is a supported box. The runtime is named EXPLICITLY by the
// runner and asserted here — the user-level manifest belongs to whatever
// headset last connected (VR_SPEC §2.6).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using namespace enginetest;

namespace {

int gFailures = 0, gChecks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++gChecks;                                                               \
        if (!(cond)) { ++gFailures; std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
        else std::printf("    ok   %s\n", #cond);                                \
    } while (0)
/// An EXPRESSION, not a statement: several checks below guard the work that
/// follows them ("if the control rendered at all, compare it"), and a macro
/// that cannot be tested would have to be written twice at every such site.
#define CHECK_MSG(cond, ...)                                                     \
    ([&]() -> bool {                                                             \
        ++gChecks;                                                               \
        const bool ok_ = (cond);                                                 \
        if (!ok_) {                                                              \
            ++gFailures;                                                         \
            std::printf("    FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__); std::printf("\n");                         \
        } else { std::printf("    ok   %s — ", #cond); std::printf(__VA_ARGS__); std::printf("\n"); } \
        return ok_;                                                              \
    }())
#define REQUIRE(cond)                                                            \
    do { const bool ok_ = (cond); CHECK(ok_); if (!ok_) return 1; } while (0)

EngineConfig vrConfig() {
    EngineConfig cfg;
    cfg.backend      = Backend::Vulkan;
    cfg.pluginDir    = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile      = "test_vr_session-ogre.log";
    cfg.vr           = VrMode::IfAvailable;
    return cfg;
}

/// A scene with something in it that has PARALLAX: three cubes at three depths,
/// lit. A single object at the centre of the view moves by a fraction of a
/// pixel between eyes at 6 cm of separation and 320 px of width — the stereo
/// difference has to be visible for the worldScale-one half of the control to
/// mean anything, and near geometry is what makes it so.
void buildScene(Scene *scene) {
    addDirectionalLight(scene, Vec3{ -0.4f, -1.0f, -0.55f }, 3.14159f);
    // A NEAR SILHOUETTE AGAINST A FAR SURFACE, AND BOTH TALL. The parallax the
    // eyes' 6 cm produces is only visible where a near edge crosses a far one,
    // and a runtime's head sits where IT decides — Monado's simulated HMD puts
    // it at the stage's origin, i.e. on the floor, so a fixture built at a
    // person's eye height is out of frame and the picture is all floor (which
    // is how the first version of this scene measured 6 bytes of parallax and
    // called it stereo). A pillar in front of a wall, both spanning several
    // metres vertically, is in frame from any head height.
    const NodeId pillar = addTestCube(scene, Colour{ 0.80f, 0.25f, 0.15f, 1.0f }, 0.0f, 0.45f);
    setNodePosition(scene, pillar, Vec3{ 0.35f, 1.0f, -1.0f });
    setNodeScale(scene, pillar, Vec3{ 0.25f, 4.0f, 0.25f });
    const NodeId wall = addTestCube(scene, Colour{ 0.20f, 0.55f, 0.85f, 1.0f }, 0.0f, 0.55f);
    setNodePosition(scene, wall, Vec3{ 0.0f, 1.5f, -4.0f });
    setNodeScale(scene, wall, Vec3{ 12.0f, 8.0f, 0.2f });
    const NodeId floor = addTestCube(scene, Colour{ 0.35f, 0.36f, 0.38f, 1.0f }, 0.0f, 0.9f);
    setNodePosition(scene, floor, Vec3{ 0.0f, -0.05f, 0.0f });
    setNodeScale(scene, floor, Vec3{ 40.0f, 0.1f, 40.0f });
}

struct Half { std::vector<unsigned char> px; unsigned w = 0, h = 0; };

/// Splits a both-eyes readback down the middle.
bool splitEyes(const Image &img, Half &left, Half &right) {
    if (img.width < 2u || img.height < 1u || img.rgba.size() < size_t(img.width) * img.height * 4u)
        return false;
    const unsigned half = img.width / 2u;
    left.w = right.w = half;
    left.h = right.h = img.height;
    left.px.resize(size_t(half) * img.height * 4u);
    right.px.resize(left.px.size());
    for (unsigned y = 0; y < img.height; ++y) {
        const unsigned char *row = &img.rgba[size_t(y) * img.width * 4u];
        std::memcpy(&left.px[size_t(y) * half * 4u], row, size_t(half) * 4u);
        std::memcpy(&right.px[size_t(y) * half * 4u], row + size_t(half) * 4u, size_t(half) * 4u);
    }
    return true;
}

size_t differingBytes(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b,
                      int *worstOut = nullptr) {
    size_t n = 0; int worst = 0;
    const size_t m = std::min(a.size(), b.size());
    for (size_t i = 0; i < m; ++i) {
        const int d = std::abs(int(a[i]) - int(b[i]));
        if (d) ++n;
        if (d > worst) worst = d;
    }
    if (worstOut) *worstOut = worst;
    return n;
}

/// How much of an image is not the corner pixel — "something was drawn".
double drawnFraction(const std::vector<unsigned char> &px) {
    if (px.size() < 4u) return 0.0;
    const unsigned char c0 = px[0], c1 = px[1], c2 = px[2];
    size_t n = 0;
    for (size_t i = 0; i < px.size(); i += 4)
        if (px[i] != c0 || px[i + 1] != c1 || px[i + 2] != c2) ++n;
    return double(n) / double(px.size() / 4);
}

/// Renders until the picture STOPS MOVING, then hands it back — never a fixed
/// number of frames and never a wall-clock settle.
///
/// The desktop A/B below compares a picture taken before a session with one
/// taken after it, in one process, and the FIRST of those is taken on a
/// possibly COLD shader cache: a frame rendered while a permutation is still
/// compiling is not the frame the same view renders a second later, so a fixed
/// "render three frames" produced a difference that had nothing to do with VR
/// (seen once, on the first run after a rebuild — CLAUDE.md's cold-cache
/// class). Reading until two consecutive readbacks agree is the same lesson
/// cameras.exposure learned: count frames, or read until the value holds still.
bool stableReadback(Engine *e, View *v, Image &out, unsigned budget = 120u) {
    Image prev;
    if (!v->readPixels(prev)) return false;
    for (unsigned i = 0; i < budget; ++i) {
        e->renderOneFrame();
        if (!v->readPixels(out)) return false;
        if (out.rgba == prev.rgba && !out.rgba.empty()) return true;
        prev = out;
    }
    return false;
}

/// Pumps the session until it has submitted `want` frames, bounded by a FRAME
/// budget and never by wall time (VR_SPEC §6 flake class (b): a loaded box
/// makes a clock lie, it does not make a counter lie).
unsigned long long pump(Engine *e, unsigned long long want, unsigned budget) {
    for (unsigned i = 0; i < budget && e->vrStatus().frames < want; ++i) {
        e->advanceResources();
        e->renderOneFrame();
    }
    return e->vrStatus().frames;
}

}  // namespace

int main() {
    std::printf("vr.session — the VR session in the engine (VR_SPEC §6)\n");

    std::unique_ptr<Engine> engine;
    {
        std::string error;
        engine.reset(Engine::create(vrConfig(), error).release());
        if (!engine) { std::printf("SKIP: the engine did not start: %s\n", error.c_str()); return 77; }
    }
    const VrInfo info = engine->vrInfo();
    std::printf("RUNTIME '%s' %s | system '%s' | OpenXR %u.%u | eye %ux%u | mask=%d depth=%d\n",
                info.runtime.c_str(), info.runtimeVersion.c_str(), info.system.c_str(),
                info.apiMajor, info.apiMinor, info.eyeWidth, info.eyeHeight,
                int(info.visibilityMask), int(info.depthLayer));
    if (!engine->vrAvailable()) {
        std::printf("SKIP: no OpenXR session-capable runtime: %s\n", info.reason.c_str());
        return 77;
    }
    // THE MANIFEST LAW (VR_SPEC §2.6): the runner names the runtime and this
    // asserts it, so a gate can never silently run against whatever the owner's
    // headset last wrote into ~/.config/openxr/1/.
    if (const char *want = std::getenv("JAH_VR_EXPECT_RUNTIME"))
        CHECK_MSG(info.runtime.find(want) != std::string::npos,
                  "the runtime is the one the runner named ('%s', got '%s')", want,
                  info.runtime.c_str());

    // A View FIRST: the engine registers its Hlms with the first one, and
    // createScene refuses before that.
    View *desktop = engine->createOffscreenView("desktop", 320, 240, Colour{ 0.16f, 0.20f, 0.28f, 1.0f });
    if (!desktop) {
        std::printf("SKIP: no offscreen view: %s\n", engine->lastError().c_str());
        return 77;
    }
    Scene *scene = engine->createScene("vr");
    REQUIRE(scene != nullptr);
    buildScene(scene);
    REQUIRE(desktop->setScene(scene));
    testCameraLookAt(desktop, Vec3{ 0.6f, 1.5f, 2.4f }, Vec3{ 0.0f, 1.3f, -0.6f });
    const PostFxDesc desktopFxBefore = desktop->postFx();

    // ---- THE DESKTOP'S PICTURE, BEFORE ------------------------------------
    // VR_SPEC §0's constraint, measured in this process: without a headset the
    // tool is today's tool. The same view, the same pose, before a session and
    // after one — byte for byte.
    Image before;
    REQUIRE(stableReadback(engine.get(), desktop, before));

    // =======================================================================
    // 1. THE CONTROL SESSION: worldScale ZERO, i.e. the two eyes in one place.
    // =======================================================================
    Image zeroImg;
    {
        VrConfig cfg;
        cfg.mirror = VrMirrorMode::None;
        cfg.worldScale = 0.0f;
        CHECK_MSG(engine->beginVrSession(scene, cfg), "beginVrSession: %s",
                  engine->lastError().c_str());
        REQUIRE(engine->vrStatus().active);
        View *vrView = engine->vrView();
        REQUIRE(vrView != nullptr);
        CHECK_MSG(vrView->width() == info.eyeWidth * 2u && vrView->height() == info.eyeHeight,
                  "the session's target is two eyes wide (%ux%u for a %ux%u eye)",
                  vrView->width(), vrView->height(), info.eyeWidth, info.eyeHeight);

        // THE PHASE-2 PROFILE (VR_SPEC §9 item 6), applied by the session:
        // HDR on, MSAA 1 (HDR + MSAA segfaults this driver), and every effect
        // that samples a NEIGHBOURHOOD off, because at the middle of this
        // target the neighbourhood is the other eye.
        const PostFxDesc fx = vrView->postFx();
        CHECK(fx.hdr);
        CHECK(!fx.ssao);
        CHECK(fx.smaaPreset < 0);
        CHECK(fx.ssr == 0);
        CHECK(vrView->sampleCount() == 1u);

        // THE DESKTOP KEEPS DRAWING WHILE A SESSION RUNS (F4). The runtime's
        // first frames carry shouldRender = 0, and the pump used to answer
        // those by returning out of renderOneFrame before ANY view rendered —
        // so a headset that was taken off, a dashboard that came up or a
        // runtime that paused froze the editor's viewport and every host that
        // counts frames. The desktop view's own counter is the witness.
        const unsigned long long deskBefore = desktop->framesPresented();
        const unsigned long long frames = pump(engine.get(), 60ull, 600u);
        const unsigned long long deskDrawn = desktop->framesPresented() - deskBefore;
        const VrStatus st = engine->vrStatus();
        CHECK_MSG(frames >= 60ull, "the runtime accepted %llu frames (>= 60)", frames);
        CHECK_MSG(st.state == VrState::Focused, "the session reached FOCUSED (state %d)",
                  int(st.state));
        // NOT equal to `frames`: the runtime's first frames carry
        // shouldRender = 0 (it is still synchronising), and those are submitted
        // with no layers and drawn not at all — which is the spec's contract,
        // not a dropped frame.
        CHECK_MSG(st.rendered + 3ull >= st.frames && st.rendered >= 50ull,
                  "the runtime asked for %llu pictures of the %llu frames submitted",
                  st.rendered, st.frames);
        std::printf("STATE  frames=%llu rendered=%llu ipd=%.4f m asymmetricFov=%d space='%s' "
                    "refresh=%.1f Hz\n",
                    st.frames, st.rendered, st.ipd, int(st.asymmetricFov),
                    engine->vrInfo().space.c_str(), engine->vrInfo().refreshHz);
        // The IPD is a property of the RUNTIME, not of us: it is reported and
        // sanity-checked, never asserted to a value.
        CHECK_MSG(st.ipd > 0.03f && st.ipd < 0.10f, "the located eyes are %.4f m apart", st.ipd);
        CHECK_MSG(deskDrawn >= st.rendered,
                  "the desktop view drew %llu frames while the session drew %llu — a session "
                  "the runtime is not asking pictures for must not stop the editor",
                  deskDrawn, st.rendered);
        if (!st.asymmetricFov)
            std::printf("NOTE   this runtime gives both eyes the SAME fov, so only the POSES "
                        "separate them (an asymmetric per-eye projection is unexercised "
                        "until a real headset)\n");

        REQUIRE(vrView->readPixels(zeroImg));
        Half l, r;
        REQUIRE(splitEyes(zeroImg, l, r));
        // NO "did it draw" CHECK HERE, deliberately: at worldScale 0 the HEAD
        // collapses to the space's origin as well as the eyes, so what this
        // arm looks at is the floor of the room from the floor. What it is for
        // is the equality below, and the picture's content is beside the point.
        int worst = 0;
        const size_t diff = differingBytes(l.px, r.px, &worst);
        CHECK_MSG(diff == 0u,
                  "AT worldScale 0 THE TWO EYES ARE BYTE-IDENTICAL: %zu of %zu bytes differ, "
                  "worst %d/255 — both halves rendered, through per-eye matrices that agree "
                  "when the eyes do, into exactly half the target each",
                  diff, l.px.size(), worst);
        engine->endVrSession();
        CHECK(!engine->vrStatus().active);
        CHECK(engine->vrView() == nullptr);
    }

    // =======================================================================
    // 2. THE REAL SESSION: worldScale ONE — the same two halves must now DIFFER.
    // =======================================================================
    {
        VrConfig cfg;
        cfg.mirror = VrMirrorMode::Left;
        cfg.worldScale = 1.0f;
        CHECK_MSG(engine->beginVrSession(scene, cfg), "beginVrSession (second): %s",
                  engine->lastError().c_str());
        REQUIRE(engine->vrStatus().active);
        // THE MIRROR, onto the desktop view: its own picture underneath, the
        // left eye painted over it.
        engine->setVrMirrorView(desktop);
        pump(engine.get(), 30ull, 300u);
        View *vrView = engine->vrView();
        REQUIRE(vrView != nullptr);
        Image oneImg;
        REQUIRE(vrView->readPixels(oneImg));
        Half l, r;
        REQUIRE(splitEyes(oneImg, l, r));
        CHECK_MSG(drawnFraction(l.px) > 0.02, "the LEFT eye drew a scene (%.1f%% of it is not "
                  "the clear colour)", 100.0 * drawnFraction(l.px));
        CHECK_MSG(drawnFraction(r.px) > 0.02, "the RIGHT eye drew a scene (%.1f%%)",
                  100.0 * drawnFraction(r.px));
        int worst = 0;
        const size_t diff = differingBytes(l.px, r.px, &worst);
        CHECK_MSG(diff > 0u,
                  "AT worldScale 1 THE TWO EYES DIFFER: %zu of %zu bytes, worst %d/255 — that "
                  "difference IS the parallax the IPD produces",
                  diff, l.px.size(), worst);

        // ---- THE MIRROR IS THE LEFT EYE, BYTE FOR BYTE ------------------
        // NOT "the desktop's pixels changed": the mirror quad loads DontCare,
        // so a mirror that painted uninitialised memory would pass that and a
        // mirror of the WRONG HALF would pass it twice over. When the desktop
        // view and the runtime's eye are the same size the quad's resample is
        // an identity — a destination pixel centre at (i+0.5)/w maps to source
        // u = (i+0.5)/2w, i.e. exactly the source texel's centre, so bilinear
        // returns that texel unchanged — and the assertion can be equality.
        Image mirrored;
        REQUIRE(desktop->readPixels(mirrored));
        if (mirrored.width == l.w && mirrored.height == l.h) {
            int mworst = 0;
            const size_t mirrorDiff = differingBytes(mirrored.rgba, l.px, &mworst);
            CHECK_MSG(mirrorDiff == 0u,
                      "THE MIRROR IS THE LEFT EYE: %zu of %zu bytes differ, worst %d/255",
                      mirrorDiff, l.px.size(), mworst);
        } else {
            // A runtime whose eye size is not the mirror's: the resample is
            // real and only the "it is not the desktop's own picture" half can
            // be asserted. Said out loud rather than silently weakened.
            const size_t mirrorDiff = differingBytes(mirrored.rgba, before.rgba);
            CHECK_MSG(mirrorDiff > 0u,
                      "the mirror changed the desktop view's picture (%zu bytes; the eye is "
                      "%ux%u and the mirror %ux%u, so equality is not available here)",
                      mirrorDiff, l.w, l.h, mirrored.width, mirrored.height);
        }

        // ---- THE REVERSE-Z DETECTOR (VR_SPEC §6) ------------------------
        // The left eye, rendered MONO through Camera::setCustomProjectionMatrix
        // at that eye's exact pose and projection, must be the left half of the
        // stereo frame. The two paths differ in exactly one place: the stereo
        // one hands its projections to VrData, which stores them RAW, and the
        // mono one hands the same matrix to the Camera, which runs it through
        // the render system's own conversion. A session that stops converting
        // for VrData (the defect this round fixed) renders the headset with
        // INVERTED DEPTH while every other picture in the process is right —
        // and this is the assertion that says so.
        Image mono;
        if (CHECK_MSG(engine->vrEyeScreenshot(0u, mono), "vrEyeScreenshot(left): %s",
                      engine->lastError().c_str())) {
            CHECK_MSG(mono.width == l.w && mono.height == l.h,
                      "the control is one eye's size (%ux%u vs %ux%u)", mono.width, mono.height,
                      l.w, l.h);
            int mworst = 0;
            const size_t monoDiff = differingBytes(mono.rgba, l.px, &mworst);
            // <= 1/255, AND THE MARGIN IS MEASURED, NOT ASSUMED. The two
            // pictures come out of two chain INSTANCES: the session's tonemap
            // reduces its own exposure over frames, the control's multiplies by
            // the constant that reduction converged to, and the last bits of
            // that constant are where the residual lives — one channel of one
            // pixel, measured on this fixture. The A/B that says this is still
            // a detector: handing VrData the UNCONVERTED projection (the defect
            // this assertion exists for) moves 133,940 of 307,200 bytes, worst
            // 65/255, and collapses the parallax below from 26,473 bytes to 288
            // — four orders of magnitude of margin over the tolerance.
            CHECK_MSG(mworst <= 1,
                      "THE LEFT EYE EQUALS A MONO RENDER AT THAT EYE'S POSE AND PROJECTION: "
                      "%zu of %zu bytes differ, worst %d/255 (the unconverted-projection "
                      "defect reads 133,940 and 65/255 here)",
                      monoDiff, l.px.size(), mworst);
        }

        engine->setVrMirrorView(nullptr);
        engine->endVrSession();
        CHECK(!engine->vrStatus().active);
    }

    // ---- THE DESKTOP'S PICTURE, AFTER -------------------------------------
    CHECK_MSG(desktop->postFx() == desktopFxBefore,
              "the desktop view's post chain is exactly what it was before the session");
    Image after;
    REQUIRE(stableReadback(engine.get(), desktop, after));
    int worst = 0;
    const size_t deskDiff = differingBytes(before.rgba, after.rgba, &worst);
    CHECK_MSG(deskDiff == 0u,
              "THE DESKTOP RENDERS THE SAME BYTES AFTER A SESSION AS BEFORE ONE: %zu of %zu "
              "bytes differ, worst %d/255", deskDiff, before.rgba.size(), worst);

    // ---- a session on a dead runtime must refuse, never hang --------------
    CHECK(!engine->beginVrSession(nullptr, VrConfig()));
    CHECK(!engine->lastError().empty());

    std::printf("%s  %d checks, %d failures\n", gFailures ? "FAIL" : "PASS", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
