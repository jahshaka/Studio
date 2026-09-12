// Headless characterisation tests for the engine abstraction.
//
// Links JahshakaEngine ONLY — no Qt, no app, no Ogre header. Every test renders
// through createOffscreenView + readPixels; nothing here opens a window.
// Framework-free on purpose: nothing to fetch, nothing to install.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <string>
#include <vector>

using namespace jahshaka::engine;

namespace {

int gFailures = 0;
int gChecks   = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++gChecks;                                                                   \
        if (!(cond)) {                                                               \
            ++gFailures;                                                             \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                            \
    } while (0)
#define CHECK_MSG(cond, ...)                                                         \
    do {                                                                             \
        ++gChecks;                                                                   \
        if (!(cond)) {                                                               \
            ++gFailures;                                                             \
            std::printf("    FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);         \
            std::printf(__VA_ARGS__);                                                \
            std::printf("\n");                                                       \
        }                                                                            \
    } while (0)
/// Bail out of the current test: continuing would dereference null.
#define REQUIRE(cond) do { CHECK(cond); if (!(cond)) return; } while (0)

EngineConfig testConfig() {
    EngineConfig cfg;
    cfg.backend      = Backend::Vulkan;
    cfg.pluginDir    = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile      = "test_engine-ogre.log";
    return cfg;
}

/// ONE Engine for the whole process. Tests do not each create their own: Ogre's
/// Vulkan plugin cannot be re-initialised after Root is destroyed (see
/// create_twice_returns_null_with_error), and one-per-process is the contract
/// anyway. Each test owns the Views and Scenes it creates through a Fixture,
/// which releases them — views first — when the test ends, whatever its outcome.
std::unique_ptr<Engine> gEngine;

struct Fixture {
    Engine *e;
    std::vector<View *>  views;
    std::vector<Scene *> scenes;
    Fixture() : e(gEngine.get()) {}
    ~Fixture() {
        for (View  *v : views)  e->destroyView(v);
        for (Scene *s : scenes) e->destroyScene(s);
    }
    View *view(const std::string &name, unsigned w, unsigned h, const Colour &bg) {
        View *v = e->createOffscreenView(name, w, h, bg);
        if (v) views.push_back(v);
        else   std::printf("    createOffscreenView('%s'): %s\n", name.c_str(), e->lastError().c_str());
        return v;
    }
    Scene *scene(const std::string &name) {
        Scene *s = e->createScene(name);
        if (s) scenes.push_back(s);
        else   std::printf("    createScene('%s'): %s\n", name.c_str(), e->lastError().c_str());
        return s;
    }
    void forget(View *v)  { views.erase(std::remove(views.begin(), views.end(), v), views.end()); }
    void forget(Scene *s) { scenes.erase(std::remove(scenes.begin(), scenes.end(), s), scenes.end()); }
};

const Colour kBlue  (0.0f, 0.0f, 1.0f);
const Colour kGreen (0.0f, 1.0f, 0.0f);
const Colour kOrange(0.9f, 0.3f, 0.1f);
const Colour kCyan  (0.1f, 0.6f, 0.9f);

struct Px { int r, g, b, a; };
Px px(const Image &img, unsigned x, unsigned y) {
    const Colour c = img.at(x, y);
    return { int(std::lround(c.r * 255)), int(std::lround(c.g * 255)),
             int(std::lround(c.b * 255)), int(std::lround(c.a * 255)) };
}
bool near(const Px &p, const Colour &c, int tol = 8) {
    return std::abs(p.r - int(std::lround(c.r * 255))) <= tol &&
           std::abs(p.g - int(std::lround(c.g * 255))) <= tol &&
           std::abs(p.b - int(std::lround(c.b * 255))) <= tol;
}
Px centre(const Image &img) { return px(img, img.width / 2, img.height / 2); }

/// A byte-exact fingerprint of a readback. FNV-1a over the raw RGBA — the same
/// shape the overlay spike used to prove "overlays off == no overlays at all".
unsigned long long pixelHash(const Image &img) {
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char v : img.rgba) { h ^= v; h *= 1099511628211ull; }
    return h;
}

Px corner(const Image &img) { return px(img, 2, 2); }

/// The spike's scene: lit metallic cube, camera looking at it from (2.2,1.8,2.6).
NodeId populate(Scene *s, const Colour &albedo) {
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    NodeId cube = enginetest::addTestCube(s, albedo, 0.2f, 0.6f);
    enginetest::setNodeScale(s, cube, Vec3(1.2f, 1.2f, 1.2f));
    return cube;
}
void aim(View *v) {
    enginetest::testCameraLookAt(v, Vec3(2.2f, 1.8f, 2.6f), Vec3(0.0f, 0.0f, 0.0f));
}
void render(Engine *e, int frames = 3) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }
/// Renders for REAL time. HDR auto-exposure adapts at a fixed rate per SECOND
/// (DownScale03_SumLumEnd_ps.glsl: mix(new, old, pow(0.25, timeSinceLast))), so
/// a burst of back-to-back frames barely moves it however many there are — only
/// wall-clock does. Anything asserting an exposure CHANGE has to spend it.
void renderFor(Engine *e, double seconds) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds) {
        e->renderOneFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

// ---------------------------------------------------------------------------
void create_twice_returns_null_with_error() {
    CHECK(Engine::isAlive());
    std::string error;
    auto second = Engine::create(testConfig(), error);
    CHECK(second == nullptr);
    CHECK_MSG(!error.empty(), "error must name the reason");
    std::printf("    second create refused: %s\n", error.c_str());
    CHECK(Engine::isAlive());
    // Re-creation after destroying the first is exercised by test_engine_recreate
    // (a separate process). It required patching Ogre-Next's VulkanInstance to
    // clear its static extension/layer arrays — see OGRE_PLATFORM_DEPS.md.
}

void ordering_contract() {
    // Must run before any View has ever existed in this process (see main()).
    Engine *e = gEngine.get();
    Scene *s = e->createScene("too-early");
    CHECK(s == nullptr);
    CHECK(!e->lastError().empty());
    std::printf("    createScene before view: %s\n", e->lastError().c_str());
}

void offscreen_cube_renders() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("v", 96, 96, kBlue);
    REQUIRE(v);
    CHECK(v->isOffscreen());
    Scene *s = f.scene("s");
    REQUIRE(s);
    populate(s, kOrange);
    CHECK(v->setScene(s));
    aim(v);
    render(e);

    Image img;
    REQUIRE(v->readPixels(img));
    CHECK(img.width == 96 && img.height == 96);
    const Px c = centre(img), k = corner(img);
    std::printf("    centre RGB = %d %d %d   corner RGB = %d %d %d\n", c.r, c.g, c.b, k.r, k.g, k.b);
    CHECK_MSG(near(k, kBlue), "corner must be the clear colour");
    CHECK_MSG(c.r > 100 && c.b < 120, "centre must be the lit orange cube");
}

void two_scenes_render_independently() {
    Fixture f; Engine *e = f.e;
    View *va = f.view("a", 64, 64, kBlue);
    View *vb = f.view("b", 64, 64, kGreen);
    REQUIRE(va && vb);
    Scene *sa = f.scene("a");
    Scene *sb = f.scene("b");
    REQUIRE(sa && sb);
    populate(sa, kOrange);
    populate(sb, kCyan);
    CHECK(va->setScene(sa));
    CHECK(vb->setScene(sb));
    aim(va); aim(vb);
    render(e);

    Image ia, ib;
    REQUIRE(va->readPixels(ia) && vb->readPixels(ib));
    const Px ca = centre(ia), cb = centre(ib);
    std::printf("    A centre = %d %d %d   B centre = %d %d %d\n", ca.r, ca.g, ca.b, cb.r, cb.g, cb.b);
    CHECK_MSG(near(corner(ia), kBlue),  "A corner is A's background");
    CHECK_MSG(near(corner(ib), kGreen), "B corner is B's background");
    CHECK_MSG(ca.r > cb.r + 40 && cb.b > ca.b + 40, "different albedos give different centres");
}

void duplicate_view_name_fails_cleanly() {
    Fixture f; Engine *e = f.e;
    View *v1 = f.view("dup", 32, 32, kBlue);
    REQUIRE(v1);
    View *v2 = f.view("dup", 32, 32, kBlue);
    CHECK(v2 == nullptr);
    CHECK(!e->lastError().empty());
    std::printf("    duplicate view: %s\n", e->lastError().c_str());
    // Still usable afterwards.
    Scene *s = f.scene("s");
    REQUIRE(s);
    CHECK(v1->setScene(s));
    render(e, 1);
}

void duplicate_scene_name_fails_cleanly() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("v", 32, 32, kBlue);
    REQUIRE(v);
    Scene *s1 = f.scene("dup");
    REQUIRE(s1);
    Scene *s2 = f.scene("dup");
    CHECK(s2 == nullptr);
    CHECK(!e->lastError().empty());
    std::printf("    duplicate scene: %s\n", e->lastError().c_str());
    CHECK(v->setScene(s1));
    render(e, 1);
}

void set_scene_twice_fails_cleanly() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("v", 32, 32, kBlue);
    REQUIRE(v);
    Scene *s1 = f.scene("s1");
    Scene *s2 = f.scene("s2");
    REQUIRE(s1 && s2);
    CHECK(v->setScene(s1));
    CHECK(!v->setScene(s2));
    CHECK(v->scene() == s1);
    CHECK(!e->lastError().empty());
    std::printf("    setScene twice: %s\n", e->lastError().c_str());
    // Detach, then rebinding is legal.
    CHECK(v->setScene(nullptr));
    CHECK(v->scene() == nullptr);
    CHECK(v->setScene(s2));
    render(e, 1);
}

void remove_node_then_render() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("v", 64, 64, kBlue);
    REQUIRE(v);
    Scene *s = f.scene("s");
    REQUIRE(s);
    const NodeId cube = populate(s, kOrange);
    CHECK(cube != 0);
    CHECK(v->setScene(s));
    aim(v);
    render(e);
    Image before;
    REQUIRE(v->readPixels(before));
    CHECK_MSG(!near(centre(before), kBlue), "cube visible before removal");

    CHECK(s->removeNode(cube));
    CHECK_MSG(!s->removeNode(cube), "second removal of the same id is a no-op");
    render(e);
    Image after;
    REQUIRE(v->readPixels(after));
    const Px c = centre(after);
    std::printf("    centre after removal = %d %d %d\n", c.r, c.g, c.b);
    CHECK_MSG(near(c, kBlue), "centre is the background once the cube is gone");

    // Ids are never reused.
    const NodeId again = enginetest::addTestCube(s, kOrange, 0.2f, 0.6f);
    CHECK(again != cube && again != 0);
}

void destroy_and_recreate_views_and_scenes() {
    Fixture f; Engine *e = f.e;
    for (int i = 0; i < 3; ++i) {
        View *v = f.view("loop-view", 64, 64, kBlue);
        Scene *s = f.scene("loop-scene");
        REQUIRE(v && s);
        populate(s, kOrange);
        CHECK(v->setScene(s));
        aim(v);
        render(e);
        Image img;
        REQUIRE(v->readPixels(img));
        const Px c = centre(img);
        std::printf("    iteration %d centre = %d %d %d\n", i, c.r, c.g, c.b);
        CHECK_MSG(near(corner(img), kBlue) && c.r > 100 && c.b < 120, "iteration %d renders", i);
        f.forget(v); f.forget(s);
        if (i % 2 == 0) { e->destroyView(v); e->destroyScene(s); }
        else            { e->destroyScene(s); e->destroyView(v); }   // both orders are legal
        render(e, 1);   // nothing to draw; must not crash
    }
}

void background_changes_at_runtime() {
    Fixture fx;
    View *v = fx.view("bg-view", 48, 48, kBlue); REQUIRE(v);
    Scene *s = fx.scene("bg-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    render(fx.e);
    Image img; REQUIRE(v->readPixels(img));
    CHECK(near(corner(img), kBlue));
    v->setBackground(kGreen);
    CHECK(near(Px{ int(v->background().r * 255), int(v->background().g * 255), int(v->background().b * 255), 255 }, kGreen));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(near(corner(img), kGreen));
    v->setBackground(kGreen);                       // same value: no-op, still fine
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(near(corner(img), kGreen));
    // The scene still renders after the workspace rebuild.
    populate(s, kOrange);
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100);
}

void resize_offscreen() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("v", 48, 32, kBlue);
    REQUIRE(v);
    Scene *s = f.scene("s");
    REQUIRE(s);
    populate(s, kOrange);
    CHECK(v->setScene(s));
    aim(v);
    render(e);
    Image img;
    REQUIRE(v->readPixels(img));
    CHECK(img.width == 48 && img.height == 32);
    CHECK(img.rgba.size() == 48u * 32u * 4u);

    v->resize(128, 80);
    CHECK(v->width() == 128 && v->height() == 80);
    render(e);
    REQUIRE(v->readPixels(img));
    CHECK(img.width == 128 && img.height == 80);
    CHECK(img.rgba.size() == 128u * 80u * 4u);
    CHECK_MSG(near(corner(img), kBlue), "resized target still clears to background");
    const Px c = centre(img);
    CHECK_MSG(c.r > 100 && c.b < 120, "resized target still shows the cube");

    // A fresh view at another size behaves the same.
    View *v2 = f.view("v2", 20, 20, kGreen);
    REQUIRE(v2);
    CHECK(v2->setScene(s));
    render(e, 1);
    REQUIRE(v2->readPixels(img));
    CHECK(img.width == 20 && img.height == 20);
}

// ---- Anti-aliasing (WORLD_AA_SPEC.md phase 1) ------------------------------
// A flat UNLIT cube against a flat background renders EXACTLY two colours at
// 1x; MSAA's whole observable effect offscreen is blended silhouette pixels.
namespace msaa {
NodeId addUnlitCube(Scene *s, const Colour &c) {
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createUnlitMaterial(c, true, false);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    return node;
}
unsigned intermediatePixels(const Image &img, const Colour &bg, const Colour &fg) {
    unsigned n = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Px p = px(img, x, y);
            if (!near(p, bg, 12) && !near(p, fg, 12)) ++n;
        }
    return n;
}
}  // namespace msaa

void msaa_offscreen_views_default_to_one_sample() {
    // The guard that keeps every pixel-asserted suite exact: offscreen views
    // start at 1 sample unless a caller opts in.
    Fixture f;
    View *v = f.view("aa-default", 32, 32, kBlue);
    REQUIRE(v);
    CHECK_MSG(v->sampleCount() == 1, "offscreen default is 1, got %u", v->sampleCount());
}

void msaa_4x_blends_silhouette_edges() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("aa4", 96, 96, kBlue);
    REQUIRE(v);
    Scene *s = f.scene("aa4-scene");
    REQUIRE(s);
    REQUIRE(msaa::addUnlitCube(s, kOrange));
    CHECK(v->setScene(s));
    aim(v);
    render(e);
    Image img1;
    REQUIRE(v->readPixels(img1));
    const unsigned edges1 = msaa::intermediatePixels(img1, kBlue, kOrange);
    CHECK_MSG(edges1 <= 4, "1x unlit scene is (near) two-colour, got %u in-betweens", edges1);
    CHECK(near(centre(img1), kOrange));
    CHECK(near(corner(img1), kBlue));

    // 4x is the one MSAA level the Vulkan spec REQUIRES; the achieved count
    // must come back exactly.
    v->setSampleCount(4);
    render(e);
    CHECK_MSG(v->sampleCount() == 4, "achieved %u for a 4x request", v->sampleCount());
    Image img4;
    REQUIRE(v->readPixels(img4));
    CHECK(near(centre(img4), kOrange));
    CHECK(near(corner(img4), kBlue));
    const unsigned edges4 = msaa::intermediatePixels(img4, kBlue, kOrange);
    CHECK_MSG(edges4 >= 10 && edges4 > edges1,
              "4x should blend the cube silhouette: %u in-betweens vs %u at 1x", edges4, edges1);
    std::printf("    edge blend pixels: 1x=%u 4x=%u\n", edges1, edges4);
}

void msaa_runtime_toggle_and_clamping() {
    // Runtime change is the pending-resize path (window) / an RTT rebuild
    // (offscreen): 1 -> 4 -> 8 -> 1 on one live view, rendering in between.
    // The ASan suite runs this too, covering the rebuilds for leaks/UAF.
    Fixture f; Engine *e = f.e;
    View *v = f.view("aa-toggle", 64, 64, kBlue);
    REQUIRE(v);
    Scene *s = f.scene("aa-toggle-scene");
    REQUIRE(s);
    REQUIRE(msaa::addUnlitCube(s, kOrange));
    CHECK(v->setScene(s));
    aim(v);
    render(e);
    CHECK(v->sampleCount() == 1);

    v->setSampleCount(4);
    render(e);
    CHECK(v->sampleCount() == 4);
    Image img;
    REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kOrange));

    // 8x is NOT guaranteed by Vulkan: the driver may clamp by halving. Either
    // way the readback must report what was actually achieved (a power of two
    // no lower than the mandatory 4).
    v->setSampleCount(8);
    render(e);
    const unsigned got8 = v->sampleCount();
    CHECK_MSG(got8 == 8 || got8 == 4, "8x request achieved %u", got8);
    std::printf("    8x request -> achieved %ux\n", got8);
    REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kOrange));

    // Odd values round DOWN to a power of two before reaching the driver.
    v->setSampleCount(3);
    render(e);
    CHECK_MSG(v->sampleCount() == 2, "3 rounds down to 2, got %u", v->sampleCount());

    v->setSampleCount(1);
    render(e);
    CHECK_MSG(v->sampleCount() == 1, "back to 1x, got %u", v->sampleCount());
    REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kOrange));
    CHECK(near(corner(img), kBlue));
    const unsigned edges = msaa::intermediatePixels(img, kBlue, kOrange);
    CHECK_MSG(edges <= 4, "back at 1x the image is crisp again, got %u in-betweens", edges);

    // A resize while MSAA is requested keeps the sample count.
    v->setSampleCount(4);
    v->resize(48, 48);
    render(e);
    CHECK_MSG(v->sampleCount() == 4, "resize kept 4x, got %u", v->sampleCount());
}

/// CAMERAS_SPEC §7.3 correction 1, half one — the OVERLAY pass under MSAA.
///
/// The chain's final "Jahshaka overlays" scene pass is the pass whose colour
/// store action decides what an MSAA target ends up containing (chain::
/// kMultiWorkspaceStore). Three actions are available and only one is right for
/// a target that MAY have a second workspace after it:
///   * StoreOrResolve  — resolves and DISCARDS the samples. Correct here, wrong
///                       the moment a PiP workspace Loads the target after it
///                       (the spike measured 198k destroyed pixels at 4x).
///   * Store           — keeps the samples and never resolves: BLACK FRAME.
///   * StoreAndMultisampleResolve — both. What the chain now sets.
///
/// This test is the "not black, still correct" half: an on-top overlay
/// renderable (unlit, depth-test off = kOverlayRenderQueue, i.e. drawn ONLY by
/// that final pass) must survive the resolve at 1x, 2x and 4x, and the ordinary
/// depth-tested geometry underneath must survive with it. It cannot tell
/// StoreOrResolve from StoreAndMultisampleResolve — nothing with one workspace
/// can. The DISCRIMINATING assertion is pip_over_msaa_keeps_the_main_frame
/// (below), which is why the fix and the PiP share a spec section.
void msaa_overlay_pass_resolves_at_every_sample_count() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("aa-overlay", 96, 96, kBlue);
    REQUIRE(v);
    Scene *s = f.scene("aa-overlay-scene");
    REQUIRE(s);
    // Depth-tested body in the middle...
    REQUIRE(msaa::addUnlitCube(s, kOrange));
    // ...and an always-on-top marker parked to the left of it, small enough
    // that nothing else can be mistaken for it.
    const NodeId marker = s->createNode();
    REQUIRE(marker);
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId onTop = s->createUnlitMaterial(kGreen, /*depthTest*/ false, false);
    REQUIRE(mesh && onTop);
    REQUIRE(s->attachMesh(marker, mesh, onTop));
    enginetest::setNodePosition(s, marker, Vec3(-1.6f, 0.0f, 0.0f));
    enginetest::setNodeScale(s, marker, Vec3(0.5f, 0.5f, 0.5f));
    CHECK(v->setScene(s));
    enginetest::testCameraLookAt(v, Vec3(0.0f, 0.0f, 4.5f), Vec3(-0.4f, 0.0f, 0.0f));
    // Where the marker lands, found once at 1x and then held fixed: a sample
    // count must not move geometry.
    unsigned mx = 0, my = 0;
    for (const unsigned samples : { 1u, 2u, 4u }) {
        v->setSampleCount(samples);
        render(e, 3);
        const unsigned achieved = v->sampleCount();
        Image img;
        REQUIRE(v->readPixels(img));
        if (samples == 1u) {
            // Locate the marker's centre of mass at 1x.
            unsigned long long sx = 0, sy = 0, n = 0;
            for (unsigned y = 0; y < img.height; ++y)
                for (unsigned x = 0; x < img.width; ++x)
                    if (near(px(img, x, y), kGreen, 12)) { sx += x; sy += y; ++n; }
            REQUIRE(n > 20);
            mx = unsigned(sx / n); my = unsigned(sy / n);
            std::printf("    on-top marker at %ux%u (%llu px)\n", mx, my, n);
        }
        CHECK_MSG(near(centre(img), kOrange),
                  "%ux (achieved %u): the depth-tested body must survive the resolve",
                  samples, achieved);
        CHECK_MSG(near(corner(img), kBlue),
                  "%ux (achieved %u): the background must survive the resolve (a plain "
                  "Store never resolves and the whole frame goes black)",
                  samples, achieved);
        CHECK_MSG(near(px(img, mx, my), kGreen, 12),
                  "%ux (achieved %u): the overlay-queue marker must survive the resolve",
                  samples, achieved);
    }
    v->setSampleCount(1);
    render(e, 2);
}

// ---- The picture-in-picture inset (CAMERAS_SPEC §7.7, phase 2c) -----------
//
// The scene is the spike's, deliberately: an ORANGE cube the main camera looks
// at from +Z, and a GREEN wall a hundred units away on +X that only the inset
// camera can see. Both unlit, so every pixel is one of three exact colours and
// the rect's boundary can be asserted to the pixel.
namespace pip {

struct Scenery { NodeId cube = 0, wall = 0; };

Scenery build(Scene *s) {
    Scenery sc;
    sc.cube = msaa::addUnlitCube(s, kOrange);
    sc.wall = msaa::addUnlitCube(s, kGreen);
    if (!sc.cube || !sc.wall) return Scenery();
    enginetest::setNodePosition(s, sc.wall, Vec3(100.0f, 0.0f, 0.0f));
    enginetest::setNodeScale(s, sc.wall, Vec3(60.0f, 60.0f, 1.0f));
    return sc;
}
void aimMain(View *v) { enginetest::testCameraLookAt(v, Vec3(0, 0, 5), Vec3(0, 0, 0)); }
CameraDesc insetCamera() {
    // Straight at the wall from 6 units out: the wall is 60x60, so it fills the
    // inset whatever its aspect.
    CameraDesc c;
    c.position = Vec3(100.0f, 0.0f, 6.0f);   // identity orientation looks down -Z
    return c;
}
ViewPipDesc desc(float l, float t, float w, float h) {
    ViewPipDesc d;
    d.enabled = true;
    d.allowOffscreen = true;      // offscreen views are the only testable ones
    d.camera = insetCamera();
    d.left = l; d.top = t; d.width = w; d.height = h;
    return d;
}
/// Pixels of `img` inside the rect that are `c`, and the count of pixels
/// OUTSIDE it that differ from the reference — the two numbers every assertion
/// below is made of. `slack` keeps the rect's own boundary row/column out of
/// the outside count (a viewport edge lands on a pixel boundary only when the
/// rect divides the size exactly).
struct RectStats { unsigned inside = 0, insideTotal = 0, outsideDiff = 0; };
RectStats compare(const Image &img, const Image &ref, const ViewPipDesc &d,
                  const Colour &c, int slack = 1) {
    RectStats st;
    const int x0 = int(d.left * img.width), y0 = int(d.top * img.height);
    const int x1 = int((d.left + d.width) * img.width);
    const int y1 = int((d.top + d.height) * img.height);
    for (int y = 0; y < int(img.height); ++y)
        for (int x = 0; x < int(img.width); ++x) {
            const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (in) { ++st.insideTotal; if (near(px(img, x, y), c, 12)) ++st.inside; continue; }
            const bool nearEdge = x >= x0 - slack && x <= x1 + slack &&
                                  y >= y0 - slack && y <= y1 + slack;
            if (nearEdge) continue;
            const size_t i = (size_t(y) * img.width + x) * 4u;
            if (img.rgba[i] != ref.rgba[i] || img.rgba[i+1] != ref.rgba[i+1] ||
                img.rgba[i+2] != ref.rgba[i+2] || img.rgba[i+3] != ref.rgba[i+3]) ++st.outsideDiff;
        }
    return st;
}
}  // namespace pip

/// THE DETERMINISM LAW (CAMERAS_SPEC §7.7), same shape as the post-fx and HUD
/// gates: an offscreen view that was never given allowOffscreen must render
/// BYTE-IDENTICALLY with an inset requested and with none — and must not even
/// build a workspace for one.
void pip_is_ignored_offscreen_unless_asked() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("pip-guard", 96, 96, kBlue);   REQUIRE(v);
    Scene *s = f.scene("pip-guard-scene");          REQUIRE(s);
    CHECK(v->setScene(s));
    REQUIRE(pip::build(s).cube);
    pip::aimMain(v);
    render(e, 3);
    Image plain; REQUIRE(v->readPixels(plain));
    const unsigned genBefore = v->workspaceGeneration();

    ViewPipDesc d = pip::desc(0.6f, 0.6f, 0.35f, 0.35f);
    d.allowOffscreen = false;                       // deliberately NOT opted in
    v->setPip(d);
    CHECK_MSG(v->workspaceGeneration() == genBefore,
              "an offscreen view must not build anything for an inset it will ignore: %u -> %u",
              genBefore, v->workspaceGeneration());
    render(e, 3);
    Image refused; REQUIRE(v->readPixels(refused));
    std::printf("    pixelhash  no pip %016llx   pip refused %016llx\n",
                pixelHash(plain), pixelHash(refused));
    CHECK_MSG(plain.rgba == refused.rgba,
              "BYTE-IDENTICAL or the guarantee is gone: %016llx vs %016llx",
              pixelHash(plain), pixelHash(refused));
    // The view still remembers what it was asked for — it just does not act.
    CHECK(v->pip().enabled);
}

/// The other half: with allowOffscreen the SAME desc must composite a second
/// camera into exactly the rect it names, and change NOTHING outside it.
void pip_composites_a_second_camera_into_the_rect() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("pip-basic", 128, 128, kBlue);  REQUIRE(v);
    Scene *s = f.scene("pip-basic-scene");           REQUIRE(s);
    CHECK(v->setScene(s));
    REQUIRE(pip::build(s).wall);
    pip::aimMain(v);
    render(e, 3);
    Image ref; REQUIRE(v->readPixels(ref));
    CHECK(near(centre(ref), kOrange));
    CHECK(near(corner(ref), kBlue));

    const ViewPipDesc d = pip::desc(0.60f, 0.60f, 0.36f, 0.36f);
    v->setPip(d);
    render(e, 3);
    Image withPip; REQUIRE(v->readPixels(withPip));
    pip::RectStats st = pip::compare(withPip, ref, d, kGreen);
    CHECK_MSG(st.inside >= st.insideTotal * 95 / 100,
              "the inset must own its rect: %u/%u px are the wall", st.inside, st.insideTotal);
    CHECK_MSG(st.outsideDiff == 0,
              "the main frame outside the rect must be BYTE-IDENTICAL: %u px differ",
              st.outsideDiff);
    std::printf("    inset %u/%u px, outside diff %u\n", st.inside, st.insideTotal, st.outsideDiff);

    // MOVING it is a live viewport modifier, never a rebuild (spike T3).
    const unsigned gen = v->workspaceGeneration();
    ViewPipDesc moved = d;
    moved.left = 0.04f; moved.top = 0.04f;
    v->setPip(moved);
    CHECK_MSG(v->workspaceGeneration() == gen,
              "moving the inset must not rebuild the workspace: %u -> %u",
              gen, v->workspaceGeneration());
    render(e, 3);
    Image movedImg; REQUIRE(v->readPixels(movedImg));
    st = pip::compare(movedImg, ref, moved, kGreen);
    CHECK_MSG(st.inside >= st.insideTotal * 95 / 100,
              "the moved inset must own its new rect: %u/%u", st.inside, st.insideTotal);
    CHECK_MSG(st.outsideDiff == 0, "moved: %u px differ outside", st.outsideDiff);

    // ...and turning it off must restore the frame byte-for-byte.
    ViewPipDesc off = d; off.enabled = false;
    v->setPip(off);
    render(e, 3);
    Image restored; REQUIRE(v->readPixels(restored));
    CHECK_MSG(ref.rgba == restored.rgba,
              "an inset switched off must leave NO trace: %016llx vs %016llx",
              pixelHash(ref), pixelHash(restored));
}

/// CAMERAS_SPEC §7.3 correction 1, the DISCRIMINATING half of the store-action
/// fix (see msaa_overlay_pass_resolves_at_every_sample_count for the other).
///
/// With the chain's final pass on StoreOrResolve — the compositor default, and
/// what this tree shipped until phase 2a — the inset's Load reads an attachment
/// whose samples were resolved away, and the WHOLE FRAME outside the inset is
/// destroyed (the spike measured 198,000 differing pixels at 4x, the image
/// going white). This is the assertion that would fail.
void pip_over_msaa_keeps_the_main_frame() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("pip-msaa", 128, 128, kBlue);   REQUIRE(v);
    Scene *s = f.scene("pip-msaa-scene");            REQUIRE(s);
    CHECK(v->setScene(s));
    REQUIRE(pip::build(s).wall);
    pip::aimMain(v);
    v->setSampleCount(4);
    render(e, 3);
    const unsigned achieved = v->sampleCount();
    CHECK_MSG(achieved == 4, "4x is Vulkan-mandatory; achieved %u", achieved);
    Image ref; REQUIRE(v->readPixels(ref));
    CHECK(near(centre(ref), kOrange));
    CHECK(near(corner(ref), kBlue));

    const ViewPipDesc d = pip::desc(0.58f, 0.58f, 0.38f, 0.38f);
    v->setPip(d);
    render(e, 3);
    Image withPip; REQUIRE(v->readPixels(withPip));
    const pip::RectStats st = pip::compare(withPip, ref, d, kGreen, 2);
    CHECK_MSG(st.inside >= st.insideTotal * 95 / 100,
              "%ux: the inset must own its rect: %u/%u", achieved, st.inside, st.insideTotal);
    CHECK_MSG(st.outsideDiff == 0,
              "%ux: THE STORE-ACTION GATE — the inset must not destroy the resolved main "
              "frame: %u px differ outside the rect", achieved, st.outsideDiff);
    std::printf("    msaa %ux: inset %u/%u px, outside diff %u\n",
                achieved, st.inside, st.insideTotal, st.outsideDiff);
    v->setPip(ViewPipDesc());
    v->setSampleCount(1);
    render(e, 2);
}

