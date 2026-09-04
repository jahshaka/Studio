// Engine re-creation in one process: create → render → destroy, three times.
//
// This was impossible until Ogre-Next's VulkanInstance was patched to clear its
// static enabledExtensions/enabledLayers arrays (engines/ogre-next,
// RenderSystems/Vulkan/src/OgreVulkanDevice.cpp — see OGRE_PLATFORM_DEPS.md).
// Without the patch the second Root requested garbage extension names and
// vkCreateInstance failed with VK_ERROR_EXTENSION_NOT_PRESENT.
//
// Separate executable on purpose: the main suite shares one Engine per process.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool runOnce(int iteration) {
    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_engine_recreate-ogre.log";
    std::string error;
    auto e = Engine::create(cfg, error);
    char msg[128];
    std::snprintf(msg, sizeof msg, "iteration %d: Engine::create succeeded", iteration);
    CHECK(e != nullptr, msg);
    if (!e) { std::printf("    error: %s\n", error.c_str()); return false; }

    View *v = e->createOffscreenView("view", 96, 96, Colour(0.0f, 0.0f, 1.0f));
    Scene *s = e->createScene("scene");
    std::snprintf(msg, sizeof msg, "iteration %d: view + scene created", iteration);
    CHECK(v && s, msg);
    if (!v || !s) return false;
    v->setScene(s);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    NodeId cube = enginetest::addTestCube(s, Colour(0.9f, 0.3f, 0.1f), 0.2f, 0.6f);
    enginetest::setNodeScale(s, cube, Vec3(1.2f, 1.2f, 1.2f));
    enginetest::testCameraLookAt(v, Vec3(2.2f, 1.8f, 2.6f), Vec3(0, 0, 0));
    for (int i = 0; i < 3; ++i) e->renderOneFrame();

    Image img;
    CHECK(v->readPixels(img) && img.width == 96, "readPixels");
    const Colour c = img.at(48, 48), k = img.at(2, 2);
    std::printf("    centre %.0f %.0f %.0f  corner %.0f %.0f %.0f\n",
                c.r * 255, c.g * 255, c.b * 255, k.r * 255, k.g * 255, k.b * 255);
    std::snprintf(msg, sizeof msg, "iteration %d: corner is the clear colour", iteration);
    CHECK(k.b > 0.8f && k.r < 0.15f && k.g < 0.15f, msg);
    std::snprintf(msg, sizeof msg, "iteration %d: centre is the lit cube", iteration);
    CHECK(c.r > 0.4f && c.b < 0.5f, msg);

    // ---- AREA lights must work on EVERY Engine, not just the first ----
    //
    // HlmsPbs::loadLtcMatrix has to run once per Root before any area light is
    // drawn, and the "once" flag used to be a function-local static inside
    // OgreScene::setLight: it survived Engine destruction, so from the second
    // Engine onward the flag said "loaded" while the new Root's HlmsPbs had no
    // LTC matrix (deep audit 2026-09, area 5). The flag now lives in
    // lightextras and is reset by its shutdown(). This assertion only means
    // anything from iteration 2 on — which is exactly the point.
    {
        LightDesc off;                       // dark the directional and the ambient:
        off.type = LightType::Directional;   // whatever lights the cube now IS the area light
        off.intensity = 0.0f;
        s->setLight(1u, off);                // the directional is the first node created
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        for (int i = 0; i < 2; ++i) e->renderOneFrame();
        Image dark;
        v->readPixels(dark);
        const Colour d = dark.at(48, 48);
        std::snprintf(msg, sizeof msg, "iteration %d: cube is dark with no lights", iteration);
        CHECK(d.r < 0.05f && d.g < 0.05f && d.b < 0.05f, msg);

        // A rect light facing the cube from +Z. addDirectionalLight only builds
        // the node with -Y rotated onto the direction; setLight then makes it
        // an LTC area light and setNodePosition puts it in front.
        const NodeId area = enginetest::addDirectionalLight(s, Vec3(0.0f, 0.0f, -1.0f), 1.0f);
        enginetest::setNodePosition(s, area, Vec3(0.0f, 0.0f, 2.0f));
        LightDesc a;
        a.type = LightType::Area;
        a.accurate = true;                   // LT_AREA_LTC: the kind that needs the matrix
        a.colour = Colour(1.0f, 1.0f, 1.0f);
        a.intensity = 40.0f;
        a.range = 20.0f;
        a.rectWidth = 3.0f;
        a.rectHeight = 3.0f;
        std::snprintf(msg, sizeof msg, "iteration %d: area light accepted", iteration);
        CHECK(s->setLight(area, a), msg);
        for (int i = 0; i < 3; ++i) e->renderOneFrame();
        Image lit;
        v->readPixels(lit);
        const Colour L = lit.at(48, 48);
        std::printf("    area-lit centre %.0f %.0f %.0f\n", L.r * 255, L.g * 255, L.b * 255);
        std::snprintf(msg, sizeof msg, "iteration %d: the AREA light lights the cube", iteration);
        CHECK(L.r > 0.05f, msg);
    }

    e->destroyView(v);
    e->destroyScene(s);
    e.reset();
    std::snprintf(msg, sizeof msg, "iteration %d: engine destroyed, none alive", iteration);
    CHECK(!Engine::isAlive(), msg);
    return true;
}

int main() {
    for (int i = 1; i <= 3; ++i)
        if (!runOnce(i)) break;
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS (3 create/render/destroy cycles)\n", failures);
    return failures ? 1 : 0;
}
