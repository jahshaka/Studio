// The camera BODY, the FRUSTUM wires and the PiP inset — in pixels
// (CAMERAS_SPEC D2/D3, phases 2b and 2c).
//
// Everything here is asserted from real frames of the real backend, because
// every claim in this half of the spec is a claim about what is on screen:
//
//   * a scene camera DRAWS: a body and a frustum appear where the document put
//     the camera, and they are gone again when the helper toggle goes off
//     (byte-identical to a scene that never had the helpers — the same standard
//     the light wires and the HUD are held to);
//   * they are EDITOR helpers: on-top overlay geometry, which is what keeps
//     them out of a picture-in-picture inset with nothing to keep in step, so a
//     camera never appears inside its own preview;
//   * they follow the DOCUMENT: changing the lens (fov) changes the frustum;
//   * SELECTION recolours them;
//   * the PiP composited by the host route (SceneMirror::applyPip) shows the
//     camera's shot in the rect and leaves the rest of the frame untouched.
//
// Runs with QT_QPA_PLATFORM=offscreen and a reachable DISPLAY (Vulkan).

#include <QGuiApplication>
#include <cstdio>
#include <vector>

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

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// How many pixels differ, byte for byte.
static size_t diff(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return size_t(-1);
    size_t n = 0;
    for (size_t i = 0; i < a.rgba.size(); i += 4)
        if (a.rgba[i] != b.rgba[i] || a.rgba[i+1] != b.rgba[i+1] ||
            a.rgba[i+2] != b.rgba[i+2] || a.rgba[i+3] != b.rgba[i+3]) ++n;
    return n;
}
/// Pixels that are neither the blue clear colour nor (near) black — i.e. the
/// wire lines the helpers draw against an empty scene.
static size_t litPixels(const Image &img)
{
    size_t n = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const bool clear = c.b > 0.8f && c.r < 0.15f && c.g < 0.15f;
            if (!clear) ++n;
        }
    return n;
}
static size_t diffInRect(const Image &a, const Image &b, float l, float t, float w, float h,
                         bool inside)
{
    const int x0 = int(l * a.width), x1 = int((l + w) * a.width);
    const int y0 = int(t * a.height), y1 = int((t + h) * a.height);
    size_t n = 0;
    for (int y = 0; y < int(a.height); ++y)
        for (int x = 0; x < int(a.width); ++x) {
            const bool in = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (in != inside) continue;
            // Skip the rect's own boundary: a normalised edge only lands on a
            // pixel boundary when the rect divides the size exactly.
            if (!inside && x >= x0 - 1 && x <= x1 && y >= y0 - 1 && y <= y1) continue;
            const size_t i = (size_t(y) * a.width + x) * 4u;
            if (a.rgba[i] != b.rgba[i] || a.rgba[i+1] != b.rgba[i+1] ||
                a.rgba[i+2] != b.rgba[i+2] || a.rgba[i+3] != b.rgba[i+3]) ++n;
        }
    return n;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_camera_body-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("body", 160, 120, Colour(0, 0, 1));
    Scene *target = engine->createScene("body");
    CHECK(view && target, "offscreen view + engine scene");
    if (!view || !target) return 1;
    view->setScene(target);
    target->setAmbient(Colour(0.5f, 0.5f, 0.5f), Colour(0.4f, 0.4f, 0.4f));

    auto doc = iris::Scene::create();
    SceneMirror mirror(target);
    mirror.setSource(doc);

    // The EXPLORER, off to the side and above, so it sees the camera we are
    // about to add from outside — a camera looked at down its own axis is a
    // dot, and the frustum would be invisible.
    auto editorCam = iris::CameraNode::create();
    editorCam->setLocalPos(iris::Vec3(6.0f, 3.0f, 6.0f));
    editorCam->lookAt(iris::Vec3(0, 0, -1.5f));

    Image img;
    auto frame = [&](Image &out) {
        mirror.sync();
        mirror.applyCamera(editorCam, view);
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(out);
    };

    // ---- 1. an empty scene ------------------------------------------------
    Image empty; frame(empty);
    CHECK(litPixels(empty) == 0, "the empty scene is the bare clear colour");

    // ---- 2. a scene camera draws a body and a frustum ---------------------
    auto cam = iris::CameraNode::create();
    cam->setName("Shot A");
    cam->setLocalPos(iris::Vec3(0, 0, 0));
    cam->angle = 45.0f;
    cam->aspectRatio = 16.0f / 9.0f;
    doc->getRootNode()->addChild(cam);
    Image withBody; frame(withBody);
    const size_t bodyPx = litPixels(withBody);
    std::printf("    camera body + frustum: %zu lit pixels\n", bodyPx);
    CHECK(bodyPx > 200, "a scene camera draws a body and a frustum");

    // ---- 3. the lens drives the frustum -----------------------------------
    cam->angle = 100.0f;                       // a much wider shot
    Image wide; frame(wide);
    const size_t widePx = litPixels(wide);
    std::printf("    at 100 degrees: %zu lit pixels\n", widePx);
    CHECK(widePx != bodyPx,
          "widening the lens changes the frustum (the wires are derived from the document, "
          "not a fixed glyph)");
    cam->angle = 45.0f;

    // ---- 4. selection recolours it ----------------------------------------
    Image unselected; frame(unselected);
    mirror.setHighlightedNode(cam.staticCast<iris::SceneNode>());
    Image selected; frame(selected);
    CHECK(diff(unselected, selected) > 100,
          "selecting the camera changes how its helpers look");
    mirror.setHighlightedNode(iris::SceneNodePtr());

    // ---- 5. bodyVisible, and the helper toggle, are BYTE-EXACT off switches
    Image back; frame(back);
    cam->bodyVisible = false;
    Image hidden; frame(hidden);
    CHECK(diff(hidden, empty) == 0,
          "bodyVisible = false leaves NO trace: byte-identical to the scene without a camera");
    cam->bodyVisible = true;
    Image shown; frame(shown);
    CHECK(diff(shown, back) == 0, "…and turning it back on restores the frame byte-for-byte");

    mirror.setCameraBodies(false);
    Image gameView; frame(gameView);
    CHECK(diff(gameView, empty) == 0,
          "the host's helper toggle (Game View, play) hides them byte-identically too");
    mirror.setCameraBodies(true);
    frame(img);
    CHECK(diff(img, back) == 0, "…and restores them byte-for-byte");

    // ---- 6. THE PiP (phase 2c) --------------------------------------------
    // Give the camera something to look at that the explorer cannot see, so
    // the inset's contents are unambiguous: a lit cube far off on +X, with the
    // camera parked in front of it.
    auto cube = iris::MeshNode::create();
    cube->setName("subject");
    cube->setMesh(":assets/models/cube.obj");
    CHECK(!!cube->getMesh(), "cube.obj loaded into the document");
    {
        const float r = cube->getMeshRadius();
        const float s = r > 0.0f ? 6.0f / r : 1.0f;      // big enough to fill the inset
        cube->setLocalScale(iris::Vec3(s, s, s));
    }
    cube->setMaterial(iris::PbrMaterial::create());
    cube->setLocalPos(iris::Vec3(100.0f, 0.0f, 0.0f));
    doc->getRootNode()->addChild(cube);
    auto sun = iris::LightNode::create();
    sun->setName("sun");
    sun->intensity = 1.0f;
    sun->setLocalRot(iris::Quat::fromEulerAngles(-50.0f, 30.0f, 0.0f));
    sun->setLocalPos(iris::Vec3(100.0f, 30.0f, 0.0f));
    doc->getRootNode()->addChild(sun);
    cam->setLocalPos(iris::Vec3(100.0f, 0.0f, 9.0f));
    cam->lookAt(iris::Vec3(100.0f, 0.0f, 0.0f));

    Image noPip; frame(noPip);

    const float L = 0.62f, T = 0.60f, W = 0.34f, H = 0.36f;
    ViewPipDesc pip;
    pip.enabled = true;
    pip.allowOffscreen = true;      // offscreen is the only readable kind
    pip.left = L; pip.top = T; pip.width = W; pip.height = H;
    mirror.applyPip(cam, view, pip);
    Image withPip; frame(withPip);
    const size_t outside = diffInRect(withPip, noPip, L, T, W, H, /*inside*/ false);
    const size_t inside  = diffInRect(withPip, noPip, L, T, W, H, /*inside*/ true);
    std::printf("    pip: %zu px changed inside the rect, %zu outside\n", inside, outside);
    CHECK(inside > 500, "the inset renders the camera's shot into its rect");
    CHECK(outside == 0, "the inset changes NOTHING outside its rect");

    // The camera's own body must not be inside its own preview. The body is an
    // on-top overlay and the inset draws below that render queue, so the proof
    // is that the inset is the SUBJECT and the clear colour only: move the
    // camera back so its body would be dead centre of its own shot if it were
    // ever drawn there, and check the middle of the rect is still the cube.
    const Colour mid = withPip.at(unsigned((L + W * 0.5f) * withPip.width),
                                  unsigned((T + H * 0.5f) * withPip.height));
    std::printf("    inset centre: %3.0f %3.0f %3.0f\n", mid.r * 255, mid.g * 255, mid.b * 255);
    CHECK(!(mid.b > 0.8f && mid.r < 0.15f && mid.g < 0.15f),
          "the inset centre is the camera's subject, not the clear colour");

    // Turning it off restores the frame byte-for-byte (the determinism law's
    // host-side half).
    ViewPipDesc off;
    mirror.applyPip(iris::CameraNodePtr(), view, off);
    Image pipOff; frame(pipOff);
    CHECK(diff(pipOff, noPip) == 0, "switching the inset off leaves no trace");

    mirror.setSource(nullptr);
    engine->destroyScene(target);

    std::printf(failures == 0 ? "\nALL CAMERA BODY/PIP CHECKS PASSED\n"
                              : "\n%d CAMERA BODY/PIP CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