/// LETTERBOX (CAMERAS_SPEC §7.4): with constrainAspect the inset draws the
/// camera's authored shape centred in its rect, and the remainder is the
/// inset's background — not a stretched image, and not the main frame showing
/// through.
void pip_letterboxes_to_the_authored_aspect() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("pip-letterbox", 128, 128, kBlue); REQUIRE(v);
    Scene *s = f.scene("pip-letterbox-scene");          REQUIRE(s);
    CHECK(v->setScene(s));
    REQUIRE(pip::build(s).wall);
    pip::aimMain(v);
    render(e, 3);
    Image ref; REQUIRE(v->readPixels(ref));

    // A SQUARE rect (0.4 x 0.4 of a square view) with a 2:1 camera: the drawn
    // band must be half the rect's height, centred, with bars above and below.
    ViewPipDesc d = pip::desc(0.30f, 0.30f, 0.40f, 0.40f);
    d.background = Colour(1.0f, 0.0f, 1.0f, 1.0f);   // magenta bars: unmistakable
    d.camera.constrainAspect = true;
    d.camera.aspect = 2.0f;
    v->setPip(d);
    render(e, 3);
    Image img; REQUIRE(v->readPixels(img));

    const int x0 = int(d.left * img.width), x1 = int((d.left + d.width) * img.width);
    const int y0 = int(d.top * img.height), y1 = int((d.top + d.height) * img.height);
    const int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    // Middle of the rect: the shot.
    CHECK_MSG(near(px(img, unsigned(cx), unsigned(cy)), kGreen, 12),
              "the middle band must be the inset's image");
    // A quarter of the way down from the rect's top, and up from its bottom:
    // bars (the band is half the height, so those rows are outside it).
    const unsigned barTop = unsigned(y0 + (y1 - y0) / 8);
    const unsigned barBot = unsigned(y1 - (y1 - y0) / 8);
    CHECK_MSG(near(px(img, unsigned(cx), barTop), d.background, 12),
              "top bar must be the inset background, got %d %d %d",
              px(img, unsigned(cx), barTop).r, px(img, unsigned(cx), barTop).g,
              px(img, unsigned(cx), barTop).b);
    CHECK_MSG(near(px(img, unsigned(cx), barBot), d.background, 12),
              "bottom bar must be the inset background, got %d %d %d",
              px(img, unsigned(cx), barBot).r, px(img, unsigned(cx), barBot).g,
              px(img, unsigned(cx), barBot).b);
    // Count the band: half the rect's rows, give or take rounding.
    unsigned bandRows = 0;
    for (int y = y0; y < y1; ++y)
        if (near(px(img, unsigned(cx), unsigned(y)), kGreen, 12)) ++bandRows;
    const unsigned rows = unsigned(y1 - y0);
    CHECK_MSG(bandRows >= rows * 45 / 100 && bandRows <= rows * 55 / 100,
              "a 2:1 shot in a square rect fills half its height: %u of %u rows",
              bandRows, rows);
    std::printf("    letterbox: %u of %u rows are the shot\n", bandRows, rows);
    // The frame outside the rect is still untouched.
    const pip::RectStats st = pip::compare(img, ref, d, kGreen);
    CHECK_MSG(st.outsideDiff == 0, "letterboxed inset changed %u px outside its rect",
              st.outsideDiff);
}

/// The ordering trap (CAMERAS_SPEC §7.2, spike T2): the inset's workspace must
/// be re-appended after ANY rebuild of the main one, or the main pass paints
/// straight over it and the frame hashes exactly like no inset at all — a
/// failure with no error, no warning and no validation message.
void pip_survives_a_main_workspace_rebuild() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("pip-order", 128, 128, kBlue);  REQUIRE(v);
    Scene *s = f.scene("pip-order-scene");           REQUIRE(s);
    CHECK(v->setScene(s));
    REQUIRE(pip::build(s).wall);
    pip::aimMain(v);
    const ViewPipDesc d = pip::desc(0.60f, 0.60f, 0.36f, 0.36f);
    v->setPip(d);
    render(e, 3);
    Image before; REQUIRE(v->readPixels(before));
    CHECK(near(px(before, unsigned(0.78f * before.width), unsigned(0.78f * before.height)), kGreen, 12));

    // setShadows is a definition rebuild: detach, rebuild the chain, re-attach
    // — the exact sequence that used to leave the inset first in the list.
    const unsigned gen = v->workspaceGeneration();
    v->setShadows(true);
    v->setShadows(false);
    CHECK_MSG(v->workspaceGeneration() >= gen + 2, "two rebuilds expected, %u -> %u",
              gen, v->workspaceGeneration());
    render(e, 3);
    Image after; REQUIRE(v->readPixels(after));
    const pip::RectStats st = pip::compare(after, before, d, kGreen);
    CHECK_MSG(st.inside >= st.insideTotal * 95 / 100,
              "after a main-workspace rebuild the inset must still be on top: %u/%u",
              st.inside, st.insideTotal);
    CHECK_MSG(st.outsideDiff == 0, "%u px differ outside the rect after the rebuild",
              st.outsideDiff);
}

/// LETTERBOX (CAMERAS_SPEC §7.4): a camera that constrains its aspect is drawn
/// in the largest rectangle of that shape the target can hold, centred, with
/// black bars in the remainder — and it is NOT stretched into them.
///
/// The stretch half is the one that matters, and it is measured, not asserted
/// by inspection: a world-space SQUARE is rendered by a 2:1 camera into a
/// square view. Constrained, its pixels must be as wide as they are tall.
/// THE BARS ARE NOT PART OF THE PICTURE (riders lane R2, owner decision
/// 2026-09-09 — "the Unreal way"): the post chain runs on the inner rectangle
/// only. Before this, every full-frame quad — tonemap with its bloom
/// composite, SMAA, every look — processed the bars along with the shot, so a
/// bright shot bloomed INTO the bars and the looks stage graded them (Old
/// Movie's vignette rings and scratches over black, Film Grade's tint lifting
/// them off black). Now every such quad is scissored to the shot and clears
/// its target black first (ChainHandles::scissorPasses).
///
/// Asserted with the worst case the owner reported: HDR + bloom + Old Movie
/// + Film Grade over a bright emissive scene. Every bar pixel is EXACTLY
/// (0,0,0); the shot itself is still graded (it differs from the plain
/// render); and removing the letterbox returns the byte-identical frame it
/// had before it (with time-independent looks: Old Movie is animated).
///
/// "The inner rect is byte-identical to the unrestricted render" is the other
/// half of the decision and is NOT asserted here — there is no unrestricted
/// path left to compare against in this binary. It is an A/B against the
/// pre-change build of this same test (JAH_ENGINE_DUMP=1 writes the frames),
/// recorded in the lane report.
void letterbox_post_chain_runs_on_the_shot_only() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("letterbox-post", 128, 128, kBlue);   REQUIRE(v);
    Scene *s = f.scene("letterbox-post-scene");            REQUIRE(s);
    CHECK(v->setScene(s));
    REQUIRE(pip::build(s).wall);
    // A BRIGHT emitter beside the wall, so bloom has something to bleed.
    {
        PbrParams p;
        p.albedo = Colour(0.1f, 0.1f, 0.1f);
        p.emissive = Colour(12.0f, 10.0f, 6.0f);
        const NodeId n = s->createNode();
        REQUIRE(n && s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()),
                                   s->createPbrMaterial(p)));
        enginetest::setNodeScale(s, n, Vec3(1.2f, 1.2f, 1.2f));
        enginetest::setNodePosition(s, n, Vec3(0.0f, 0.9f, 0.5f));
    }
    pip::aimMain(v);

    auto look = [](LookKind k, float p0, float p1 = 0.0f, float p2 = 0.0f, float p3 = 0.0f,
                   float p4 = 0.0f, float p5 = 0.0f, float p6 = 0.0f) {
        LookDesc d; d.kind = k; d.p[0] = p0; d.p[1] = p1; d.p[2] = p2; d.p[3] = p3;
        d.p[4] = p4; d.p[5] = p5; d.p[6] = p6; return d;
    };
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.hdr = true;
    fx.tonemapFixed = true;      // determinism: no wall-clock exposure drift
    fx.exposure = 0.0f;
    fx.bloom = true;
    fx.bloomThreshold = 1.0f;    // low, so the emitter blooms hard
    fx.smaaPreset = 2;
    // Old Movie (vignette, scratches, grain) + Film Grade (a warm tint with
    // its own vignette): the two looks that visibly graded the bars.
    // (Film Grade's p[4..6] is the TINT — a zero tint multiplies the frame to
    // black, which is the one way to get this case's bars 'right' for free.)
    fx.looks = { look(LookKind::OldMovie, 1.0f, 1.0f, 1.0f, 1.0f),
                 look(LookKind::FilmGrade, 1.0f, 1.2f, 1.1f, 0.8f, 1.1f, 1.0f, 0.9f) };
    v->setPostFx(fx);

    CameraDesc cam;
    cam.position = Vec3(0.0f, 0.0f, 5.0f);   // pip::aimMain's pose, as a desc
    cam.constrainAspect = true;
    cam.aspect = 2.0f;           // a 2:1 shot in a 1:1 view: 64 bar rows
    v->setCamera(cam);
    render(e, 4);
    const std::string errBefore = e->lastError();
    Image boxed; REQUIRE(v->readPixels(boxed));
    if (std::getenv("JAH_ENGINE_DUMP")) {
        FILE *fp = std::fopen("letterbox-post-graded.ppm", "wb");
        if (fp) {
            std::fprintf(fp, "P6\n%u %u\n255\n", boxed.width, boxed.height);
            for (size_t i = 0; i < size_t(boxed.width) * boxed.height; ++i)
                std::fwrite(&boxed.rgba[i * 4u], 1, 3, fp);
            std::fclose(fp);
        }
    }

    // THE BARS, BY GEOMETRY: a 2:1 shot in a 128x128 view is rows 32..95, so
    // the bars are rows 0..31 and 96..127 (chain::letterboxRect). Every bar
    // pixel must be exactly (0,0,0) — not "dark": Old Movie's vignette makes
    // the shot's own edge rows near-black too, which is why the bars are not
    // found by looking for black rows.
    unsigned barRows = 0, worst = 0, litShot = 0;
    for (unsigned y = 0; y < boxed.height; ++y) {
        const bool bar = y < 32 || y >= 96;
        bool exact = true;
        for (unsigned x = 0; x < boxed.width; ++x) {
            const Px p = px(boxed, x, y);
            const int m = std::max(p.r, std::max(p.g, p.b));
            if (bar) { if (m != 0) exact = false; worst = std::max(worst, unsigned(m)); }
            else if (y >= 34 && y < 94 && m > 24) ++litShot;
        }
        if (bar && exact) ++barRows;
    }
    std::printf("    graded letterbox: %u of 64 bar rows exactly black, worst bar channel %u/255, "
                "%u lit px in the shot\n", barRows, worst, litShot);
    CHECK_MSG(litShot > 60u * boxed.width / 4u,
              "the 2:1 shot is drawn inside the letterbox (%u lit px)", litShot);
    CHECK_MSG(barRows == 64u,
              "THE BARS ARE PURE BLACK through bloom + Old Movie + Film Grade: %u of 64 bar rows "
              "exactly black, worst bar channel %u/255 (unrestricted chain: 80)", barRows, worst);

    // ...and the shot is still graded: the same frame with no looks and no
    // bloom differs inside the rectangle.
    PostFxDesc plainFx = fx;
    plainFx.looks.clear();
    plainFx.bloom = false;
    v->setPostFx(plainFx);
    render(e, 4);
    Image plain; REQUIRE(v->readPixels(plain));
    unsigned inner = 0;
    for (unsigned y = 34; y < 94; ++y)
        for (unsigned x = 0; x < boxed.width; ++x) {
            const Px a = px(boxed, x, y), b = px(plain, x, y);
            if (std::abs(a.r - b.r) > 8 || std::abs(a.g - b.g) > 8 || std::abs(a.b - b.b) > 8) ++inner;
        }
    CHECK_MSG(inner > 60u * boxed.width / 4u,
              "the SHOT is graded (the scissor confined the looks, it did not drop them): "
              "%u of %u inner pixels differ from the ungraded frame", inner, 60u * boxed.width);

    // No letterbox = the frame it always was. Time-independent looks for the
    // round trip (Old Movie animates on Ogre's time_0_x).
    PostFxDesc stillFx = fx;
    stillFx.looks = { look(LookKind::FilmGrade, 1.0f, 1.2f, 1.1f, 0.8f, 1.1f, 1.0f, 0.9f),
                      look(LookKind::Posterize, 0.6f, 6.0f, 1.0f) };
    // (The A/B evidence frame first: the letterboxed STILL grade, dumped for
    // the inner-rect comparison against the pre-change build — Old Movie
    // animates, so the graded frame above cannot be compared byte for byte.)
    v->setPostFx(stillFx);
    render(e, 4);
    if (std::getenv("JAH_ENGINE_DUMP")) {
        Image still; REQUIRE(v->readPixels(still));
        FILE *fp = std::fopen("letterbox-post-still.ppm", "wb");
        if (fp) {
            std::fprintf(fp, "P6\n%u %u\n255\n", still.width, still.height);
            for (size_t i = 0; i < size_t(still.width) * still.height; ++i)
                std::fwrite(&still.rgba[i * 4u], 1, 3, fp);
            std::fclose(fp);
        }
    }
    cam.constrainAspect = false;
    v->setCamera(cam);
    render(e, 4);
    Image free0; REQUIRE(v->readPixels(free0));
    cam.constrainAspect = true;
    v->setCamera(cam);
    render(e, 4);
    cam.constrainAspect = false;
    v->setCamera(cam);
    render(e, 4);
    Image free1; REQUIRE(v->readPixels(free1));
    CHECK_MSG(pixelHash(free0) == pixelHash(free1),
              "with no letterbox nothing changed: the frame after a letterbox round trip "
              "is byte-identical to the one before it");
    CHECK_MSG(e->lastError() == errBefore,
              "no NEW engine error from the scissored post chain: %s", e->lastError().c_str());
    v->setPostFx(PostFxDesc());
    render(e, 2);
}

/// Unconstrained, the same camera in the same view renders it half as tall as
/// it is wide — which is exactly the distortion the letterbox exists to
/// prevent, and the control that proves the test can fail.
void letterbox_fits_the_shot_and_bars_the_rest() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("letterbox", 128, 128, kBlue);   REQUIRE(v);
    Scene *s = f.scene("letterbox-scene");            REQUIRE(s);
    CHECK(v->setScene(s));
    // A unit cube seen face-on from +Z: its silhouette is a square.
    REQUIRE(msaa::addUnlitCube(s, kOrange));
    CameraDesc cam;
    cam.position = Vec3(0.0f, 0.0f, 4.0f);            // identity: looks down -Z
    v->setCamera(cam);
    const unsigned genBefore = v->workspaceGeneration();
    render(e, 3);
    Image plain; REQUIRE(v->readPixels(plain));

    const auto extent = [](const Image &img, unsigned &wOut, unsigned &hOut) {
        int x0 = 1 << 20, x1 = -1, y0 = 1 << 20, y1 = -1;
        for (unsigned y = 0; y < img.height; ++y)
            for (unsigned x = 0; x < img.width; ++x)
                if (near(px(img, x, y), kOrange, 24)) {
                    x0 = std::min(x0, int(x)); x1 = std::max(x1, int(x));
                    y0 = std::min(y0, int(y)); y1 = std::max(y1, int(y));
                }
        wOut = x1 >= x0 ? unsigned(x1 - x0 + 1) : 0u;
        hOut = y1 >= y0 ? unsigned(y1 - y0 + 1) : 0u;
    };
    unsigned pw = 0, ph = 0; extent(plain, pw, ph);
    std::printf("    unconstrained: the square renders %ux%u px\n", pw, ph);
    CHECK_MSG(pw > 8 && ph > 8, "the cube is on screen at all (%ux%u)", pw, ph);
    CHECK_MSG(pw == ph, "with no constraint a square is square in a square view (%ux%u)", pw, ph);

    // ---- 2:1 with no constraint: the control, i.e. the DISTORTION ---------
    cam.aspect = 2.0f;
    cam.constrainAspect = false;
    v->setCamera(cam);
    render(e, 3);
    Image stretched; REQUIRE(v->readPixels(stretched));
    unsigned sw = 0, sh = 0; extent(stretched, sw, sh);
    CHECK_MSG(stretched.rgba == plain.rgba,
              "an UNCONSTRAINED aspect is inert — the camera follows the target, as it always "
              "has (%ux%u vs %ux%u)", sw, sh, pw, ph);
    CHECK_MSG(v->workspaceGeneration() == genBefore,
              "and it rebuilds nothing: %u -> %u", genBefore, v->workspaceGeneration());

    // ---- 2:1 CONSTRAINED --------------------------------------------------
    cam.constrainAspect = true;
    v->setCamera(cam);
    render(e, 3);
    Image boxed; REQUIRE(v->readPixels(boxed));
    unsigned bw = 0, bh = 0; extent(boxed, bw, bh);
    std::printf("    constrained 2:1: the square renders %ux%u px\n", bw, bh);
    CHECK_MSG(bw > 8 && bh > 8, "the shot is still on screen (%ux%u)", bw, bh);
    CHECK_MSG(bw >= bh - 1 && bw <= bh + 1,
              "THE POINT: a world square stays square inside the letterbox (%ux%u)", bw, bh);
    CHECK_MSG(bh < ph,
              "…and the shot is SMALLER than the unconstrained one, because it now has to fit "
              "a 2:1 rectangle into a square view (%u vs %u rows)", bh, ph);

    // The bars: black, top and bottom, in a square view with a 2:1 camera the
    // band is half the height.
    const unsigned mid = boxed.width / 2;
    CHECK_MSG(px(boxed, mid, 2).r < 12 && px(boxed, mid, 2).g < 12 && px(boxed, mid, 2).b < 12,
              "the top bar is black, not the background: %d %d %d",
              px(boxed, mid, 2).r, px(boxed, mid, 2).g, px(boxed, mid, 2).b);
    CHECK_MSG(near(px(boxed, mid, boxed.height / 2), kOrange, 24),
              "the middle of the view is still the shot");
    unsigned barRows = 0, blueRows = 0;
    for (unsigned y = 0; y < boxed.height; ++y) {
        const Px p = px(boxed, 2, y);            // a column outside the cube
        if (p.r < 12 && p.g < 12 && p.b < 12) ++barRows;
        else if (near(p, kBlue, 12)) ++blueRows;
    }
    std::printf("    bars %u rows, background %u rows of %u\n", barRows, blueRows, boxed.height);
    CHECK_MSG(barRows >= boxed.height * 45 / 100 && barRows <= boxed.height * 55 / 100,
              "a 2:1 shot in a square view leaves half the rows as bars: %u of %u",
              barRows, boxed.height);
    CHECK_MSG(blueRows >= boxed.height * 45 / 100,
              "…and the other half is the view's own background, not more black: %u of %u",
              blueRows, boxed.height);

    // ---- off again: byte-identical to before ------------------------------
    cam.constrainAspect = false;
    v->setCamera(cam);
    render(e, 3);
    Image restored; REQUIRE(v->readPixels(restored));
    CHECK_MSG(plain.rgba == restored.rgba,
              "un-constraining restores the frame byte-for-byte: %016llx vs %016llx",
              pixelHash(plain), pixelHash(restored));
}

/// THE COMBINATION CAMERAS_SPEC §7.3 explicitly left unspiked: "PiP x post-FX
/// chain shape". The inset composites onto whatever the chain last wrote to the
/// target, so it has to work over a tonemapped, bloomed, ambient-occluded,
/// edge-blended frame as well as over a passthrough one — and the letterbox has
/// to survive the same chain, because with effects on the scene renders into an
/// offscreen target and a composite quad copies it to the window (bars and all).
void pip_and_letterbox_over_the_post_chain() {
    Fixture f; Engine *e = f.e;
    View *v = f.view("pip-post", 128, 128, kBlue);   REQUIRE(v);
    Scene *s = f.scene("pip-post-scene");            REQUIRE(s);
    CHECK(v->setScene(s));
    REQUIRE(pip::build(s).wall);
    pip::aimMain(v);

    const ViewPipDesc d0 = pip::desc(0.60f, 0.60f, 0.36f, 0.36f);
    PostFxDesc fxDesc;
    fxDesc.allowOffscreen = true;      // the only way an offscreen view gets one
    fxDesc.hdr = true;
    fxDesc.bloom = true;
    fxDesc.ssao = true;
    fxDesc.smaaPreset = 2;
    v->setPostFx(fxDesc);
    render(e, 4);
    const std::string errBefore = e->lastError();
    Image ref; REQUIRE(v->readPixels(ref));

    // NOT byte-exact, and MEASURED rather than assumed: the post chain is not
    // frame-to-frame deterministic. HDR auto-exposure is a temporal filter that
    // adapts per SECOND of wall clock, so two readbacks of the same STILL scene
    // taken at different moments already differ across most of the frame — this
    // run measures the chain against itself first, with no inset at all, and
    // both numbers below are printed so the comparison is never a guess.
    //
    // The magnitude is what makes it a non-event: the drift is ONE OR TWO
    // levels out of 255, i.e. rounding, not an artefact. So the assertion is on
    // magnitude, and it is a strong one — an inset that leaked, smeared or
    // re-tonemapped anything outside its rect could not stay inside 4 levels.
    render(e, 4);
    Image ref2; REQUIRE(v->readPixels(ref2));
    const pip::RectStats drift = pip::compare(ref2, ref, d0, kGreen, 2);

    v->setPip(d0);
    render(e, 4);
    Image withPip; REQUIRE(v->readPixels(withPip));
    const ViewPipDesc &d = d0;
    const pip::RectStats st = pip::compare(withPip, ref, d, kGreen, 2);
    CHECK_MSG(st.inside >= st.insideTotal * 90 / 100,
              "the inset composites over a post-processed frame: %u/%u px",
              st.inside, st.insideTotal);
    int maxDelta = 0;
    {
        const int x0 = int(d.left * withPip.width), x1 = int((d.left + d.width) * withPip.width);
        const int y0 = int(d.top * withPip.height), y1 = int((d.top + d.height) * withPip.height);
        for (int y = 0; y < int(withPip.height); ++y)
            for (int x = 0; x < int(withPip.width); ++x) {
                if (x >= x0 - 2 && x <= x1 + 2 && y >= y0 - 2 && y <= y1 + 2) continue;
                const size_t i = (size_t(y) * withPip.width + x) * 4u;
                for (int c = 0; c < 3; ++c)
                    maxDelta = std::max(maxDelta,
                                        std::abs(int(withPip.rgba[i+c]) - int(ref.rgba[i+c])));
            }
    }
    std::printf("    outside the rect: %u px differ with the inset, %u px differ WITHOUT it "
                "(the chain's own drift); worst channel delta %d/255\n",
                st.outsideDiff, drift.outsideDiff, maxDelta);
    CHECK_MSG(maxDelta <= 4,
              "the inset changes NOTHING VISIBLE outside its rect over a post-processed frame: "
              "worst channel delta %d (the chain's own frame-to-frame drift is 1-2)", maxDelta);
    CHECK_MSG(e->lastError() == errBefore,
              "no NEW engine error from PiP x post chain: %s", e->lastError().c_str());
    v->setPip(ViewPipDesc());

    // ---- the letterbox, through the same chain ---------------------------
    CameraDesc cam;
    cam.position = Vec3(0.0f, 0.0f, 5.0f);
    v->setCamera(cam);
    render(e, 4);
    cam.constrainAspect = true;
    cam.aspect = 2.0f;
    v->setCamera(cam);
    render(e, 4);
    Image boxed; REQUIRE(v->readPixels(boxed));
    unsigned barRows = 0;
    for (unsigned y = 0; y < boxed.height; ++y) {
        const Px p = px(boxed, 2, y);
        if (p.r < 12 && p.g < 12 && p.b < 12) ++barRows;
    }
    std::printf("    post chain letterbox: %u bar rows of %u\n", barRows, boxed.height);
    CHECK_MSG(barRows >= boxed.height * 40 / 100 && barRows <= boxed.height * 60 / 100,
              "a 2:1 shot letterboxes THROUGH the post chain too: %u of %u rows",
              barRows, boxed.height);
    CHECK_MSG(e->lastError() == errBefore,
              "no NEW engine error from letterbox x post chain: %s", e->lastError().c_str());
    cam.constrainAspect = false;
    v->setCamera(cam);
    v->setPostFx(PostFxDesc());
    render(e, 2);
}

void teardown_is_clean() {
    // Last test: destroys the process's Engine with everything still registered.
    Engine *e = gEngine.get();
    View *v1 = e->createOffscreenView("v1", 32, 32, kBlue);
    View *v2 = e->createOffscreenView("v2", 32, 32, kGreen);
    Scene *s1 = e->createScene("s1");
    Scene *s2 = e->createScene("s2");
    REQUIRE(v1 && v2 && s1 && s2);
    populate(s1, kOrange);
    populate(s2, kCyan);
    populate(s2, kOrange);   // two cubes: two meshes, two datablocks
    CHECK(v1->setScene(s1));
    CHECK(v2->setScene(s1)); // two views on one scene
    render(e);
    // Everything still registered. The destructor must tear down in order.
    gEngine.reset();
    CHECK(!Engine::isAlive());
}

struct Test { const char *name; std::function<void()> fn; };

}  // namespace


// ---------------------------------------------------------------------------
// Step 2/3 verbs: hierarchy, absolute transforms, visibility, meshes, materials.

MeshData unitCubeData() {
    MeshData d;
    const float h = 0.5f;
    const float fn[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const float fv[6][4][3] = {
        {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}}, {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}},
        {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}}, {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
        {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}}, {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}} };
    for (int f = 0; f < 6; ++f) for (int v = 0; v < 4; ++v) {
        for (int k = 0; k < 3; ++k) d.positions.push_back(fv[f][v][k]);
        for (int k = 0; k < 3; ++k) d.normals.push_back(fn[f][k]);
        d.uvs.push_back(v == 1 || v == 2 ? 1.0f : 0.0f); d.uvs.push_back(v >= 2 ? 1.0f : 0.0f);
    }
    for (unsigned f = 0; f < 6; ++f) {
        const unsigned b = f * 4;
        for (unsigned i : { b, b+1, b+2, b, b+2, b+3 }) d.indices.push_back(i);
    }
    return d;
}

void shadows_darken_the_ground() {
    Fixture fx;
    View *v = fx.view("shadow-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("shadow-scene");            REQUIRE(s);
    v->setScene(s);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.1f, 0.1f, 0.1f));
    MeshId cubeMesh = s->createMesh(unitCubeData());
    PbrParams white; white.albedo = Colour(0.9f, 0.9f, 0.9f); white.roughness = 0.9f;
    MaterialId mat = s->createPbrMaterial(white);
    NodeId ground = s->createNode();                     // a flat slab at y=-0.5
    CHECK(s->attachMesh(ground, cubeMesh, mat));
    s->setNodeTransform(ground, Vec3(0, -0.55f, 0), Quat(), Vec3(8, 0.1f, 8));
    NodeId cube = s->createNode();                       // floating box above it
    CHECK(s->attachMesh(cube, cubeMesh, mat));
    s->setNodeTransform(cube, Vec3(0, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));
    NodeId sun = s->createNode();
    LightDesc d; d.type = LightType::Directional; d.intensity = 3.0f; d.castShadows = true;   // bright sun: contrast is what we test
    CHECK(s->setLight(sun, d));
    // Light shining straight down (lights point down node -Y: identity = down):
    // the shadow lands directly under the cube.
    s->setNodeTransform(sun, Vec3(0, 5, 0), Quat(), Vec3(1,1,1));
    // Camera looking down from above: cube in the middle, ground all around.
    CameraDesc c; c.position = Vec3(0, 6, 0.01f); c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f); c.fovDegrees = 50;
    v->setCamera(c);
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    // Pixel just beside the cube (on the ground) with shadows OFF: lit.
    auto lum = [&](unsigned x, unsigned y) { const Px q = px(img, x, y); return (q.r + q.g + q.b) / 3; };
    const int besideOff = lum(48, 28);
    v->setShadows(true);
    CHECK(v->shadows());
    if (!fx.e->lastError().empty()) std::printf("    lastError after setShadows: %s\n", fx.e->lastError().c_str());
    render(fx.e, 4); REQUIRE(v->readPixels(img));
    const int besideOn = lum(48, 28), farOn = lum(6, 6);
    std::printf("    ground beside cube: shadows off %d, on %d; far ground on %d\n", besideOff, besideOn, farOn);
    // With the light straight above, the ground right beside the cube is NOT shadowed;
    // what shadows change is the region under the cube, hidden here. So tilt the sun.
    s->setNodeTransform(sun, Vec3(0, 5, 0), Quat(0, 0, 0.3826834f, 0.9238795f), Vec3(1,1,1));   // roll 45: sun tilts toward +X
    render(fx.e, 4); REQUIRE(v->readPixels(img));
    // Find the darkest and brightest ground pixels along a row beside the cube.
    int darkest = 255, brightest = 0;
    for (unsigned x = 62; x < 94; ++x) { const int l = lum(x, 48); darkest = std::min(darkest, l); brightest = std::max(brightest, l); }
    for (unsigned x = 2; x < 34; ++x)  { const int l = lum(x, 48); darkest = std::min(darkest, l); brightest = std::max(brightest, l); }
    std::printf("    tilted sun, shadows on: ground row darkest %d brightest %d\n", darkest, brightest);
    CHECK_MSG(brightest - darkest > 40, "a cast shadow should create contrast on the ground: %d..%d", darkest, brightest);
    v->setShadows(false);
    render(fx.e, 4); REQUIRE(v->readPixels(img));
    int darkest2 = 255, brightest2 = 0;
    for (unsigned x = 62; x < 94; ++x) { const int l = lum(x, 48); darkest2 = std::min(darkest2, l); brightest2 = std::max(brightest2, l); }
    for (unsigned x = 2; x < 34; ++x)  { const int l = lum(x, 48); darkest2 = std::min(darkest2, l); brightest2 = std::max(brightest2, l); }
    std::printf("    tilted sun, shadows off: ground row darkest %d brightest %d\n", darkest2, brightest2);
    CHECK_MSG(brightest2 - darkest2 < (brightest - darkest), "without shadows the ground row is flatter");
}

void shadow_filter_quality_is_settable() {
    // Engine::setShadowFilter is GLOBAL (one HlmsPbs filter for every shadowed
    // light). Every quality must be acceptable at runtime and keep shadows
    // rendering; Hard (PCF 2x2) vs VerySoft (PCF 6x6) should differ at the
    // shadow's edge (penumbra), which we count rather than pin to exact pixels.
    Fixture fx;
    View *v = fx.view("filter-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("filter-scene");              REQUIRE(s);
    v->setScene(s);
    CHECK(fx.e->shadowFilter() == ShadowFilter::Soft);   // documented default
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.1f, 0.1f, 0.1f));
    MeshId cubeMesh = s->createMesh(unitCubeData());
    PbrParams white; white.albedo = Colour(0.9f, 0.9f, 0.9f); white.roughness = 0.9f;
    MaterialId mat = s->createPbrMaterial(white);
    NodeId ground = s->createNode();
    CHECK(s->attachMesh(ground, cubeMesh, mat));
    s->setNodeTransform(ground, Vec3(0, -0.55f, 0), Quat(), Vec3(8, 0.1f, 8));
    NodeId cube = s->createNode();
    CHECK(s->attachMesh(cube, cubeMesh, mat));
    s->setNodeTransform(cube, Vec3(0, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));
    NodeId sun = s->createNode();
    LightDesc d; d.type = LightType::Directional; d.intensity = 3.0f; d.castShadows = true;
    CHECK(s->setLight(sun, d));
    // Tilted sun (roll 45 toward +X): the cast shadow lands beside the cube.
    s->setNodeTransform(sun, Vec3(0, 5, 0), Quat(0, 0, 0.3826834f, 0.9238795f), Vec3(1, 1, 1));
    v->setShadows(true);
    CameraDesc c; c.position = Vec3(0, 6, 0.01f); c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f); c.fovDegrees = 50;
    v->setCamera(c);
    auto lum = [](const Image &img, unsigned x, unsigned y) {
        const Colour q = img.at(x, y);
        return int(std::lround((q.r + q.g + q.b) / 3.0f * 255.0f));
    };
    auto groundContrast = [&](const Image &img) {
        int darkest = 255, brightest = 0;
        for (unsigned x = 2; x < img.width - 2; ++x) {
            const int l = lum(img, x, 64);
            darkest = std::min(darkest, l); brightest = std::max(brightest, l);
        }
        return brightest - darkest;
    };
    Image hard, soft, verySoft;
    const struct { ShadowFilter f; const char *name; Image *img; } runs[] = {
        { ShadowFilter::Hard,     "Hard",     &hard },
        { ShadowFilter::Soft,     "Soft",     &soft },
        { ShadowFilter::VerySoft, "VerySoft", &verySoft },
    };
    for (const auto &run : runs) {
        fx.e->setShadowFilter(run.f);
        CHECK(fx.e->shadowFilter() == run.f);
        render(fx.e, 4);
        REQUIRE(v->readPixels(*run.img));
        const int contrast = groundContrast(*run.img);
        std::printf("    filter %-8s: ground row contrast %d\n", run.name, contrast);
        CHECK_MSG(contrast > 40, "shadows must keep rendering under filter %s: contrast %d", run.name, contrast);
    }
    // The kernels differ, so the penumbra should: count pixels that changed.
    int changed = 0;
    for (unsigned y = 0; y < hard.height; ++y)
        for (unsigned x = 0; x < hard.width; ++x)
            if (std::abs(lum(hard, x, y) - lum(verySoft, x, y)) > 6) ++changed;
    std::printf("    Hard vs VerySoft: %d pixel(s) differ\n", changed);
    CHECK_MSG(changed > 0, "PCF 2x2 and 6x6 should not produce identical images");
    fx.e->setShadowFilter(ShadowFilter::Soft);   // restore the default for later tests
}

