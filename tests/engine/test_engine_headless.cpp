// THE HEADLESS BOOT — Ogre's NULL render system (SPECS/SCENEGRAPH_SPEC.md §3b,
// EngineConfig::headless).
//
// This suite is the lane's FIRST ASSERTION and its permanent guard. The research
// probes (spikes/scenegraph-null-rs) proved the NODE GRAPH works with no display
// and no GPU; what they left unproven — flagged in the spec's open items — is
// whether MESHES AND ITEMS do, because the NULL VaoManager hands out buffers
// that are plain memory pretending to be GPU objects. Everything the document
// model needs from the engine is exercised here, through the public boundary:
//
//   * the engine boots with DISPLAY unset and no driver installed,
//   * the document graph scene manager exists,
//   * Scenes, nodes, hierarchy and transforms work,
//   * MESHES (v2 vertex/index buffers + VAO), MATERIALS and attachMesh work,
//     including a skinned mesh and a mesh vertex update,
//   * Views of BOTH kinds refuse politely, with a reason,
//   * renderOneFrame is legal and does nothing.
//
// The test itself runs with DISPLAY removed from its environment (see
// tests/engine/CMakeLists.txt) — the assertion is not "it works on this box",
// it is "it works on a box that has no graphics at all".
//
// Links JahshakaEngine ONLY: no Qt, no app, no Ogre header.
#include "jahshaka/engine/Engine.h"

#include <cstdio>
#include <cstdlib>
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

/// A unit quad with normals, uvs and 32-bit indices; no tangents (the engine
/// generates them, which is itself a code path worth running here).
MeshData quad(bool skinned = false) {
    MeshData m;
    m.positions = { 0,0,0,  1,0,0,  0,1,0,  1,1,0 };
    m.normals   = { 0,0,1,  0,0,1,  0,0,1,  0,0,1 };
    m.uvs       = { 0,0,   1,0,   0,1,   1,1 };
    m.indices   = { 0, 1, 2, 2, 1, 3 };
    if (skinned) {
        m.blendIndices.assign(16, 0);
        m.blendWeights.assign(16, 0.0f);
        for (int v = 0; v < 4; ++v) m.blendWeights[v * 4] = 1.0f;
    }
    return m;
}

}  // namespace

