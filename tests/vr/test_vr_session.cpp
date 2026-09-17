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

        // ---- THE RIG, AND A MIRROR ONTO A SWITCHED-OFF VIEW (phase 3) ---
        //
        // TWO PHASE-3 FACTS IN ONE A/B, because each is the other's control.
        //
        //   * `Engine::setVrOrigin` places the runtime's reference space in the
        //     WORLD. Moving it must move the picture the eyes see — that is the
        //     whole of locomotion, and a setter that quietly did nothing would
        //     pass every state assertion ever written.
        //   * A View that has been switched OFF is still a mirror (VR-2's F7,
        //     the Player's VR mode): the mirror is its own workspace over the
        //     view's target, so a disabled view stops drawing the world a
        //     second time at window size and keeps receiving the eye. That is
        //     what makes the desktop cost A COPY rather than A SECOND RENDER.
        //
        // The A/B: switch the desktop view OFF, move the rig, pump. If the
        // mirror were dead, the desktop would still hold the picture it holds
        // now (nothing else writes that target — the view's own workspace is
        // disabled). It must instead be the NEW left eye, byte for byte.
        {
            Image beforeMove;
            REQUIRE(desktop->readPixels(beforeMove));
            desktop->setEnabled(false);
            engine->setVrOrigin(Vec3(3.0f, 0.0f, -4.0f), 35.0f);
            pump(engine.get(), engine->vrStatus().frames + 12ull, 120u);

            const VrStatus moved = engine->vrStatus();
            CHECK_MSG(moved.origin.x == 3.0f && moved.origin.z == -4.0f && moved.originYaw == 35.0f,
                      "the engine reports the rig it was given: (%.2f, %.2f, %.2f) yaw %.2f",
                      moved.origin.x, moved.origin.y, moved.origin.z, moved.originYaw);
            CHECK_MSG(moved.posesValid, "the head is located in the moved rig");
            // The head must be WHERE THE RIG PUT IT: the runtime's own pose,
            // turned by the rig's yaw and carried by its translation. The
            // simulated HMD sits at a fixed spot, so "not at the world origin
            // any more" is the honest statement available here.
            std::printf("    head after the rig moved: (%.3f, %.3f, %.3f)\n",
                        moved.headPosition.x, moved.headPosition.y, moved.headPosition.z);
            CHECK_MSG(std::fabs(moved.headPosition.x - 3.0f) < 3.0f &&
                          std::fabs(moved.headPosition.z + 4.0f) < 3.0f,
                      "and the head moved with it (within arm's reach of the rig's origin)");

            Image movedEye;
            REQUIRE(engine->vrView()->readPixels(movedEye));
            Half ml, mr;
            REQUIRE(splitEyes(movedEye, ml, mr));
            const size_t eyeMoved = differingBytes(ml.px, l.px);
            CHECK_MSG(eyeMoved > 0u,
                      "MOVING THE RIG MOVES THE PICTURE: %zu of %zu bytes of the left eye",
                      eyeMoved, l.px.size());

            Image mirroredOff;
            REQUIRE(desktop->readPixels(mirroredOff));
            const size_t desktopMoved = differingBytes(mirroredOff.rgba, beforeMove.rgba);
            CHECK_MSG(desktopMoved > 0u,
                      "A SWITCHED-OFF VIEW IS STILL A MIRROR: the desktop picture changed by "
                      "%zu bytes while its own workspace was disabled", desktopMoved);
            if (mirroredOff.width == ml.w && mirroredOff.height == ml.h) {
                int mworst = 0;
                const size_t d = differingBytes(mirroredOff.rgba, ml.px, &mworst);
                CHECK_MSG(d == 0u,
                          "...and it is the new LEFT EYE, byte for byte: %zu of %zu differ, "
                          "worst %d/255", d, ml.px.size(), mworst);
            }
            // ---- AND THE MIRROR FOLLOWS THE HOST'S WISH (F9) ------------
            // The mirror does not stop when its view does — it is a workspace
            // over that view's target — so "is anybody looking at this page" is
            // the HOST's question and it has to be able to ANSWER it. Clearing
            // the mirror must take the workspace down (the desktop keeps
            // whatever it had), and re-setting it must bring the eye back.
            engine->setVrMirrorView(nullptr);
            CHECK_MSG(engine->vrMirrorView() == nullptr, "the mirror can be cleared");
            Image cleared;
            pump(engine.get(), engine->vrStatus().frames + 6ull, 60u);
            REQUIRE(desktop->readPixels(cleared));
            engine->setVrOrigin(Vec3(-6.0f, 0.0f, 2.0f), -20.0f);
            pump(engine.get(), engine->vrStatus().frames + 12ull, 120u);
            Image afterClear;
            REQUIRE(desktop->readPixels(afterClear));
            CHECK_MSG(differingBytes(afterClear.rgba, cleared.rgba) == 0u,
                      "A CLEARED MIRROR STOPS PAINTING: the desktop did not move while the "
                      "rig did (its own workspace is still disabled)");
            engine->setVrMirrorView(desktop);
            CHECK_MSG(engine->vrMirrorView() == desktop, "...and it can be taken again");
            pump(engine.get(), engine->vrStatus().frames + 12ull, 120u);
            Image afterRetake;
            REQUIRE(desktop->readPixels(afterRetake));
            CHECK_MSG(differingBytes(afterRetake.rgba, cleared.rgba) > 0u,
                      "and the eye comes back");

            desktop->setEnabled(true);
            // PUT THE RIG BACK before the cases below read the eyes again.
            engine->setVrOrigin(Vec3(0.0f, 0.0f, 0.0f), 0.0f);
            pump(engine.get(), engine->vrStatus().frames + 12ull, 120u);
            REQUIRE(engine->vrView()->readPixels(oneImg));
            REQUIRE(splitEyes(oneImg, l, r));
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

    // =======================================================================
    // 4. THE WiVRn SHAPE: THE RUNTIME WANTS NO PICTURE AND NO VIEW IS ENABLED
    //    (lane VR-3b, 2026-09-17 — the owner's failed smoke, as a suite).
    //
    // WHAT FAILED ON THE HEADSET, and what no test could reach before. WiVRn
    // answers `shouldRender = 0` for its first frames; the pump switches the
    // session's own View off for those (VR_SPEC F4), the Player's is off by
    // design and the editor's is hidden behind the Player page — so the engine
    // runs whole frames with NOTHING enabled, and everything that assumed "a
    // frame draws something" was wrong at once: the host skipped the frame
    // entirely (so the pump never called xrWaitFrame again and the runtime
    // never synchronised — a black headset for ever), the mirror painted an eye
    // target no pass had ever written (stale VRAM on the desktop), and a view's
    // ray-reflection listener was torn down mid-frame, submitting to the queue
    // from a destructor.
    //
    // Monado's simulated HMD asks for a picture immediately and cannot be told
    // not to, so the pump is told instead: JAHSHAKA_VR_TEST_NO_RENDER_FRAMES
    // replaces the runtime's answer for the first N frames of ONE session (the
    // frames are really waited for, begun and ended). See vrTestNoRenderFrames.
    // =======================================================================
    {
        setFixtureSky(scene, false);
        // The desktop's own picture, settled, with no session anywhere near it.
        Image quiet;
        REQUIRE(stableReadback(engine.get(), desktop, quiet));

        setenv("JAHSHAKA_VR_TEST_NO_RENDER_FRAMES", "30", 1);
        VrConfig cfg;
        cfg.mirror = VrMirrorMode::Left;
        const bool began = CHECK_MSG(engine->beginVrSession(scene, cfg), "beginVrSession: %s",
                                     engine->lastError().c_str());
        unsetenv("JAHSHAKA_VR_TEST_NO_RENDER_FRAMES");
        if (began) {
            engine->setVrMirrorView(desktop);
            // ...AND NOW NOTHING AT ALL IS ENABLED: the session's View is off
            // for every no-picture frame, and this is the only other one.
            desktop->setEnabled(false);

            // THE FRAME LOOP IS THE SESSION'S HEARTBEAT. Frames the runtime
            // ACCEPTED must climb while `rendered` stays at nothing — that is
            // the pair the owner's log could not show: a session that stopped
            // being pumped answers "no picture" for ever.
            for (int i = 0; i < 20; ++i) { engine->advanceResources(); engine->renderOneFrame(); }
            const VrStatus noPicture = engine->vrStatus();
            CHECK_MSG(noPicture.frames >= 15ull && noPicture.rendered == 0ull,
                      "THE LOOP RUNS WITH NOTHING ENABLED: %llu frames accepted, %llu drawn",
                      noPicture.frames, noPicture.rendered);
            CHECK_MSG(noPicture.active, "and the session is alive (state %d)", int(noPicture.state));

            // THE MIRROR PAINTS NOTHING YET, PROVED ON THE PIXELS. The desktop
            // view is switched off, so its target can only change if the mirror
            // wrote it — and the only thing the mirror could write is an eye
            // target no pass has ever touched.
            Image duringNoPicture;
            REQUIRE(desktop->readPixels(duringNoPicture));
            int worstStale = 0;
            const size_t staleDiff =
                differingBytes(duringNoPicture.rgba, quiet.rgba, &worstStale);
            CHECK_MSG(staleDiff == 0u,
                      "THE MIRROR NEVER PAINTS AN EYE NOBODY HAS DRAWN: %zu of %zu bytes "
                      "moved, worst %d/255", staleDiff, quiet.rgba.size(), worstStale);

            // ...AND IT DOES PAINT once the runtime asks for a picture again.
            for (int i = 0; i < 200 && engine->vrStatus().rendered == 0ull; ++i) {
                engine->advanceResources();
                engine->renderOneFrame();
            }
            CHECK_MSG(engine->vrStatus().rendered > 0ull,
                      "the runtime asked for a picture again after the forced stretch (%llu drawn)",
                      engine->vrStatus().rendered);
            for (int i = 0; i < 3; ++i) engine->renderOneFrame();
            Image mirroredNow;
            REQUIRE(desktop->readPixels(mirroredNow));
            CHECK_MSG(differingBytes(mirroredNow.rgba, quiet.rgba) > 0u,
                      "AND THE MIRROR PAINTS THE EYE ONCE THERE IS ONE (the still-disabled "
                      "desktop view's target changed)");

            engine->setVrMirrorView(nullptr);
            engine->endVrSession();
            CHECK(!engine->vrStatus().active);
            desktop->setEnabled(true);
        }
    }

    // =======================================================================
    // 5. THE SESSION THE RUNTIME TAKES AWAY (lane VR-3b — the owner's second
    //    WiVRn run: READY -> SYNCHRONIZED -> stopped inside a second).
    //
    // A stopped session used to read `Idle` with `active` still true, which is
    // indistinguishable from a session that has not begun — so no host could
    // act on it: the desktop view stayed switched off, the render driver kept
    // the session's pacing (a zero interval with vsync off) against a pump that
    // would never block again, and the log's only trace was 131,505 skipped
    // ticks. A session the runtime stops is OVER: `active` says so and the
    // engine ends it inside the frame, exactly as it ends a lost one.
    //
    // The hook asks the runtime to exit; the STOPPING event, the xrEndSession
    // and everything after are the product path (see vrTestStopAfterFrames).
    // =======================================================================
    {
        setFixtureSky(scene, false);
        setenv("JAHSHAKA_VR_TEST_STOP_AFTER_FRAMES", "10", 1);
        VrConfig cfg;
        cfg.mirror = VrMirrorMode::None;
        const bool began = CHECK_MSG(engine->beginVrSession(scene, cfg), "beginVrSession: %s",
                                     engine->lastError().c_str());
        unsetenv("JAHSHAKA_VR_TEST_STOP_AFTER_FRAMES");
        if (began) {
            bool ended = false;
            int frames = 0;
            for (; frames < 400 && !ended; ++frames) {
                engine->advanceResources();
                engine->renderOneFrame();
                ended = !engine->vrStatus().active;
            }
            CHECK_MSG(ended,
                      "A SESSION THE RUNTIME STOPPED IS OVER: vrStatus().active went false "
                      "after %d frames", frames);
            CHECK_MSG(engine->vrView() == nullptr,
                      "...and the engine ENDED it inside the frame (no session View is left)");
            CHECK_MSG(engine->vrState() == VrState::Idle,
                      "...leaving the engine idle and able to start another (state %d)",
                      int(engine->vrState()));
            // The proof that it is really gone: a new session begins.
            if (CHECK_MSG(engine->beginVrSession(scene, VrConfig()), "a new session begins after "
                          "the runtime took the last one away: %s", engine->lastError().c_str())) {
                pump(engine.get(), 5ull, 200u);
                CHECK_MSG(engine->vrStatus().frames >= 5ull, "and it pumps (%llu frames)",
                          engine->vrStatus().frames);
                engine->endVrSession();
            }
            CHECK(!engine->vrStatus().active);
        }
    }

    // =======================================================================
    // 6. THE DRIVER'S OWN RULE, WITH ZERO VIEWS ENABLED (lane VR-4; the named
    //    follow-up from VR-3b's second read).
    //
    // WHAT CASE 4 ABOVE DOES NOT PROVE. It calls `renderOneFrame` on every
    // iteration, unconditionally — so it shows that a frame with nothing
    // enabled still pumps the session, and says nothing at all about the rule
    // that decides whether that frame HAPPENS. The host's render driver skips
    // the frame when `hasEnabledViews()` is false (EngineRenderDriver::tick's
    // `anythingToDraw`), and on the owner's WiVRn smoke that skip was the whole
    // defect: the session's own View is off through the no-picture stretch, the
    // Player's is off by design, the editor's is hidden — nothing is enabled,
    // the driver skips, the pump never calls xrWaitFrame again, the runtime
    // never synchronises and keeps answering "no picture" for ever. The fix is
    // one line in the engine (`hasEnabledViews()` returns true while a session
    // exists: the frame loop IS the session's heartbeat) and it had no test.
    //
    // So this case is the DRIVER, in three lines: the same gate, the same
    // order, with every view in the process switched off. Revert that one line
    // and this case hangs at frame one and fails on the count (measured: 1
    // accepted frame instead of 30).
    // =======================================================================
    {
        setFixtureSky(scene, false);
        setenv("JAHSHAKA_VR_TEST_NO_RENDER_FRAMES", "40", 1);
        VrConfig cfg;
        cfg.mirror = VrMirrorMode::None;
        const bool began = CHECK_MSG(engine->beginVrSession(scene, cfg), "beginVrSession: %s",
                                     engine->lastError().c_str());
        unsetenv("JAHSHAKA_VR_TEST_NO_RENDER_FRAMES");
        if (began) {
            desktop->setEnabled(false);
            // EVERY view, not just the desktop's: the session's own View is off
            // for a no-picture frame, and this asserts there is nothing else.
            std::vector<View *> all;
            engine->listViews(all);
            for (View *v : all) v->setEnabled(false);
            CHECK_MSG(engine->hasEnabledViews(),
                      "A VR SESSION IS SOMETHING TO DRAW: hasEnabledViews() is true with every "
                      "View in the process switched off, which is what keeps the driver calling "
                      "renderOneFrame");

            // THE DRIVER'S LOOP, exactly as enginerenderdriver.cpp writes it.
            // FEWER TICKS THAN THE FORCED STRETCH (40), so every frame in the
            // loop is a no-picture one and `rendered` is an exact zero rather
            // than a threshold.
            unsigned long long skipped = 0ull;
            for (int i = 0; i < 30; ++i) {
                if (engine->hasEnabledViews()) {
                    engine->advanceResources();
                    engine->renderOneFrame();
                } else {
                    ++skipped;
                }
            }
            const VrStatus st = engine->vrStatus();
            CHECK_MSG(skipped == 0ull, "the driver skipped %llu of 30 ticks", skipped);
            CHECK_MSG(st.frames >= 25ull,
                      "THE HEARTBEAT: the session submitted %llu frames under the driver's own "
                      "gate with nothing enabled anywhere", st.frames);
            CHECK_MSG(st.rendered == 0ull,
                      "...and drew none of them (the forced no-picture stretch): %llu",
                      st.rendered);
            engine->endVrSession();
            CHECK(!engine->vrStatus().active);
            // ...and with the session gone the rule goes back to what it was.
            CHECK_MSG(!engine->hasEnabledViews(),
                      "with no session and no enabled view the driver skips again");
            desktop->setEnabled(true);
        }
    }

    // =======================================================================
    // 7. THE HANDS (lane VR-4, VR_SPEC §5 phase 4). POSES ONLY.
    //
    // WHAT IS ASSERTED, and what cannot be. The action set, its two grip-pose
    // actions on the simple-controller profile and the attach are OURS and are
    // asserted: `handActions` is the answer to "did the runtime take them".
    // Whether a POSE arrives is the RUNTIME's business — Monado's simulated
    // builder creates two Simple Controllers only when the runner asks it to
    // (SIMULATED_LEFT/SIMULATED_RIGHT, run_vr_session.sh), and a box whose
    // Monado is older or differently built simply reports no hands. So the pose
    // half is reported and checked FOR CONSISTENCY (a valid hand must be a
    // finite pose near the room), never asserted into existence: a suite that
    // demanded two controllers would red on every box that has none, which is
    // the same mistake as demanding a headset.
    // =======================================================================
    {
        setFixtureSky(scene, false);
        VrConfig cfg;
        cfg.mirror = VrMirrorMode::None;
        const bool began = CHECK_MSG(engine->beginVrSession(scene, cfg), "beginVrSession: %s",
                                     engine->lastError().c_str());
        if (began) {
            pump(engine.get(), 60ull, 600u);
            const VrStatus st = engine->vrStatus();
            CHECK_MSG(st.handActions,
                      "THE ACTION SET IS ATTACHED: one set, two grip-pose actions bound on "
                      "/interaction_profiles/khr/simple_controller, attached once before the "
                      "first xrSyncActions");
            std::printf("HANDS  left valid=%d (%.3f %.3f %.3f) | right valid=%d (%.3f %.3f %.3f) "
                        "| actions=%d joints=%d\n",
                        int(st.hands[VrHandLeft].valid), st.hands[VrHandLeft].position.x,
                        st.hands[VrHandLeft].position.y, st.hands[VrHandLeft].position.z,
                        int(st.hands[VrHandRight].valid), st.hands[VrHandRight].position.x,
                        st.hands[VrHandRight].position.y, st.hands[VrHandRight].position.z,
                        int(st.handActions), int(st.handJoints));
            int located = 0;
            for (unsigned h = 0; h < VrHandCount; ++h) {
                const VrPose &p = st.hands[h];
                if (!p.valid) continue;
                ++located;
                const float len = std::sqrt(p.position.x * p.position.x +
                                            p.position.y * p.position.y +
                                            p.position.z * p.position.z);
                CHECK_MSG(std::isfinite(len) && len < 100.0f,
                          "hand %u is located %.3f m from the rig's origin — a pose, not a "
                          "garbage read", h, len);
                const float q = std::sqrt(p.rotation.x * p.rotation.x +
                                          p.rotation.y * p.rotation.y +
                                          p.rotation.z * p.rotation.z +
                                          p.rotation.w * p.rotation.w);
                CHECK_MSG(std::fabs(q - 1.0f) < 1e-3f,
                          "...with a unit orientation (|q| = %.5f)", q);
            }
            if (!located)
                std::printf("NOTE   this runtime located no controller (Monado's simulated "
                            "builder makes none unless SIMULATED_LEFT/RIGHT ask for one). The "
                            "action set is still proved; the poses are the runtime's half.\n");

            // THE HANDS RIDE THE RIG, exactly as the head does — the one thing
            // about them that is ours and not the runtime's. Moving the origin
            // must move a located hand by the same vector it moves the head.
            if (located) {
                const Vec3 headBefore = st.headPosition;
                const VrPose handBefore = st.hands[st.hands[VrHandLeft].valid ? VrHandLeft
                                                                              : VrHandRight];
                engine->setVrOrigin(Vec3{ 7.0f, 0.0f, -3.0f }, 0.0f);
                pump(engine.get(), engine->vrStatus().frames + 4ull, 60u);
                const VrStatus moved = engine->vrStatus();
                const VrPose handAfter = moved.hands[moved.hands[VrHandLeft].valid ? VrHandLeft
                                                                                   : VrHandRight];
                if (CHECK_MSG(handAfter.valid, "the hand is still located after the rig moved")) {
                    // A simulated head DRIFTS on a wall clock, so the two
                    // deltas are compared to each other rather than to the
                    // rig's own vector: what is asserted is that the hand and
                    // the head were carried by the SAME transform.
                    const float dxHand = handAfter.position.x - handBefore.position.x;
                    const float dxHead = moved.headPosition.x - headBefore.x;
                    CHECK_MSG(std::fabs(dxHand - dxHead) < 0.25f,
                              "THE HAND RIDES THE RIG WITH THE HEAD: the origin moved +7 m in x, "
                              "the head by %.3f and the hand by %.3f", dxHead, dxHand);
                }
                engine->setVrOrigin(Vec3{ 0.0f, 0.0f, 0.0f }, 0.0f);
            }
            engine->endVrSession();
            CHECK(!engine->vrStatus().active);
            CHECK_MSG(!engine->vrStatus().hands[VrHandLeft].valid &&
                          !engine->vrStatus().hands[VrHandRight].valid,
                      "and an ended session reports no hands");
        }
    }

    // =======================================================================
    // 8. WHAT THE WEARER SEES, IN BOTH MODES (lane VR-4; the rule is the
    //    owner's, 2026-09-17). The picture matrix, on the EYES' own pixels.
    //
    // WHAT WAS WRONG BEFORE THIS LANE, measured at this tree: the session's View
    // simply inherited `helpersVisible = true` and never said so, so the
    // HEADSET DREW THE WHOLE DESK in every mode — including the Player's, which
    // shows no furniture at all on the desktop.
    //
    // THE RULE, in two sentences. The DESK'S furniture (grid, light and camera
    // icons, selection outline, gizmo — kHelperBit) follows the HOST MODE: an
    // editor preview shows it in the headset, because watching the editor work
    // from inside the scene is the mode's whole purpose, and a Player shows
    // none. The WEARER'S furniture (the controller proxies — kVrHelperBit) is
    // drawn in EVERY eye, in both modes, because a player needs to see their own
    // hands as much as an author does.
    //
    // HOW IT IS MEASURED. Not by byte equality: the simulated head drifts on a
    // wall clock, so two eye readbacks minutes or milliseconds apart are never
    // identical. Two SATURATED HUES are counted instead — a magenta ring of
    // desk-helpers and a cyan ring of VR-channel objects, both surrounding the
    // wearer so a drifting heading always has some of each in frame — and what
    // is asserted is presence and ABSENCE of a colour, which head motion cannot
    // manufacture.
    // =======================================================================
    {
        setFixtureSky(scene, false);
        // TWO RINGS AROUND THE WEARER, at the cardinal points plus underfoot:
        // Monado's simulated head drifts, and a single object in one direction
        // is a coin toss.
        NodeId deskHelper[5] = { 0, 0, 0, 0, 0 };
        NodeId vrHelper[5] = { 0, 0, 0, 0, 0 };
        const Vec3 ringA[5] = { Vec3{ 0.0f, 1.3f, -1.4f }, Vec3{ 0.0f, 1.3f, 1.4f },
                                Vec3{ -1.4f, 1.3f, 0.0f }, Vec3{ 1.4f, 1.3f, 0.0f },
                                Vec3{ 0.0f, 0.30f, 0.0f } };
        const Vec3 ringB[5] = { Vec3{ 0.0f, 0.55f, -1.1f }, Vec3{ 0.0f, 0.55f, 1.1f },
                                Vec3{ -1.1f, 0.55f, 0.0f }, Vec3{ 1.1f, 0.55f, 0.0f },
                                Vec3{ 0.0f, 2.30f, 0.0f } };
        for (int i = 0; i < 5; ++i) {
            deskHelper[i] = addTestCube(scene, Colour{ 0.95f, 0.02f, 0.95f, 1.0f }, 0.0f, 0.6f);
            setNodePosition(scene, deskHelper[i], ringA[i]);
            setNodeScale(scene, deskHelper[i], Vec3{ 0.8f, 0.8f, 0.8f });
            scene->setNodeHelper(deskHelper[i], true);          // the DESK's

            vrHelper[i] = addTestCube(scene, Colour{ 0.02f, 0.95f, 0.95f, 1.0f }, 0.0f, 0.6f);
            setNodePosition(scene, vrHelper[i], ringB[i]);
            setNodeScale(scene, vrHelper[i], Vec3{ 0.6f, 0.6f, 0.6f });
            scene->setNodeHelper(vrHelper[i], true);
            scene->setNodeVrHelper(vrHelper[i], true);          // ...and the WEARER'S
        }
        // How much of a picture is magenta / cyan. A DIFFERENCE test, not a
        // ratio: the VR view carries HDR and the filmic tonemap (the phase-2
        // profile) and a bright saturated surface comes back through it
        // DESATURATED — a ratio test on the raw channels read the magenta ring
        // as colourless and scored a picture full of it at zero (measured on
        // this fixture: 45,813 magenta px in the plain desktop readback, 0 in
        // the graded eye). What survives any grade is which channel is LOWER.
        const auto countHues = [](const Image &im, size_t &magenta, size_t &cyan) {
            magenta = cyan = 0;
            for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) {
                const int r = im.rgba[i], g = im.rgba[i + 1], b = im.rgba[i + 2];
                if (r - g > 25 && b - g > 25) ++magenta;
                if (g - r > 25 && b - r > 25) ++cyan;
            }
        };

        // ---- THE PLAYER'S SHAPE: no desk furniture in the eyes ------------
        {
            VrConfig cfg;
            cfg.mirror = VrMirrorMode::None;
            cfg.helpers = false;              // what the Player passes (the default)
            const bool began = CHECK_MSG(engine->beginVrSession(scene, cfg),
                                         "beginVrSession (player shape): %s",
                                         engine->lastError().c_str());
            if (began) {
                View *vrView = engine->vrView();
                REQUIRE(vrView != nullptr);
                CHECK(!vrView->helpersVisible());
                CHECK(vrView->vrHelpersVisible());
                pump(engine.get(), 50ull, 500u);
                Image eyes;
                REQUIRE(vrView->readPixels(eyes));
                size_t magenta = 0, cyan = 0;
                countHues(eyes, magenta, cyan);
                std::printf("EYES   player shape: %zu magenta (the desk's) / %zu cyan (the "
                            "wearer's) of %zu px\n", magenta, cyan,
                            eyes.rgba.size() / 4u);
                CHECK_MSG(cyan > 200u,
                          "A PLAYER SEES THEIR OWN CONTROLLERS: %zu px of the VR channel",
                          cyan);
                CHECK_MSG(magenta == 0u,
                          "...AND NONE OF THE DESK'S FURNITURE: %zu px of kHelperBit geometry "
                          "reached the eyes", magenta);
                engine->endVrSession();
            }
        }

        // ---- THE EDITOR'S SHAPE: the desk comes with them -----------------
        {
            VrConfig cfg;
            cfg.mirror = VrMirrorMode::None;
            cfg.helpers = true;               // what EditorVrPreview passes
            const bool began = CHECK_MSG(engine->beginVrSession(scene, cfg),
                                         "beginVrSession (editor shape): %s",
                                         engine->lastError().c_str());
            if (began) {
                View *vrView = engine->vrView();
                REQUIRE(vrView != nullptr);
                CHECK(vrView->helpersVisible());
                CHECK(vrView->vrHelpersVisible());
                pump(engine.get(), 50ull, 500u);
                Image eyes;
                REQUIRE(vrView->readPixels(eyes));
                size_t magenta = 0, cyan = 0;
                countHues(eyes, magenta, cyan);
                std::printf("EYES   editor shape: %zu magenta (the desk's) / %zu cyan (the "
                            "wearer's) of %zu px\n", magenta, cyan,
                            eyes.rgba.size() / 4u);
                // THE THIRD ROW OF THE MATRIX, and the control for the two
                // above: the DESK sees both rings, so "the eyes saw none of the
                // magenta" is a statement about the eyes and not about a
                // fixture that drew nothing.
                {
                    Image dsk;
                    if (desktop->readPixels(dsk)) {
                        size_t dm = 0, dc = 0;
                        countHues(dsk, dm, dc);
                        std::printf("EYES   the desk: %zu magenta / %zu cyan\n", dm, dc);
                        CHECK_MSG(dm > 200u && dc > 200u,
                                  "THE DESKTOP EDITOR VIEW SEES BOTH: %zu px of the desk's "
                                  "furniture and %zu of the wearer's", dm, dc);
                    }
                }
                CHECK_MSG(magenta > 200u,
                          "AN EDITOR PREVIEW SHOWS THE EDITOR WORKING: %zu px of the desk's "
                          "furniture — the grid, the icons, the selection outline — in the "
                          "headset", magenta);
                CHECK_MSG(cyan > 200u, "...and the controllers too: %zu px", cyan);
                engine->endVrSession();
            }
        }
        CHECK(!engine->vrStatus().active);
        // The fixture goes back exactly as it was, so the desktop A/B below
        // compares the same world it opened with.
        for (int i = 0; i < 5; ++i) {
            scene->setNodeVisible(deskHelper[i], false);
            scene->setNodeVisible(vrHelper[i], false);
        }
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