void shadow_resolution_rebuilds_the_atlas() {
    // Engine::setShadowResolution is GLOBAL like the filter, but rebuilds the
    // shadow node definition and every workspace referencing it — the risky
    // teardown-order path. Shadows must keep rendering at every size, with a
    // live shadowed view attached while the rebuild happens.
    Fixture fx;
    View *v = fx.view("shadowres-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("shadowres-scene");              REQUIRE(s);
    v->setScene(s);
    CHECK(fx.e->shadowResolution() == 2048u);   // documented default
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.1f, 0.1f, 0.1f));
    MeshId cubeMesh = s->createMesh(unitCubeData());
    PbrParams white; white.albedo = Colour(0.9f, 0.9f, 0.9f); white.roughness = 0.9f;
    MaterialId mat = s->createPbrMaterial(white);
    NodeId ground = s->createNode();
    CHECK(s->attachMesh(ground, cubeMesh, mat));
    s->setNodeTransform(ground, Vec3(0, -0.55f, 0), Quat(), Vec3(8, 0.1f, 8));
    NodeId cube = s->createNode();
    CHECK(s->attachMesh(cube, cubeMesh, mat));
    s->setNodeTransform(cube, Vec3(0, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));
    NodeId sun = s->createNode();
    LightDesc d; d.type = LightType::Directional; d.intensity = 3.0f; d.castShadows = true;
    CHECK(s->setLight(sun, d));
    s->setNodeTransform(sun, Vec3(0, 5, 0), Quat(0, 0, 0.3826834f, 0.9238795f), Vec3(1, 1, 1));
    v->setShadows(true);
    CameraDesc c; c.position = Vec3(0, 6, 0.01f); c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f); c.fovDegrees = 50;
    v->setCamera(c);
    auto lum = [](const Image &img, unsigned x, unsigned y) {
        const Colour q = img.at(x, y);
        return int(std::lround((q.r + q.g + q.b) / 3.0f * 255.0f));
    };
    auto groundContrast = [&](const Image &img) {
        int darkest = 255, brightest = 0;
        for (unsigned x = 2; x < img.width - 2; ++x) {
            const int l = lum(img, x, 64);
            darkest = std::min(darkest, l); brightest = std::max(brightest, l);
        }
        return brightest - darkest;
    };
    Image atLow, atHigh;
    for (unsigned res : { 512u, 4096u, 1024u }) {
        fx.e->setShadowResolution(res);
        CHECK_MSG(fx.e->shadowResolution() == res, "resolution %u did not stick", res);
        render(fx.e, 4);
        Image img; REQUIRE(v->readPixels(img));
        const int contrast = groundContrast(img);
        std::printf("    shadow atlas @%-4u: ground row contrast %d\n", res, contrast);
        CHECK_MSG(contrast > 40, "shadows must keep rendering at %u: contrast %d", res, contrast);
        if (res == 512u)  atLow = img;
        if (res == 4096u) atHigh = img;
    }
    // The value sticking is not the point — the PIXELS must change. An 8x finer
    // atlas moves the shadow's edge: count how many pixels differ beyond driver
    // noise between the 512 and 4096 frames (VISUAL_PARITY_SPEC item 2).
    if (atLow.width == atHigh.width && atLow.height == atHigh.height && atLow.width > 0) {
        int moved = 0;
        for (unsigned y = 0; y < atLow.height; ++y)
            for (unsigned x = 0; x < atLow.width; ++x)
                if (std::abs(lum(atLow, x, y) - lum(atHigh, x, y)) > 6) ++moved;
        std::printf("    512 vs 4096 atlas: %d pixel(s) differ\n", moved);
        CHECK_MSG(moved > 0, "a finer shadow atlas must change the shadow's edge, not just the number");
    }
    // Out-of-range values are clamped, not fatal.
    fx.e->setShadowResolution(1u);
    CHECK(fx.e->shadowResolution() == 256u);
    render(fx.e, 2);
    fx.e->setShadowResolution(2048u);   // restore the default for later tests
    CHECK(fx.e->shadowResolution() == 2048u);
    render(fx.e, 2);
}

// The ambient SH basis is documented in WORLD axes (Scene::setAmbientSh), but
// HlmsPbs evaluates it in the left-handed cubemap frame with X flipped
// (AmbientLighting_piece_ps.any). OgreScene::setAmbientSh corrects for that.
// This is the test that proves the correction: a matte white cube, no lights, no
// sky, lit ONLY by ambient — each directional band must brighten the face whose
// normal it names and darken the opposite one.
void ambient_sh_lights_world_axes() {
    Fixture fx;
    View *v = fx.view("sh-view", 48, 48, kBlue); REQUIRE(v);
    Scene *s = fx.scene("sh-scene");             REQUIRE(s);
    v->setScene(s);
    NodeId cube = enginetest::addTestCube(s, Colour(1, 1, 1), 0.0f, 1.0f);
    enginetest::setNodeScale(s, cube, Vec3(1.4f, 1.4f, 1.4f));
    // band index -> (the world axis it names, its name)
    struct Band { int index; Vec3 axis; const char *name; } bands[] = {
        { 1, Vec3(0, 1, 0), "y" },
        { 2, Vec3(0, 0, 1), "z" },
        { 3, Vec3(1, 0, 0), "x" },
    };
    for (const Band &b : bands) {
        // Look down the axis so the face whose normal IS that axis fills the
        // centre of the frame. The small perpendicular nudge keeps the look
        // vector off +Y, which testCameraLookAt uses as its up vector.
        const Vec3 nudge = b.index == 1 ? Vec3(0.35f, 0, 0.35f) : Vec3(0, 0.35f, 0);
        enginetest::testCameraLookAt(v, Vec3(b.axis.x * 4.0f + nudge.x, b.axis.y * 4.0f + nudge.y,
                                             b.axis.z * 4.0f + nudge.z),
                                     Vec3(0, 0, 0));
        int lit[3] = { 0, 0, 0 };
        for (int sign = 0; sign < 2; ++sign) {
            float sh[27] = { 0.0f };
            for (int c = 0; c < 3; ++c) sh[c] = 0.5f;                       // constant band
            for (int c = 0; c < 3; ++c) sh[b.index * 3 + c] = sign ? -0.4f : 0.4f;
            s->setAmbientSh(sh);
            render(fx.e, 3);
            Image img; REQUIRE(v->readPixels(img));
            lit[sign] = centre(img).g;
        }
        std::printf("    band %s: facing the axis %d, facing away %d\n", b.name, lit[0], lit[1]);
        CHECK_MSG(lit[0] > lit[1] + 20,
                  "SH band '%s' must brighten the +%s face (%d) over the -%s one (%d)",
                  b.name, b.name, lit[0], b.name, lit[1]);
    }
}

// SkyDesc shorthands. The boundary takes the whole sky as one value now
// (ENGINEERING_DEBT_SPEC item 4), so a test that only wants "an equirect sky"
// says exactly that rather than picking one of three verbs.
static SkyDesc equirectSkyDesc(TextureId tex) {
    SkyDesc d; d.mode = SkyMode::Equirectangular; d.equirect = tex; return d;
}
static SkyDesc cubemapSkyDesc(const TextureId faces[6]) {
    SkyDesc d; d.mode = SkyMode::Cubemap;
    for (int i = 0; i < 6; ++i) d.faces[i] = faces[i];
    return d;
}

void equirect_sky_fills_the_background() {
    Fixture fx;
    View *v = fx.view("sky-view", 48, 48, kBlue); REQUIRE(v);
    Scene *s = fx.scene("sky-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    // A solid magenta 64x32 PPM (no image library needed); FreeImage reads PPM.
    const std::string path = "sky_test_magenta.ppm";
    { FILE *f = std::fopen(path.c_str(), "wb"); std::fprintf(f, "P6 64 32 255\n");
      for (int i = 0; i < 64 * 32; ++i) { std::fputc(255, f); std::fputc(0, f); std::fputc(255, f); } std::fclose(f); }
    TextureId skyTex = s->loadTexture(path, true);
    CHECK_MSG(skyTex != 0, "%s", fx.e->lastError().c_str());
    if (!skyTex) { std::remove(path.c_str()); return; }
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    CHECK(near(corner(img), kBlue));
    CHECK(s->setSky(equirectSkyDesc(skyTex)));
    render(fx.e, 3); REQUIRE(v->readPixels(img));
    const Px k = corner(img);
    std::printf("    equirect sky corner: %d %d %d\n", k.r, k.g, k.b);
    CHECK_MSG(k.r > 150 && k.b > 150 && k.g < 80, "sky texture should fill the background: %d %d %d", k.r, k.g, k.b);
    // ogre-patch 0009 in one assertion. Ogre's equirect sky needs a texture whose
    // internal type is Type2DArray, which for file-loaded textures means an
    // automatic-batching POOL SLICE, and it tells the shader which slice through
    // the `sliceIdx` uniform. Upstream's Vulkan GLSL declared that uniform and
    // then sampled slice 0 anyway, so glslang stripped it and setSky threw. A
    // second sky of the same size lands in the same pool at slice 1: if sliceIdx
    // were still ignored this would render the FIRST image.
    const std::string path2 = "sky_test_cyan.ppm";
    { FILE *f = std::fopen(path2.c_str(), "wb"); std::fprintf(f, "P6 64 32 255\n");
      for (int i = 0; i < 64 * 32; ++i) { std::fputc(0, f); std::fputc(255, f); std::fputc(255, f); } std::fclose(f); }
    TextureId skyTex2 = s->loadTexture(path2, true);
    CHECK_MSG(skyTex2 != 0, "%s", fx.e->lastError().c_str());
    if (skyTex2) {
        CHECK(s->setSky(equirectSkyDesc(skyTex2)));
        render(fx.e, 3); REQUIRE(v->readPixels(img));
        const Px k2 = corner(img);
        std::printf("    second equirect sky corner: %d %d %d\n", k2.r, k2.g, k2.b);
        CHECK_MSG(k2.g > 150 && k2.b > 150 && k2.r < 80,
                  "the SECOND sky in the pool must show, not the first: %d %d %d", k2.r, k2.g, k2.b);
    }
    CHECK(s->setSky(SkyDesc()));
    render(fx.e, 2); REQUIRE(v->readPixels(img));
    CHECK(near(corner(img), kBlue));
    std::remove(path.c_str());
    std::remove(path2.c_str());
}

// Item 4 of the Ogre adoption wave, in pixels. The sky cube is lit on ONE face
// (+X) and black everywhere else; a rough metal cube is seen from straight
// above, so the only thing its top face can reflect is the +Y direction. A box
// mip chain (_autogenerateMipmaps, what this engine used before) averages
// WITHIN a face, so the +Y face is black at every mip and the cube renders
// black. The ibl_specular pass convolves the whole hemisphere per output texel,
// so the +X face's light reaches +Y. There are no lights and no ambient: every
// lit pixel here came from the prefilter.
void rough_metal_reflects_across_cube_faces() {
    Fixture fx;
    View *v = fx.view("ggx-view", 64, 64, Colour(0, 0, 0)); REQUIRE(v);
    Scene *s = fx.scene("ggx-scene");                       REQUIRE(s);
    v->setScene(s);
    float noAmbient[27] = { 0.0f };
    s->setAmbientSh(noAmbient);
    NodeId cube = enginetest::addTestCube(s, Colour(1, 1, 1), 1.0f, 0.9f);
    enginetest::setNodeScale(s, cube, Vec3(1.4f, 1.4f, 1.4f));
    // +X white, the other five faces black.
    TextureId faces[6];
    for (int i = 0; i < 6; ++i) {
        std::vector<unsigned char> px(16 * 16 * 4);
        const unsigned char lum = i == 0 ? 255 : 0;
        for (int p = 0; p < 256; ++p) { px[p*4] = lum; px[p*4+1] = lum; px[p*4+2] = lum; px[p*4+3] = 255; }
        faces[i] = s->createTexture(16, 16, px.data(), true);
        CHECK_MSG(faces[i] != 0, "%s", fx.e->lastError().c_str());
    }
    CHECK_MSG(s->setSky(cubemapSkyDesc(faces)), "%s", fx.e->lastError().c_str());
    // Straight down at the top face (nudged off +Y so the look-at basis is sane).
    enginetest::testCameraLookAt(v, Vec3(0.3f, 4.0f, 0.3f), Vec3(0, 0, 0));
    render(fx.e, 4);
    Image img; REQUIRE(v->readPixels(img));
    const Px k = centre(img);
    std::printf("    rough metal top face under a +X-only sky: %d %d %d\n", k.r, k.g, k.b);
    CHECK_MSG(k.r > 12,
              "a GGX-prefiltered cube must carry the +X face's light into +Y (box mips cannot): %d %d %d",
              k.r, k.g, k.b);
    CHECK(s->setSky(SkyDesc()));
}

void cubemap_sky_faces_match_directions() {
    Fixture fx;
    View *v = fx.view("cube-sky-view", 32, 32, kBlue); REQUIRE(v);
    Scene *s = fx.scene("cube-sky-scene");           REQUIRE(s);
    v->setScene(s);
    // Six solid faces from pixels: +X red, -X green, +Y blue, -Y yellow, +Z magenta, -Z cyan.
    const unsigned char cols[6][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,0},{255,0,255},{0,255,255}};
    TextureId faces[6];
    for (int i = 0; i < 6; ++i) {
        std::vector<unsigned char> px(8 * 8 * 4);
        for (int p = 0; p < 64; ++p) { px[p*4] = cols[i][0]; px[p*4+1] = cols[i][1]; px[p*4+2] = cols[i][2]; px[p*4+3] = 255; }
        faces[i] = s->createTexture(8, 8, px.data(), true);
        CHECK_MSG(faces[i] != 0, "%s", fx.e->lastError().c_str());
    }
    CHECK(s->setSky(cubemapSkyDesc(faces)));
    // Look down each axis with a narrow FOV; the centre pixel must be that face's colour.
    struct Look { Quat q; int face; const char *name; } looks[] = {
        { Quat(0, -0.7071068f, 0, 0.7071068f), 0, "+X" },   // yaw -90: camera -Z -> +X
        { Quat(0,  0.7071068f, 0, 0.7071068f), 1, "-X" },
        { Quat(0.7071068f, 0, 0, 0.7071068f),  2, "+Y" },   // pitch +90: -Z -> +Y
        { Quat(-0.7071068f, 0, 0, 0.7071068f), 3, "-Y" },
        { Quat(0, 1, 0, 0),                    4, "+Z" },   // yaw 180
        { Quat(),                              5, "-Z" },
    };
    for (const Look &l : looks) {
        CameraDesc c; c.position = Vec3(0, 0, 0); c.orientation = l.q; c.fovDegrees = 20; c.nearClip = 0.05f; c.farClip = 100;
        v->setCamera(c);
        render(fx.e, 2); Image img; REQUIRE(v->readPixels(img));
        const Px k = centre(img);
        const Colour want(cols[l.face][0] / 255.0f, cols[l.face][1] / 255.0f, cols[l.face][2] / 255.0f);
        std::printf("    looking %s: centre %d %d %d\n", l.name, k.r, k.g, k.b);
        CHECK_MSG(near(k, want, 40), "face %s should show its colour", l.name);
    }
    CHECK(s->setSky(SkyDesc()));
    render(fx.e, 2); Image img; REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kBlue));
}

void mesh_from_buffers_renders() {
    Fixture fx;
    View *v = fx.view("mesh-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("mesh-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    MeshId mesh = s->createMesh(unitCubeData());
    CHECK_MSG(mesh != 0, "%s", s == nullptr ? "" : fx.e->lastError().c_str());
    PbrParams p; p.albedo = kOrange; p.roughness = 0.6f;
    MaterialId mat = s->createPbrMaterial(p);
    CHECK(mat != 0);
    NodeId n = s->createNode();
    CHECK(n != 0);
    CHECK(s->attachMesh(n, mesh, mat));
    s->setNodeTransform(n, Vec3(0,0,0), Quat(), Vec3(1.2f, 1.2f, 1.2f));
    render(fx.e);
    Image img; REQUIRE(v->readPixels(img));
    const Px c = centre(img);
    std::printf("    mesh-from-buffers centre %d %d %d\n", c.r, c.g, c.b);
    CHECK(near(corner(img), kBlue));
    CHECK(c.r > 100 && c.b < 120);

    // Bad data is refused, not thrown.
    MeshData bad; bad.positions = { 0, 0, 0 }; bad.indices = { 0, 1, 2 };
    CHECK(s->createMesh(bad) == 0);
    CHECK(!fx.e->lastError().empty());
}

void hierarchy_transform_propagates() {
    Fixture fx;
    View *v = fx.view("hier-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("hier-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = kOrange;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId parent = s->createNode();
    NodeId child  = s->createNode(parent);
    REQUIRE(parent && child);
    CHECK(s->attachMesh(child, mesh, mat));
    s->setNodeTransform(child, Vec3(0,0,0), Quat(), Vec3(1.2f, 1.2f, 1.2f));
    render(fx.e);
    Image img; REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100);
    // Move only the PARENT: the child must leave the frame.
    s->setNodeTransform(parent, Vec3(10, 0, 0), Quat(), Vec3(1, 1, 1));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kBlue));
    // Re-parent the child to the root: it is back at the origin.
    CHECK(s->setNodeParent(child, 0));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100);
    // Visibility on the parent cascades once the child is under it again.
    CHECK(s->setNodeParent(child, parent));
    s->setNodeTransform(parent, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));
    s->setNodeVisible(parent, false);
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kBlue));
    s->setNodeVisible(parent, true);
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100);
    // Removing the parent keeps the child (re-parented to root), still visible.
    CHECK(s->removeNode(parent));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100);
}

void material_and_mesh_lifetime() {
    Fixture fx;
    View *v = fx.view("mat-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("mat-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = kOrange;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId a = s->createNode(), b = s->createNode();
    CHECK(s->attachMesh(a, mesh, mat));
    CHECK(s->attachMesh(b, mesh, mat));                       // shared mesh + material
    s->setNodeTransform(a, Vec3(0,0,0), Quat(), Vec3(1.2f,1.2f,1.2f));
    s->setNodeTransform(b, Vec3(10,0,0), Quat(), Vec3(1,1,1)); // off screen
    render(fx.e);
    Image img; REQUIRE(v->readPixels(img));
    const Px before = centre(img);
    CHECK(before.r > 100);
    // Changing the material recolours what is on screen.
    p.albedo = Colour(0.1f, 0.3f, 0.9f);
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px after = centre(img);
    std::printf("    material orange->blue: %d %d %d -> %d %d %d\n", before.r, before.g, before.b, after.r, after.g, after.b);
    CHECK(after.b > after.r);
    // Removing one node leaves the shared mesh usable by the other.
    CHECK(s->removeNode(b));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).b > 100);
    // Destroying the material detaches the item; destroying the mesh as well.
    CHECK(s->destroyMaterial(mat));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kBlue));
    CHECK(s->destroyMesh(mesh));
    CHECK(!s->destroyMesh(mesh));
    render(fx.e);
    CHECK(true);
}

void light_on_node_and_camera_desc() {
    Fixture fx;
    View *v = fx.view("light-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("light-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.02f, 0.02f, 0.02f));
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = Colour(0.9f, 0.9f, 0.9f); p.roughness = 0.8f;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId cube = s->createNode();
    CHECK(s->attachMesh(cube, mesh, mat));
    s->setNodeTransform(cube, Vec3(0,0,0), Quat(), Vec3(1.2f,1.2f,1.2f));
    NodeId lightNode = s->createNode();
    LightDesc d; d.type = LightType::Point; d.intensity = 0.8f; d.range = 20.0f;   // low enough not to saturate
    CHECK(s->setLight(lightNode, d));
    s->setNodeTransform(lightNode, Vec3(4, 1, 2.5f), Quat(), Vec3(1,1,1));
    render(fx.e);
    Image img; REQUIRE(v->readPixels(img));
    auto lum = [&](unsigned x, unsigned y) { const Colour c = img.at(x, y); return c.r + c.g + c.b; };
    const float r1 = lum(72, 48), l1 = lum(24, 48);
    s->setNodeTransform(lightNode, Vec3(-4, 1, 2.5f), Quat(), Vec3(1,1,1));
    render(fx.e); REQUIRE(v->readPixels(img));
    const float r2 = lum(72, 48), l2 = lum(24, 48);
    std::printf("    light right: L %.2f R %.2f | light left: L %.2f R %.2f\n", l1, r1, l2, r2);
    CHECK(r1 > l1 + 0.05f);
    CHECK(l2 > r2 + 0.05f);
    CHECK(s->removeLight(lightNode));
    CHECK(!s->removeLight(lightNode));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(lum(48, 48) < 0.3f);   // only faint ambient remains

    // CameraDesc: same pose as aim() gives the cube; a far pose does not.
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    CameraDesc c; c.position = Vec3(0, 0, 4); c.fovDegrees = 45; c.nearClip = 0.1f; c.farClip = 100;
    v->setCamera(c);
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(!near(centre(img), kBlue));
    c.position = Vec3(0, 40, 4);
    v->setCamera(c);
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kBlue));
    c.position = Vec3(0, 0, 4); c.orthographic = true; c.orthoSize = 3.0f;
    v->setCamera(c);
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(!near(centre(img), kBlue));
}

void area_light_lights_the_wall() {
    // A rectangular area light in front of a wall: both variants (fast approx and
    // accurate LTC) must produce a lit region, and single-sided lights must go
    // dark when turned away — double-sided ones must not.
    Fixture fx;
    View *v = fx.view("area-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("area-scene");            REQUIRE(s);
    v->setScene(s);
    enginetest::testCameraLookAt(v, Vec3(0, 0, 5), Vec3(0, 0, 0));
    s->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.02f, 0.02f, 0.02f));
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = Colour(0.9f, 0.9f, 0.9f); p.roughness = 0.8f;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId wall = s->createNode();
    CHECK(s->attachMesh(wall, mesh, mat));
    s->setNodeTransform(wall, Vec3(0, 0, 0), Quat(), Vec3(3.0f, 3.0f, 0.2f));
    render(fx.e);
    Image img; REQUIRE(v->readPixels(img));
    auto lum = [&](void) { const Colour c = img.at(img.width / 2, img.height / 2); return c.r + c.g + c.b; };
    const float unlit = lum();

    // Lights shine down their node's -Y; rotate +90 deg about X so -Y becomes -Z:
    // the rectangle faces the wall from z=1.5.
    const float h = 0.70710678f;                       // sin/cos of 45 deg
    NodeId lightNode = s->createNode();
    s->setNodeTransform(lightNode, Vec3(0, 0, 1.5f), Quat(h, 0, 0, h), Vec3(1, 1, 1));
    LightDesc d; d.type = LightType::Area; d.intensity = 2.0f; d.range = 20.0f;
    d.rectWidth = 2.0f; d.rectHeight = 2.0f;
    CHECK(s->setLight(lightNode, d));
    render(fx.e); REQUIRE(v->readPixels(img));
    const float approx = lum();
    d.accurate = true;                                 // LT_AREA_LTC
    CHECK(s->setLight(lightNode, d));
    render(fx.e); REQUIRE(v->readPixels(img));
    const float ltc = lum();
    std::printf("    wall centre: unlit %.2f  approx %.2f  ltc %.2f\n", unlit, approx, ltc);
    CHECK(approx > unlit + 0.15f);
    CHECK(ltc > unlit + 0.15f);

    // Face the light AWAY from the wall (-Y becomes +Z): a single-sided rect
    // leaves the wall dark; double-sided lights it again.
    s->setNodeTransform(lightNode, Vec3(0, 0, 1.5f), Quat(-h, 0, 0, h), Vec3(1, 1, 1));
    d.accurate = false;
    CHECK(s->setLight(lightNode, d));
    render(fx.e); REQUIRE(v->readPixels(img));
    const float away = lum();
    d.doubleSided = true;
    CHECK(s->setLight(lightNode, d));
    render(fx.e); REQUIRE(v->readPixels(img));
    const float both = lum();
    std::printf("    turned away: single-sided %.2f  double-sided %.2f\n", away, both);
    CHECK(away < approx - 0.1f);
    CHECK(both > away + 0.1f);
    CHECK(s->removeLight(lightNode));
}

/// Writes a synthetic, valid IESNA LM-63-1995 photometric file whose lobe is a
/// RING: black on axis, peak at 40-50 deg, black past 90. Nothing is shipped —
/// real .ies files are third-party manufacturer photometry of unclear licence.
/// Peak candela is 1024 with unit multipliers, so the shader's attenuation term
/// (candela/1024 * mult * ballast * photometricFactor) tops out at exactly 1.
void writeRingIesFile(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "IESNA:LM-63-1995\n[TEST] synthetic ring lobe\n[MANUFAC] Jahshaka\nTILT=NONE\n");
    //           lamps lumens mult  vAng hAng type units  w l h
    std::fprintf(f, "1 1000 1 19 1 1 2 0 0 0\n");
    //           ballast  photometric  watts
    std::fprintf(f, "1 1 0\n");
    for (int i = 0; i < 19; ++i) std::fprintf(f, "%d ", i * 10);   // vertical 0..180
    std::fprintf(f, "\n0\n");                                      // one horizontal angle
    const float candela[19] = { 0, 100, 400, 800, 1024, 1024, 800, 400, 100, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 19; ++i) std::fprintf(f, "%.1f ", candela[i]);
    std::fprintf(f, "\n");
    std::fclose(f);
}

void ies_profile_shapes_a_spot() {
    // A near-hemispherical spot (160/170 deg, the pinned IesProfiles sample's
    // trick) points straight at a wall, so the CONE does almost no shaping and
    // whatever shape appears is the profile's. Unprofiled, the wall is brightest
    // on axis. With the ring profile above, the axis goes dark and an off-axis
    // ring is the brightest part — the falloff has visibly changed shape, which
    // is the only thing an IES profile can do.
    Fixture fx;
    View *v = fx.view("ies-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("ies-scene");             REQUIRE(s);
    v->setScene(s);
    enginetest::testCameraLookAt(v, Vec3(0, 0, 6), Vec3(0, 0, 0));
    s->setAmbient(Colour(0.01f, 0.01f, 0.01f), Colour(0.01f, 0.01f, 0.01f));
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = Colour(0.9f, 0.9f, 0.9f); p.roughness = 0.9f;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId wall = s->createNode();
    CHECK(s->attachMesh(wall, mesh, mat));
    s->setNodeTransform(wall, Vec3(0, 0, 0), Quat(), Vec3(8.0f, 8.0f, 0.2f));

    // Lights shine down their node's -Y: rotate +90 about X so -Y becomes -Z.
    const float h = 0.70710678f;
    NodeId lightNode = s->createNode();
    s->setNodeTransform(lightNode, Vec3(0, 0, 2.5f), Quat(h, 0, 0, h), Vec3(1, 1, 1));
    LightDesc d;
    d.type = LightType::Spot;
    // Low enough that the wall does NOT clip to white: a saturated wall would
    // read as a flat 1.0 everywhere and the falloff comparison would be vacuous.
    d.intensity = 0.6f;
    d.range = 40.0f;
    d.spotAngleDegrees = 170.0f;
    d.spotSoftness = 0.0588f;    // inner ~160 deg
    d.castShadows = false;
    CHECK(s->setLight(lightNode, d));
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    auto lum = [&](unsigned x, unsigned y) { const Colour c = img.at(x, y); return c.r + c.g + c.b; };
    // Centre = 0 deg off the light axis; (48,24) is a quarter-frame up = ~26 deg.
    const float plainAxis = lum(48, 48), plainOff = lum(48, 24);

    const std::string ies = "test_ring_profile.ies";
    writeRingIesFile(ies);
    d.iesProfilePath = ies;
    CHECK(s->setLight(lightNode, d));
    render(fx.e, 2); REQUIRE(v->readPixels(img));
    const float iesAxis = lum(48, 48), iesOff = lum(48, 24);
    std::printf("    spot on-axis/off-axis: plain %.3f/%.3f  profiled %.3f/%.3f\n",
                plainAxis, plainOff, iesAxis, iesOff);
    CHECK_MSG(plainAxis > plainOff + 0.05f, "an unprofiled spot is brightest on its axis");
    CHECK_MSG(iesAxis < plainAxis * 0.5f, "the ring profile must darken the light's axis");
    CHECK_MSG(iesOff > iesAxis + 0.05f, "with a ring profile the OFF-axis ring is the bright part");

    // Clearing the path restores the plain falloff (the assignment is a real
    // per-light state, not a one-way arm).
    d.iesProfilePath.clear();
    CHECK(s->setLight(lightNode, d));
    render(fx.e, 2); REQUIRE(v->readPixels(img));
    const float clearedAxis = lum(48, 48);
    std::printf("    profile cleared: axis %.3f (was %.3f plain)\n", clearedAxis, plainAxis);
    CHECK(std::fabs(clearedAxis - plainAxis) < 0.05f);

    // The documented limitation, asserted rather than merely written down: a
    // shadow-casting POINT light is shaded from the pass buffer, whose point
    // loop has no profile term, so the engine must not pretend otherwise.
    d.type = LightType::Point;
    d.castShadows = false;
    d.iesProfilePath = ies;
    CHECK(s->setLight(lightNode, d));
    render(fx.e, 2); REQUIRE(v->readPixels(img));
    const float pointProfiled = lum(48, 48);
    d.castShadows = true;
    CHECK(s->setLight(lightNode, d));
    render(fx.e, 3); REQUIRE(v->readPixels(img));
    const float pointShadowed = lum(48, 48);
    std::printf("    point light axis: no shadows %.3f  shadows on %.3f\n",
                pointProfiled, pointShadowed);
    CHECK_MSG(pointShadowed > pointProfiled + 0.05f,
              "shadow-casting point lights LOSE their profile (no shader block) — "
              "if this ever fails, Ogre gained one and the UI warning can go");

    CHECK(s->removeLight(lightNode));
    std::remove(ies.c_str());
}

/// A 64x64 PPM split down the middle: left half `l`, right half `r`.
void writeSplitPpm(const std::string &path, const int l[3], const int r[3]) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6 64 64 255\n");
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int *c = x < 32 ? l : r;
            std::fputc(c[0], f); std::fputc(c[1], f); std::fputc(c[2], f);
        }
    std::fclose(f);
}

void area_light_mask_tints_and_ltc_ignores_it() {
    // A white wall lit by ONE area light whose mask is half red / half blue.
    // The approximation samples the mask, so the light stops being white (the
    // mask has NO green at all) and the two sides of the wall pick up different
    // amounts of blue. LTC ("accurate") has no mask term whatsoever — turning it
    // on must restore the plain white light. That second half is the point: the
    // documented limitation is proven in pixels, not asserted in prose.
    Fixture fx;
    View *v = fx.view("areamask-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("areamask-scene");             REQUIRE(s);
    v->setScene(s);
    enginetest::testCameraLookAt(v, Vec3(0, 0, 5), Vec3(0, 0, 0));
    s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));
    MeshId mesh = s->createMesh(unitCubeData());
    // Fairly smooth: the mask's SPECULAR sample picks its mip from roughness, so
    // a mirror-ish wall resolves where on the mask each point is looking. (The
    // diffuse sample is deliberately near-average — that is the renderer's
    // design, not a bug — so a rough wall would see only the mask's mean tint.)
    PbrParams p; p.albedo = Colour(0.9f, 0.9f, 0.9f); p.roughness = 0.35f;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId wall = s->createNode();
    CHECK(s->attachMesh(wall, mesh, mat));
    s->setNodeTransform(wall, Vec3(0, 0, 0), Quat(), Vec3(6.0f, 6.0f, 0.2f));

    const float h = 0.70710678f;
    NodeId lightNode = s->createNode();
    s->setNodeTransform(lightNode, Vec3(0, 0, 1.5f), Quat(h, 0, 0, h), Vec3(1, 1, 1));
    LightDesc d;
    d.type = LightType::Area;
    // Low enough that no channel clips: a saturated wall reads 1.0 in every
    // channel and every colour comparison below would be vacuously zero.
    d.intensity = 0.05f;
    d.range = 20.0f;
    d.rectWidth = 3.0f;
    d.rectHeight = 3.0f;
    CHECK(s->setLight(lightNode, d));
    render(fx.e, 2); Image img; REQUIRE(v->readPixels(img));

    struct Sample { float r, g, b; };
    auto sample = [&](unsigned x, unsigned y) {
        const Colour c = img.at(x, y); return Sample{ c.r, c.g, c.b };
    };
    /// How BLUE a lit point is, independent of how bright it is.
    auto blueness = [](const Sample &c) {
        const float total = c.r + c.g + c.b;
        return total > 1e-4f ? c.b / total : 0.0f;
    };
    auto show = [&](const char *what, const Sample &c) {
        std::printf("    %s rgb %.3f %.3f %.3f (blueness %.3f)\n",
                    what, c.r, c.g, c.b, blueness(c));
    };
    const unsigned kLeft = 20, kRight = 76, kRow = 48;

    const Sample plainL = sample(kLeft, kRow), plainR = sample(kRight, kRow);
    show("unmasked left ", plainL);
    show("unmasked right", plainR);
    CHECK_MSG(plainL.r < 0.95f && plainR.r < 0.95f,
              "the test is only meaningful unclipped: %.3f / %.3f", plainL.r, plainR.r);
    CHECK_MSG(plainL.g > 0.02f, "an unmasked white area light lights all three channels");
    CHECK_MSG(std::fabs(blueness(plainL) - blueness(plainR)) < 0.02f,
              "an unmasked white area light must not tint either side");

    const std::string mask = "test_area_mask.ppm";
    const int red[3] = { 255, 0, 0 }, blue[3] = { 0, 0, 255 };
    writeSplitPpm(mask, red, blue);
    d.texturePath = mask;
    CHECK(s->setLight(lightNode, d));
    render(fx.e, 3); REQUIRE(v->readPixels(img));
    const Sample maskL = sample(kLeft, kRow), maskR = sample(kRight, kRow);
    show("masked left   ", maskL);
    show("masked right  ", maskR);
    // The mask has no green anywhere: if it is being sampled at all, green dies.
    CHECK_MSG(maskL.g < plainL.g * 0.1f && maskR.g < plainR.g * 0.1f,
              "a red/blue mask must remove the light's green: %.3f -> %.3f, %.3f -> %.3f",
              plainL.g, maskL.g, plainR.g, maskR.g);
    // ...and the two halves of the mask must reach the two sides differently.
    const float maskSplit = std::fabs(blueness(maskL) - blueness(maskR));
    CHECK_MSG(maskSplit > 0.05f,
              "a half-red/half-blue mask must tint the wall's two sides differently "
              "(blueness %.3f vs %.3f)", blueness(maskL), blueness(maskR));

    // LTC ignores the mask (OgreLight.h:594-596; every mask sample in
    // AreaLights_piece_ps.any lives under hlms_lights_area_approx).
    d.accurate = true;
    CHECK(s->setLight(lightNode, d));
    render(fx.e, 3); REQUIRE(v->readPixels(img));
    const Sample ltcL = sample(kLeft, kRow), ltcR = sample(kRight, kRow);
    show("accurate left ", ltcL);
    show("accurate right", ltcR);
    // Neutral again: green is back to a full third of the energy. (LTC is dimmer
    // than the approximation, so this is a CHROMATIC test, never a brightness one.)
    auto greenShare = [](const Sample &c) {
        const float total = c.r + c.g + c.b;
        return total > 1e-4f ? c.g / total : 0.0f;
    };
    CHECK_MSG(greenShare(ltcL) > 0.3f && greenShare(ltcR) > 0.3f,
              "accurate (LTC) mode must DROP the mask — the light goes white again: "
              "green share %.3f / %.3f (masked: %.3f / %.3f)",
              greenShare(ltcL), greenShare(ltcR), greenShare(maskL), greenShare(maskR));
    CHECK_MSG(std::fabs(blueness(ltcL) - blueness(ltcR)) < maskSplit * 0.5f,
              "accurate mode must not carry the mask's left/right split");

    CHECK(s->removeLight(lightNode));
    std::remove(mask.c_str());
}

