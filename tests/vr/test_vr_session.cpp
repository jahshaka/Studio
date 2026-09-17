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
/// The fixture's sky, on or off. THE FIRST SESSION RUNS WITHOUT ONE, and that
/// is not tidiness: the worldScale-0 control asserts the two halves BIT-EXACT,
/// and a sky is the one thing in the picture whose two eyes are computed by two
/// numerically different (and equally correct) routes — the first eye
/// unprojects the pass camera's own inverse view-projection, the second mixes
/// four corner rays — so at a camera height where the scattering integral is
/// steep the last bits of those two routes are worth tens of thousands of
/// bytes. The sky's own proofs live in the second session, against a mono
/// render of each eye, which is the comparison that can carry them.
void setFixtureSky(Scene *scene, bool on) {
    SkyDesc sky;
    sky.mode = on ? SkyMode::Atmosphere : SkyMode::NoSky;
    sky.sun.enabled = on;
    sky.sun.dir[0] = 0.35f; sky.sun.dir[1] = 0.45f; sky.sun.dir[2] = -0.82f;
    sky.sun.angularDiameterDeg = 6.0f;
    scene->setSky(sky);
}

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

/// How much of a picture really disagrees with another: the fraction of BYTES
/// that differ by more than `tol`, and the mean absolute difference.
///
/// WHY NOT BIT-EXACT, measured rather than assumed (lane VR-2's F2 round): two
/// renders of the same pose through two different chain INSTANCES differ by ~1
/// in 255 everywhere (each chain's tonemap converged on its own) and by a lot
/// on the pixels of a high-contrast EDGE, because the two paths compose the
/// same pose through different arithmetic (a per-eye view matrix built from a
/// head matrix times an eye offset, against one built from the eye's own pose)
/// and the last bits move a boundary by one pixel. Neither is a defect and
/// neither hides one: the failure this comparison exists to catch — the eyes
/// drawn through the wrong projection convention — moves 44 % of the picture
/// (measured: 133,940 of 307,200 bytes, worst 65/255).
struct PictureDiff { double meanAbs = 0.0; double fractionOver = 0.0; int worst = 0; };
/// The same comparison over a BAND of rows — the sky is the top of the picture,
/// and "where does the residual live" is the question that separates a routing
/// difference from a grading one.
PictureDiff pictureDiffRows(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b,
                            unsigned w, unsigned y0, unsigned y1, int tol = 8) {
    PictureDiff d;
    size_t over = 0, n = 0; double sum = 0.0;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                const size_t i = (size_t(y) * w + x) * 4u + size_t(c);
                if (i >= a.size() || i >= b.size()) continue;
                const int delta = std::abs(int(a[i]) - int(b[i]));
                sum += delta; ++n;
                if (delta > tol) ++over;
                if (delta > d.worst) d.worst = delta;
            }
    if (n) { d.meanAbs = sum / double(n); d.fractionOver = double(over) / double(n); }
    return d;
}
PictureDiff pictureDiff(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b,
                        int tol = 8) {
    PictureDiff d;
    const size_t n = std::min(a.size(), b.size());
    if (!n) return d;
    size_t over = 0; double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const int delta = std::abs(int(a[i]) - int(b[i]));
        sum += delta;
        if (delta > tol) ++over;
        if (delta > d.worst) d.worst = delta;
    }
    d.meanAbs = sum / double(n);
    d.fractionOver = double(over) / double(n);
    return d;
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
        setFixtureSky(scene, false);
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
        {   // JAH_VR_DUMP=<dir>: the two halves, for a human to look at. Kept
            // because every question this suite could not answer in words was
            // answered by opening these two files side by side.
            const char *dir = std::getenv("JAH_VR_DUMP");
            if (dir) {
                auto dump = [&](const Half &h, const char *name) {
                    char path[512]; std::snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
                    FILE *f = std::fopen(path, "wb");
                    if (!f) return;
                    std::fprintf(f, "P6\n%u %u\n255\n", h.w, h.h);
                    for (size_t i = 0; i < size_t(h.w) * h.h; ++i)
                        std::fwrite(&h.px[i * 4], 1, 3, f);
                    std::fclose(f);
                };
                dump(l, "scale0-left"); dump(r, "scale0-right");
            }
        }
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
        // A SKY AND A SUN DISC for this one, because they are drawn by SCREEN
        // QUADS and a screen quad is the one thing instanced stereo does not
        // carry on its own: the pass draws it twice, but only a vertex shader
        // that writes `gl_ViewportIndex` sends the second copy to the second
        // eye, and only one that knows that eye's own view points its rays
        // where the eye looks. A fixture with no sky cannot see either failure.
        setFixtureSky(scene, true);
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

        // ---- THE SKY CHANGES UNDER THE SESSION (V2F-1 and V2F-2) ---------
        // Both halves of the screen-quad swap are exercised by one churn: the
        // sky's quad is DESTROYED when the sky goes (the session holds a raw
        // pointer to it and must not touch it again), and when it comes back
        // the material has the same NAME as the clone the session made last
        // time (cloning onto a registered name throws ERR_DUPLICATE_ITEM —
        // inside beginFrame, which takes the whole frame down with it: no eye,
        // no desktop, every frame until the session ends).
        //
        // The assertion is therefore the plainest one there is: THE FRAMES KEEP
        // COMING. A thrown clone stops them dead.
        {
            const unsigned long long before = engine->vrStatus().frames;
            setFixtureSky(scene, false);
            pump(engine.get(), before + 10ull, 200u);
            const unsigned long long mid = engine->vrStatus().frames;
            CHECK_MSG(mid >= before + 10ull,
                      "the sky switched OFF mid-session and the frames kept coming (%llu -> %llu)",
                      before, mid);
            setFixtureSky(scene, true);
            pump(engine.get(), mid + 10ull, 200u);
            const unsigned long long after = engine->vrStatus().frames;
            CHECK_MSG(after >= mid + 10ull,
                      "...and back ON, with the same material name as last time (%llu -> %llu)",
                      mid, after);

            // ...AND THE HARDER HALF: A SKY WHOSE QUAD SURVIVES THE CHANGE.
            // `SceneManager::setSky` re-applies its own material to its own
            // quad on EVERY call (OgreSceneManager.cpp:1153), so an equirect
            // sky re-pushed with a different image hands the session a quad it
            // already owns, holding the BASE material again. Cloning a second
            // time under the name the first clone still holds throws
            // ERR_DUPLICATE_ITEM inside beginFrame, which takes the whole
            // frame — every frame — with it. The atmosphere above cannot show
            // that (its quad is its own and its material is set once); this
            // can, and it is the case V2F-1 was reported from.
            unsigned char px[4 * 4 * 4];
            for (int i = 0; i < 4 * 4; ++i) {
                px[i * 4 + 0] = (unsigned char)(40 + i * 3);
                px[i * 4 + 1] = 70; px[i * 4 + 2] = 150; px[i * 4 + 3] = 255;
            }
            const TextureId equirect = scene->createTexture(4, 4, px, true);
            for (int i = 0; i < 4 * 4; ++i) px[i * 4 + 1] = 200;   // a second image
            const TextureId equirect2 = scene->createTexture(4, 4, px, true);
            if (equirect && equirect2) {
                SkyDesc eq;
                eq.mode = SkyMode::Equirectangular;
                eq.equirect = equirect;
                scene->setSky(eq);
                const unsigned long long base = engine->vrStatus().frames;
                pump(engine.get(), base + 10ull, 200u);
                const unsigned long long once = engine->vrStatus().frames;
                CHECK_MSG(once >= base + 10ull,
                          "an equirect sky's quad joined the session (%llu -> %llu)", base, once);
                // ...and now a DIFFERENT image through the same sky, which is
                // what re-applies the base material onto a quad the session
                // already swapped (the engine only re-calls setSky when the
                // description really changed — SkyDesc::sameSky compares the
                // texture id, so a second image is the smallest real change).
                eq.equirect = equirect2;
                scene->setSky(eq);
                pump(engine.get(), once + 10ull, 200u);
                const unsigned long long twice = engine->vrStatus().frames;
                CHECK_MSG(twice >= once + 10ull,
                          "THE SKY CHANGED ON A QUAD THE SESSION ALREADY OWNED and the frames "
                          "kept coming (%llu -> %llu)", once, twice);
                CHECK_MSG(engine->lastError().find("Duplicate") == std::string::npos,
                          "no duplicate-material throw: lastError is '%s'",
                          engine->lastError().c_str());
            }
            setFixtureSky(scene, true);
            pump(engine.get(), engine->vrStatus().frames + 5ull, 100u);
            CHECK_MSG(engine->vrStatus().state == VrState::Focused,
                      "the session is still focused after the churn (state %d)",
                      int(engine->vrStatus().state));
        }
        pump(engine.get(), engine->vrStatus().frames + 10ull, 100u);
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

        // ---- THE SKY IS IN BOTH EYES (F2) -------------------------------
        // The sky, the atmosphere and the sun disc are SCREEN QUADS, and a
        // screen quad under instanced stereo is drawn twice by the pass but
        // sent twice to the SAME viewport unless its vertex shader says
        // otherwise — so the right eye's sky rows are the clear colour, and the
        // left eye's rays come from a camera that is not an eye. The top of
        // each eye is sky by construction in this fixture (the pillar and the
        // wall do not reach it), so the test is that both tops are lit and that
        // they agree with a mono render of that eye.
        const auto topRowMean = [](const Half &h) {
            double sum = 0.0; size_t n = 0;
            for (unsigned y = 0; y < h.h / 8u; ++y)
                for (unsigned x = 0; x < h.w; ++x) {
                    const size_t i = (size_t(y) * h.w + x) * 4u;
                    sum += h.px[i] + h.px[i + 1] + h.px[i + 2]; n += 3;
                }
            return n ? sum / double(n) : 0.0;
        };
        const double skyL = topRowMean(l), skyR = topRowMean(r);
        CHECK_MSG(skyL > 8.0 && skyR > 8.0,
                  "BOTH EYES HAVE A SKY: the top eighth reads %.1f/255 left and %.1f/255 right",
                  skyL, skyR);
        CHECK_MSG(std::fabs(skyL - skyR) < 0.5 * std::max(skyL, skyR),
                  "and they are the same sky (%.1f vs %.1f)", skyL, skyR);

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
        // THE RESIDUAL FOLLOWS THE ORDER, NOT THE EYE — measured, and it is
        // what closes the question of whether the second eye's ray route is as
        // good as the first's (V2F-3). `vrEyeScreenshot` renders ~90 frames of
        // its own and the session's auto-exposure moves a little through them,
        // so whichever control is taken FIRST is compared with the session's
        // picture at the moment its exposure constant was read and reads
        // 0.000/255 — and the other carries the drift, whether that is the
        // right eye (1.209 over the sky rows) or, with this switch on, the left
        // (1.230). The routes are not the difference; the clock is.
        const bool rightFirst = std::getenv("JAH_VR_RIGHT_FIRST") != nullptr;
        Image firstShot;
        if (rightFirst) engine->vrEyeScreenshot(1u, firstShot);
        Image mono;
        if (CHECK_MSG(engine->vrEyeScreenshot(0u, mono), "vrEyeScreenshot(left): %s",
                      engine->lastError().c_str())) {

            CHECK_MSG(mono.width == l.w && mono.height == l.h,
                      "the control is one eye's size (%ux%u vs %ux%u)", mono.width, mono.height,
                      l.w, l.h);
            const PictureDiff md = pictureDiff(mono.rgba, l.px);
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
            const PictureDiff mdSky = pictureDiffRows(mono.rgba, l.px, l.w, 0, l.h / 4u);
            const PictureDiff mdRest = pictureDiffRows(mono.rgba, l.px, l.w, l.h / 4u, l.h);
            std::printf("    SPLIT left: sky rows mean %.3f (worst %d), the rest mean %.3f "
                        "(worst %d)\n", mdSky.meanAbs, mdSky.worst, mdRest.meanAbs, mdRest.worst);
            CHECK_MSG(md.meanAbs < 1.0 && md.fractionOver < 0.02,
                      "THE LEFT EYE IS A MONO RENDER AT THAT EYE'S POSE AND PROJECTION: mean "
                      "%.3f/255, %.3f%% of bytes over 8 (the bar is 2%%), worst %d (the unconverted-projection "
                      "defect reads mean 11.5 and 44%% here)",
                      md.meanAbs, 100.0 * md.fractionOver, md.worst);
        }
        // THE RIGHT EYE TOO, and it is not a symmetry for its own sake: the
        // right half is the one a viewport-index failure leaves empty and the
        // one a per-eye-ray failure paints with the left eye's sky.
        Image monoR;
        if (rightFirst) monoR = firstShot;
        if (CHECK_MSG(rightFirst ? !monoR.rgba.empty() : engine->vrEyeScreenshot(1u, monoR),
                      "vrEyeScreenshot(right): %s", engine->lastError().c_str())) {
            if (const char *dir = std::getenv("JAH_VR_DUMP")) {
                auto dumpImg = [&](const std::vector<unsigned char> &px, unsigned w, unsigned h,
                                   const char *name) {
                    char path[512]; std::snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
                    FILE *f = std::fopen(path, "wb"); if (!f) return;
                    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
                    for (size_t i = 0; i < size_t(w) * h; ++i) std::fwrite(&px[i * 4], 1, 3, f);
                    std::fclose(f);
                };
                dumpImg(r.px, r.w, r.h, "right-eye");
                dumpImg(monoR.rgba, monoR.width, monoR.height, "right-control");
                dumpImg(l.px, l.w, l.h, "left-eye");
                dumpImg(mono.rgba, mono.width, mono.height, "left-control");
            }
            const PictureDiff rd = pictureDiff(monoR.rgba, r.px);
            const PictureDiff rdSky = pictureDiffRows(monoR.rgba, r.px, r.w, 0, r.h / 4u);
            const PictureDiff rdRest = pictureDiffRows(monoR.rgba, r.px, r.w, r.h / 4u, r.h);
            std::printf("    SPLIT right: sky rows mean %.3f (worst %d), the rest mean %.3f "
                        "(worst %d)%s\n", rdSky.meanAbs, rdSky.worst, rdRest.meanAbs,
                        rdRest.worst, rightFirst ? "  [right control taken FIRST]" : "");
            CHECK_MSG(rd.meanAbs < 1.0 && rd.fractionOver < 0.02,
                      "THE RIGHT EYE IS A MONO RENDER AT THAT EYE'S POSE AND PROJECTION: mean "
                      "%.3f/255, %.3f%% of bytes over 8, worst %d — the half that a "
                      "viewport-index failure leaves empty and a per-eye-ray failure paints "
                      "with the LEFT eye's sky",
                      rd.meanAbs, 100.0 * rd.fractionOver, rd.worst);
        }

        engine->setVrMirrorView(nullptr);
        engine->endVrSession();
        CHECK(!engine->vrStatus().active);
    }

    // =======================================================================
    // 3. THE QUAD DIES UNDER THE SESSION (V2F-2): a sky that goes away destroys
    //    the Rectangle2D the session swapped, and the session holds a RAW
    //    pointer to it. The end of the session must not hand a material back to
    //    freed memory.
    // =======================================================================
    {
        setFixtureSky(scene, true);
        VrConfig cfg;
        cfg.mirror = VrMirrorMode::None;
        REQUIRE(engine->beginVrSession(scene, cfg));
        pump(engine.get(), 10ull, 200u);
        setFixtureSky(scene, false);          // the sky's quad is destroyed here
        pump(engine.get(), engine->vrStatus().frames + 10ull, 200u);
        CHECK_MSG(engine->vrStatus().frames >= 20ull,
                  "the sky's quad died mid-session and the frames kept coming (%llu)",
                  engine->vrStatus().frames);
        engine->endVrSession();               // ...and this must not touch it
        CHECK(!engine->vrStatus().active);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        CHECK_MSG(true, "the session ended after its screen quad was destroyed");
    }

    // ---- THE DESKTOP'S PICTURE, AFTER -------------------------------------
    // THE SCENE BACK AS IT WAS. The `before` picture was taken with no sky (the
    // first session's control needs none) and the second session added one, so
    // a desktop A/B taken now would be comparing two different WORLDS — which
    // is a fixture bug, not a VR one, and it read 230,400 bytes when this line
    // was missing.
    setFixtureSky(scene, false);
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
