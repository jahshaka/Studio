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
    for (unsigned res : { 512u, 4096u, 1024u }) {
        fx.e->setShadowResolution(res);
        CHECK_MSG(fx.e->shadowResolution() == res, "resolution %u did not stick", res);
        render(fx.e, 4);
        Image img; REQUIRE(v->readPixels(img));
        const int contrast = groundContrast(img);
        std::printf("    shadow atlas @%-4u: ground row contrast %d\n", res, contrast);
        CHECK_MSG(contrast > 40, "shadows must keep rendering at %u: contrast %d", res, contrast);
    }
    // Out-of-range values are clamped, not fatal.
    fx.e->setShadowResolution(1u);
    CHECK(fx.e->shadowResolution() == 256u);
    render(fx.e, 2);
    fx.e->setShadowResolution(2048u);   // restore the default for later tests
    CHECK(fx.e->shadowResolution() == 2048u);
    render(fx.e, 2);
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
    CHECK(s->setSky(SkyMode::Equirectangular, skyTex));
    render(fx.e, 3); REQUIRE(v->readPixels(img));
    const Px k = corner(img);
    std::printf("    equirect sky corner: %d %d %d\n", k.r, k.g, k.b);
    CHECK_MSG(k.r > 150 && k.b > 150 && k.g < 80, "sky texture should fill the background: %d %d %d", k.r, k.g, k.b);
    CHECK(s->setSky(SkyMode::NoSky, 0));
    render(fx.e, 2); REQUIRE(v->readPixels(img));
    CHECK(near(corner(img), kBlue));
    std::remove(path.c_str());
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
    CHECK(s->setSkyCubemap(faces));
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
    CHECK(s->setSky(SkyMode::NoSky, 0));
    render(fx.e, 2); Image img; REQUIRE(v->readPixels(img));
    CHECK(near(centre(img), kBlue));
}

void mesh_from_buffers_renders() {
    Fixture fx;
    View *v = fx.view("mesh-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("mesh-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    s->addDirectionalLight(Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
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
    s->addDirectionalLight(Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
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
    s->addDirectionalLight(Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
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
    v->setCameraPosition(Vec3(0, 0, 5));
    v->lookAt(Vec3(0, 0, 0));
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

void overlay_lines_draw_on_top() {
    Fixture fx;
    View *v = fx.view("overlay-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("overlay-scene");            REQUIRE(s);
    v->setScene(s); aim(v);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    s->addDirectionalLight(Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
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
    s->addDirectionalLight(Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
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
    s->addDirectionalLight(Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
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

void fog_fades_distant_surfaces_to_fog_colour() {
    Fixture fx;
    View *v = fx.view("fog-view", 96, 96, kBlue); REQUIRE(v);
    Scene *s = fx.scene("fog-scene");             REQUIRE(s);
    v->setScene(s);
    populate(s, kOrange);   // near cube at the origin; its front face is ~3.0 from the eye
    aim(v);
    // A huge cube 50 units down the view axis (half-extent 30 after scaling the
    // ±0.5 test cube): its faces fill every pixel around the near cube. Measured
    // eye distance of that backdrop is >= ~8.2 (the cube's near corner passes
    // close to the camera), so with fogEnd = 7 it is fully fogged everywhere.
    NodeId farNode = s->addTestCube(kCyan, 0.0f, 0.6f);
    REQUIRE(farNode != 0);
    s->setNodePosition(farNode, Vec3(-28.6f, -23.4f, -33.8f));
    s->setNodeScale(farNode, Vec3(60.0f, 60.0f, 60.0f));

    const Colour kMagenta(1.0f, 0.0f, 1.0f);   // 0/1 channels: sRGB-invariant
    Image img;
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px near0 = centre(img);
    const Px far0  = px(img, img.width / 2, 6);
    CHECK_MSG(!near(far0, kMagenta, 30),
              "baseline far surface is not fog-coloured: %d %d %d", far0.r, far0.g, far0.b);

    // Fog on: the near cube (eye distance ~3.0, before fogStart) is untouched;
    // the far surface (past fogEnd) must converge to exactly the fog colour.
    s->setFog(true, kMagenta, 5.0f, 7.0f);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px near1 = centre(img);
    const Px far1  = px(img, img.width / 2, 6);
    std::printf("    near %d %d %d -> %d %d %d | far %d %d %d -> %d %d %d\n",
                near0.r, near0.g, near0.b, near1.r, near1.g, near1.b,
                far0.r, far0.g, far0.b, far1.r, far1.g, far1.b);
    CHECK_MSG(near(far1, kMagenta),
              "fully fogged surface equals the fog colour: %d %d %d", far1.r, far1.g, far1.b);
    CHECK_MSG(std::abs(near1.r - near0.r) <= 6 && std::abs(near1.g - near0.g) <= 6 &&
              std::abs(near1.b - near0.b) <= 6,
              "surface before fogStart stays material-coloured: %d %d %d vs %d %d %d",
              near1.r, near1.g, near1.b, near0.r, near0.g, near0.b);

    // Toggle off: the same shaders render unfogged again (the enable flag rides
    // the pass buffer — no shader variant switch to go stale).
    s->setFog(false, kMagenta, 5.0f, 7.0f);
    render(fx.e); REQUIRE(v->readPixels(img));
    const Px far2 = px(img, img.width / 2, 6);
    CHECK_MSG(std::abs(far2.r - far0.r) <= 6 && std::abs(far2.g - far0.g) <= 6 &&
              std::abs(far2.b - far0.b) <= 6,
              "fog off restores the baseline: %d %d %d vs %d %d %d",
              far2.r, far2.g, far2.b, far0.r, far0.g, far0.b);
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
        { "equirect_sky_fills_the_background",      equirect_sky_fills_the_background },
        { "cubemap_sky_faces_match_directions",     cubemap_sky_faces_match_directions },
        { "mesh_from_buffers_renders",              mesh_from_buffers_renders },
        { "hierarchy_transform_propagates",         hierarchy_transform_propagates },
        { "material_and_mesh_lifetime",             material_and_mesh_lifetime },
        { "light_on_node_and_camera_desc",          light_on_node_and_camera_desc },
        { "area_light_lights_the_wall",             area_light_lights_the_wall },
        { "overlay_lines_draw_on_top",              overlay_lines_draw_on_top },
        { "pbr_alpha_blend_mixes_with_background",  pbr_alpha_blend_mixes_with_background },
        { "pbr_alpha_cutout_discards_below_cutoff", pbr_alpha_cutout_discards_below_cutoff },
        { "pbr_two_sided_shows_inside_faces",       pbr_two_sided_shows_inside_faces },
        { "fog_fades_distant_surfaces_to_fog_colour", fog_fades_distant_surfaces_to_fog_colour },
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