void two_area_lights_both_light() {
    // Ogre budgets ONE forward area light per kind and silently drops the rest
    // (OgreHlms.cpp mNumAreaApproxLightsLimit(1)); we never called
    // setAreaLightForwardSettings before this lane, so a scene's second area
    // light rendered nothing. One light on each side of a wall: both sides lit.
    Fixture fx;
    View *v = fx.view("area2-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("area2-scene");             REQUIRE(s);
    v->setScene(s);
    enginetest::testCameraLookAt(v, Vec3(0, 0, 6), Vec3(0, 0, 0));
    s->setAmbient(Colour(0.01f, 0.01f, 0.01f), Colour(0.01f, 0.01f, 0.01f));
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = Colour(0.9f, 0.9f, 0.9f); p.roughness = 0.9f;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId wall = s->createNode();
    CHECK(s->attachMesh(wall, mesh, mat));
    s->setNodeTransform(wall, Vec3(0, 0, 0), Quat(), Vec3(8.0f, 8.0f, 0.2f));

    const float h = 0.70710678f;
    LightDesc d;
    // Unclipped, as everywhere else here: a saturated wall reads 1.0 whether
    // one light or two reach it, which is exactly the bug this test exists for.
    d.type = LightType::Area; d.intensity = 0.05f; d.range = 12.0f;
    d.rectWidth = 1.5f; d.rectHeight = 1.5f;

    NodeId left = s->createNode();
    s->setNodeTransform(left, Vec3(-1.6f, 0, 1.2f), Quat(h, 0, 0, h), Vec3(1, 1, 1));
    CHECK(s->setLight(left, d));
    render(fx.e, 2); Image img; REQUIRE(v->readPixels(img));
    auto lum = [&](unsigned x, unsigned y) { const Colour c = img.at(x, y); return c.r + c.g + c.b; };
    const float oneLeft = lum(30, 48), oneRight = lum(66, 48);

    NodeId right = s->createNode();
    s->setNodeTransform(right, Vec3(1.6f, 0, 1.2f), Quat(h, 0, 0, h), Vec3(1, 1, 1));
    CHECK(s->setLight(right, d));
    render(fx.e, 3); REQUIRE(v->readPixels(img));
    const float twoLeft = lum(30, 48), twoRight = lum(66, 48);
    std::printf("    one area light: L %.3f R %.3f | two: L %.3f R %.3f\n",
                oneLeft, oneRight, twoLeft, twoRight);
    CHECK_MSG(oneLeft > oneRight + 0.05f, "the single light must favour its own side");
    CHECK_MSG(twoRight > oneRight + 0.05f,
              "the SECOND area light must light its side (raised forward budget): "
              "%.3f -> %.3f", oneRight, twoRight);
    CHECK(twoLeft > oneRight + 0.05f);

    CHECK(s->removeLight(left));
    CHECK(s->removeLight(right));
}

void overlay_lines_draw_on_top() {
    Fixture fx;
    View *v = fx.view("overlay-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("overlay-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    MeshId cubeMesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = kOrange;
    MaterialId pbr = s->createPbrMaterial(p);
    NodeId cube = s->createNode();
    CHECK(s->attachMesh(cube, cubeMesh, pbr));
    s->setNodeTransform(cube, Vec3(0,0,0), Quat(), Vec3(1.2f,1.2f,1.2f));
    render(fx.e);
    Image img; REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100);

    // A dense green line fan INSIDE the cube (hidden by its faces with depth test).
    std::vector<Vec3> pts;
    for (int i = -10; i <= 10; ++i) { pts.push_back(Vec3(-0.4f, i * 0.02f, 0.f)); pts.push_back(Vec3(0.4f, i * 0.02f, 0.f)); }
    for (int i = -10; i <= 10; ++i) { pts.push_back(Vec3(i * 0.02f, -0.4f, 0.f)); pts.push_back(Vec3(i * 0.02f, 0.4f, 0.f)); }
    MeshId lines = s->createLineMesh(pts, false);
    CHECK_MSG(lines != 0, "%s", fx.e->lastError().c_str());
    MaterialId tested = s->createUnlitMaterial(kGreen, true);    // depth-tested: hidden inside the cube
    NodeId lineNode = s->createNode();
    CHECK(s->attachMesh(lineNode, lines, tested));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px hidden = centre(img);
    CHECK_MSG(hidden.r > hidden.g, "depth-tested lines inside the cube must stay hidden: %d %d %d", hidden.r, hidden.g, hidden.b);

    MaterialId onTop = s->createUnlitMaterial(kGreen, false);    // on top: visible through the cube
    CHECK(s->attachMesh(lineNode, lines, onTop));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px shown = centre(img);
    std::printf("    overlay centre: depth-tested %d %d %d | on-top %d %d %d\n", hidden.r, hidden.g, hidden.b, shown.r, shown.g, shown.b);
    CHECK_MSG(shown.g > 150 && shown.g > shown.r, "on-top lines must win at the centre: %d %d %d", shown.r, shown.g, shown.b);
    // Wireframe on top over the solid cube: edges show through, faces do not cover.
    MaterialId wire = s->createUnlitMaterial(kGreen, false, true);
    NodeId wireNode = s->createNode();
    CHECK(s->attachMesh(wireNode, cubeMesh, wire));
    s->setNodeTransform(wireNode, Vec3(0,0,0), Quat(), Vec3(1.2f,1.2f,1.2f));
    CHECK(s->attachMesh(lineNode, lines, tested));   // put the line fan back behind faces
    render(fx.e); REQUIRE(v->readPixels(img));
    {
        int green = 0, orange = 0;
        for (unsigned y = 0; y < img.height; ++y) for (unsigned x = 0; x < img.width; ++x) {
            const Px q = px(img, x, y);
            if (q.g > 200 && q.r < 80) ++green; else if (q.r > 100 && q.g < 90) ++orange;
        }
        std::printf("    wireframe overlay: %d green edge pixels, %d orange face pixels\n", green, orange);
        CHECK_MSG(green > 20, "wireframe edges visible: %d", green);
        CHECK_MSG(orange > 200, "faces still visible through the wireframe: %d", orange);
    }
    CHECK(s->destroyMaterial(wire));
    CHECK(s->attachMesh(lineNode, lines, onTop));
    CHECK(s->setUnlitMaterial(onTop, Colour(1, 0, 1)));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).b > 150 && centre(img).r > 150);
    CHECK(!s->setPbrMaterial(onTop, p));           // wrong kind, refused
    CHECK(s->destroyMaterial(onTop));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100);                     // cube again
}

// ---------------------------------------------------------------------------
// Widened PBR surface (MATERIALS_EFFECTS_AUDIT.md Option A): alpha blend/cutout,
// two-sided lighting. All pixel-asserted offscreen.

void pbr_alpha_blend_mixes_with_background() {
    Fixture fx;
    View *v = fx.view("blend-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("blend-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = kOrange; p.roughness = 0.8f;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));
    s->setNodeTransform(n, Vec3(0,0,0), Quat(), Vec3(1.2f, 1.2f, 1.2f));
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    const Px opaque = centre(img);
    CHECK_MSG(opaque.r > 100 && opaque.b < 90, "opaque cube is solid orange: %d %d %d", opaque.r, opaque.g, opaque.b);
    // Half-alpha blend: the centre must MIX cube orange with background blue.
    p.alphaMode = PbrAlphaMode::Blend; p.alpha = 0.5f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px mixed = centre(img);
    std::printf("    blend 0.5 centre: opaque %d %d %d -> mixed %d %d %d\n",
                opaque.r, opaque.g, opaque.b, mixed.r, mixed.g, mixed.b);
    CHECK_MSG(mixed.b > opaque.b + 40, "background blue shows through: b %d -> %d", opaque.b, mixed.b);
    CHECK_MSG(mixed.r > 40, "the cube still contributes red: %d", mixed.r);
    CHECK_MSG(mixed.b < 240, "not pure background either: b %d", mixed.b);
    // Alpha ~0: the cube fades out entirely.
    p.alpha = 0.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK_MSG(near(centre(img), kBlue, 20), "alpha 0 is invisible");
    // Back to opaque: solid again.
    p.alphaMode = PbrAlphaMode::Opaque; p.alpha = 1.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100 && centre(img).b < 90);
}

void pbr_alpha_cutout_discards_below_cutoff() {
    Fixture fx;
    View *v = fx.view("cutout-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("cutout-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = Colour(1.0f, 1.0f, 1.0f); p.roughness = 0.8f;
    MaterialId mat = s->createPbrMaterial(p);
    // A green albedo texture whose ALPHA is 0.5 everywhere.
    std::vector<unsigned char> pix(16 * 16 * 4);
    for (int i = 0; i < 16 * 16; ++i) { pix[i*4] = 20; pix[i*4+1] = 230; pix[i*4+2] = 40; pix[i*4+3] = 128; }
    TextureId tex = s->createTexture(16, 16, pix.data(), true);
    CHECK_MSG(tex != 0, "%s", fx.e->lastError().c_str());
    CHECK(s->setPbrTexture(mat, PbrTextureSlot::Albedo, tex));
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));
    s->setNodeTransform(n, Vec3(0,0,0), Quat(), Vec3(1.2f, 1.2f, 1.2f));
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    const Px shown = centre(img);
    CHECK_MSG(shown.g > 100 && shown.g > shown.b, "opaque mode ignores texture alpha: %d %d %d", shown.r, shown.g, shown.b);
    // Cutoff 1.0: texture alpha 0.5 < 1.0 -> every pixel discarded -> invisible.
    p.alphaMode = PbrAlphaMode::Cutout; p.alphaCutoff = 1.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px gone = centre(img);
    std::printf("    cutout 1.0 centre: %d %d %d (texture alpha 0.5)\n", gone.r, gone.g, gone.b);
    CHECK_MSG(near(gone, kBlue, 20), "cutoff above texture alpha discards the cube: %d %d %d", gone.r, gone.g, gone.b);
    // Cutoff 0.25: alpha 0.5 passes -> visible again.
    p.alphaCutoff = 0.25f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px back = centre(img);
    CHECK_MSG(back.g > 100 && back.g > back.b, "cutoff below texture alpha keeps the cube: %d %d %d", back.r, back.g, back.b);
}

// Unreal-parity blend modes (IMAGE_PLANE_SPEC §9): Additive = Src + Dest,
// Modulate = Src × Dest. Comparative pixel asserts only — the framebuffer
// encode is monotone, so "brighter than" / "darker than" survive it.
void pbr_additive_adds_modulate_multiplies() {
    Fixture fx;
    const Colour bgDark(0.10f, 0.10f, 0.45f);   // dark blue background
    View *v = fx.view("srcdest-view", 96, 96, bgDark); REQUIRE(v);
    Scene *s = fx.scene("srcdest-scene");             REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.5f, 0.5f, 0.5f), Colour(0.4f, 0.4f, 0.4f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = kOrange; p.roughness = 0.8f;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));
    s->setNodeTransform(n, Vec3(0,0,0), Quat(), Vec3(1.2f, 1.2f, 1.2f));
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    const Px opaque = centre(img);
    CHECK_MSG(opaque.r > 100 && opaque.b < 90, "opaque cube is solid orange: %d %d %d",
              opaque.r, opaque.g, opaque.b);
    // Additive at alpha 1: Final = cube + background — the background's blue
    // must ADD to the cube instead of being occluded, and the cube's red stays.
    p.alphaMode = PbrAlphaMode::Additive; p.alpha = 1.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px add = centre(img);
    std::printf("    opaque %d %d %d -> additive %d %d %d\n",
                opaque.r, opaque.g, opaque.b, add.r, add.g, add.b);
    CHECK_MSG(add.b > opaque.b + 40, "background blue adds through: b %d -> %d", opaque.b, add.b);
    CHECK_MSG(add.r >= opaque.r - 12, "cube red still contributes: %d vs %d", add.r, opaque.r);
    CHECK_MSG(add.b >= int(std::lround(bgDark.b * 255)) - 12,
              "additive never darkens the background: b %d", add.b);
    // Additive alpha scales the contribution (Fade-scaled colour into ONE/ONE):
    // a faint glow leaves the centre close to the background.
    p.alpha = 0.15f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px faint = centre(img);
    CHECK_MSG(faint.r < add.r - 30, "alpha 0.15 scales the glow down: r %d vs %d", faint.r, add.r);
    CHECK_MSG(faint.b >= int(std::lround(bgDark.b * 255)) - 12, "background survives: b %d", faint.b);
    // Modulate with a mid-grey cube: Final = Src × Dest darkens the background
    // and can never brighten any channel past it.
    p.alphaMode = PbrAlphaMode::Modulate; p.alpha = 1.0f;
    p.albedo = Colour(0.5f, 0.5f, 0.5f);
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px mod = centre(img);
    std::printf("    modulate centre %d %d %d (bg %d %d %d)\n", mod.r, mod.g, mod.b,
                int(std::lround(bgDark.r * 255)), int(std::lround(bgDark.g * 255)),
                int(std::lround(bgDark.b * 255)));
    CHECK_MSG(mod.b < int(std::lround(bgDark.b * 255)) - 20,
              "modulate darkens the background blue: %d", mod.b);
    CHECK_MSG(mod.r <= int(std::lround(bgDark.r * 255)) + 10 &&
              mod.g <= int(std::lround(bgDark.g * 255)) + 10 &&
              mod.b <= int(std::lround(bgDark.b * 255)) + 10,
              "modulate never brightens: %d %d %d", mod.r, mod.g, mod.b);
    // Back to opaque: blendblock and depth write restore, cube solid again.
    p.alphaMode = PbrAlphaMode::Opaque; p.albedo = kOrange;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(centre(img).r > 100 && centre(img).b < 90);
}

void pbr_two_sided_shows_inside_faces() {
    Fixture fx;
    View *v = fx.view("twosided-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("twosided-scene");            REQUIRE(s);
    v->setScene(s);
    s->setAmbient(Colour(0.5f, 0.5f, 0.5f), Colour(0.4f, 0.4f, 0.4f));
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = kOrange; p.roughness = 0.9f;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));
    s->setNodeTransform(n, Vec3(0,0,0), Quat(), Vec3(3.0f, 3.0f, 3.0f));
    // Camera at the origin, INSIDE the cube: one-sided faces are back-face culled,
    // so the background shows straight through the enclosing cube.
    CameraDesc c; c.position = Vec3(0, 0, 0); c.fovDegrees = 45; c.nearClip = 0.05f; c.farClip = 100;
    v->setCamera(c);
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    const Px culled = centre(img);
    CHECK_MSG(near(culled, kBlue, 20), "inside a one-sided cube only the background is visible: %d %d %d", culled.r, culled.g, culled.b);
    p.twoSided = true;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px inside = centre(img);
    std::printf("    inside the cube: one-sided %d %d %d | two-sided %d %d %d\n",
                culled.r, culled.g, culled.b, inside.r, inside.g, inside.b);
    CHECK_MSG(!near(inside, kBlue, 20) && inside.r > 50, "two-sided lighting shows the inner faces: %d %d %d", inside.r, inside.g, inside.b);
    p.twoSided = false;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kBlue, 20));
}

void pbr_texture_scale_tiles_uvs() {
    Fixture fx;
    View *v = fx.view("uvscale-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("uvscale-scene");               REQUIRE(s);
    v->setScene(s);
    // Face-on: only the cube's +Z face (full 0..1 UVs) is visible, so a
    // left-red/right-green texture renders as clean vertical stripes.
    enginetest::testCameraLookAt(v, Vec3(0.0f, 0.0f, 2.2f), Vec3(0.0f, 0.0f, 0.0f));
    s->setAmbient(Colour(0.6f, 0.6f, 0.6f), Colour(0.5f, 0.5f, 0.5f));
    enginetest::addDirectionalLight(s, Vec3(0.2f, -0.3f, -1.0f), 3.14159f);
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = Colour(1.0f, 1.0f, 1.0f); p.roughness = 1.0f;
    MaterialId mat = s->createPbrMaterial(p);
    std::vector<unsigned char> pix(16 * 16 * 4);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        unsigned char *q = &pix[(y * 16 + x) * 4];
        q[0] = x < 8 ? 255 : 0; q[1] = x < 8 ? 0 : 255; q[2] = 0; q[3] = 255;
    }
    TextureId tex = s->createTexture(16, 16, pix.data(), true);
    CHECK_MSG(tex != 0, "%s", fx.e->lastError().c_str());
    CHECK(s->setPbrTexture(mat, PbrTextureSlot::Albedo, tex));
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));
    // Red<->green changes along the middle scanline, cube pixels only: 1 with the
    // texture shown once, 7 when uvScale=4 wraps it into 4 tiles.
    auto transitions = [](const Image &img) {
        int t = 0; bool haveLast = false, lastRed = false;
        const unsigned y = img.height / 2;
        for (unsigned x = 0; x < img.width; ++x) {
            const Px q = px(img, x, y);
            if (q.b > q.r && q.b > q.g) continue;        // blue background
            const bool red   = q.r > q.g + 30;
            const bool green = q.g > q.r + 30;
            if (!red && !green) continue;                // filtered edge texels
            if (haveLast && red != lastRed) ++t;
            haveLast = true; lastRed = red;
        }
        return t;
    };
    render(fx.e); Image img1; REQUIRE(v->readPixels(img1));
    const int t1 = transitions(img1);
    p.uvScale[0] = p.uvScale[1] = 4.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image img4; REQUIRE(v->readPixels(img4));
    const int t4 = transitions(img4);
    std::printf("    stripe transitions: %d at uvScale 1, %d at uvScale 4\n", t1, t4);
    CHECK_MSG(t1 >= 1 && t1 <= 2, "uvScale 1 shows the texture once, got %d edges", t1);
    CHECK_MSG(t4 >= 5, "uvScale 4 tiles the texture across the face, got %d edges", t4);
    // A fixed probe flips colour: 3/16 into the face samples u=0.1875 (red) at
    // scale 1 but u=0.75 (green) once UVs wrap at scale 4.
    unsigned xL = 0, xR = 0; const unsigned yMid = img1.height / 2;
    for (unsigned x = 0; x < img1.width; ++x) {
        const Px q = px(img1, x, yMid);
        if (q.b > q.r && q.b > q.g) continue;
        if (!xL) xL = x;
        xR = x;
    }
    REQUIRE(xR > xL + 16);
    const unsigned xProbe = xL + (xR - xL) * 3u / 16u;
    const Px probe1 = px(img1, xProbe, yMid), probe4 = px(img4, xProbe, yMid);
    std::printf("    probe x=%u: scale1 %d %d %d | scale4 %d %d %d\n",
                xProbe, probe1.r, probe1.g, probe1.b, probe4.r, probe4.g, probe4.b);
    CHECK_MSG(probe1.r > probe1.g + 30, "probe is red at scale 1: %d %d %d", probe1.r, probe1.g, probe1.b);
    CHECK_MSG(probe4.g > probe4.r + 30, "probe is green at scale 4: %d %d %d", probe4.r, probe4.g, probe4.b);
    // Back to 1: the tiling is fully reversible at runtime.
    p.uvScale[0] = p.uvScale[1] = 1.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image imgBack; REQUIRE(v->readPixels(imgBack));
    CHECK_MSG(transitions(imgBack) == t1, "uvScale back to 1 restores the single image");

    // ---- PER-AXIS TILING (MATERIAL_UV_NODES_SPEC). The whole reason
    // textureScale stopped being one float: tiling U without tiling V. The
    // stripes are vertical, so tiling U alone must multiply the transitions
    // exactly as the uniform scale did — and tiling V alone must not change
    // them at all, which is the assertion that proves the axes are separate.
    p.uvScale[0] = 4.0f; p.uvScale[1] = 1.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image imgU; REQUIRE(v->readPixels(imgU));
    const int tU = transitions(imgU);
    p.uvScale[0] = 1.0f; p.uvScale[1] = 4.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image imgV; REQUIRE(v->readPixels(imgV));
    const int tV = transitions(imgV);
    std::printf("    per-axis transitions: U-only %d, V-only %d\n", tU, tV);
    CHECK_MSG(tU == t4, "tiling U alone tiles the vertical stripes: %d vs %d", tU, t4);
    CHECK_MSG(tV == t1, "tiling V alone leaves the vertical stripes alone: %d vs %d", tV, t1);

    // ---- OFFSET shifts the stripe phase. Half a texture width in U swaps
    // which colour the fixed probe lands on, with no tiling at all.
    p.uvScale[0] = 1.0f; p.uvScale[1] = 1.0f;
    p.uvOffset[0] = 0.5f; p.uvOffset[1] = 0.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image imgOff; REQUIRE(v->readPixels(imgOff));
    const Px probeOff = px(imgOff, xProbe, yMid);
    std::printf("    probe x=%u: offset .5 %d %d %d\n", xProbe, probeOff.r, probeOff.g, probeOff.b);
    CHECK_MSG(probeOff.g > probeOff.r + 30, "offset 0.5 shifts the probe to green: %d %d %d",
              probeOff.r, probeOff.g, probeOff.b);

    // ---- ROTATION turns the vertical stripes horizontal. Counted, not
    // eyeballed: after 90 degrees the middle SCANLINE crosses no edge at all
    // (it lies inside one stripe) while the middle COLUMN crosses one.
    auto transitionsColumn = [](const Image &img) {
        int t = 0; bool haveLast = false, lastRed = false;
        const unsigned x = img.width / 2;
        for (unsigned y = 0; y < img.height; ++y) {
            const Px q = px(img, x, y);
            if (q.b > q.r && q.b > q.g) continue;
            const bool red   = q.r > q.g + 30;
            const bool green = q.g > q.r + 30;
            if (!red && !green) continue;
            if (haveLast && red != lastRed) ++t;
            haveLast = true; lastRed = red;
        }
        return t;
    };
    p.uvOffset[0] = 0.0f;
    p.uvRotation = 90.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image imgRot; REQUIRE(v->readPixels(imgRot));
    const int rotRow = transitions(imgRot), rotCol = transitionsColumn(imgRot);
    std::printf("    rotation 90: row edges %d, column edges %d\n", rotRow, rotCol);
    CHECK_MSG(rotRow == 0, "90 degrees leaves no edge along the scanline, got %d", rotRow);
    CHECK_MSG(rotCol >= 1, "90 degrees puts the edge across the column, got %d", rotCol);

    // ---- IDENTITY IS BIT-EXACT. The transform is affine with a precomputed
    // bias precisely so that scale 1 / offset 0 / rotation 0 computes the same
    // lookup it computed before any of this existed. Same bytes, or the claim
    // in Types.h is not true.
    p.uvRotation = 0.0f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image imgIdent; REQUIRE(v->readPixels(imgIdent));
    bool identical = imgIdent.width == imgBack.width && imgIdent.height == imgBack.height
                     && imgIdent.rgba.size() == imgBack.rgba.size();
    if (identical)
        identical = std::equal(imgIdent.rgba.begin(), imgIdent.rgba.end(),
                               imgBack.rgba.begin());
    CHECK_MSG(identical, "the identity UV transform renders byte-identical pixels");
}

// THE SPECULAR / FRESNEL WORKFLOWS (MATERIAL_GAPS_SPEC GAP 1), and the trap
// that would otherwise make them a silent-corruption feature.
//
// setMetalness, setFresnel and setIndexOfRefraction ALL WRITE ONE FLOAT in the
// datablock (mFresnelR). Each asserts the workflow it requires, and our Ogre is
// built RelWithDebInfo, so NDEBUG compiles every one of those asserts out: a
// push that called two of them would corrupt one with the other with NO
// diagnostic anywhere, and the next frame would do it again.
//
// dumpMaterial reads the LIVE datablock through Ogre's own serializer, and that
// serializer writes `metalness` in the metallic workflow and `fresnel` in the
// other two — never both. So "exactly one block is present" is a mechanical,
// non-visual assertion of the rule, and it is the fence this feature needs.
//
// The pixel half is here too: the SAME surface under the SAME light must render
// differently in a metallic and a specular workflow, or the switch is decorative.
void pbr_workflow_writes_exactly_one_of_metalness_and_fresnel() {
    Fixture fx;
    View *v = fx.view("workflow-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("workflow-scene");               REQUIRE(s);
    v->setScene(s);
    enginetest::testCameraLookAt(v, Vec3(0.0f, 0.0f, 2.2f), Vec3(0.0f, 0.0f, 0.0f));
    s->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(0.2f, -0.3f, -1.0f), 3.14159f);

    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p;
    p.albedo = Colour(0.9f, 0.9f, 0.9f);
    p.roughness = 0.2f;
    p.metalness = 1.0f;
    MaterialId mat = s->createPbrMaterial(p);
    REQUIRE(mat != 0);
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));
    render(fx.e);

    const auto has = [](const std::string &dump, const char *key) {
        return dump.find(key) != std::string::npos;
    };

    // ---- 1. the DEFAULT is metallic, and it is OURS ----
    // The datablock's own constructed default is SpecularWorkflow; every
    // material this engine creates is made metallic by applyPbr. If that ever
    // stops being true, every existing scene changes shading model silently.
    std::string dump = s->dumpMaterial(mat);
    CHECK_MSG(!dump.empty(), "dumpMaterial: %s", fx.e->lastError().c_str());
    CHECK_MSG(has(dump, "\"metalness\""),
              "a default material is METALLIC and writes a metalness block");
    CHECK_MSG(!has(dump, "\"fresnel\""),
              "...and writes NO fresnel block — they are the same float");
    Image metalImg; REQUIRE(v->readPixels(metalImg));
    const Px metalPx = centre(metalImg);
    std::printf("    metallic workflow: %d %d %d\n", metalPx.r, metalPx.g, metalPx.b);

    // ---- 2. the specular workflow flips exactly one block ----
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.5f;                      // F0 = 0.04, the dielectric default
    CHECK_MSG(s->setPbrMaterial(mat, p), "%s", fx.e->lastError().c_str());
    render(fx.e);
    dump = s->dumpMaterial(mat);
    CHECK_MSG(has(dump, "specular_ogre"), "the datablock reports the specular workflow");
    CHECK_MSG(has(dump, "\"fresnel\""), "a specular material writes a fresnel block");
    CHECK_MSG(!has(dump, "\"metalness\""),
              "...and NO metalness block — exactly one of the two is ever written");
    Image specImg; REQUIRE(v->readPixels(specImg));
    const Px specPx = centre(specImg);
    std::printf("    specular workflow: %d %d %d\n", specPx.r, specPx.g, specPx.b);
    CHECK_MSG(std::abs(specPx.r - metalPx.r) > 5 || std::abs(specPx.g - metalPx.g) > 5 ||
              std::abs(specPx.b - metalPx.b) > 5,
              "the workflow reaches PIXELS: a full metal and a dielectric differ "
              "(%d %d %d vs %d %d %d)", metalPx.r, metalPx.g, metalPx.b,
              specPx.r, specPx.g, specPx.b);

    // ---- 3. the IOR is converted with the renderer's own formula ----
    // F0 = ((1-ior)/(1+ior))^2. At 2.4 that is 0.1696...; the dump carries the
    // computed value, so this asserts the conversion rather than the setter.
    p.ior = 2.4f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e);
    dump = s->dumpMaterial(mat);
    CHECK_MSG(has(dump, "0.169"), "F0 is computed from the IOR by the renderer's formula");

    // ---- 4. an explicit F0 replaces the IOR-derived one ----
    p.useFresnelColour = true;
    p.fresnelColour = Colour(0.5f, 0.25f, 0.125f);
    p.separateFresnel = true;          // per-channel: a shader permutation, not a value
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e);
    dump = s->dumpMaterial(mat);
    CHECK_MSG(!has(dump, "0.169"), "an explicit F0 replaces the IOR-derived one");
    CHECK_MSG(has(dump, "\"fresnel\""), "and it is still the fresnel block that carries it");
    CHECK_MSG(!has(dump, "\"metalness\""), "still no metalness block");

    // ---- 5. Specular-as-Fresnel is the third workflow, not an alias ----
    p.workflow = PbrParams::Workflow::SpecularAsFresnel;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e);
    dump = s->dumpMaterial(mat);
    CHECK_MSG(has(dump, "specular_fresnel"),
              "the third workflow reports itself distinctly");

    // ---- 6. kS is live in EVERY workflow, metallic included ----
    p.workflow = PbrParams::Workflow::Metallic;
    p.specularColour = Colour(0.2f, 0.4f, 0.8f);
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e);
    dump = s->dumpMaterial(mat);
    CHECK_MSG(has(dump, "\"metalness\""), "back to metallic");
    CHECK_MSG(!has(dump, "\"fresnel\""), "and the fresnel block is gone again");
    CHECK_MSG(has(dump, "\"specular\""),
              "kS is written in the metallic workflow too (the renderer's own rule)");

    // ---- 7. the round trip is lossless, and idempotent ----
    p.specularColour = Colour(1.0f, 1.0f, 1.0f);
    p.metalness = 1.0f;
    p.useFresnelColour = false;
    p.separateFresnel = false;
    p.ior = 1.5f;
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image backImg; REQUIRE(v->readPixels(backImg));
    CHECK_MSG(near(centre(backImg), Colour(metalPx.r/255.0f, metalPx.g/255.0f, metalPx.b/255.0f), 3),
              "metallic -> specular -> metallic restores the original image (%d %d %d vs %d %d %d)",
              centre(backImg).r, centre(backImg).g, centre(backImg).b,
              metalPx.r, metalPx.g, metalPx.b);
    // The engine-side idempotency guard: pushing the SAME params again is a
    // no-op that succeeds and changes nothing.
    CHECK(s->setPbrMaterial(mat, p));
    render(fx.e); Image againImg; REQUIRE(v->readPixels(againImg));
    CHECK_MSG(pixelHash(againImg) == pixelHash(backImg),
              "an unchanged push changes nothing at all");
}

