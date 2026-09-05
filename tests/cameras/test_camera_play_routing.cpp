// The active camera takes the view WHEN PLAYING — in pixels (CAMERAS_SPEC D6).
//
// The seam is SceneMirror::applyCamera, which is "the ONLY way to move a View's
// camera" (Engine.h). Phase 1 makes exactly one substitution there: when the
// DOCUMENT says it is playing and an active camera is set, that camera's state
// drives the view instead of whatever camera the caller passed. Everything the
// editor and the player already do — the same three sync/applySky/applyCamera
// calls — is untouched.
//
// What this suite proves, with real frames from the real backend:
//
//   * EDITING is unchanged. With an active camera set and the scene NOT
//     playing, the view is still the explorer's. This is the assertion that
//     stops phase 1 from taking the main viewport (pilot mode is phase 3).
//   * PLAYING routes. Flip the document's play flag and the same applyCamera
//     call renders the active camera's shot instead.
//   * Clearing the active camera hands the view back.
//   * A guid whose camera was deleted does not render through a corpse.
//
// The two cameras are aimed at two different, unambiguous things: the explorer
// looks at a green cube, the scene camera looks away at the blue clear colour.
// Pixels are the assertion, not the CameraDesc.
//
// Runs with QT_QPA_PLATFORM=offscreen and a reachable DISPLAY (Vulkan).

#include <QGuiApplication>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static Colour centre(const Image &i) { return i.at(i.width / 2, i.height / 2); }
static bool isBlue(const Colour &c) { return c.b > 0.8f && c.r < 0.15f && c.g < 0.15f; }
static void show(const char *tag, const Image &i)
{
    const Colour c = centre(i);
    std::printf("    %-34s centre %3.0f %3.0f %3.0f\n", tag, c.r * 255, c.g * 255, c.b * 255);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_camera_play_routing-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("cams", 96, 96, Colour(0, 0, 1));
    Scene *target = engine->createScene("cams");
    CHECK(view && target, "offscreen view + engine scene");
    if (!view || !target) return 1;
    view->setScene(target);
    target->setAmbient(Colour(0.5f, 0.5f, 0.5f), Colour(0.4f, 0.4f, 0.4f));

    // ---- the document: one cube at the origin -----------------------------
    auto doc = iris::Scene::create();
    auto meshNode = iris::MeshNode::create();
    meshNode->setName("cube");
    meshNode->setMesh(":assets/models/cube.obj");
    CHECK(!!meshNode->getMesh(), "cube.obj loaded into the document");
    {   // normalise to unit radius, exactly like the mirror suite
        const float r = meshNode->getMeshRadius();
        const float s = r > 0.0f ? 1.0f / r : 1.0f;
        meshNode->setLocalScale(iris::Vec3(s, s, s));
    }
    meshNode->setMaterial(iris::PbrMaterial::create());
    doc->getRootNode()->addChild(meshNode);

    // A sun, so the cube is lit and unmistakably NOT the blue clear colour.
    auto light = iris::LightNode::create();
    light->setName("sun");
    light->intensity = 1.0f;
    light->setLocalRot(iris::Quat::fromEulerAngles(-50.0f, 30.0f, 0.0f));
    light->setLocalPos(iris::Vec3(0.0f, 0.0f, -60.0f));   // out of frame; icon billboard too
    doc->getRootNode()->addChild(light);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    mirror.sync();

    auto frame = [&](Image &img) {
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
    };

    // The EXPLORER: in front of the cube, looking at it. Not a scene node —
    // exactly like the editor's mEditorCam.
    auto editorCam = iris::CameraNode::create();
    editorCam->setLocalPos(iris::Vec3(0.0f, 0.0f, 4.0f));
    editorCam->lookAt(iris::Vec3(0, 0, 0));

    // The SCENE camera: a real child of the root, pointed away from everything
    // so its shot is the bare blue clear colour.
    auto sceneCam = iris::CameraNode::create();
    sceneCam->setName("Shot A");
    sceneCam->setLocalPos(iris::Vec3(0.0f, 0.0f, 4.0f));
    sceneCam->lookAt(iris::Vec3(0.0f, 0.0f, 40.0f));      // 180 degrees away
    doc->getRootNode()->addChild(sceneCam);
    mirror.sync();
    CHECK(doc->cameras.size() == 1, "the scene camera registered itself in the document");

    Image img;

    // ---- 1. baseline: the explorer sees the cube --------------------------
    mirror.applyCamera(editorCam, view);
    frame(img); show("explorer, not playing", img);
    CHECK(!isBlue(centre(img)), "the explorer camera sees the cube");

    // ---- 2. an active camera does NOT hijack EDITING ----------------------
    CHECK(doc->setActiveCamera(sceneCam->getGUID()), "scene camera armed as the active camera");
    CHECK(!doc->isPlaying(), "the document is not playing");
    mirror.applyCamera(editorCam, view);
    frame(img); show("active camera set, EDITING", img);
    CHECK(!isBlue(centre(img)),
          "EDITING still renders the explorer's view — an active camera must not take the "
          "main viewport (that is phase 3's pilot mode, not this)");

    // ---- 3. play routes through the active camera -------------------------
    doc->setPlaying(true);
    mirror.applyCamera(editorCam, view);
    frame(img); show("active camera set, PLAYING", img);
    CHECK(isBlue(centre(img)),
          "PLAY renders through the active camera (which is pointed at nothing) even though "
          "applyCamera was handed the explorer");

    // ---- 4. and follows it when it moves ----------------------------------
    sceneCam->lookAt(iris::Vec3(0, 0, 0));                // turn it back at the cube
    mirror.applyCamera(editorCam, view);
    frame(img); show("active camera turned back", img);
    CHECK(!isBlue(centre(img)), "moving the ACTIVE camera moves the played view");

    // ---- 5. clearing it hands the view back -------------------------------
    sceneCam->lookAt(iris::Vec3(0.0f, 0.0f, 40.0f));      // away again
    mirror.applyCamera(editorCam, view);
    frame(img);
    CHECK(isBlue(centre(img)), "…and away again (the seam is live, not a one-shot)");
    CHECK(doc->setActiveCamera(QString()), "active camera cleared");
    mirror.applyCamera(editorCam, view);
    frame(img); show("no active camera, PLAYING", img);
    CHECK(!isBlue(centre(img)),
          "with no active camera, play renders the free viewer again");

    // ---- 6. a deleted camera cannot be rendered through --------------------
    doc->setActiveCamera(sceneCam->getGUID());
    doc->getRootNode()->removeChild(sceneCam);
    mirror.sync();
    CHECK(doc->getActiveCameraGuid().isEmpty(),
          "deleting the active camera cleared the choice (Scene::removeNode)");
    mirror.applyCamera(editorCam, view);
    frame(img); show("active camera deleted, PLAYING", img);
    CHECK(!isBlue(centre(img)),
          "play falls back to the free viewer instead of a camera that no longer exists");

    // ---- 7. stop playing ---------------------------------------------------
    doc->setPlaying(false);
    mirror.applyCamera(editorCam, view);
    frame(img);
    CHECK(!isBlue(centre(img)), "stopping play leaves the explorer in charge");

    mirror.setSource(nullptr);
    engine->destroyScene(target);

    std::printf(failures == 0 ? "\nALL CAMERA PLAY-ROUTING CHECKS PASSED\n"
                              : "\n%d CAMERA PLAY-ROUTING CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