int main() {
    // NO DISPLAY. The suite's whole point is that this is not needed; asserting
    // it here as well as in CMake means the proof survives a hand-run.
    if (const char *d = std::getenv("DISPLAY")) {
        std::printf("NOTE: DISPLAY is set (%s) — this suite does not use it, but the "
                    "ctest entry unsets it so the claim is actually tested.\n", d);
    }

    EngineConfig cfg;
    cfg.headless     = true;
    cfg.pluginDir    = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile      = "test_engine_headless-ogre.log";
    // Deliberately configured: the engine must REFUSE to persist a shader cache
    // written by the NULL render system (a real device would be offered it).
    cfg.shaderCacheDir = "headless-cache-must-not-exist";
    cfg.appBuildId     = "test_engine_headless";

    std::string error;
    std::unique_ptr<Engine> engine = Engine::create(cfg, error);
    if (!engine) {
        std::printf("FAIL: the headless engine would not start: %s\n", error.c_str());
        return 1;
    }
    std::printf("headless engine up\n");
    CHECK(engine->isHeadless());

    // ---- the shader cache is off, silently (Types.h) -----------------------
    {
        const ShaderCacheStats st = engine->shaderCacheStats();
        CHECK_MSG(!st.enabled, "a NULL-render-system run must persist no shader cache");
    }

    // ---- the document graph scene manager ----------------------------------
    void *docScene = engine->documentGraphScene();
    CHECK_MSG(docScene != nullptr, "documentGraphScene: %s", engine->lastError().c_str());
    // Idempotent: one manager, however often it is asked for.
    CHECK(engine->documentGraphScene() == docScene);

    // ---- views of both kinds refuse, with a reason --------------------------
    {
        View *onscreen = engine->createView("headless/onscreen", 0, 64, 64, Colour(0, 0, 0, 1));
        CHECK(onscreen == nullptr);
        CHECK_MSG(engine->lastError().find("headless") != std::string::npos,
                  "createView's refusal should name the reason, got: %s",
                  engine->lastError().c_str());
        View *off = engine->createOffscreenView("headless/offscreen", 64, 64, Colour(0, 0, 0, 1));
        CHECK(off == nullptr);
        CHECK_MSG(engine->lastError().find("headless") != std::string::npos,
                  "createOffscreenView's refusal should name the reason, got: %s",
                  engine->lastError().c_str());
    }

    // ---- a Scene, with no View anywhere ------------------------------------
    // (createScene needs the Hlms, which documentGraphScene registered — a
    // headless host never creates a View, so this is the only way it gets one.)
    Scene *scene = engine->createScene("headless/scene");
    if (!scene) {
        std::printf("FAIL: createScene: %s\n", engine->lastError().c_str());
        return 1;
    }
    scene->setAmbient(Colour(0.2f, 0.2f, 0.2f, 1), Colour(0.05f, 0.05f, 0.05f, 1));

    // ---- nodes, hierarchy, transforms --------------------------------------
    const NodeId parent = scene->createNode();
    const NodeId child  = scene->createNode(parent);
    CHECK(parent != 0 && child != 0 && parent != child);
    scene->setNodeTransform(parent, Vec3(1, 2, 3), Quat(), Vec3(1, 1, 1));
    scene->setNodeTransform(child, Vec3(0, 1, 0), Quat(), Vec3(2, 2, 2));
    scene->setNodeVisible(child, false);
    scene->setNodeVisible(child, true);

    // ---- THE ASSERTION: meshes, materials, attachment ----------------------
    const MeshId mesh = scene->createMesh(quad());
    CHECK_MSG(mesh != 0, "createMesh under the NULL render system: %s",
              engine->lastError().c_str());
    PbrParams pbr;
    pbr.albedo    = Colour(0.8f, 0.2f, 0.1f, 1.0f);
    pbr.metalness = 0.0f;
    pbr.roughness = 0.5f;
    const MaterialId mat = scene->createPbrMaterial(pbr);
    CHECK_MSG(mat != 0, "createPbrMaterial: %s", engine->lastError().c_str());
    CHECK_MSG(scene->attachMesh(child, mesh, mat), "attachMesh: %s",
              engine->lastError().c_str());
    // Attaching again replaces; detach releases. Both are Item lifetime paths.
    CHECK(scene->attachMesh(child, mesh, mat));
    CHECK(scene->detachMesh(child));
    CHECK(scene->attachMesh(child, mesh, mat));

    // A dynamic mesh + a vertex rewrite: the CPU-skinning upload path, which
    // pushes bytes through a staging buffer on a real device.
    MeshData dyn = quad();
    dyn.dynamic = true;
    const MeshId dynMesh = scene->createMesh(dyn);
    CHECK_MSG(dynMesh != 0, "createMesh(dynamic): %s", engine->lastError().c_str());
    const NodeId dynNode = scene->createNode();
    CHECK(scene->attachMesh(dynNode, dynMesh, mat));
    std::vector<float> moved = dyn.positions;
    for (size_t i = 1; i < moved.size(); i += 3) moved[i] += 5.0f;
    CHECK_MSG(scene->updateMeshVertices(dynMesh, moved, dyn.normals),
              "updateMeshVertices: %s", engine->lastError().c_str());

    // A GPU-skinned mesh + a rig: a second vertex declaration and the bone
    // buffer, all of which the NULL VaoManager has to hand out.
    {
        const MeshId skinnedMesh = scene->createMesh(quad(true));
        CHECK_MSG(skinnedMesh != 0, "createMesh(skinned): %s", engine->lastError().c_str());
        SkeletonDesc skel;
        skel.id = "headless/rig";
        BoneDesc root;
        root.name = "root";
        skel.bones.push_back(root);
        const NodeId rigNode = scene->createNode();
        CHECK_MSG(scene->attachSkinnedMesh(rigNode, skinnedMesh, mat, skel),
                  "attachSkinnedMesh: %s", engine->lastError().c_str());
        CHECK(scene->removeNode(rigNode));
        CHECK(scene->destroyMesh(skinnedMesh));
    }

    // A light, so the scene has something besides geometry (lights allocate no
    // buffers, but setLight walks the same node table).
    {
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1, 1, 1, 1);
        l.intensity = 1.0f;
        const NodeId lightNode = scene->createNode();
        CHECK_MSG(scene->setLight(lightNode, l), "setLight: %s", engine->lastError().c_str());
    }

    // ---- a frame: legal, and nothing happens -------------------------------
    engine->renderOneFrame();
    engine->renderOneFrame();
    std::printf("renderOneFrame x2 survived with no View\n");

    // ---- teardown, in the engine's order -----------------------------------
    CHECK(scene->detachMesh(child));
    CHECK(scene->destroyMesh(mesh));
    CHECK(scene->destroyMesh(dynMesh));
    CHECK(scene->destroyMaterial(mat));
    engine->destroyScene(scene);
    engine.reset();

    std::printf("%s: %d checks, %d failures\n", gFailures ? "FAILED" : "PASSED", gChecks,
                gFailures);
    return gFailures ? 1 : 0;
}