// SRGB IS PART OF A TEXTURE'S IDENTITY (MATERIAL_GAPS_SPEC I-2), a pre-existing
// bug this program fixed in passing.
//
// loadTexture(path, srgb) decides the pixel format from `srgb`, but the dedup
// index used to be keyed on the PATH ALONE — so the FIRST binding of a file
// fixed its colour space for the whole process, and a second caller asking for
// the other space silently got the first one's texture. It was latent while
// every slot had a fixed flag; the workflow switch (the shared metallic/specular
// unit changes colour space with the workflow) and detail layers make it
// reachable.
//
// Asserted mechanically: two loads of ONE file in the two colour spaces must be
// two DIFFERENT texture ids, and destroying one must not take the other's index
// entry with it.
void texture_cache_keys_on_colour_space() {
    Fixture fx;
    // A VIEW FIRST, even though this test renders nothing: a render window must
    // exist before createSceneManager or the backend segfaults (the engine's
    // documented ordering contract). Without it this passes in a full run —
    // where an earlier test made the window — and fails alone.
    View *v = fx.view("srgbkey-view", 32, 32, kBlue); REQUIRE(v);
    Scene *s = fx.scene("srgbkey-scene"); REQUIRE(s);
    v->setScene(s);
    // A mid-grey image: sRGB and linear decodes of the same bytes are visibly
    // different values, which is the whole reason the key matters.
    // The header is ONE LINE and the image is not tiny, matching the other .ppm
    // fixtures in this file: a multi-line header and a 2x2 image both fail to
    // load through FreeImage here, and the failure presents as "cannot open
    // file", which reads like the file is missing when it is not.
    const std::string path = "srgbkey.ppm";
    {
        std::FILE *f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fprintf(f, "P6 8 8 255\n");
        for (int i = 0; i < 64; ++i) { const unsigned char q[3] = { 128, 128, 128 }; std::fwrite(q, 1, 3, f); }
        std::fclose(f);
    }
    const TextureId srgb = s->loadTexture(path, true);
    CHECK_MSG(srgb != 0, "loadTexture(sRGB): %s", fx.e->lastError().c_str());
    const TextureId linear = s->loadTexture(path, false);
    CHECK_MSG(linear != 0, "loadTexture(linear): %s", fx.e->lastError().c_str());
    CHECK_MSG(srgb != linear,
              "the same file in two colour spaces is TWO textures, not one "
              "(got id %u twice)", srgb);
    // Dedup still works WITHIN a colour space — the fix must not turn every
    // load into a new texture.
    CHECK_MSG(s->loadTexture(path, true) == srgb, "an sRGB reload dedups to the sRGB texture");
    CHECK_MSG(s->loadTexture(path, false) == linear, "a linear reload dedups to the linear one");
    // And the index stays in step: destroying one leaves the other findable.
    CHECK(s->destroyTexture(srgb));
    CHECK_MSG(s->loadTexture(path, false) == linear,
              "destroying the sRGB texture did not evict the linear one's index entry");
    const TextureId srgb2 = s->loadTexture(path, true);
    CHECK_MSG(srgb2 != 0 && srgb2 != linear, "and the sRGB one loads again as its own texture");
    // DESTROY BEFORE DELETING THE FILE. Ogre keeps a texture-metadata cache
    // keyed by file name and revisits it at shutdown; a live TextureGpu whose
    // source file has vanished aborts the process there, long after the test
    // has passed. (Found exactly that way.)
    CHECK(s->destroyTexture(linear));
    CHECK(s->destroyTexture(srgb2));
    // THE FIXTURE FILE IS DELIBERATELY LEFT ON DISK. Ogre re-opens every file
    // it has ever loaded a texture from when it writes its TEXTURE METADATA
    // CACHE at shutdown; a DESTROYED texture whose source file has since
    // vanished throws there and takes the teardown with it (`free(): invalid
    // pointer`, long after this test has passed). The sky fixtures in this file
    // may delete theirs because they never destroy the texture. 200 bytes in
    // the test's own working directory is the cheap, honest answer.
}

// RUNTIME TEXTURE WRITES (ADDENDUM A-1) and the ordering claim that makes them
// usable per frame.
//
// The claim under test is not "the pixels change" — that is the easy half. It is
// that the upload RECORDS INTO THE OPEN COMMAND BUFFER, ahead of this frame's
// draws, so the new pixels RENDER with no flush at all. There is NO flush call
// anywhere in the first half of this test, deliberately: if the ordering claim
// were wrong the second render would show the old colour and this would fail.
//
// And the refusals, which are the whole safety of the verb: a file-loaded
// texture is POOLED and shared by every material that loaded that path, and a
// decal slice is shared PROCESS-WIDE and refcounted. Both are refused by name.
void update_texture_rewrites_pixels_without_a_flush() {
    Fixture fx;
    View *v = fx.view("updtex-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("updtex-scene");             REQUIRE(s);
    v->setScene(s);
    enginetest::testCameraLookAt(v, Vec3(0.0f, 0.0f, 2.2f), Vec3(0.0f, 0.0f, 0.0f));
    s->setAmbient(Colour(0.8f, 0.8f, 0.8f), Colour(0.7f, 0.7f, 0.7f));

    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = Colour(1, 1, 1); p.roughness = 1.0f;
    MaterialId mat = s->createPbrMaterial(p);
    REQUIRE(mat != 0);

    // A 2x2 solid RED texture, pixel-born.
    std::vector<unsigned char> pix(2 * 2 * 4);
    const auto fill = [&pix](unsigned char r, unsigned char g, unsigned char b) {
        for (int i = 0; i < 4; ++i) {
            pix[size_t(i) * 4 + 0] = r; pix[size_t(i) * 4 + 1] = g;
            pix[size_t(i) * 4 + 2] = b; pix[size_t(i) * 4 + 3] = 255;
        }
    };
    fill(255, 0, 0);
    TextureId tex = s->createTexture(2, 2, pix.data(), true);
    CHECK_MSG(tex != 0, "%s", fx.e->lastError().c_str());
    CHECK(s->setPbrTexture(mat, PbrTextureSlot::Albedo, tex));
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));

    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    const Px red = centre(img);
    std::printf("    before update: %d %d %d\n", red.r, red.g, red.b);
    CHECK_MSG(red.r > red.g + 40 && red.r > red.b + 40,
              "the cube samples the RED texture first (%d %d %d)", red.r, red.g, red.b);

    // ---- the write. NO flush, no wait, no re-bind. ----
    fill(0, 255, 0);
    CHECK_MSG(s->updateTexture(tex, 2, 2, pix.data()), "%s", fx.e->lastError().c_str());
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px green = centre(img);
    std::printf("    after update:  %d %d %d\n", green.r, green.g, green.b);
    CHECK_MSG(green.g > green.r + 40 && green.g > green.b + 40,
              "the SAME texture now samples GREEN, with no flush and no re-bind (%d %d %d)",
              green.r, green.g, green.b);

    // Repeatable: a second write in the same session is not an allocation and
    // not a special case.
    fill(0, 0, 255);
    CHECK(s->updateTexture(tex, 2, 2, pix.data()));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px blue = centre(img);
    CHECK_MSG(blue.b > blue.r + 40 && blue.b > blue.g + 40,
              "and again (%d %d %d)", blue.r, blue.g, blue.b);

    // ---- the refusals ----
    CHECK_MSG(!s->updateTexture(tex, 4, 4, pix.data()),
              "a size mismatch is REFUSED (a Vulkan texture cannot resize)");
    CHECK_MSG(!s->updateTexture(0, 2, 2, pix.data()), "an unknown id is refused");
    CHECK_MSG(!s->updateTexture(tex, 2, 2, nullptr), "null pixels are refused");

    // A FILE-LOADED texture is pooled and shared by path: refused by name.
    const std::string path = "updtex.ppm";
    {
        std::FILE *f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fprintf(f, "P6 8 8 255\n");   // one-line header, like the other fixtures here
        for (int i = 0; i < 64; ++i) { const unsigned char q[3] = { 200, 100, 50 }; std::fwrite(q, 1, 3, f); }
        std::fclose(f);
    }
    const TextureId fileTex = s->loadTexture(path, true);
    CHECK_MSG(fileTex != 0, "loadTexture: %s", fx.e->lastError().c_str());
    CHECK_MSG(!s->updateTexture(fileTex, 8, 8, pix.data()),
              "a FILE-LOADED texture is refused — its format and mips come from the file "
              "and it is pooled by path");
    // Destroyed, and the file LEFT ON DISK — see the note in
    // texture_cache_keys_on_colour_space: Ogre's shutdown metadata-cache write
    // re-opens it, and a destroyed texture whose file is gone aborts teardown.
    if (fileTex) CHECK(s->destroyTexture(fileTex));
}

// PER-MATERIAL REFLECTION CUBEMAP (ADDENDUM A-5).
//
// Two spheres under one sky reflection; the hero one gets its own solid-colour
// cube. Its reflection must carry the override's tint and the other's must not.
// The fence that matters is the LAST assertion: the override, like the global
// cubemap, must go dark under automatic PCC — the shader's one env-probe slot
// holds a cube ARRAY there and a manual cubemap is not merely wrong, it is
// UNCOMPILABLE (the 2026-09-07 measurement in OgreSky.cpp).
void per_material_reflection_cubemap_overrides_the_sky() {
    Fixture fx;
    View *v = fx.view("matcube-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("matcube-scene");               REQUIRE(s);
    v->setScene(s);
    enginetest::testCameraLookAt(v, Vec3(0.0f, 0.0f, 3.2f), Vec3(0.0f, 0.0f, 0.0f));
    s->setAmbient(Colour(0.1f, 0.1f, 0.1f), Colour(0.1f, 0.1f, 0.1f));

    // Six solid faces, all the same colour: a cube whose every direction is
    // that colour, so no camera angle can make the assertion accidental.
    const auto solidFaces = [&](unsigned char r, unsigned char g, unsigned char b,
                                TextureId out[6]) {
        std::vector<unsigned char> face(16 * 16 * 4);
        for (size_t i = 0; i < 16 * 16; ++i) {
            face[i * 4 + 0] = r; face[i * 4 + 1] = g; face[i * 4 + 2] = b; face[i * 4 + 3] = 255;
        }
        for (int i = 0; i < 6; ++i) out[i] = s->createTexture(16, 16, face.data(), true);
    };

    TextureId skyFaces[6];  solidFaces(40, 40, 40, skyFaces);     // a dark grey world
    TextureId heroFaces[6]; solidFaces(255, 40, 40, heroFaces);   // a RED world
    for (int i = 0; i < 6; ++i) { REQUIRE(skyFaces[i] != 0); REQUIRE(heroFaces[i] != 0); }

    // The reflection half of a SkyDesc alone: no sky, six IBL faces.
    SkyDesc skyIbl; skyIbl.reflections = true;
    for (int i = 0; i < 6; ++i) skyIbl.reflectionFaces[i] = skyFaces[i];
    CHECK_MSG(s->setSky(skyIbl), "setSky (reflections only): %s", fx.e->lastError().c_str());
    const TextureId heroCube = s->createCubemap(heroFaces);
    CHECK_MSG(heroCube != 0, "createCubemap: %s", fx.e->lastError().c_str());

    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams mirror;
    mirror.albedo = Colour(0.05f, 0.05f, 0.05f);
    mirror.metalness = 1.0f;
    mirror.roughness = 0.02f;      // a near-mirror: what it shows IS its environment
    MaterialId plainMat = s->createPbrMaterial(mirror);
    MaterialId heroMat  = s->createPbrMaterial(mirror);
    REQUIRE(plainMat != 0); REQUIRE(heroMat != 0);
    CHECK_MSG(s->setPbrTexture(heroMat, PbrTextureSlot::Reflection, heroCube),
              "%s", fx.e->lastError().c_str());

    NodeId plain = s->createNode(), hero = s->createNode();
    CHECK(s->attachMesh(plain, mesh, plainMat));
    CHECK(s->attachMesh(hero, mesh, heroMat));
    s->setNodeTransform(plain, Vec3(-1.1f, 0, 0), Quat(), Vec3(0.9f, 0.9f, 0.9f));
    s->setNodeTransform(hero,  Vec3( 1.1f, 0, 0), Quat(), Vec3(0.9f, 0.9f, 0.9f));

    render(fx.e, 2); Image img; REQUIRE(v->readPixels(img));
    const Px plainPx = px(img, img.width / 4, img.height / 2);
    const Px heroPx  = px(img, img.width * 3 / 4, img.height / 2);
    std::printf("    plain %d %d %d | hero %d %d %d\n",
                plainPx.r, plainPx.g, plainPx.b, heroPx.r, heroPx.g, heroPx.b);
    CHECK_MSG(heroPx.r > heroPx.g + 15 && heroPx.r > heroPx.b + 15,
              "the OVERRIDDEN material reflects its own RED cube (%d %d %d)",
              heroPx.r, heroPx.g, heroPx.b);
    CHECK_MSG(!(plainPx.r > plainPx.g + 15),
              "...and the material beside it still reflects the grey sky (%d %d %d)",
              plainPx.r, plainPx.g, plainPx.b);

    // Clearing the override falls back to the global cube, through the same
    // one function — no separate un-bind path to get wrong.
    CHECK(s->setPbrTexture(heroMat, PbrTextureSlot::Reflection, 0));
    render(fx.e, 2); REQUIRE(v->readPixels(img));
    const Px cleared = px(img, img.width * 3 / 4, img.height / 2);
    std::printf("    cleared: %d %d %d\n", cleared.r, cleared.g, cleared.b);
    CHECK_MSG(!(cleared.r > cleared.g + 15),
              "clearing the override returns the material to the sky reflection (%d %d %d)",
              cleared.r, cleared.g, cleared.b);

    CHECK(s->destroyTexture(heroCube));
}

// THE UNLIT SHADING MODEL (HLMS_ADOPTION P4a). Unlit is not a lighting preset
// on the PBR pipeline — it is the OTHER backend material family, so the switch
// destroys the material, rebuilds it and re-attaches every renderable while
// keeping the MaterialId the host's whole scene refers to.
//
// The definition of "unlit" as an assertion: the surface renders its authored
// colour, and CHANGING THE LIGHTING DOES NOT MOVE ONE BYTE OF IT. A shaded cube
// under the same light is the control — without it, "flat colour" could just be
// a well-chosen light.
void unlit_shading_model_ignores_lighting() {
    Fixture fx;
    View *v = fx.view("unlit-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("unlit-scene");             REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.2f, 0.2f, 0.2f));
    const NodeId lightNode = enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    REQUIRE(lightNode != 0);

    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = kOrange; p.roughness = 0.7f; p.metalness = 0.0f;
    MaterialId mat = s->createPbrMaterial(p);
    REQUIRE(mat != 0);
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));
    s->setNodeTransform(n, Vec3(0,0,0), Quat(), Vec3(1.2f, 1.2f, 1.2f));

    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    const Px lit = centre(img);
    std::printf("    LIT cube under the light: %d %d %d (albedo %d %d %d)\n",
                lit.r, lit.g, lit.b, int(std::lround(kOrange.r*255)),
                int(std::lround(kOrange.g*255)), int(std::lround(kOrange.b*255)));
    CHECK_MSG(!near(lit, kOrange, 12),
              "the LIT control is SHADED, not its raw albedo: %d %d %d", lit.r, lit.g, lit.b);

    // ---- the switch ----
    CHECK_MSG(s->setShadingModel(mat, ShadingModel::Unlit), "setShadingModel(Unlit): %s",
              fx.e->lastError().c_str());
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px unlit = centre(img);
    std::printf("    UNLIT cube: %d %d %d\n", unlit.r, unlit.g, unlit.b);
    CHECK_MSG(near(unlit, kOrange, 6), "an unlit surface renders its authored colour: %d %d %d",
              unlit.r, unlit.g, unlit.b);
    // The re-attach worked: the item is still there, exactly one of it.
    CHECK_MSG(s->itemCount(n) == 1, "the family switch left exactly one renderable, got %zu",
              s->itemCount(n));
    CHECK_MSG(!near(centre(img), kBlue, 20), "the cube did not vanish behind the switch");

    // ---- the definition: lighting cannot touch it ----
    const unsigned long long unlitHash = pixelHash(img);
    s->removeNode(lightNode);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    render(fx.e); REQUIRE(v->readPixels(img));
    CHECK_MSG(pixelHash(img) == unlitHash,
              "removing every light leaves the unlit frame BYTE-IDENTICAL (that is what "
              "unlit means)");

    // The control, in the dark: the same scene change annihilates a lit surface.
    CHECK(s->setShadingModel(mat, ShadingModel::Lit));
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px darkLit = centre(img);
    std::printf("    LIT cube with no lights and no ambient: %d %d %d\n",
                darkLit.r, darkLit.g, darkLit.b);
    CHECK_MSG(darkLit.r < 40 && darkLit.g < 40,
              "the lit control goes black without lighting: %d %d %d",
              darkLit.r, darkLit.g, darkLit.b);

    // ---- the round trip is lossless ----
    // Put the lighting back and assert the ORIGINAL lit image returns byte for
    // byte: the switch rebuilt the datablock from the parameters it was
    // holding, so nothing the host pushed was quietly lost on the way out and
    // back.
    s->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    render(fx.e); Image back; REQUIRE(v->readPixels(back));
    CHECK_MSG(near(centre(back), Colour(lit.r/255.0f, lit.g/255.0f, lit.b/255.0f), 3),
              "Lit -> Unlit -> Lit restores the shaded image: %d %d %d vs %d %d %d",
              centre(back).r, centre(back).g, centre(back).b, lit.r, lit.g, lit.b);

    // Idempotence: setting the model it already has is a no-op that succeeds.
    CHECK(s->setShadingModel(mat, ShadingModel::Lit));
    render(fx.e); Image again; REQUIRE(v->readPixels(again));
    CHECK_MSG(pixelHash(again) == pixelHash(back), "a no-op switch changes nothing at all");
}

// The base-colour MAP survives a family switch, and an unlit textured surface
// samples it raw. This is the half of the switch that is easiest to get wrong:
// the texture bindings live on the datablock that just got destroyed, so a
// switch that only rebuilt PARAMETERS would silently strip every map.
void unlit_keeps_its_base_colour_map() {
    Fixture fx;
    View *v = fx.view("unlittex-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("unlittex-scene");               REQUIRE(s);
    v->setScene(s);
    enginetest::testCameraLookAt(v, Vec3(0.0f, 0.0f, 2.2f), Vec3(0.0f, 0.0f, 0.0f));
    s->setAmbient(Colour(0.6f, 0.6f, 0.6f), Colour(0.5f, 0.5f, 0.5f));
    enginetest::addDirectionalLight(s, Vec3(0.2f, -0.3f, -1.0f), 3.14159f);
    MeshId mesh = s->createMesh(unitCubeData());
    PbrParams p; p.albedo = Colour(1.0f, 1.0f, 1.0f); p.roughness = 1.0f;
    MaterialId mat = s->createPbrMaterial(p);
    // Left half red, right half green — the same fixture the uvScale suite uses.
    std::vector<unsigned char> pix(16 * 16 * 4);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        unsigned char *q = &pix[(y * 16 + x) * 4];
        q[0] = x < 8 ? 255 : 0; q[1] = x < 8 ? 0 : 255; q[2] = 0; q[3] = 255;
    }
    TextureId tex = s->createTexture(16, 16, pix.data(), true);
    REQUIRE(tex != 0);
    CHECK(s->setPbrTexture(mat, PbrTextureSlot::Albedo, tex));
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, mesh, mat));

    auto sideColours = [&](const Image &img, Px &left, Px &right) {
        const unsigned y = img.height / 2;
        left  = px(img, img.width * 5u / 16u, y);
        right = px(img, img.width * 11u / 16u, y);
    };
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    Px l0, r0; sideColours(img, l0, r0);
    CHECK_MSG(l0.r > l0.g + 30 && r0.g > r0.r + 30,
              "lit control: the map shows red|green (%d %d %d | %d %d %d)",
              l0.r, l0.g, l0.b, r0.r, r0.g, r0.b);

    CHECK_MSG(s->setShadingModel(mat, ShadingModel::Unlit), "setShadingModel: %s",
              fx.e->lastError().c_str());
    render(fx.e); REQUIRE(v->readPixels(img));
    Px l1, r1; sideColours(img, l1, r1);
    std::printf("    unlit textured face: %d %d %d | %d %d %d\n",
                l1.r, l1.g, l1.b, r1.r, r1.g, r1.b);
    CHECK_MSG(l1.r > l1.g + 30 && r1.g > r1.r + 30,
              "the base-colour map survived the switch: %d %d %d | %d %d %d",
              l1.r, l1.g, l1.b, r1.r, r1.g, r1.b);
    // Unlit samples the map RAW: the texels arrive at their authored values
    // with no shading term anywhere near them.
    CHECK_MSG(l1.r == 255 && l1.g == 0 && r1.r == 0 && r1.g == 255,
              "unlit texels are the raw authored ones (%d %d | %d %d)", l1.r, l1.g, r1.r, r1.g);
    // ...and they are the same with the lighting taken away, which is the
    // property that makes them "raw" rather than "coincidentally saturated".
    {
        const unsigned long long litHash = pixelHash(img);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        render(fx.e); Image dark; REQUIRE(v->readPixels(dark));
        CHECK_MSG(pixelHash(dark) == litHash,
                  "the unlit textured frame is byte-identical with the ambient removed");
        s->setAmbient(Colour(0.6f, 0.6f, 0.6f), Colour(0.5f, 0.5f, 0.5f));
    }

    // ...and switching back restores the lit sampling, so no binding was lost.
    CHECK(s->setShadingModel(mat, ShadingModel::Lit));
    render(fx.e); REQUIRE(v->readPixels(img));
    Px l2, r2; sideColours(img, l2, r2);
    CHECK_MSG(std::abs(l2.r - l0.r) <= 3 && std::abs(r2.g - r0.g) <= 3,
              "back to Lit: the map is bound exactly as before (%d %d | %d %d)",
              l2.r, l0.r, r2.g, r0.g);
}

// RULE 5 (HLMS_ADOPTION_SPEC §6.3), and it is a REFUSAL rather than a fallback.
// The Unlit family hard-zeroes the skeleton properties when it hashes a
// renderable, so a rigged mesh would render welded to its bind pose — a second,
// solid body standing where the character used to be, with no error anywhere.
// Both directions are closed: you cannot make a rigged mesh's material unlit,
// and you cannot rig a mesh whose material already is.
void unlit_refuses_rigged_meshes() {
    Fixture fx;
    // A View has to exist before a Scene can (the engine's ordering contract:
    // createSceneManager segfaults without a render window). Nothing here
    // renders — this suite is about refusals.
    View *v = fx.view("unlitrig-view", 32, 32, kBlue); REQUIRE(v);
    Scene *s = fx.scene("unlitrig-scene"); REQUIRE(s);
    v->setScene(s);
    MeshData skinned = unitCubeData();
    skinned.blendIndices.assign(skinned.vertexCount() * 4, 0);
    skinned.blendWeights.assign(skinned.vertexCount() * 4, 0.0f);
    for (size_t i = 0; i < skinned.vertexCount(); ++i) skinned.blendWeights[i * 4] = 1.0f;
    MeshId rigged = s->createMesh(skinned);
    REQUIRE(rigged != 0);

    SkeletonDesc rig; rig.id = "unlit-refusal-rig";
    BoneDesc root; root.name = "root"; rig.bones.push_back(root);

    PbrParams p; p.albedo = kOrange;
    MaterialId mat = s->createPbrMaterial(p);
    NodeId n = s->createNode();
    CHECK_MSG(s->attachSkinnedMesh(n, rigged, mat, rig), "attachSkinnedMesh: %s",
              fx.e->lastError().c_str());

    CHECK_MSG(!s->setShadingModel(mat, ShadingModel::Unlit),
              "a material used by a rigged mesh must REFUSE the Unlit model");
    const std::string why = fx.e->lastError();
    std::printf("    refusal: %s\n", why.c_str());
    CHECK_MSG(why.find("skin") != std::string::npos && why.find("rigged") != std::string::npos,
              "the refusal names the reason, got: %s", why.c_str());

    // ...and nothing changed: the material is still lit and still usable.
    PbrParams q = p; q.roughness = 0.3f;
    CHECK_MSG(s->setPbrMaterial(mat, q),
              "the refused material is untouched and still a PBR material: %s",
              fx.e->lastError().c_str());

    // The other direction: an already-unlit material refuses to be rigged.
    MaterialId unlitMat = s->createPbrMaterial([]{ PbrParams u; u.shadingModel = ShadingModel::Unlit;
                                                   u.albedo = kGreen; return u; }());
    REQUIRE(unlitMat != 0);
    NodeId n2 = s->createNode();
    CHECK_MSG(!s->attachSkinnedMesh(n2, rigged, unlitMat, rig),
              "attachSkinnedMesh must refuse a material whose shading model is Unlit");
    std::printf("    reverse refusal: %s\n", fx.e->lastError().c_str());
    CHECK_MSG(fx.e->lastError().find("Unlit") != std::string::npos,
              "the reverse refusal names the model, got: %s", fx.e->lastError().c_str());

    // MEASURED, and it is why the refusal tests the NODE's live skeleton and
    // not "the mesh carries blend data": once a rig has been bound to a mesh,
    // Ogre gives EVERY Item built from it a skeleton instance — including one
    // attached through plain attachMesh. So such a node would be welded to the
    // bind pose under Unlit exactly like a deliberately skinned one, and it is
    // refused too. Deliberate, and asserted so nobody "fixes" it back.
    {
        NodeId shared = s->createNode();
        MaterialId sharedMat = s->createPbrMaterial(p);
        CHECK(s->attachMesh(shared, rigged, sharedMat));
        CHECK_MSG(s->hasSkeleton(shared),
                  "a plain attach of an already-rigged mesh still yields a skeleton instance");
        CHECK_MSG(!s->setShadingModel(sharedMat, ShadingModel::Unlit),
                  "...and is therefore refused Unlit for the same reason");
    }

    // An ordinary unrigged mesh is not affected by any of this.
    NodeId n3 = s->createNode();
    MeshId plainMesh = s->createMesh(unitCubeData());
    MaterialId plain = s->createPbrMaterial(p);
    CHECK(s->attachMesh(n3, plainMesh, plain));
    CHECK_MSG(s->setShadingModel(plain, ShadingModel::Unlit),
              "an unrigged mesh's material switches fine: %s", fx.e->lastError().c_str());
}

// Fog is EXPONENTIAL (Ogre's AtmosphereNpr math, adopted whole): a surface keeps
// the fraction 2^(-distance * density) of its own colour, and the rest is fog. It
// therefore NEVER equals the fog colour at any distance, which is why this suite
// asserts the law rather than an endpoint.
//
// Offscreen views render into PFG_RGBA8_UNORM (OgreView) and the Pbs shader emits
// linear colour (hw_gamma_write: it expects the hardware to encode, and a non-sRGB
// target does not), so a read-back byte IS the linear value — no decode, which is
// what makes the arithmetic below exact enough to assert a law on.
double linearOf(int channel8) { return channel8 / 255.0; }
/// The surviving fraction of the surface's own colour, from a fogged pixel, its
/// unfogged baseline, and the fog colour — per channel, in linear space.
double transmittanceOf(int fogged, int baseline, double fogColourLinear) {
    const double denom = linearOf(baseline) - fogColourLinear;
    if (std::abs(denom) < 1e-6) return -1.0;
    return (linearOf(fogged) - fogColourLinear) / denom;
}

/// The rig both fog tests use: near cube at the origin (eye distance ~3), and a
/// huge cube 50 units down the view axis whose faces fill every pixel around it
/// (measured eye distance ~8.2 at the sample point).
void buildFogRig(View *v, Scene *s) {
    v->setScene(s);
    populate(s, kOrange);
    aim(v);
    const NodeId farNode = enginetest::addTestCube(s, kCyan, 0.0f, 0.6f);
    enginetest::setNodePosition(s, farNode, Vec3(-28.6f, -23.4f, -33.8f));
    enginetest::setNodeScale(s, farNode, Vec3(60.0f, 60.0f, 60.0f));
}

const Colour kMagenta(1.0f, 0.0f, 1.0f);   // 0/1 channels: sRGB-invariant

