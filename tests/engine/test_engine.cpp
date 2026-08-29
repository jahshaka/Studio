// Headless characterisation tests for the engine abstraction.
//
// Links JahshakaEngine ONLY — no Qt, no app, no Ogre header. Every test renders
// through createOffscreenView + readPixels; nothing here opens a window.
// Framework-free on purpose: nothing to fetch, nothing to install.
#include "jahshaka/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
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
Px corner(const Image &img) { return px(img, 2, 2); }

/// The spike's scene: lit metallic cube, camera looking at it from (2.2,1.8,2.6).
NodeId populate(Scene *s, const Colour &albedo) {
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    s->addDirectionalLight(Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    NodeId cube = s->addTestCube(albedo, 0.2f, 0.6f);
    s->setNodeScale(cube, Vec3(1.2f, 1.2f, 1.2f));
    return cube;
}
void aim(View *v) {
    v->setCameraPosition(Vec3(2.2f, 1.8f, 2.6f));
    v->lookAt(Vec3(0.0f, 0.0f, 0.0f));
}
void render(Engine *e, int frames = 3) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

// ---------------------------------------------------------------------------
void create_twice_returns_null_with_error() {
    CHECK(Engine::isAlive());
    std::string error;
    auto second = Engine::create(testConfig(), error);
    CHECK(second == nullptr);
    CHECK_MSG(!error.empty(), "error must name the reason");
    std::printf("    second create refused: %s\n", error.c_str());
    CHECK(Engine::isAlive());
    // Re-creation after destroying the first is NOT exercised here. It is
    // genuinely impossible with the current Ogre-Next build: the Vulkan plugin's
    // VulkanInstance::enumerateExtensionsAndLayers (OgreVulkanDevice.cpp:88)
    // appends to static FastArrays that survive dlclose, so the second Root
    // requests duplicate/dangling extension names and vkCreateInstance fails with
    // VK_ERROR_EXTENSION_NOT_PRESENT. Two-line fix upstream (clear() both arrays);
    // until then hosts create the Engine once and keep it — teardown_is_clean,
    // last in this suite, covers destruction.
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
    const NodeId again = s->addTestCube(kOrange, 0.2f, 0.6f);
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
