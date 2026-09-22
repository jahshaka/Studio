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

/// `--shadow-vao` runs ONE cycle and only the shadow-VAO arm, so the alias
/// assertions have a ctest name of their own (`meshbake.shadow_vao_alias`) that
/// gate-scope can select. The default run does all three cycles AND the arm — the
/// arm's whole subject is a destroy that used to throw, so it belongs in the suite
/// that destroys three engines as well as in a suite of its own.
static bool gShadowVaoOnly = false;

/// THE MIXED SHADOW-VAO LIST (ATOM P1's AT-A11, ogre-patch 0088) — the body of
/// `meshbake.shadow_vao_alias`, and a case of this suite as well.
///
/// THIS SUITE IS WHERE IT BELONGS because this suite is what the double free
/// killed: a MIXED list — an optimized shadow VAO at level 0 with the coarse levels
/// aliasing the normal ones — used to read as fully INDEPENDENT in
/// `SubMesh::destroyShadowMappingVaos`, so the aliased entries were destroyed there
/// and again by `~SubMesh`, throwing on the FIRST MESH DESTROY AFTER AN IMPORT. The
/// patch makes the alias test per entry; the checks are that the shape really is
/// mixed AND that the destroy still comes back clean.
static void shadowVaoArm(Engine *engine, Scene *s, int iteration) {
    char msg[192];
    MeshData chained = enginetest::unitCubeMesh();
    // Six levels over one vertex buffer. The INDEX LISTS need not be simpler than
    // level 0 for this — the subject is the VAO LIST'S SHAPE, not the simplification
    // — so each level is the cube's own list and the bounds are an ascending ladder,
    // which is what the reader, `createMesh`'s validation and the strategy require.
    for (int L = 1; L <= 5; ++L) {
        chained.lodIndices.push_back(chained.indices);
        chained.lodErrors.push_back(0.01f * float(L));
        chained.lodBounds.push_back(0.02f * float(L));
    }
    const MeshId cm = s->createMesh(chained);
    std::snprintf(msg, sizeof msg, "iteration %d: a 6-level mesh is created", iteration);
    CHECK(cm != 0, msg);
    unsigned levels = 0, independent = 0;
    const bool got = s->meshVaoShape(cm, levels, independent);
    std::printf("    6-level mesh: levels %u, independent shadow VAOs %u\n", levels, independent);
    std::snprintf(msg, sizeof msg, "iteration %d: the chain really is 6 levels deep", iteration);
    CHECK(got && levels == 6, msg);
    std::snprintf(msg, sizeof msg,
                  "iteration %d: and it holds ONE shadow VAO set, not one per level"
                  " (ogre-patch 0088)", iteration);
    CHECK(got && independent == 1, msg);
    // And it destroys cleanly — the exact operation that used to throw.
    std::snprintf(msg, sizeof msg,
                  "iteration %d: destroying the mixed-list mesh does not double free",
                  iteration);
    CHECK(s->destroyMesh(cm), msg);

    // AND THE BOUNDARY REFUSES A MALFORMED LEVEL, LOUDLY (the lane's audit, item 4).
    // `buildMeshV2` no longer re-walks the levels, so `createMesh` is the ONE
    // validator — and it has to be, because this very suite hand-builds `lodIndices`
    // and an out-of-range index would otherwise reach the driver as an index buffer.
    {
        MeshData bad = enginetest::unitCubeMesh();
        bad.lodIndices.push_back(std::vector<unsigned>{ 0u, 1u, 99999u });
        bad.lodErrors.push_back(0.01f);
        bad.lodBounds.push_back(0.02f);
        std::snprintf(msg, sizeof msg,
                      "iteration %d: a LOD level naming a vertex the mesh has not is REFUSED",
                      iteration);
        CHECK(s->createMesh(bad) == 0, msg);
        std::printf("    refusal: %s\n", engine->lastError().c_str());
    }
}

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

    if (gShadowVaoOnly) { shadowVaoArm(e.get(), s, iteration); e->destroyView(v); e->destroyScene(s);
                          e.reset();
                          CHECK(!Engine::isAlive(), "engine destroyed, none alive");
                          return true; }

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

    shadowVaoArm(e.get(), s, iteration);

    e->destroyView(v);
    e->destroyScene(s);
    e.reset();
    std::snprintf(msg, sizeof msg, "iteration %d: engine destroyed, none alive", iteration);
    CHECK(!Engine::isAlive(), msg);
    return true;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--shadow-vao") gShadowVaoOnly = true;
    const int cycles = gShadowVaoOnly ? 1 : 3;
    for (int i = 1; i <= cycles; ++i)
        if (!runOnce(i)) break;
    if (failures) std::printf("RESULT: %d FAILURE(S)\n", failures);
    else if (gShadowVaoOnly) std::printf("RESULT: PASS (the mixed shadow-VAO list)\n");
    else std::printf("RESULT: PASS (3 create/render/destroy cycles)\n");
    return failures ? 1 : 0;
}