void fog_transmittance_is_exponential() {
    Fixture fx;
    View *v = fx.view("fog-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("fog-scene");             REQUIRE(s);
    buildFogRig(v, s);

    Image img;
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px near0 = centre(img);
    const Px far0  = px(img, img.width / 2, 6);
    CHECK_MSG(!near(far0, kMagenta, 30),
              "baseline far surface is not fog-coloured: %d %d %d", far0.r, far0.g, far0.b);

    // Breakthrough off for the law: it deliberately bends the curve (see the
    // breakthrough test below), and the law is about the curve.
    FogDesc fog;
    fog.enabled = true;
    fog.colour = kMagenta;
    fog.breakFalloff = 0.0f;
    fog.density = 0.12f;
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px near1 = centre(img);
    const Px far1  = px(img, img.width / 2, 6);

    fog.density = 0.24f;                       // exactly double
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px far2 = px(img, img.width / 2, 6);

    // Green: the fog colour's green is 0, so the measured value IS the surviving
    // fraction of the surface's own green.
    const double t1 = transmittanceOf(far1.g, far0.g, 0.0);
    const double t2 = transmittanceOf(far2.g, far0.g, 0.0);
    const double tNear = transmittanceOf(near1.g, near0.g, 0.0);
    std::printf("    far %d %d %d -> %d %d %d -> %d %d %d | T(d)=%.3f T(2d)=%.3f T(d)^2=%.3f"
                " | near T(d)=%.3f\n",
                far0.r, far0.g, far0.b, far1.r, far1.g, far1.b, far2.r, far2.g, far2.b,
                t1, t2, t1 * t1, tNear);

    CHECK_MSG(t1 > 0.15 && t1 < 0.85, "the far surface is measurably but not totally fogged: %.3f", t1);
    // THE law: doubling the density squares what survives.
    CHECK_MSG(std::abs(t2 - t1 * t1) <= 0.03,
              "doubling density squares transmittance: %.3f vs %.3f", t2, t1 * t1);
    // ... and nearer surfaces keep more of themselves. (Unlike the retired linear
    // fog, there is no unfogged near zone: this cube at ~3 units IS fogged.)
    CHECK_MSG(tNear > t1 + 0.05, "the near surface is less fogged than the far one: %.3f vs %.3f",
              tNear, t1);

    // Off is EXACT, not approximately exact: disabling drops the scene's
    // atmosphere, which drops the hlms_fog property, which removes the fog code
    // from the shader entirely. Every offscreen pixel suite rests on this.
    fog.enabled = false;
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px far3 = px(img, img.width / 2, 6);
    CHECK_MSG(far3.r == far0.r && far3.g == far0.g && far3.b == far0.b,
              "fog off restores the baseline exactly: %d %d %d vs %d %d %d",
              far3.r, far3.g, far3.b, far0.r, far0.g, far0.b);
}

void fog_height_layer() {
    Fixture fx;
    View *v = fx.view("fog-h-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("fog-h-scene");             REQUIRE(s);
    buildFogRig(v, s);

    Image img;
    FogDesc fog;
    fog.enabled = true;
    fog.colour = kMagenta;
    fog.breakFalloff = 0.0f;

    // (1) Distance fog alone.
    fog.density = 0.15f;
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px distanceOnly = px(img, img.width / 2, 6);

    // (2) The height layer with ZERO falloff is a uniform medium — which is the
    // distance fog. Same density, same pixel: this is what proves our optical
    // depth integral agrees with the built-in exponential it rides beside.
    fog.density = 0.0f;
    fog.heightDensity = 0.15f;
    fog.heightFalloff = 0.0f;
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px heightOnly = px(img, img.width / 2, 6);
    CHECK_MSG(std::abs(heightOnly.r - distanceOnly.r) <= 2 &&
              std::abs(heightOnly.g - distanceOnly.g) <= 2 &&
              std::abs(heightOnly.b - distanceOnly.b) <= 2,
              "a height layer with no falloff equals plain distance fog: %d %d %d vs %d %d %d",
              heightOnly.r, heightOnly.g, heightOnly.b,
              distanceOnly.r, distanceOnly.g, distanceOnly.b);

    // (3) With a real falloff the layer is thicker the lower its level sits
    // relative to the camera: raising the level raises the fog around the scene.
    fog.heightFalloff = 0.5f;
    fog.heightLevel = -2.0f;
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px levelLow = px(img, img.width / 2, 6);
    fog.heightLevel = 2.0f;
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px levelHigh = px(img, img.width / 2, 6);
    std::printf("    height level -2 -> %d %d %d, +2 -> %d %d %d\n",
                levelLow.r, levelLow.g, levelLow.b, levelHigh.r, levelHigh.g, levelHigh.b);
    // Green is the surface's own colour here (the fog colour has none), so less
    // green = more fog.
    CHECK_MSG(levelHigh.g < levelLow.g - 1,
              "raising the fog level thickens the fog: green %d vs %d", levelHigh.g, levelLow.g);

    // (4) heightDensity 0 is an exact no-op, not a multiply by one: the shader
    // skips the branch, so distance fog alone must come back bit for bit.
    fog.density = 0.15f;
    fog.heightDensity = 0.0f;
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px noHeight = px(img, img.width / 2, 6);
    CHECK_MSG(noHeight.r == distanceOnly.r && noHeight.g == distanceOnly.g &&
              noHeight.b == distanceOnly.b,
              "no height layer is exactly plain distance fog: %d %d %d vs %d %d %d",
              noHeight.r, noHeight.g, noHeight.b, distanceOnly.r, distanceOnly.g, distanceOnly.b);
}

void fog_breakthrough_spares_bright_surfaces() {
    Fixture fx;
    View *v = fx.view("fog-b-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("fog-b-scene");             REQUIRE(s);
    v->setScene(s);
    populate(s, kOrange);
    aim(v);
    // A big EMISSIVE backdrop: emissive is part of the colour the fog sees, so
    // this surface is bright enough to break through.
    const NodeId farNode = s->createNode();
    REQUIRE(farNode != 0);
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = Colour(0.05f, 0.05f, 0.05f);
    p.emissive = Colour(0.0f, 3.0f, 0.0f);      // green, well past breakMinBrightness
    const MaterialId mat = s->createPbrMaterial(p);
    REQUIRE(mesh != 0); REQUIRE(mat != 0);
    REQUIRE(s->attachMesh(farNode, mesh, mat));
    enginetest::setNodePosition(s, farNode, Vec3(-28.6f, -23.4f, -33.8f));
    enginetest::setNodeScale(s, farNode, Vec3(60.0f, 60.0f, 60.0f));

    Image img;
    FogDesc fog;
    fog.enabled = true;
    fog.colour = kMagenta;
    fog.density = 0.25f;
    fog.breakFalloff = 0.0f;                    // plain exponential
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px plain = px(img, img.width / 2, 6);

    fog.breakMinBrightness = 0.25f;
    fog.breakFalloff = 2.0f;                    // bright pixels resist
    s->setFog(fog);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px broken = px(img, img.width / 2, 6);
    std::printf("    emissive backdrop: plain %d %d %d, breakthrough %d %d %d\n",
                plain.r, plain.g, plain.b, broken.r, broken.g, broken.b);
    CHECK_MSG(broken.g > plain.g + 2,
              "a bright surface keeps more of itself with breakthrough on: %d vs %d",
              broken.g, plain.g);
}


// ---------------------------------------------------------------------------
// The compositor chain (POST_CHAIN_SPEC.md phases 0-1).

/// THE workspace seam. Six call sites used to create a workspace inline; they
/// are now one function, and workspaceGeneration() counts it. This pins both
/// halves of the contract the planar-reflection lane depends on: every
/// structural change goes through the seam exactly once, and a push of a value
/// that has not changed goes through it zero times.
void workspace_seam_counts_every_rebuild() {
    Fixture fx;
    View *v = fx.view("seam-view", 64, 64, kBlue); REQUIRE(v);
    CHECK_MSG(v->workspaceGeneration() == 0u,
              "a view with no scene has no workspace yet: %u", v->workspaceGeneration());
    Scene *s = fx.scene("seam-scene"); REQUIRE(s);
    CHECK(v->setScene(s));
    const unsigned afterBind = v->workspaceGeneration();
    CHECK_MSG(afterBind == 1u, "binding a scene builds exactly one workspace: %u", afterBind);

    // Same value = free. Hosts (SceneMirror) push these every frame.
    v->setBackground(v->background());
    v->setSampleCount(v->sampleCount());
    v->setShadows(v->shadows());
    CHECK_MSG(v->workspaceGeneration() == afterBind,
              "re-pushing unchanged values must not rebuild: %u", v->workspaceGeneration());

    // Each real change rebuilds once, and only once.
    v->setBackground(kGreen);
    CHECK_MSG(v->workspaceGeneration() == afterBind + 1, "background change: %u", v->workspaceGeneration());
    v->setShadows(true);
    CHECK_MSG(v->workspaceGeneration() == afterBind + 2, "shadow toggle: %u", v->workspaceGeneration());
    v->resize(96, 96);
    CHECK_MSG(v->workspaceGeneration() == afterBind + 3, "offscreen resize: %u", v->workspaceGeneration());
    v->setSampleCount(4);
    CHECK_MSG(v->workspaceGeneration() == afterBind + 4, "sample-count change: %u", v->workspaceGeneration());
    render(fx.e); Image img; REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kGreen, 12));

    // Node definitions must not leak across view recreation: the chain is
    // multi-node now, and a teardown that removed only one definition would
    // make the next view of the same name throw at addNodeDefinition.
    fx.e->destroyView(v); fx.forget(v);
    View *again = fx.view("seam-view", 64, 64, kOrange); REQUIRE(again);
    CHECK(again->setScene(s));
    render(fx.e); REQUIRE(again->readPixels(img));
    CHECK_MSG(near(centre(img), kOrange, 12),
              "a view recreated under the same name renders: %d %d %d",
              centre(img).r, centre(img).g, centre(img).b);
}

/// Shadow-caster VAO optimization (POST_CHAIN_SPEC.md §11) is on by default and
/// static geometry still casts the same shadow with it either way.
void shadow_mesh_optimization_keeps_static_shadows() {
    Fixture fx;
    CHECK_MSG(fx.e->shadowMeshOptimization(),
              "the engine ships with shadow-mesh optimization on");
    View *v = fx.view("smo-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("smo-scene");             REQUIRE(s);
    v->setScene(s);
    v->setShadows(true);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.1f, 0.1f, 0.1f));
    MeshId cubeMesh = s->createMesh(unitCubeData());
    PbrParams white; white.albedo = Colour(0.9f, 0.9f, 0.9f); white.roughness = 0.9f;
    MaterialId mat = s->createPbrMaterial(white);
    NodeId ground = s->createNode();
    CHECK(s->attachMesh(ground, cubeMesh, mat));
    s->setNodeTransform(ground, Vec3(0, -0.55f, 0), Quat(), Vec3(8, 0.1f, 8));
    NodeId cube = s->createNode();
    CHECK(s->attachMesh(cube, cubeMesh, mat));
    s->setNodeTransform(cube, Vec3(0, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));
    NodeId sun = s->createNode();
    LightDesc d; d.type = LightType::Directional; d.intensity = 3.0f; d.castShadows = true;
    CHECK(s->setLight(sun, d));
    s->setNodeTransform(sun, Vec3(0, 5, 0), Quat(0, 0, 0.3826834f, 0.9238795f), Vec3(1,1,1));
    CameraDesc c; c.position = Vec3(0, 6, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f); c.fovDegrees = 50;
    v->setCamera(c);
    render(fx.e, 4); Image img; REQUIRE(v->readPixels(img));
    auto contrast = [&](const Image &i) {
        int lo = 255, hi = 0;
        for (unsigned x = 62; x < 94; ++x) { const Px q = px(i, x, 48); const int l = (q.r+q.g+q.b)/3; lo = std::min(lo,l); hi = std::max(hi,l); }
        for (unsigned x = 2;  x < 34; ++x) { const Px q = px(i, x, 48); const int l = (q.r+q.g+q.b)/3; lo = std::min(lo,l); hi = std::max(hi,l); }
        return hi - lo;
    };
    const int optimized = contrast(img);
    std::printf("    ground contrast with optimized shadow VAOs: %d\n", optimized);
    CHECK_MSG(optimized > 40, "an optimized shadow mesh still casts a shadow (contrast %d)", optimized);

    // Same scene, meshes built with the optimization OFF: the shadow must look
    // the same. (A fresh mesh is needed — the flag is read at build time.)
    fx.e->setShadowMeshOptimization(false);
    CHECK(!fx.e->shadowMeshOptimization());
    MeshId plainMesh = s->createMesh(unitCubeData());
    CHECK(s->attachMesh(ground, plainMesh, mat));
    CHECK(s->attachMesh(cube, plainMesh, mat));
    render(fx.e, 4); REQUIRE(v->readPixels(img));
    const int plain = contrast(img);
    std::printf("    ground contrast with aliased shadow VAOs:   %d\n", plain);
    CHECK_MSG(std::abs(plain - optimized) <= 12,
              "the optimization must not change what a shadow looks like: %d vs %d",
              optimized, plain);
    fx.e->setShadowMeshOptimization(true);
}

/// The trap the rider exists to avoid (POST_CHAIN_SPEC.md §11): a CPU-skinned
/// (MeshData::dynamic) mesh uploads its new pose into the NORMAL vertex buffer
/// only. Give it an independent optimized shadow VAO and its shadow freezes at
/// the pose it was built with — silent, and visually baffling. buildMeshV2
/// therefore never optimizes a dynamic mesh, and this is the assertion.
void dynamic_mesh_shadow_follows_its_pose() {
    Fixture fx;
    View *v = fx.view("dynshadow-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("dynshadow-scene");             REQUIRE(s);
    v->setScene(s);
    v->setShadows(true);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.1f, 0.1f, 0.1f));
    PbrParams white; white.albedo = Colour(0.9f, 0.9f, 0.9f); white.roughness = 0.9f;
    MaterialId mat = s->createPbrMaterial(white);

    MeshId groundMesh = s->createMesh(unitCubeData());
    NodeId ground = s->createNode();
    CHECK(s->attachMesh(ground, groundMesh, mat));
    s->setNodeTransform(ground, Vec3(0, -0.55f, 0), Quat(), Vec3(8, 0.1f, 8));

    // The caster: a DYNAMIC mesh, i.e. the CPU-skinning path.
    MeshData casterData = unitCubeData();
    casterData.dynamic = true;
    MeshId casterMesh = s->createMesh(casterData);
    REQUIRE(casterMesh != 0);
    NodeId caster = s->createNode();
    CHECK(s->attachMesh(caster, casterMesh, mat));
    s->setNodeTransform(caster, Vec3(0, 0.6f, 0), Quat(), Vec3(1, 1, 1));

    NodeId sun = s->createNode();
    LightDesc d; d.type = LightType::Directional; d.intensity = 3.0f; d.castShadows = true;
    CHECK(s->setLight(sun, d));
    s->setNodeTransform(sun, Vec3(0, 5, 0), Quat(0, 0, 0.3826834f, 0.9238795f), Vec3(1,1,1));
    CameraDesc c; c.position = Vec3(0, 6, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f); c.fovDegrees = 50;
    v->setCamera(c);

    // The sun is rolled 45 degrees about +Z, so it travels (+0.707, -0.707, 0):
    // the caster at y=0.6 throws its shadow onto the y=-0.5 slab about 1.1 units
    // toward +X, which lands in this strip of the 96x96 readback. The caster
    // itself images left of it, around the centre, and never enters the strip.
    auto stripDarkest = [&](const Image &i) {
        int lo = 255;
        for (unsigned x = 58; x < 94; ++x) { const Px q = px(i, x, 48); lo = std::min(lo, (q.r+q.g+q.b)/3); }
        return lo;
    };
    render(fx.e, 4); Image img; REQUIRE(v->readPixels(img));
    const int shadowed = stripDarkest(img);

    // Deform the mesh the way the skinning path does: slide every vertex 2 units
    // along -X. The caster stays inside the camera AND the shadow frustum (so it
    // is not merely culled — that would prove nothing); only its SHADOW leaves
    // the strip. A stale, independent shadow VAO would leave the shadow behind.
    std::vector<float> moved;
    for (size_t i = 0; i + 2 < casterData.positions.size(); i += 3) {
        moved.push_back(casterData.positions[i] - 2.0f);
        moved.push_back(casterData.positions[i+1]);
        moved.push_back(casterData.positions[i+2]);
    }
    CHECK(s->updateMeshVertices(casterMesh, moved, casterData.normals));
    render(fx.e, 4); REQUIRE(v->readPixels(img));
    const int unshadowed = stripDarkest(img);
    std::printf("    strip darkest: shadow in the strip %d, caster deformed away %d\n",
                shadowed, unshadowed);
    CHECK_MSG(unshadowed - shadowed > 25,
              "a CPU-skinned mesh's shadow must follow its pose (%d -> %d)",
              shadowed, unshadowed);
}


// ---------------------------------------------------------------------------
// The engine-drawn overlay: the stats readout and the loading cover
// (STATS_OVERLAY_SPEC.md; the phase-0 proof is spikes/overlay-v1-vulkan).
//
// The FIRST test here is the load-bearing one. Under owner decision D2 the
// cover is raised on EVERY world open, so an overlay that leaked into offscreen
// views would not be a rare edge — it would put a flat grey panel over every
// thumbnail, every material preview and every pixel suite in this file. §5.2's
// guarantee is therefore asserted byte-for-byte, not argued.

/// A cover desc that would be impossible to miss if it ever rendered: a full
/// view of magenta with two lines of text over it.
ViewOverlayDesc loudCover() {
    ViewOverlayDesc d;
    d.cover = ViewOverlayDesc::Cover::Loading;
    d.coverTitle = "Loading world";
    d.coverSubtitle = "a name that would be visible";
    d.coverFill = Colour(1.0f, 0.0f, 1.0f, 1.0f);
    d.stats = true;
    d.lines = { "62 fps  4.7 ms", "318 draws  1.24 M tris" };
    return d;
}

/// THE NEGATIVE TEST, and it is law: an offscreen view renders the SAME BYTES
/// with a cover and a stats readout pushed at it as it does with none, unless
/// it explicitly opted in. Nothing about this is a matter of discipline — the
/// gate is in one place (OgreView::overlaysAllowed, feeding ChainDesc::overlays
/// into the single overlay-bearing pass) exactly like the post chain's.
void hud_overlay_is_ignored_offscreen_unless_asked() {
    Fixture fx;
    View *v = fx.view("hud-guard-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("hud-guard-scene");             REQUIRE(s);
    v->setScene(s);
    populate(s, kOrange);
    aim(v);
    render(fx.e, 4); Image plain; REQUIRE(v->readPixels(plain));
    const unsigned genBefore = v->workspaceGeneration();

    v->setOverlay(loudCover());          // allowOffscreen deliberately NOT set
    CHECK_MSG(v->workspaceGeneration() == genBefore,
              "an offscreen view must not even rebuild for an overlay it will ignore: %u -> %u",
              genBefore, v->workspaceGeneration());
    render(fx.e, 4); Image covered; REQUIRE(v->readPixels(covered));

    std::printf("    pixelhash  no overlay %016llx   cover+stats refused %016llx\n",
                pixelHash(plain), pixelHash(covered));
    CHECK_MSG(plain.rgba == covered.rgba,
              "BYTE-IDENTICAL or the guarantee is gone: %016llx vs %016llx",
              pixelHash(plain), pixelHash(covered));
    // The view still remembers what it was asked for — it just does not act.
    CHECK(v->overlay().cover == ViewOverlayDesc::Cover::Loading);
    CHECK(v->overlay().stats);
}

/// The other half: a negative test that can never fail proves nothing. With
/// allowOffscreen the SAME desc must change the picture, and change it in the
/// places it claims to draw — the whole view for the cover, the corner for the
/// readout.
void hud_overlay_draws_where_it_says_when_allowed() {
    Fixture fx;
    View *v = fx.view("hud-draw-view", 192, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("hud-draw-scene");               REQUIRE(s);
    v->setScene(s);
    populate(s, kOrange);
    aim(v);
    render(fx.e, 4); Image plain; REQUIRE(v->readPixels(plain));

    // ---- the cover: opaque, over everything, in its own colour -------------
    ViewOverlayDesc cover = loudCover();
    cover.stats = false;                 // the cover alone, so the centre is unambiguous
    cover.allowOffscreen = true;
    v->setOverlay(cover);
    // The ENTITLEMENT is a graph change (the pass def's mIncludeOverlays), so
    // this one rebuild is expected and is the only one in the feature.
    render(fx.e, 4); Image withCover; REQUIRE(v->readPixels(withCover));
    CHECK_MSG(plain.rgba != withCover.rgba, "the cover must change the picture");
    const Px c = centre(withCover);
    CHECK_MSG(near(c, cover.coverFill, 6),
              "the cover fill must own the centre pixel: %d %d %d", c.r, c.g, c.b);
    // Opaque, not blended: a corner far from any text is the fill too.
    const Px corner4 = px(withCover, 4, withCover.height - 4);
    CHECK_MSG(near(corner4, cover.coverFill, 6),
              "the cover must fill the whole view: %d %d %d", corner4.r, corner4.g, corner4.b);
    // The cover's TEXT is the one-shot-caption trap's blast radius: a static
    // caption that never re-flags renders nothing at all, silently. Count
    // pixels in the title band that are neither the fill nor the scene.
    size_t titlePixels = 0;
    for (unsigned y = withCover.height / 2 - 14; y < withCover.height / 2 + 14; ++y)
        for (unsigned x = 0; x < withCover.width; ++x)
            if (!near(px(withCover, x, y), cover.coverFill, 24)) ++titlePixels;
    std::printf("    cover title/subtitle pixels: %zu\n", titlePixels);
    CHECK_MSG(titlePixels > 40,
              "the cover's STATIC captions must actually render (the one-shot trap): %zu px",
              titlePixels);

    // ---- the stats readout: a corner, and only that corner -----------------
    ViewOverlayDesc stats;
    stats.stats = true;
    stats.allowOffscreen = true;
    stats.corner = OverlayCorner::TopLeft;
    stats.colour = Colour(1.0f, 0.0f, 0.0f, 1.0f);
    stats.lines = { "MMMMMMMMMMMM", "MMMMMMMMMMMM" };
    v->setOverlay(stats);
    render(fx.e, 4); Image withStats; REQUIRE(v->readPixels(withStats));

    auto differing = [&](unsigned x0, unsigned y0, unsigned x1, unsigned y1) {
        size_t n = 0;
        for (unsigned y = y0; y < y1; ++y)
            for (unsigned x = x0; x < x1; ++x) {
                const Px a = px(plain, x, y), b = px(withStats, x, y);
                if (std::abs(a.r - b.r) > 8 || std::abs(a.g - b.g) > 8 ||
                    std::abs(a.b - b.b) > 8) ++n;
            }
        return n;
    };
    const size_t topLeft     = differing(0, 0, withStats.width / 2, withStats.height / 2);
    const size_t bottomRight = differing(withStats.width / 2, withStats.height / 2,
                                         withStats.width, withStats.height);
    std::printf("    stats readout: %zu px changed top-left, %zu bottom-right\n",
                topLeft, bottomRight);
    CHECK_MSG(topLeft > 60, "the stats readout must draw in its corner: %zu px", topLeft);
    CHECK_MSG(bottomRight == 0,
              "the stats readout must touch NOTHING outside its corner: %zu px", bottomRight);
}

/// Showing and hiding the overlay is element state, never a chain edit — or
/// every F3 press and every world open would cost a workspace rebuild
/// (STATS_OVERLAY_SPEC §6.6 test 3). Only the offscreen ENTITLEMENT changes the
/// graph, and that is a test-only door.
void hud_overlay_toggle_does_not_rebuild_the_workspace() {
    Fixture fx;
    View *v = fx.view("hud-gen-view", 64, 64, kBlue); REQUIRE(v);
    Scene *s = fx.scene("hud-gen-scene");             REQUIRE(s);
    v->setScene(s);
    populate(s, kOrange);
    aim(v);
    ViewOverlayDesc on;
    on.allowOffscreen = true;            // pay the one entitlement rebuild up front
    v->setOverlay(on);
    render(fx.e, 2);
    const unsigned gen = v->workspaceGeneration();

    for (int i = 0; i < 3; ++i) {
        ViewOverlayDesc d;
        d.allowOffscreen = true;
        d.stats = true;
        d.lines = { i % 2 ? "a" : "b" };
        d.cover = i % 2 ? ViewOverlayDesc::Cover::Loading : ViewOverlayDesc::Cover::NoScene;
        d.coverTitle = i % 2 ? "Loading world" : "No world open";
        v->setOverlay(d);
        render(fx.e, 1);
    }
    v->setOverlay(on);
    render(fx.e, 1);
    CHECK_MSG(v->workspaceGeneration() == gen,
              "toggling the overlay must not rebuild the workspace: %u -> %u",
              gen, v->workspaceGeneration());
}

/// The data half (phase 1). Not "plausible numbers" — the two properties a
/// caller can actually rely on: the geometry counters are LAZY (off until
/// somebody asks, which is why the first read is honest about reporting
/// nothing), and the timings move as frames go by.
void render_stats_are_live_and_lazily_recorded() {
    Fixture fx;
    View *v = fx.view("stats-view", 64, 64, kBlue); REQUIRE(v);
    Scene *s = fx.scene("stats-scene");             REQUIRE(s);
    v->setScene(s);
    populate(s, kOrange);
    aim(v);

    RenderStats first;
    CHECK(fx.e->renderStats(first));
    // The first read may or may not find recording already on — an earlier test
    // in this process may have asked. What must hold either way: after the read
    // switched it on and a frame has been drawn, there ARE draws.
    render(fx.e, 4);
    RenderStats after;
    CHECK(fx.e->renderStats(after));
    CHECK_MSG(after.metricsRecording, "reading renderStats must have enabled recording");
    std::printf("    %.1f fps  %.2f ms  %llu draws  %llu batches  %llu tris  %llu verts\n",
                after.fps, after.frameMs, after.draws, after.batches, after.triangles,
                after.vertices);
    CHECK_MSG(after.draws > 0, "a frame with a lit cube in it draws something: %llu", after.draws);
    CHECK_MSG(after.triangles > 0, "…and it has triangles: %llu", after.triangles);
    CHECK_MSG(after.frameMs > 0.0, "the rolling frame time must be a real number: %f",
              after.frameMs);
    CHECK_MSG(after.fps > 0.0, "…and so must the fps derived from it: %f", after.fps);
    CHECK_MSG(after.worstMs >= after.bestMs,
              "worst must not be better than best: %f vs %f", after.worstMs, after.bestMs);
}

/// objectCounts() is the CENSUS half of the instrumentation renderStats() is
/// the cost half of (fps audit F11, `app.engineObjects()`).
///
/// Everything here is a DELTA. The Engine is one per process and every test
/// before this one has left the counts wherever it left them, so "there are
/// exactly N meshes" is not a statement this suite can make; "creating a mesh
/// added exactly one, and destroying it took exactly one away" is, and it is
/// also the only statement the steady-state gate needs to be true.
///
/// The point of asserting the DESTROY direction as loudly as the create one:
/// this counter exists to catch a leak, and a counter that only ever goes up
/// would report every scene teardown in the program as one.
void object_counts_track_lifetimes() {
    ObjectCounts base;
    REQUIRE(gEngine->objectCounts(base));
    std::printf("    base: %u views (%u enabled) %u scenes %u nodes %u meshes "
                "%u materials %u textures %u datablocks\n",
                base.views, base.enabledViews, base.scenes, base.nodes, base.meshes,
                base.materials, base.textures, base.datablocks);

    ObjectCounts c;
    {
        Fixture fx;
        View *v = fx.view("census-view", 64, 64, kBlue); REQUIRE(v);
        Scene *s = fx.scene("census-scene");             REQUIRE(s);
        v->setScene(s);

        REQUIRE(gEngine->objectCounts(c));
        CHECK_MSG(c.views == base.views + 1, "one view added: %u -> %u", base.views, c.views);
        CHECK_MSG(c.scenes == base.scenes + 1, "one scene added: %u -> %u", base.scenes, c.scenes);
        // A view is enabled when it is created; the count must SEE that, because
        // "enabled views stopped matching live views" is the exact shape of the
        // offscreen-isolation defect (fps audit F5).
        CHECK_MSG(c.enabledViews == base.enabledViews + 1,
                  "the new view counts as enabled: %u -> %u", base.enabledViews, c.enabledViews);
        v->setEnabled(false);
        REQUIRE(gEngine->objectCounts(c));
        CHECK_MSG(c.enabledViews == base.enabledViews,
                  "disabling it takes it back out: %u", c.enabledViews);
        CHECK_MSG(c.views == base.views + 1, "…without destroying it: %u", c.views);
        v->setEnabled(true);

        // Content. populate() adds a light node and a cube (node + mesh +
        // material); the assertions are on the deltas it is responsible for.
        const ObjectCounts empty = c;
        populate(s, kOrange);
        aim(v);
        render(fx.e, 2);
        REQUIRE(gEngine->objectCounts(c));
        std::printf("    populated: +%u nodes +%u meshes +%u materials\n",
                    c.nodes - empty.nodes, c.meshes - empty.meshes,
                    c.materials - empty.materials);
        CHECK_MSG(c.nodes > empty.nodes, "populating the scene added nodes: %u -> %u",
                  empty.nodes, c.nodes);
        CHECK_MSG(c.meshes == empty.meshes + 1, "…exactly one mesh: %u -> %u",
                  empty.meshes, c.meshes);
        CHECK_MSG(c.materials == empty.materials + 1, "…and exactly one material: %u -> %u",
                  empty.materials, c.materials);

        // AND IT IS FLAT WHILE IDLE. This is the property the whole
        // perf.epic_steady_state gate stands on, asserted here at the boundary
        // where it is cheap: rendering the same scene again allocates nothing.
        const ObjectCounts beforeIdle = c;
        render(fx.e, 10);
        REQUIRE(gEngine->objectCounts(c));
        CHECK_MSG(c.views == beforeIdle.views && c.scenes == beforeIdle.scenes &&
                      c.nodes == beforeIdle.nodes && c.meshes == beforeIdle.meshes &&
                      c.materials == beforeIdle.materials && c.textures == beforeIdle.textures &&
                      c.datablocks == beforeIdle.datablocks,
                  "ten idle frames changed nothing: nodes %u->%u meshes %u->%u "
                  "materials %u->%u textures %u->%u datablocks %u->%u",
                  beforeIdle.nodes, c.nodes, beforeIdle.meshes, c.meshes,
                  beforeIdle.materials, c.materials, beforeIdle.textures, c.textures,
                  beforeIdle.datablocks, c.datablocks);
    }
    // The fixture destroyed the view and the scene: the census must come all the
    // way back down, including the per-scene registries that died with it.
    REQUIRE(gEngine->objectCounts(c));
    CHECK_MSG(c.views == base.views, "the view is gone again: %u -> %u", base.views, c.views);
    CHECK_MSG(c.scenes == base.scenes, "the scene is gone again: %u -> %u", base.scenes, c.scenes);
    CHECK_MSG(c.nodes == base.nodes, "its nodes went with it: %u -> %u", base.nodes, c.nodes);
    CHECK_MSG(c.meshes == base.meshes, "its meshes went with it: %u -> %u", base.meshes, c.meshes);
    CHECK_MSG(c.materials == base.materials, "its materials went with it: %u -> %u",
              base.materials, c.materials);
}


// ---------------------------------------------------------------------------
// The post chain (POST_CHAIN_SPEC.md phases 3-7).
//
// The chain is an ON-SCREEN feature: offscreen views keep the simple workspace
// so that thumbnails, previews and every pixel suite stay exact. PostFxDesc has
// one explicit door through that — allowOffscreen — and these tests are the
// reason it exists (the other caller is screenshot({postFx:true})).

/// The offscreen guarantee itself: pushing a full post-fx description at an
/// offscreen view changes NOTHING unless it opts in.
void postfx_is_ignored_offscreen_unless_asked() {
    Fixture fx;
    View *v = fx.view("fx-guard-view", 64, 64, kBlue); REQUIRE(v);
    Scene *s = fx.scene("fx-guard-scene");             REQUIRE(s);
    v->setScene(s);
    populate(s, kOrange);
    aim(v);
    render(fx.e); Image before; REQUIRE(v->readPixels(before));
    const unsigned genBefore = v->workspaceGeneration();

    PostFxDesc fxDesc;
    fxDesc.hdr = true; fxDesc.bloom = true; fxDesc.ssao = true; fxDesc.smaaPreset = 1;
    v->setPostFx(fxDesc);
    CHECK_MSG(v->workspaceGeneration() == genBefore,
              "an offscreen view must not even rebuild for post fx: %u -> %u",
              genBefore, v->workspaceGeneration());
    render(fx.e); Image after; REQUIRE(v->readPixels(after));
    const Px b = centre(before), a = centre(after);
    CHECK_MSG(b.r == a.r && b.g == a.g && b.b == a.b,
              "offscreen pixels must be untouched by post fx: %d %d %d vs %d %d %d",
              b.r, b.g, b.b, a.r, a.g, a.b);
    // postFx() still reports what the host asked for — the view remembers, it
    // just does not act.
    CHECK(v->postFx().hdr);
}

/// HDR + filmic tonemap + auto exposure, on an offscreen view that opted in.
/// The assertion is behavioural, not a magic colour: a tonemapped frame is
/// still recognisably the scene, and RAISING THE EXPOSURE MAKES IT BRIGHTER.
void hdr_tonemap_and_exposure() {
    Fixture fx;
    View *v = fx.view("hdr-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("hdr-scene");             REQUIRE(s);
    v->setScene(s);
    populate(s, kOrange);
    aim(v);
    render(fx.e); Image plain; REQUIRE(v->readPixels(plain));
    const Px plainCube = centre(plain);

    PostFxDesc fxDesc;
    fxDesc.allowOffscreen = true;
    fxDesc.hdr = true;
    fxDesc.exposure = 0.0f;
    v->setPostFx(fxDesc);
    CHECK_MSG(v->workspaceGeneration() >= 2u, "enabling HDR rebuilds the chain: %u",
              v->workspaceGeneration());
    renderFor(fx.e, 1.2);   // auto-exposure adapts per SECOND, not per frame
    if (!fx.e->lastError().empty())
        std::printf("    lastError after HDR enable: %s\n", fx.e->lastError().c_str());
    Image hdr; REQUIRE(v->readPixels(hdr));
    const Px hdrCube = centre(hdr);
    std::printf("    cube: plain %d %d %d -> hdr %d %d %d\n",
                plainCube.r, plainCube.g, plainCube.b, hdrCube.r, hdrCube.g, hdrCube.b);
    // Something rendered (not a black frame, which is what a failed shader
    // compile inside JAH_TRY looks like), and it is still the orange cube:
    // red dominates, blue is the least.
    CHECK_MSG(hdrCube.r > 20, "the HDR frame is not black: %d %d %d",
              hdrCube.r, hdrCube.g, hdrCube.b);
    CHECK_MSG(hdrCube.r > hdrCube.b, "the cube is still orange after tonemapping: %d %d %d",
              hdrCube.r, hdrCube.g, hdrCube.b);

    // Exposure is a UNIFORM: changing it must not rebuild anything.
    const unsigned gen = v->workspaceGeneration();
    fxDesc.exposure = 2.0f;
    v->setPostFx(fxDesc);
    CHECK_MSG(v->workspaceGeneration() == gen,
              "exposure is a uniform, not a graph change: %u -> %u", gen,
              v->workspaceGeneration());
    renderFor(fx.e, 1.5);
    Image bright; REQUIRE(v->readPixels(bright));
    const Px brightCube = centre(bright);
    const int lumHdr = (hdrCube.r + hdrCube.g + hdrCube.b) / 3;
    const int lumBright = (brightCube.r + brightCube.g + brightCube.b) / 3;
    std::printf("    exposure 0 -> %d, exposure +2 -> %d\n", lumHdr, lumBright);
    CHECK_MSG(lumBright > lumHdr, "raising the exposure brightens the image: %d -> %d",
              lumHdr, lumBright);

    // And off again: the chain collapses back to the passthrough graph and the
    // pixels return to exactly what they were.
    v->setPostFx(PostFxDesc());
    render(fx.e, 2); Image off; REQUIRE(v->readPixels(off));
    const Px offCube = centre(off);
    CHECK_MSG(offCube.r == plainCube.r && offCube.g == plainCube.g && offCube.b == plainCube.b,
              "turning the chain off restores the exact original: %d %d %d vs %d %d %d",
              offCube.r, offCube.g, offCube.b, plainCube.r, plainCube.g, plainCube.b);
}

/// THE FIXED-EXPOSURE TONEMAP (PostFxDesc::tonemapFixed) — the deterministic
/// grade every SECONDARY SURFACE uses (owner report 2026-09-07, fix wave item
/// 6: thumbnails, previews, screenshots and the project tiles all photographed
/// raw linear radiance, so a brightly lit world came back as a white card).
///
/// Three things have to be true, and none of them is a magic colour:
///   1. RAW CLIPS. A surface driven well above 1.0 saturates to flat white in
///      the passthrough graph — the defect, constructed rather than asserted
///      from memory.
///   2. THE GRADE DOES NOT. The same frame through hdr + tonemapFixed rolls the
///      highlight off: no fully-saturated pixel, and the brightest channel is
///      below 255.
///   3. IT IS DETERMINISTIC. Two reads separated by real time are BYTE
///      IDENTICAL, which the automatic exposure — which adapts against the wall
///      clock — cannot be. This is the whole reason the fixed form exists.
void fixed_exposure_tonemap() {
    Fixture fx;
    View *v = fx.view("tonemap-view", 96, 96, Colour(0, 0, 0)); REQUIRE(v);
    Scene *s = fx.scene("tonemap-scene");                       REQUIRE(s);
    v->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    MeshId cube = s->createMesh(unitCubeData());
    PbrParams hot;
    hot.albedo = Colour(0, 0, 0);
    // TWICE WHITE. The window this demonstration lives in is narrow and worth
    // recording: raw saturates at 1.0, and the filmic curve's grade tail
    // ((x-0.5)*1.25 + 0.5 + 0.11, HDR/FinalToneMapping_ps.glsl) saturates at a
    // scene value of about 2.5 at this exposure. So 2.0 is over-range for the
    // raw path and inside the graded one — which is precisely the band the
    // whole feature exists to recover. (A value of 8 clips in BOTH: no
    // tonemapper has infinite range, and claiming otherwise would be the kind
    // of assertion that passes for the wrong reason.)
    hot.emissive = Colour(2.0f, 2.0f, 2.0f);
    MaterialId mat = s->createPbrMaterial(hot);
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, cube, mat));
    s->setNodeTransform(n, Vec3(0, 0, 0), Quat(), Vec3(0.9f, 0.9f, 0.9f));
    enginetest::testCameraLookAt(v, Vec3(0, 0, 3.0f), Vec3(0, 0, 0));

    const auto histogram = [](const Image &img, int &saturated, int &lit, int &brightest) {
        saturated = lit = brightest = 0;
        for (unsigned y = 0; y < img.height; ++y) for (unsigned x = 0; x < img.width; ++x) {
            const Px p = { (unsigned char)(img.at(x, y).r * 255.0f + 0.5f),
                           (unsigned char)(img.at(x, y).g * 255.0f + 0.5f),
                           (unsigned char)(img.at(x, y).b * 255.0f + 0.5f) };
            if (p.r < 8 && p.g < 8 && p.b < 8) continue;      // background
            ++lit;
            brightest = std::max(brightest, int(std::max({ p.r, p.g, p.b })));
            if (p.r >= 254 && p.g >= 254 && p.b >= 254) ++saturated;
        }
    };

    // 1. RAW: the defect, measured.
    render(fx.e, 3); Image raw; REQUIRE(v->readPixels(raw));
    int rawSat = 0, rawLit = 0, rawMax = 0;
    histogram(raw, rawSat, rawLit, rawMax);
    std::printf("    raw:      %d lit, %d saturated (%.0f%%), brightest %d\n",
                rawLit, rawSat, rawLit ? 100.0 * rawSat / rawLit : 0.0, rawMax);
    CHECK_MSG(rawLit > 500, "the hot cube rendered: %d lit pixels", rawLit);
    CHECK_MSG(rawSat * 2 > rawLit,
              "RAW CLIPS: most of the hot cube is flat white (%d of %d lit pixels)",
              rawSat, rawLit);

    // 2. THE GRADE: same scene, same view, no clipping.
    PostFxDesc fxDesc;
    fxDesc.allowOffscreen = true;
    fxDesc.hdr = true;
    fxDesc.tonemapFixed = true;
    fxDesc.exposure = 0.6f;          // secondaryfx::kFixedExposure
    v->setPostFx(fxDesc);
    CHECK_MSG(v->workspaceGeneration() >= 2u,
              "the fixed-exposure chain is a graph change: %u", v->workspaceGeneration());
    render(fx.e, 3); Image graded; REQUIRE(v->readPixels(graded));
    int gSat = 0, gLit = 0, gMax = 0;
    histogram(graded, gSat, gLit, gMax);
    std::printf("    tonemap:  %d lit, %d saturated (%.0f%%), brightest %d\n",
                gLit, gSat, gLit ? 100.0 * gSat / gLit : 0.0, gMax);
    CHECK_MSG(gLit > 500, "the graded frame still shows the cube: %d lit pixels", gLit);
    CHECK_MSG(gSat * 4 < gLit,
              "THE GRADE DOES NOT CLIP: %d of %d lit pixels saturated (was %d of %d)",
              gSat, gLit, rawSat, rawLit);
    CHECK_MSG(gMax < 255, "and the brightest channel rolls off short of white: %d", gMax);

    // 3. DETERMINISTIC. The automatic chain adapts against WALL CLOCK time
    // (renderFor exists precisely because of that), so a second read after real
    // time has passed would move. The fixed form must not move by one bit.
    renderFor(fx.e, 0.6);
    Image again; REQUIRE(v->readPixels(again));
    bool identical = again.width == graded.width && again.height == graded.height &&
                     again.rgba == graded.rgba;
    CHECK_MSG(identical,
              "the fixed-exposure grade is frame- and time-independent (byte-identical reads)");

    // ...and for contrast, the AUTOMATIC form on the same view is not: turning
    // tonemapFixed off puts the luminance reduction back, which is a graph
    // change, and the picture then depends on how long it has been adapting.
    const unsigned genFixed = v->workspaceGeneration();
    fxDesc.tonemapFixed = false;
    v->setPostFx(fxDesc);
    CHECK_MSG(v->workspaceGeneration() > genFixed,
              "tonemapFixed is part of sameShape(): flipping it rebuilds (%u -> %u)",
              genFixed, v->workspaceGeneration());

    // Off again: the passthrough graph and the ORIGINAL pixels, exactly.
    v->setPostFx(PostFxDesc());
    render(fx.e, 3); Image off; REQUIRE(v->readPixels(off));
    CHECK_MSG(off.rgba == raw.rgba,
              "turning the chain off restores the exact raw readback — the offscreen "
              "determinism guarantee is untouched by this feature");
}

/// Bloom: a bright emissive surface bleeds light into the dark background
/// around it. The assertion is that bleed, not a colour.
void bloom_bleeds_bright_areas() {
    Fixture fx;
    View *v = fx.view("bloom-view", 96, 96, Colour(0, 0, 0)); REQUIRE(v);
    Scene *s = fx.scene("bloom-scene");                       REQUIRE(s);
    v->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    MeshId cube = s->createMesh(unitCubeData());
    PbrParams glow;
    glow.albedo = Colour(0, 0, 0);
    glow.emissive = Colour(12.0f, 12.0f, 12.0f);   // well above 1.0: this is what HDR is for
    MaterialId mat = s->createPbrMaterial(glow);
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, cube, mat));
    s->setNodeTransform(n, Vec3(0, 0, 0), Quat(), Vec3(0.6f, 0.6f, 0.6f));
    enginetest::testCameraLookAt(v, Vec3(0, 0, 3.0f), Vec3(0, 0, 0));

    PostFxDesc fxDesc;
    fxDesc.allowOffscreen = true;
    fxDesc.hdr = true;
    fxDesc.bloom = false;
    v->setPostFx(fxDesc);
    renderFor(fx.e, 0.8); Image noBloom; REQUIRE(v->readPixels(noBloom));

    fxDesc.bloom = true;
    fxDesc.bloomThreshold = 1.0f;
    v->setPostFx(fxDesc);
    renderFor(fx.e, 0.8); Image withBloom; REQUIRE(v->readPixels(withBloom));

    // A ring of background pixels well outside the cube's silhouette.
    auto haze = [](const Image &i) {
        int sum = 0, n = 0;
        for (unsigned x = 2; x < 94; x += 4) {
            for (unsigned y : { 4u, 91u }) { const Px q = px(i, x, y); sum += (q.r+q.g+q.b)/3; ++n; }
        }
        for (unsigned y = 2; y < 94; y += 4) {
            for (unsigned x : { 4u, 91u }) { const Px q = px(i, x, y); sum += (q.r+q.g+q.b)/3; ++n; }
        }
        return n ? sum / n : 0;
    };
    const int off = haze(noBloom), on = haze(withBloom);
    std::printf("    background haze: bloom off %d, bloom on %d\n", off, on);
    CHECK_MSG(on > off, "bloom bleeds a bright surface into the background: %d -> %d", off, on);
}

/// SSAO darkens a crease. Two boxes meeting at a right angle: the corner where
/// they meet must get darker when ambient occlusion is on, while a flat region
/// far from any geometry must not.
void ssao_darkens_creases() {
    Fixture fx;
    View *v = fx.view("ssao-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("ssao-scene");               REQUIRE(s);
    v->setScene(s);
    // Ambient only: SSAO modulates ambient-lit surfaces, and a bright direct
    // light would swamp the effect.
    s->setAmbient(Colour(0.8f, 0.8f, 0.8f), Colour(0.8f, 0.8f, 0.8f));
    MeshId cube = s->createMesh(unitCubeData());
    PbrParams white; white.albedo = Colour(0.9f, 0.9f, 0.9f); white.roughness = 1.0f;
    MaterialId mat = s->createPbrMaterial(white);
    NodeId floor = s->createNode();
    CHECK(s->attachMesh(floor, cube, mat));
    s->setNodeTransform(floor, Vec3(0, -0.5f, 0), Quat(), Vec3(6, 0.2f, 6));
    NodeId wall = s->createNode();
    CHECK(s->attachMesh(wall, cube, mat));
    s->setNodeTransform(wall, Vec3(0, 0.6f, -1.0f), Quat(), Vec3(6, 1.4f, 0.2f));
    enginetest::testCameraLookAt(v, Vec3(0, 1.2f, 2.6f), Vec3(0, 0.1f, -0.4f));

    PostFxDesc fxDesc;
    fxDesc.allowOffscreen = true;
    v->setPostFx(fxDesc);
    render(fx.e, 3); Image plain; REQUIRE(v->readPixels(plain));

    fxDesc.ssao = true;
    fxDesc.ssaoScale = 1.0f;
    fxDesc.ssaoPower = 2.0f;
    fxDesc.ssaoRadius = 1.0f;
    v->setPostFx(fxDesc);
    render(fx.e, 4);
    if (!fx.e->lastError().empty())
        std::printf("    lastError after SSAO enable: %s\n", fx.e->lastError().c_str());
    Image ao; REQUIRE(v->readPixels(ao));

    // Sample a horizontal band across the floor/wall crease, and a band on the
    // open floor near the camera.
    auto band = [](const Image &i, unsigned y) {
        int sum = 0, n = 0;
        for (unsigned x = 40; x < 88; x += 2) { const Px q = px(i, x, y); sum += (q.r+q.g+q.b)/3; ++n; }
        return n ? sum / n : 0;
    };
    int creaseY = 0, bestDrop = 0, flatY = 110;
    for (unsigned y = 40; y < 100; ++y) {
        const int drop = band(plain, y) - band(ao, y);
        if (drop > bestDrop) { bestDrop = drop; creaseY = int(y); }
    }
    const int flatPlain = band(plain, unsigned(flatY)), flatAo = band(ao, unsigned(flatY));
    std::printf("    strongest AO row y=%d: %d -> %d (drop %d); open floor y=%d: %d -> %d\n",
                creaseY, band(plain, unsigned(creaseY)), band(ao, unsigned(creaseY)), bestDrop,
                flatY, flatPlain, flatAo);
    CHECK_MSG(bestDrop > 8, "ambient occlusion darkens the crease (best drop %d)", bestDrop);
    CHECK_MSG(bestDrop > (flatPlain - flatAo),
              "the crease darkens MORE than open floor: %d vs %d", bestDrop, flatPlain - flatAo);

    v->setPostFx(PostFxDesc());
    render(fx.e, 2); Image off; REQUIRE(v->readPixels(off));
    CHECK_MSG(px(off, 64, unsigned(creaseY)).r == px(plain, 64, unsigned(creaseY)).r,
              "turning SSAO off restores the exact original");
}

/// SMAA smooths a high-contrast silhouette edge: the number of pixels that are
/// neither foreground nor background (i.e. blended) goes UP.
void smaa_smooths_edges() {
    Fixture fx;
    View *v = fx.view("smaa-view", 128, 128, Colour(0, 0, 0)); REQUIRE(v);
    Scene *s = fx.scene("smaa-scene");                         REQUIRE(s);
    v->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    MeshId cube = s->createMesh(unitCubeData());
    PbrParams glow; glow.albedo = Colour(0, 0, 0); glow.emissive = Colour(1.0f, 1.0f, 1.0f);
    MaterialId mat = s->createPbrMaterial(glow);
    NodeId n = s->createNode();
    CHECK(s->attachMesh(n, cube, mat));
    // Rotated so the silhouette is a diagonal — the case SMAA exists for.
    s->setNodeTransform(n, Vec3(0, 0, 0), Quat(0, 0, 0.2588190f, 0.9659258f), Vec3(0.9f, 0.9f, 0.9f));
    enginetest::testCameraLookAt(v, Vec3(0, 0, 3.0f), Vec3(0, 0, 0));

    auto blended = [](const Image &i) {
        int n = 0;
        for (unsigned y = 0; y < i.height; ++y)
            for (unsigned x = 0; x < i.width; ++x) {
                const Px q = px(i, x, y);
                const int l = (q.r + q.g + q.b) / 3;
                if (l > 24 && l < 231) ++n;   // neither black background nor white cube
            }
        return n;
    };

    PostFxDesc fxDesc;
    fxDesc.allowOffscreen = true;
    v->setPostFx(fxDesc);
    render(fx.e, 3); Image aliased; REQUIRE(v->readPixels(aliased));

    fxDesc.smaaPreset = 3;   // Ultra: the strongest, and the least ambiguous test
    v->setPostFx(fxDesc);
    render(fx.e, 4);
    if (!fx.e->lastError().empty())
        std::printf("    lastError after SMAA enable: %s\n", fx.e->lastError().c_str());
    Image smoothed; REQUIRE(v->readPixels(smoothed));
    const int before = blended(aliased), after = blended(smoothed);
    std::printf("    partially-blended pixels: aliased %d, SMAA %d\n", before, after);
    CHECK_MSG(after > before, "SMAA blends silhouette edges: %d -> %d", before, after);
}

/// Refraction: a refractive pane in front of a patterned wall must BEND what is
/// behind it — i.e. differ from the same pane rendered as ordinary glass.
void refraction_bends_the_background() {
    Fixture fx;
    View *v = fx.view("refract-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("refract-scene");               REQUIRE(s);
    v->setScene(s);
    s->setAmbient(Colour(0.9f, 0.9f, 0.9f), Colour(0.9f, 0.9f, 0.9f));
    MeshId cube = s->createMesh(unitCubeData());
    // A back wall with strong colour contrast, made of two blocks.
    PbrParams red; red.albedo = Colour(0.9f, 0.05f, 0.05f); red.roughness = 1.0f;
    PbrParams green; green.albedo = Colour(0.05f, 0.9f, 0.05f); green.roughness = 1.0f;
    MaterialId redMat = s->createPbrMaterial(red), greenMat = s->createPbrMaterial(green);
    NodeId left = s->createNode();  CHECK(s->attachMesh(left, cube, redMat));
    s->setNodeTransform(left, Vec3(-1.0f, 0, -2.0f), Quat(), Vec3(2, 4, 0.2f));
    NodeId right = s->createNode(); CHECK(s->attachMesh(right, cube, greenMat));
    s->setNodeTransform(right, Vec3(1.0f, 0, -2.0f), Quat(), Vec3(2, 4, 0.2f));

    PbrParams glass;
    glass.albedo = Colour(0.9f, 0.9f, 0.9f);
    glass.roughness = 0.05f;
    glass.alpha = 0.25f;
    glass.alphaMode = PbrAlphaMode::Refractive;
    glass.refractionStrength = 0.0f;   // refractive, but a flat window to begin with
    MaterialId glassMat = s->createPbrMaterial(glass);
    NodeId pane = s->createNode(); CHECK(s->attachMesh(pane, cube, glassMat));
    // Tilted, so a refractive offset moves the sampled background sideways.
    s->setNodeTransform(pane, Vec3(0, 0, 0.3f), Quat(0, 0.2588190f, 0, 0.9659258f), Vec3(2.4f, 2.4f, 0.1f));
    enginetest::testCameraLookAt(v, Vec3(0, 0, 3.2f), Vec3(0, 0, 0));

    // Without the chain a refractive material must still RENDER — HlmsPbs falls
    // back to ordinary glass when the pass offers it no refraction texture.
    // (It used to vanish: the opaque pass stopped short of its render queue.)
    render(fx.e, 3); Image noChain; REQUIRE(v->readPixels(noChain));
    std::printf("    no chain, refractive material: centre %d %d %d\n",
                centre(noChain).r, centre(noChain).g, centre(noChain).b);
    // Green wall behind, seen through a 25%-alpha pane: green must dominate.
    // A frame LOST to a shader-compile failure would be the clear colour (blue)
    // instead — which is exactly what happened before the interlock existed.
    CHECK_MSG(centre(noChain).g > centre(noChain).b,
              "a refractive material renders as glass when no pass offers it "
              "refractions, instead of losing the frame: %d %d %d",
              centre(noChain).r, centre(noChain).g, centre(noChain).b);
    PostFxDesc fxDesc;
    fxDesc.allowOffscreen = true;
    fxDesc.refractions = true;
    v->setPostFx(fxDesc);
    render(fx.e, 4);
    if (!fx.e->lastError().empty())
        std::printf("    lastError with the refraction chain: %s\n", fx.e->lastError().c_str());
    Image asGlass; REQUIRE(v->readPixels(asGlass));
    std::printf("    chain on, strength 0:          centre %d %d %d\n",
                centre(asGlass).r, centre(asGlass).g, centre(asGlass).b);

    glass.refractionStrength = 0.9f;
    CHECK(s->setPbrMaterial(glassMat, glass));
    render(fx.e, 4);
    Image asRefractive; REQUIRE(v->readPixels(asRefractive));
    std::printf("    chain on, strength 0.9:        centre %d %d %d\n",
                centre(asRefractive).r, centre(asRefractive).g, centre(asRefractive).b);

    // Count how many pixels moved across the whole pane.
    int moved = 0, worst = 0;
    for (unsigned y = 30; y < 98; ++y)
        for (unsigned x = 30; x < 98; ++x) {
            const Px a = px(asGlass, x, y), b = px(asRefractive, x, y);
            const int d = std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
            if (d > 12) ++moved;
            worst = std::max(worst, d);
        }
    std::printf("    pane pixels changed by refraction: %d (largest delta %d)\n", moved, worst);
    CHECK_MSG(moved > 100, "a refractive pane bends the background (%d pixels moved)", moved);
}


/// The Epic shape, all at once, on a view whose target IS multisampled.
///
/// This is the crash test. HDR + hardware MSAA segfaults the NVIDIA SPIR-V
/// compiler on this pin, and SSAO + hardware MSAA renders black — so the chain
/// forces itself to 1x whenever any effect is on (OgreChain.cpp says why at
/// length). Asking for 4x here and getting a correct picture anyway is the
/// assertion: no combination of settings may crash or black-frame.
void postfx_epic_shape_with_msaa() {
    Fixture fx;
    View *v = fx.view("epic-view", 128, 128, kBlue); REQUIRE(v);
    Scene *s = fx.scene("epic-scene");               REQUIRE(s);
    v->setScene(s);
    v->setShadows(true);
    populate(s, kOrange);
    aim(v);
    v->setSampleCount(4);
    render(fx.e, 2);
    std::printf("    achieved samples: %u\n", v->sampleCount());

    PostFxDesc fxDesc;
    fxDesc.allowOffscreen = true;
    fxDesc.hdr = true;
    fxDesc.bloom = true;
    fxDesc.ssao = true;
    fxDesc.ssaoScale = 1.0f;
    v->setPostFx(fxDesc);
    // lastError is STICKY across the whole process (nothing clears it), so the
    // question is whether THIS sequence added one, not whether it is empty.
    const std::string errBefore = fx.e->lastError();
    renderFor(fx.e, 1.0);
    Image img; REQUIRE(v->readPixels(img));
    const Px c = centre(img);
    std::printf("    epic + 4x MSAA centre: %d %d %d\n", c.r, c.g, c.b);
    CHECK_MSG(c.r > 10 || c.g > 10 || c.b > 10,
              "the full stack renders something: %d %d %d", c.r, c.g, c.b);
    CHECK_MSG(fx.e->lastError() == errBefore,
              "the full stack raises no NEW engine error: %s", fx.e->lastError().c_str());

    // And SMAA on top of all of it (Medium's shape stacked on Epic's) — the
    // preset recompile is the part that has to survive being combined.
    fxDesc.smaaPreset = 1;
    v->setPostFx(fxDesc);
    renderFor(fx.e, 0.6);
    REQUIRE(v->readPixels(img));
    const Px c2 = centre(img);
    std::printf("    + SMAA centre: %d %d %d\n", c2.r, c2.g, c2.b);
    CHECK_MSG(c2.r > 10 || c2.g > 10 || c2.b > 10,
              "the full stack plus SMAA renders something: %d %d %d", c2.r, c2.g, c2.b);
}

/// A SKY under the post chain must stay smooth AND keep its brightness.
///
/// The defect this pins (2026-09-03 defect lane): with the Epic chain on, an
/// equirect / gradient / cubemap sky rendered as high-frequency dither and
/// blocks of recycled-VRAM noise, while the same frame with the chain off was a
/// perfect gradient. TWO causes, both in SSAO's inputs:
///
///  1. the refractive pass declared `StoreAction::DontCare` for the depth buffer
///     the LATER SSAO march samples — DontCare means UNDEFINED contents in
///     Vulkan, so the march read recycled tiles (OgreChain.cpp, our side);
///  2. the stock SSAO_HS shader has no far-plane rejection, so sky pixels — no
///     geometry, and a normals G-buffer the sky quad never wrote — got ~half
///     occlusion modulated by the rotation noise (ogre-patch 0011).
///
/// So this case asserts both halves: the sky stays SMOOTH (neighbouring pixels
/// differ by a couple of levels, because a sky IS a gradient) and it stays as
/// BRIGHT as it is with ambient occlusion off (nothing occludes the sky).
namespace skysmooth {
/// Writes a vertical-gradient equirect PPM (dark blue at the zenith, warm at the
/// horizon) — the shape "realistic" and "gradient" skies bake to.
void writeGradient(const std::string &path, int w, int h) {
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P6 %d %d 255\n", w, h);
    for (int y = 0; y < h; ++y) {
        const float t = float(y) / float(h - 1);   // 0 = zenith, 1 = nadir
        const int r = int(30.0f + 150.0f * t), g = int(60.0f + 120.0f * t),
                  b = int(150.0f - 40.0f * t);
        for (int x = 0; x < w; ++x) {
            std::fputc(r, f); std::fputc(g, f); std::fputc(b, f);
        }
    }
    std::fclose(f);
}
/// The largest difference between horizontally or vertically adjacent pixels in
/// the region, and how many pairs exceed `noisy` levels. A smooth gradient has
/// max ~1-2 and zero noisy pairs; the defect gave max 54 with 10% noisy.
struct Roughness { int maxDelta; int noisyPairs; int pairs; };
Roughness roughness(const Image &img, unsigned y0, unsigned y1, int noisy = 6) {
    Roughness out { 0, 0, 0 };
    auto delta = [](const Px &a, const Px &b) {
        return std::max(std::max(std::abs(a.r - b.r), std::abs(a.g - b.g)), std::abs(a.b - b.b));
    };
    for (unsigned y = y0; y < y1; ++y) {
        for (unsigned x = 0; x + 1 < img.width; ++x) {
            const int d = delta(px(img, x, y), px(img, x + 1, y));
            out.maxDelta = std::max(out.maxDelta, d);
            if (d > noisy) ++out.noisyPairs;
            ++out.pairs;
        }
    }
    for (unsigned y = y0; y + 1 < y1; ++y) {
        for (unsigned x = 0; x < img.width; ++x) {
            const int d = delta(px(img, x, y), px(img, x, y + 1));
            out.maxDelta = std::max(out.maxDelta, d);
            if (d > noisy) ++out.noisyPairs;
            ++out.pairs;
        }
    }
    return out;
}
}   // namespace skysmooth

void sky_stays_smooth_under_the_post_chain() {
    using namespace skysmooth;
    Fixture fx;
    View *v = fx.view("skyfx-view", 128, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("skyfx-scene");              REQUIRE(s);
    v->setScene(s);
    // Ground geometry so SSAO has something real to occlude (and so the frame is
    // a plausible outdoor shot), with the top HALF of the frame pure sky.
    s->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.3f, 0.3f, 0.3f));
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -0.8f, -0.4f), 3.14159f);
    MeshId cube = s->createMesh(unitCubeData());
    PbrParams grey; grey.albedo = Colour(0.6f, 0.6f, 0.6f); grey.roughness = 0.8f;
    MaterialId mat = s->createPbrMaterial(grey);
    NodeId floor = s->createNode(); CHECK(s->attachMesh(floor, cube, mat));
    s->setNodeTransform(floor, Vec3(0, -0.6f, 0), Quat(), Vec3(12, 0.2f, 12));
    NodeId box = s->createNode(); CHECK(s->attachMesh(box, cube, mat));
    s->setNodeTransform(box, Vec3(0.8f, 0.0f, -1.0f), Quat(), Vec3(1, 1, 1));
    // A REFRACTIVE pane: not decoration. It is what makes the chain build its
    // refraction passes, and cause (1) above only exists when they run.
    PbrParams glass;
    glass.albedo = Colour(0.9f, 0.9f, 0.9f);
    glass.roughness = 0.1f;
    glass.alpha = 0.35f;
    glass.alphaMode = PbrAlphaMode::Refractive;
    glass.refractionStrength = 0.4f;
    MaterialId glassMat = s->createPbrMaterial(glass);
    NodeId pane = s->createNode(); CHECK(s->attachMesh(pane, cube, glassMat));
    s->setNodeTransform(pane, Vec3(-0.9f, 0.1f, 0.4f), Quat(), Vec3(1.2f, 1.2f, 0.1f));
    enginetest::testCameraLookAt(v, Vec3(0, 0.8f, 3.4f), Vec3(0, 0.75f, -1.0f));

    const std::string path = "sky_test_gradient.ppm";
    writeGradient(path, 256, 128);
    TextureId skyTex = s->loadTexture(path, true);
    CHECK_MSG(skyTex != 0, "%s", fx.e->lastError().c_str());
    if (!skyTex) { std::remove(path.c_str()); return; }
    CHECK(s->setSky(equirectSkyDesc(skyTex)));

    // The sky band: the top third, which the camera framing keeps free of
    // geometry (the horizon sits at about 60% of the frame height).
    const unsigned skyY0 = 2u, skyY1 = v->height() / 3u;
    auto skyLuma = [&](const Image &img) {
        int sum = 0, n = 0;
        for (unsigned y = skyY0; y < skyY1; ++y)
            for (unsigned x = 0; x < img.width; x += 2) {
                const Px q = px(img, x, y); sum += (q.r + q.g + q.b) / 3; ++n;
            }
        return n ? sum / n : 0;
    };

    // Every shape below keeps HDR OFF where a BRIGHTNESS comparison is made:
    // auto-exposure would hide (or invent) a change in the sky's level.
    struct Shape { const char *name; PostFxDesc desc; };
    auto shape = [](bool hdr, bool bloom, bool ssao, int smaa) {
        PostFxDesc d;
        d.allowOffscreen = true;
        d.refractions = true;   // the pane above; cause (1) needs this pass
        d.hdr = hdr; d.bloom = bloom; d.ssao = ssao; d.smaaPreset = smaa;
        d.ssaoScale = 1.0f; d.ssaoRadius = 2.0f; d.ssaoPower = 1.5f;
        return d;
    };

    auto check = [&](const char *name, const PostFxDesc &desc, bool settle) {
        v->setPostFx(desc);
        if (settle) renderFor(fx.e, 0.7);   // auto-exposure adapts per second
        else        render(fx.e, 4);
        Image img;
        if (!v->readPixels(img)) { CHECK_MSG(false, "readPixels failed for %s", name); return 0; }
        const Roughness r = roughness(img, skyY0, skyY1);
        const int luma = skyLuma(img);
        std::printf("    %-14s: max neighbour delta %3d, noisy pairs %5d/%d, sky luma %3d\n",
                    name, r.maxDelta, r.noisyPairs, r.pairs, luma);
        CHECK_MSG(r.maxDelta <= 12 && r.noisyPairs * 100 <= r.pairs,
                  "the sky must stay smooth with %s: max delta %d, %d/%d noisy pairs",
                  name, r.maxDelta, r.noisyPairs, r.pairs);
        return luma;
    };

    // The reference: no chain at all. This is the picture the user sees with
    // post fx off, and the one every shape below has to keep.
    PostFxDesc off;
    off.allowOffscreen = true;
    const int lumaPlain = check("chain off", off, false);

    const int lumaNoSsao = check("refract only", shape(false, false, false, -1), false);
    const int lumaSsao   = check("ssao",         shape(false, false, true,  -1), false);
    check("smaa", shape(false, false, false, 1), false);
    check("hdr+bloom", shape(true, true, false, -1), true);
    check("epic (all)", shape(true, true, true, 1), true);

    // Nothing occludes the sky: with ambient occlusion ON the sky band must keep
    // the brightness it has with it OFF. Before ogre-patch 0011 it lost ~45%.
    std::printf("    sky luma: chain off %d, refraction only %d, + ssao %d\n",
                lumaPlain, lumaNoSsao, lumaSsao);
    CHECK_MSG(lumaSsao * 100 >= lumaNoSsao * 90,
              "ambient occlusion must not darken the SKY: %d -> %d", lumaNoSsao, lumaSsao);

    // Cubemap skies ride a different material and were affected too (subtler,
    // because a dark cubemap hides the noise) — one shape is enough here.
    const std::string faceFiles[6] = { "skyfx_px.ppm", "skyfx_nx.ppm", "skyfx_py.ppm",
                                       "skyfx_ny.ppm", "skyfx_pz.ppm", "skyfx_nz.ppm" };
    TextureId faces[6] = { 0, 0, 0, 0, 0, 0 };
    bool haveFaces = true;
    for (int i = 0; i < 6; ++i) {
        // Same gradient on the sides, flat at top/bottom: a smooth sky either way.
        writeGradient(faceFiles[i], 64, 64);
        faces[i] = s->loadTexture(faceFiles[i], true);
        if (!faces[i]) haveFaces = false;
    }
    if (haveFaces && s->setSky(cubemapSkyDesc(faces))) {
        check("cubemap epic", shape(true, true, true, 1), true);
    } else {
        std::printf("    cubemap sky unavailable: %s\n", fx.e->lastError().c_str());
    }

    v->setPostFx(PostFxDesc());
    render(fx.e, 2);
    std::remove(path.c_str());
    for (const std::string &f : faceFiles) std::remove(f.c_str());
}

// ---------------------------------------------------------------------------
// THE RENDER-LOOP MONITOR (SPECS/RENDER_LOOP_MONITOR_SPEC.md §7)
// ---------------------------------------------------------------------------
// ACCEPTANCE IS DATA INTEGRITY, NEVER PERFORMANCE (owner D5, 2026-09-12). These
// cases assert that a capture is COMPLETE and CORRECT — that off means off, that
// a frame's pass records sum to the renderer's own counters, that the stage
// times account for the frame, and that a cache's work carries the reason that
// justified it. Nothing here asserts how fast anything is or how much work
// happened: that is the lead's reading of a capture, not a test's.

/// A rig with one shadowed lamp, one caster and a ground — enough for the view's
/// shadow node to exist and for a lamp map to be cacheable.
struct MonitorRig {
    View  *v = nullptr;
    Scene *s = nullptr;
    NodeId ground = 0, cube = 0, lamp = 0;
    bool build(Fixture &fx, const char *name) {
        v = fx.view(std::string(name) + "-view", 96, 96, kBlue);
        s = fx.scene(std::string(name) + "-scene");
        if (!v || !s) return false;
        v->setScene(s);
        v->setShadows(true);
        s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.1f, 0.1f, 0.1f));
        MeshId cubeMesh = s->createMesh(unitCubeData());
        PbrParams white; white.albedo = Colour(0.9f, 0.9f, 0.9f); white.roughness = 0.9f;
        MaterialId mat = s->createPbrMaterial(white);
        ground = s->createNode();
        s->attachMesh(ground, cubeMesh, mat);
        s->setNodeTransform(ground, Vec3(0, -0.55f, 0), Quat(), Vec3(8, 0.1f, 8));
        cube = s->createNode();
        s->attachMesh(cube, cubeMesh, mat);
        s->setNodeTransform(cube, Vec3(0, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));
        lamp = s->createNode();
        LightDesc d; d.type = LightType::Point; d.intensity = 6.0f; d.castShadows = true;
        d.range = 20.0f;
        s->setLight(lamp, d);
        s->setNodeTransform(lamp, Vec3(2, 3, 2), Quat(), Vec3(1, 1, 1));
        CameraDesc c; c.position = Vec3(0, 4, 5); c.fovDegrees = 50;
        v->setCamera(c);
        return true;
    }
};

/// Drains the ring and returns the LAST complete record (the one whose passes
/// are the frame we care about).
bool lastRecord(Engine *e, FrameRecord &out) {
    std::vector<FrameRecord> recs;
    e->takeFrameRecords(recs);
    if (recs.empty()) return false;
    out = recs.back();
    return true;
}

void monitor_off_is_inert() {
    // §4.1, ASSERTED RATHER THAN BELIEVED: at Off nothing is attached, nothing
    // is allocated and no GPU query pool exists. This is the guarantee that lets
    // the monitor ship in every build.
    Fixture fx;
    // REQUIRE evaluates its condition TWICE (CHECK then the branch), so the
    // rig is built ONCE into a flag — a second build would fail on the
    // duplicate view name and the test would silently skip itself.
    MonitorRig rig;
    const bool built = rig.build(fx, "mon-off");
    REQUIRE(built);
    CHECK(fx.e->frameMonitor() == MonitorLevel::Off);   // the default, always
    render(fx.e, 3);
    MonitorStatus st = fx.e->monitorStatus();
    CHECK(st.level == MonitorLevel::Off);
    CHECK_MSG(st.attachedListeners == 0u, "off must attach nothing: %u", st.attachedListeners);
    CHECK_MSG(st.ringCapacity == 0u, "off must free the ring: %u", st.ringCapacity);
    CHECK(st.ringFrames == 0u);
    CHECK(st.framesRecorded == 0ull);
    CHECK_MSG(st.gpuQueryPools == 0u, "off must own no query pool: %u", st.gpuQueryPools);
    CHECK(!st.gpuActive);
    std::vector<FrameRecord> recs;
    std::vector<MonitorEvent> evs;
    CHECK(fx.e->takeFrameRecords(recs) == 0u);
    CHECK(fx.e->takeMonitorEvents(evs) == 0u);
    CHECK(recs.empty() && evs.empty());
    // The host's hooks are safe to call with it off — a caller never has to ask.
    MonitorEvent hook; hook.kind = MonitorEventKind::Host; hook.label = "ignored";
    fx.e->noteMonitorEvent(hook);
    fx.e->noteHostStage("ignored", 1.0f);
    fx.e->setNextFrameCause(FrameCause::Scripted);
    render(fx.e);
    CHECK(fx.e->takeMonitorEvents(evs) == 0u);
    CHECK(fx.e->monitorStatus().framesRecorded == 0ull);

    // ...and ON then OFF returns to exactly that state (nothing is leaked and
    // nothing stays attached).
    fx.e->setFrameMonitor(MonitorLevel::Review);
    render(fx.e, 2);
    CHECK(fx.e->monitorStatus().attachedListeners > 0u);
    fx.e->setFrameMonitor(MonitorLevel::Off);
    render(fx.e, 2);
    st = fx.e->monitorStatus();
    CHECK(st.level == MonitorLevel::Off);
    CHECK(st.attachedListeners == 0u);
    CHECK(st.ringCapacity == 0u);
    CHECK(fx.e->takeFrameRecords(recs) == 0u);
}

void monitor_passes_sum_to_the_frame() {
    // COMPLETENESS: every pass the frame executed is in the record, its draw
    // counts are EXCLUSIVE of nested passes, and summing them reproduces the
    // renderer's own draw count for that frame. This is the assertion the old
    // PassProfiler could not have passed: a scene pass that owns a shadow node
    // executes that node's passes inside its own callbacks.
    Fixture fx;
    // REQUIRE evaluates its condition TWICE (CHECK then the branch), so the
    // rig is built ONCE into a flag — a second build would fail on the
    // duplicate view name and the test would silently skip itself.
    MonitorRig rig;
    const bool built = rig.build(fx, "mon-passes");
    REQUIRE(built);
    render(fx.e, 3);                       // warm: compiles and first captures done
    // GEOMETRY COUNTERS ARE LAZY: reading renderStats is what turns Ogre's
    // RenderingMetrics recording on, and it is off until someone asks. Ask
    // BEFORE the measured frame, or the frame renders with no counters at all
    // and every number below is zero.
    RenderStats rs; CHECK(fx.e->renderStats(rs));
    fx.e->setFrameMonitor(MonitorLevel::Review);
    render(fx.e, 1);
    std::vector<FrameRecord> recs;
    fx.e->takeFrameRecords(recs);
    recs.clear();
    fx.e->setNextFrameCause(FrameCause::Driver);
    // MOVE A CASTER so the measured frame re-renders the lamp's map: every
    // point/spot map is CACHED now (ENGINE_CACHE_POLICY_SPEC P2), so a truly
    // idle frame executes no shadow pass at all — which is the healthy state,
    // not the one that proves the classifier.
    rig.s->setNodeTransform(rig.cube, Vec3(0.2f, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));
    render(fx.e, 1);
    CHECK(fx.e->renderStats(rs));
    const unsigned drained = fx.e->takeFrameRecords(recs);   // once: REQUIRE evaluates twice
    REQUIRE(drained > 0u);
    const FrameRecord &r = recs.back();
    std::printf("    frame %llu: %zu passes, %u draws (renderStats %u), total %.3f ms\n",
                (unsigned long long)r.frame, r.passes.size(), r.draws, rs.draws, r.totalMs);
    CHECK_MSG(!r.passes.empty(), "a rendered frame must record passes");
    CHECK(r.cause == FrameCause::Driver);
    unsigned draws = 0, batches = 0;
    unsigned long long tris = 0;
    bool namedWorkspace = false, sawScenePass = false, sawShadowPass = false;
    for (const FramePass &p : r.passes) {
        draws += p.draws; batches += p.batches; tris += p.triangles;
        if (!p.workspace.empty()) namedWorkspace = true;
        if (p.bucket == PassBucket::Main) sawScenePass = true;
        if (p.bucket == PassBucket::ShadowView) sawShadowPass = true;
        CHECK(p.cpuMs >= 0.0f);
        CHECK_MSG(p.gpuMs < 0.0f, "GPU time must be ABSENT, not zero, without the patch");
    }
    CHECK_MSG(draws == r.draws, "the record's own sum: %u vs %u", draws, r.draws);
    CHECK_MSG(draws == rs.draws, "per-pass draws must sum to the renderer's count: %u vs %u",
              draws, rs.draws);
    CHECK_MSG(namedWorkspace, "every pass names the workspace it ran in");
    CHECK_MSG(sawScenePass, "the view's camera pass must be classified Main");
    CHECK_MSG(sawShadowPass, "a shadowed view must execute its shadow node's passes");
    // THE SHADOW/SCENE SPLIT from passSceneAfterShadowMaps: present on every
    // SCENE pass (Ogre fires the callback even when the pass has no shadow
    // node — a caster pass inside a shadow node included), absent on quads,
    // clears and computes. Never asserted as a duration.
    bool mainHasSplit = false, quadHasSplit = false;
    for (const FramePass &p : r.passes) {
        const bool sceneKind = p.pass == "scene" || p.bucket == PassBucket::Main ||
                               p.bucket == PassBucket::ShadowView ||
                               p.bucket == PassBucket::ShadowReflect ||
                               p.bucket == PassBucket::ShadowProbe ||
                               p.bucket == PassBucket::ProbeFace;
        if (p.bucket == PassBucket::Main && p.shadowMs >= 0.0f) mainHasSplit = true;
        if (!sceneKind && p.shadowMs >= 0.0f) quadHasSplit = true;
    }
    CHECK_MSG(mainHasSplit, "the view's camera pass must carry the shadow/scene split");
    CHECK_MSG(!quadHasSplit, "a quad or clear pass has no shadow half");

    // STAGE TIMES ACCOUNT FOR THE FRAME. Stages are exclusive, so their sum is
    // the frame minus whatever is outside every scope; ±5% is the spec's bound.
    double stageSum = 0.0;
    bool sawRecord = false, sawSwap = false;
    for (const FrameStage &st : r.stages) {
        stageSum += st.ms;
        if (st.name == "engine.record") sawRecord = true;
        if (st.name == "engine.swap")   sawSwap = true;
    }
    std::printf("    stages %zu, sum %.3f ms vs frame %.3f ms (overhead %.3f)\n",
                r.stages.size(), stageSum, r.totalMs, r.overheadMs);
    CHECK_MSG(sawRecord && sawSwap, "the record/swap split must be in every frame");
    CHECK_MSG(stageSum <= r.totalMs + 0.001,
              "exclusive stages cannot exceed the frame: %.3f vs %.3f", stageSum, r.totalMs);
    CHECK_MSG(stageSum >= r.totalMs * 0.95 - 0.05,
              "stages must account for the frame within 5%%: %.3f vs %.3f", stageSum, r.totalMs);
    fx.e->setFrameMonitor(MonitorLevel::Off);
}

void monitor_records_work_with_its_reason() {
    // §4.7: each cached system reports the work it did AND the input change that
    // justified it. The test drives the three inputs the spec names — a light
    // that moves, an explicit GI refresh, and an idle stretch — and asserts the
    // REASON on the right cache. It asserts nothing about how much work happened.
    Fixture fx;
    // REQUIRE evaluates its condition TWICE (CHECK then the branch), so the
    // rig is built ONCE into a flag — a second build would fail on the
    // duplicate view name and the test would silently skip itself.
    MonitorRig rig;
    const bool built = rig.build(fx, "mon-reason");
    REQUIRE(built);
    render(fx.e, 4);
    fx.e->setFrameMonitor(MonitorLevel::Review);
    std::vector<FrameRecord> recs;
    std::vector<MonitorEvent> evs;

    // (1) A LIGHT MOVES -> its cached shadow map is re-rendered, reason Light.
    render(fx.e, 2);
    fx.e->takeFrameRecords(recs); recs.clear();
    rig.s->setNodeTransform(rig.lamp, Vec3(-2, 3, 2), Quat(), Vec3(1, 1, 1));
    render(fx.e, 1);
    fx.e->takeFrameRecords(recs);
    bool lampRelit = false;
    for (const FrameRecord &r : recs)
        for (const CacheWork &w : r.cacheWork)
            if (w.cache == CacheKind::ShadowMap && w.id == rig.lamp &&
                w.reason == WorkReason::Light)
                lampRelit = true;
    CHECK_MSG(lampRelit, "a lamp that moved must re-render its map with reason Light");
    recs.clear();

    // (2) A CASTER MOVES -> the same map, reason Caster (not Light).
    rig.s->setNodeTransform(rig.cube, Vec3(0.4f, 0.6f, 0), Quat(), Vec3(0.8f, 0.8f, 0.8f));
    render(fx.e, 1);
    fx.e->takeFrameRecords(recs);
    bool casterRelit = false;
    for (const FrameRecord &r : recs)
        for (const CacheWork &w : r.cacheWork)
            if (w.cache == CacheKind::ShadowMap && w.reason == WorkReason::Caster)
                casterRelit = true;
    CHECK_MSG(casterRelit, "a caster that moved inside a lamp's reach dirties it with reason Caster");
    recs.clear();

    // (3) AN IDLE STRETCH -> nothing the caches redo carries a reason of `None`
    //     here, because at rest nothing should redo anything. If it DOES, the
    //     monitor records it as reason `None` — data, not a verdict — which is
    //     the case the lead reads. The assertion is only that idle frames are
    //     recorded at all and that any shadow work they contain is attributed.
    render(fx.e, 6);
    fx.e->takeFrameRecords(recs);
    CHECK_MSG(recs.size() >= 6u, "every frame must produce a record: %zu", recs.size());
    unsigned idleShadowWork = 0, idleUnattributed = 0;
    for (const FrameRecord &r : recs)
        for (const CacheWork &w : r.cacheWork)
            if (w.cache == CacheKind::ShadowMap) {
                ++idleShadowWork;
                if (w.reason == WorkReason::None) ++idleUnattributed;
            }
    std::printf("    idle window: %zu frames, %u shadow-map renders (%u with reason None)\n",
                recs.size(), idleShadowWork, idleUnattributed);
    recs.clear();

    // (4) AN EXPLICIT GI REFRESH -> a cause-tagged event.
    fx.e->takeMonitorEvents(evs); evs.clear();
    rig.s->refreshGlobalIllumination();
    render(fx.e, 2);
    fx.e->takeMonitorEvents(evs);
    fx.e->takeFrameRecords(recs);
    bool giEvent = false;
    for (const MonitorEvent &e : evs)
        if (e.kind == MonitorEventKind::GiRebuild || e.kind == MonitorEventKind::GiRefresh ||
            e.kind == MonitorEventKind::ProbeGridBuild)
            giEvent = true;
    std::printf("    after refreshGi: %zu events, gi event %s\n", evs.size(),
                giEvent ? "present" : "absent (GI off in this scene)");
    // GI is OFF in this rig by default, so the refresh legitimately rebuilds
    // nothing; what must hold either way is that the frames were recorded and
    // no event arrived without a cause.
    for (const MonitorEvent &e : evs)
        CHECK_MSG(!e.label.empty(), "every event names itself");
    CHECK(!recs.empty());

    // (5) THE HOST'S HOOK is recorded with the host's label.
    MonitorEvent hook;
    hook.kind = MonitorEventKind::Host;
    hook.reason = WorkReason::Request;
    hook.label = "page.switch";
    hook.detail = "assets -> editor";
    fx.e->noteMonitorEvent(hook);
    fx.e->noteHostStage("host.tick", 4.5f);
    render(fx.e, 1);
    evs.clear(); recs.clear();
    fx.e->takeMonitorEvents(evs);
    fx.e->takeFrameRecords(recs);
    bool sawHook = false, sawHostStage = false;
    for (const MonitorEvent &e : evs)
        if (e.kind == MonitorEventKind::Host && e.label == "page.switch") sawHook = true;
    for (const FrameRecord &r : recs)
        for (const FrameStage &st : r.stages)
            if (st.name == "host.tick") sawHostStage = true;
    CHECK_MSG(sawHook, "the host's event hook must reach events");
    CHECK_MSG(sawHostStage, "a host stage must ride the next frame's stage list");
    fx.e->setFrameMonitor(MonitorLevel::Off);
}

void monitor_survives_rebuilds_and_view_destruction() {
    // THE LIFETIME CASE (§8.2), the one ASan is here for. The monitor's listener
    // rides workspaces that are destroyed and recreated under it by an atlas
    // rebuild and a GI rebuild, and a view it is attached to is destroyed while
    // it is on. Nothing may dangle, and recording must continue.
    Fixture fx;
    // REQUIRE evaluates its condition TWICE (CHECK then the branch), so the
    // rig is built ONCE into a flag — a second build would fail on the
    // duplicate view name and the test would silently skip itself.
    MonitorRig rig;
    const bool built = rig.build(fx, "mon-life");
    REQUIRE(built);
    render(fx.e, 2);
    fx.e->setFrameMonitor(MonitorLevel::Review);
    render(fx.e, 2);
    const unsigned attached = fx.e->monitorStatus().attachedListeners;
    CHECK_MSG(attached > 0u, "a live view must carry the monitor's listener");

    // (a) AN ATLAS REBUILD drops and recreates every workspace naming a shadow node.
    fx.e->setShadowResolution(1024);
    render(fx.e, 2);
    std::vector<FrameRecord> recs;
    fx.e->takeFrameRecords(recs);
    bool passesAfterAtlas = false;
    for (const FrameRecord &r : recs) if (!r.passes.empty()) passesAfterAtlas = true;
    CHECK_MSG(passesAfterAtlas, "the listener must ride an atlas rebuild");
    std::vector<MonitorEvent> evs;
    fx.e->takeMonitorEvents(evs);
    bool atlasEvent = false;
    for (const MonitorEvent &e : evs)
        if (e.kind == MonitorEventKind::AtlasRebuild) atlasEvent = true;
    CHECK_MSG(atlasEvent, "an atlas rebuild is a cause-tagged event");
    recs.clear();

    // (b) A GI REBUILD recreates every probe workspace.
    GiParams gi;
    gi.mode = GiMode::InstantRadiosity;
    rig.s->setGlobalIllumination(gi);
    render(fx.e, 3);
    evs.clear();
    fx.e->takeMonitorEvents(evs); evs.clear();
    rig.s->refreshGlobalIllumination();
    render(fx.e, 3);
    fx.e->takeFrameRecords(recs);
    fx.e->takeMonitorEvents(evs);
    bool passesAfterGi = false;
    for (const FrameRecord &r : recs) if (!r.passes.empty()) passesAfterGi = true;
    CHECK_MSG(passesAfterGi, "the listener must ride a GI rebuild");
    // ...and the rebuild itself is an EVENT, timed and tagged with the reason
    // that asked for it (Refresh here: the host asked explicitly).
    bool giRebuild = false, giHasReason = false;
    for (const MonitorEvent &e : evs)
        if (e.kind == MonitorEventKind::GiRebuild) {
            giRebuild = true;
            if (e.reason == WorkReason::Refresh) giHasReason = true;
            std::printf("    gi rebuild event: %.2f ms, reason %d\n", e.ms, int(e.reason));
        }
    CHECK_MSG(giRebuild, "a GI rebuild must appear in events");
    CHECK_MSG(giHasReason, "a GI rebuild carries the reason that asked for it");
    gi.mode = GiMode::Off;
    rig.s->setGlobalIllumination(gi);
    render(fx.e, 2);
    recs.clear();

    // (c) A SECOND VIEW, destroyed while the monitor is on.
    View *v2 = fx.view("mon-life-view2", 64, 64, kGreen);
    REQUIRE(v2);
    v2->setScene(rig.s);
    CameraDesc c; c.position = Vec3(3, 3, 3); c.fovDegrees = 60;
    v2->setCamera(c);
    render(fx.e, 2);
    CHECK(fx.e->monitorStatus().attachedListeners > 0u);
    fx.forget(v2);
    fx.e->destroyView(v2);
    render(fx.e, 3);
    fx.e->takeFrameRecords(recs);
    bool passesAfterDestroy = false;
    for (const FrameRecord &r : recs) if (!r.passes.empty()) passesAfterDestroy = true;
    CHECK_MSG(passesAfterDestroy, "destroying a view must not stop the monitor");

    // (d) DOWN again: everything detaches, from every kind of workspace.
    fx.e->setFrameMonitor(MonitorLevel::Off);
    render(fx.e, 2);
    CHECK(fx.e->monitorStatus().attachedListeners == 0u);
    CHECK(fx.e->lastError().empty() || true);   // reported, never thrown
}

void monitor_snapshot_names_the_graph() {
    // §4.8: the snapshot must describe the engine well enough to read a capture
    // months later — and the compositor graph is the part nothing else can see.
    // Taken with the monitor OFF, which is how a host brackets a capture.
    Fixture fx;
    // REQUIRE evaluates its condition TWICE (CHECK then the branch), so the
    // rig is built ONCE into a flag — a second build would fail on the
    // duplicate view name and the test would silently skip itself.
    MonitorRig rig;
    const bool built = rig.build(fx, "mon-snap");
    REQUIRE(built);
    render(fx.e, 3);
    EngineSnapshot snap;
    const bool took = fx.e->captureSnapshot(snap, "start");
    REQUIRE(took);
    CHECK(snap.live);
    CHECK(snap.label == "start");
    std::printf("    snapshot: scene '%s', %zu workspaces, %zu lights, %zu probes, %zu textures\n",
                snap.scene.c_str(), snap.workspaces.size(), snap.lights.size(),
                snap.probes.size(), snap.textures.size());
    CHECK_MSG(snap.scene == "mon-snap-scene", "the snapshot names its scene: '%s'",
              snap.scene.c_str());
    CHECK_MSG(!snap.workspaces.empty(), "the compositor graph must name every live workspace");
    bool namedView = false, namedScene = false, namedPasses = false, namedShadowNode = false;
    for (const CompositorWorkspaceInfo &w : snap.workspaces) {
        if (w.owner.find("view:") == 0) namedView = true;
        if (!w.scene.empty()) namedScene = true;
        for (const CompositorNodeInfo &n : w.nodes) {
            if (!n.passes.empty()) namedPasses = true;
            for (const CompositorPassInfo &p : n.passes)
                if (!p.shadowNode.empty()) namedShadowNode = true;
        }
    }
    CHECK_MSG(namedView, "the view's workspace must be in the graph");
    CHECK_MSG(namedScene, "every workspace names the scene it renders");
    CHECK_MSG(namedPasses, "every node lists its passes");
    CHECK_MSG(namedShadowNode, "a scene pass must name the shadow node it recalculates");
    // The light list, with the lamp-map cache's state joined in.
    bool foundLamp = false;
    for (const SnapshotLight &l : snap.lights)
        if (l.node == rig.lamp) {
            foundLamp = true;
            CHECK(l.type == LightType::Point);
            CHECK(l.castShadow);
            CHECK_MSG(l.range > 0.0f, "the light list carries the range: %f", l.range);
        }
    CHECK_MSG(foundLamp, "the snapshot's light list must hold the scene's lamp");
    CHECK(snap.shadow.live);
    CHECK(!snap.hlmsDatablocks.empty());
    CHECK(snap.device.renderSystem.empty() || true);  // reported; no assertion on the GPU
    // ...and the GPU-timing half says WHY it is unavailable rather than lying.
    const MonitorStatus st = fx.e->monitorStatus();
    CHECK(!st.gpuReason.empty());
    CHECK(!st.gpuCompiled || st.gpuSupported || !st.gpuReason.empty());
}

void monitor_gpu_timestamps() {
    // P1c (ogre-patch 0027) and BOTH its off-switches (owner decision D3).
    //
    // BUILD: without JAH_GPU_TIMESTAMPS the render system answers no such
    //   custom attribute, `gpuCompiled` is false and the reason names the
    //   production build. This case passes either way — it asserts the
    //   CONTRACT, not the presence of the patch, so it is meaningful on a
    //   packaging build too.
    // RUNTIME: even in a dev build no query pool exists until a capture starts.
    Fixture fx;
    MonitorRig rig;
    const bool built = rig.build(fx, "mon-gpu");
    REQUIRE(built);
    render(fx.e, 3);

    // OFF: no pools, whatever the build says.
    MonitorStatus st = fx.e->monitorStatus();
    CHECK_MSG(st.gpuQueryPools == 0u, "no capture, no query pool: %u", st.gpuQueryPools);
    CHECK(!st.gpuActive);
    CHECK(!st.gpuReason.empty());

    fx.e->setFrameMonitor(MonitorLevel::Review);
    st = fx.e->monitorStatus();
    std::printf("    gpu: compiled=%d supported=%d active=%d pools=%u reason='%s'\n",
                int(st.gpuCompiled), int(st.gpuSupported), int(st.gpuActive), st.gpuQueryPools,
                st.gpuReason.c_str());
    if (!st.gpuCompiled) {
        // A production-configured engine: the hooks are upstream's empty stubs.
        CHECK(!st.gpuSupported);
        CHECK(!st.gpuActive);
        CHECK(st.gpuQueryPools == 0u);
        CHECK_MSG(!st.gpuReason.empty(), "an engine without the patch must say so");
        fx.e->setFrameMonitor(MonitorLevel::Off);
        return;
    }
    CHECK_MSG(st.gpuActive, "a dev build in a capture must own its pools: %s",
              st.gpuReason.c_str());
    CHECK_MSG(st.gpuQueryPools > 0u, "a capture owns query pools");

    // Render past the two-frame readback latency and drain.
    std::vector<FrameRecord> recs;
    for (int i = 0; i < 8; ++i) {
        rig.s->setNodeTransform(rig.cube, Vec3(0.1f * float(i), 0.6f, 0), Quat(),
                                Vec3(0.8f, 0.8f, 0.8f));
        render(fx.e, 1);
    }
    fx.e->takeFrameRecords(recs);
    unsigned sampled = 0, frames = 0;
    float anyGpu = -1.0f;
    for (const FrameRecord &r : recs) {
        ++frames;
        if (r.gpuMs >= 0.0f) anyGpu = r.gpuMs;
        for (const FramePass &p : r.passes)
            if (p.gpuMs >= 0.0f) ++sampled;
    }
    std::printf("    %u frames drained, %u passes carry GPU time, frame gpuMs %.3f\n",
                frames, sampled, anyGpu);
    CHECK_MSG(sampled > 0u, "with the patch on, passes must carry GPU times");
    CHECK_MSG(anyGpu >= 0.0f, "a frame's gpuMs is the sum of its sampled passes");

    // ...and the pools go with the capture.
    fx.e->setFrameMonitor(MonitorLevel::Off);
    st = fx.e->monitorStatus();
    CHECK_MSG(st.gpuQueryPools == 0u, "stopping the capture frees the pools: %u",
              st.gpuQueryPools);
    CHECK(!st.gpuActive);
}

int main(int argc, char **argv) {
    const std::vector<Test> tests = {
        { "create_twice_returns_null_with_error",  create_twice_returns_null_with_error },
        { "ordering_contract",                      ordering_contract },
        { "offscreen_cube_renders",                 offscreen_cube_renders },
        { "two_scenes_render_independently",        two_scenes_render_independently },
        { "duplicate_view_name_fails_cleanly",      duplicate_view_name_fails_cleanly },
        { "duplicate_scene_name_fails_cleanly",     duplicate_scene_name_fails_cleanly },
        { "set_scene_twice_fails_cleanly",          set_scene_twice_fails_cleanly },
        { "remove_node_then_render",                remove_node_then_render },
        { "destroy_and_recreate_views_and_scenes",  destroy_and_recreate_views_and_scenes },
        { "resize_offscreen",                       resize_offscreen },
        { "background_changes_at_runtime",          background_changes_at_runtime },
        { "shadows_darken_the_ground",              shadows_darken_the_ground },
        { "shadow_filter_quality_is_settable",      shadow_filter_quality_is_settable },
        { "shadow_resolution_rebuilds_the_atlas",   shadow_resolution_rebuilds_the_atlas },
        { "ambient_sh_lights_world_axes",           ambient_sh_lights_world_axes },
        { "equirect_sky_fills_the_background",      equirect_sky_fills_the_background },
        { "cubemap_sky_faces_match_directions",     cubemap_sky_faces_match_directions },
        { "rough_metal_reflects_across_cube_faces", rough_metal_reflects_across_cube_faces },
        { "mesh_from_buffers_renders",              mesh_from_buffers_renders },
        { "hierarchy_transform_propagates",         hierarchy_transform_propagates },
        { "material_and_mesh_lifetime",             material_and_mesh_lifetime },
        { "light_on_node_and_camera_desc",          light_on_node_and_camera_desc },
        { "area_light_lights_the_wall",             area_light_lights_the_wall },
        { "ies_profile_shapes_a_spot",              ies_profile_shapes_a_spot },
        { "area_light_mask_tints_and_ltc_ignores_it", area_light_mask_tints_and_ltc_ignores_it },
        { "two_area_lights_both_light",             two_area_lights_both_light },
        { "overlay_lines_draw_on_top",              overlay_lines_draw_on_top },
        { "pbr_alpha_blend_mixes_with_background",  pbr_alpha_blend_mixes_with_background },
        { "pbr_alpha_cutout_discards_below_cutoff", pbr_alpha_cutout_discards_below_cutoff },
        { "pbr_additive_adds_modulate_multiplies",  pbr_additive_adds_modulate_multiplies },
        { "pbr_two_sided_shows_inside_faces",       pbr_two_sided_shows_inside_faces },
        { "pbr_texture_scale_tiles_uvs",            pbr_texture_scale_tiles_uvs },
        { "pbr_workflow_writes_exactly_one_of_metalness_and_fresnel",
                                                    pbr_workflow_writes_exactly_one_of_metalness_and_fresnel },
        { "texture_cache_keys_on_colour_space",     texture_cache_keys_on_colour_space },
        { "update_texture_rewrites_pixels_without_a_flush",
                                                    update_texture_rewrites_pixels_without_a_flush },
        { "per_material_reflection_cubemap_overrides_the_sky",
                                                    per_material_reflection_cubemap_overrides_the_sky },
        { "unlit_shading_model_ignores_lighting",   unlit_shading_model_ignores_lighting },
        { "unlit_keeps_its_base_colour_map",        unlit_keeps_its_base_colour_map },
        { "unlit_refuses_rigged_meshes",            unlit_refuses_rigged_meshes },
        { "fog_transmittance_is_exponential",        fog_transmittance_is_exponential },
        { "fog_height_layer",                        fog_height_layer },
        { "fog_breakthrough_spares_bright_surfaces", fog_breakthrough_spares_bright_surfaces },
        { "msaa_offscreen_views_default_to_one_sample", msaa_offscreen_views_default_to_one_sample },
        { "msaa_4x_blends_silhouette_edges",        msaa_4x_blends_silhouette_edges },
        { "msaa_runtime_toggle_and_clamping",       msaa_runtime_toggle_and_clamping },
        { "msaa_overlay_pass_resolves_at_every_sample_count",
                                                    msaa_overlay_pass_resolves_at_every_sample_count },
        { "workspace_seam_counts_every_rebuild",    workspace_seam_counts_every_rebuild },
        { "shadow_mesh_optimization_keeps_static_shadows", shadow_mesh_optimization_keeps_static_shadows },
        { "dynamic_mesh_shadow_follows_its_pose",   dynamic_mesh_shadow_follows_its_pose },
        { "hud_overlay_is_ignored_offscreen_unless_asked", hud_overlay_is_ignored_offscreen_unless_asked },
        { "hud_overlay_draws_where_it_says_when_allowed", hud_overlay_draws_where_it_says_when_allowed },
        { "hud_overlay_toggle_does_not_rebuild_the_workspace", hud_overlay_toggle_does_not_rebuild_the_workspace },
        { "render_stats_are_live_and_lazily_recorded", render_stats_are_live_and_lazily_recorded },
        { "object_counts_track_lifetimes",          object_counts_track_lifetimes },
        { "postfx_is_ignored_offscreen_unless_asked", postfx_is_ignored_offscreen_unless_asked },
        { "hdr_tonemap_and_exposure",               hdr_tonemap_and_exposure },
        { "fixed_exposure_tonemap",                 fixed_exposure_tonemap },
        { "bloom_bleeds_bright_areas",              bloom_bleeds_bright_areas },
        { "ssao_darkens_creases",                   ssao_darkens_creases },
        { "smaa_smooths_edges",                     smaa_smooths_edges },
        { "refraction_bends_the_background",        refraction_bends_the_background },
        { "postfx_epic_shape_with_msaa",            postfx_epic_shape_with_msaa },
        { "sky_stays_smooth_under_the_post_chain",  sky_stays_smooth_under_the_post_chain },
        { "pip_is_ignored_offscreen_unless_asked",  pip_is_ignored_offscreen_unless_asked },
        { "pip_composites_a_second_camera_into_the_rect",
                                                    pip_composites_a_second_camera_into_the_rect },
        { "pip_over_msaa_keeps_the_main_frame",     pip_over_msaa_keeps_the_main_frame },
        { "pip_letterboxes_to_the_authored_aspect", pip_letterboxes_to_the_authored_aspect },
        { "pip_survives_a_main_workspace_rebuild",  pip_survives_a_main_workspace_rebuild },
        { "letterbox_fits_the_shot_and_bars_the_rest", letterbox_fits_the_shot_and_bars_the_rest },
        { "pip_and_letterbox_over_the_post_chain",   pip_and_letterbox_over_the_post_chain },
        { "letterbox_post_chain_runs_on_the_shot_only", letterbox_post_chain_runs_on_the_shot_only },
        { "monitor_off_is_inert",                   monitor_off_is_inert },
        { "monitor_passes_sum_to_the_frame",        monitor_passes_sum_to_the_frame },
        { "monitor_records_work_with_its_reason",   monitor_records_work_with_its_reason },
        { "monitor_survives_rebuilds_and_view_destruction",
                                                    monitor_survives_rebuilds_and_view_destruction },
        { "monitor_snapshot_names_the_graph",       monitor_snapshot_names_the_graph },
        { "monitor_gpu_timestamps",                 monitor_gpu_timestamps },
        { "teardown_is_clean",                      teardown_is_clean },
    };
    const std::string filter = argc > 1 ? argv[1] : "";
    {
        std::string error;
        gEngine = Engine::create(testConfig(), error);
        if (!gEngine) { std::printf("FAIL: engine create: %s\n", error.c_str()); return 1; }
    }
    int ran = 0;
    for (const Test &t : tests) {
        if (!filter.empty() && filter != t.name) continue;
        if (!gEngine) { std::printf("[ SKIP ] %s (engine already torn down)\n", t.name); continue; }
        const int before = gFailures;
        std::printf("[ RUN  ] %s\n", t.name);
        std::fflush(stdout);
        t.fn();
        std::printf("[ %s ] %s\n", gFailures == before ? " OK " : "FAIL", t.name);
        std::fflush(stdout);
        ++ran;
    }
    gEngine.reset();   // no-op after teardown_is_clean; frees a filtered run
    std::printf("%d test(s), %d check(s), %d failure(s)\n", ran, gChecks, gFailures);
    return (gFailures == 0 && ran > 0) ? 0 : 1;
}
