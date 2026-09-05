// sockets.render — a camera on an ANIMATED bone, in pixels (CAMERAS_SPEC §5).
//
// The document suite proves the socket maths against matrices written by hand.
// This one proves the thing the maths exists for, through the real chain:
//
//     engine SkeletonInstance (the pose)  ->  SceneMirror::boneWorldTransforms
//     ->  iris::SocketResolver  ->  the attached CameraNode's transform
//     ->  SceneMirror::applyCamera  ->  a rendered frame
//
// and it asserts on the FRAME, because every link in that chain can be subtly
// wrong (a missing mesh-node transform, bone-parent-local FK, the one-frame
// read-back lag) in a way that still produces plausible-looking numbers.
//
// The setup: the two-bone arm every skeletal suite uses, with a clip that
// swings jointTip -90 degrees about Z over one second. A `head` socket sits on
// jointTip, offset one unit along the BONE's +Y, and a camera rides it looking
// down its own -Z at a bright red marker four units away:
//
//   t = 0   bone unrotated -> socket at (0, 2, 0) -> the marker is dead centre
//   t = 1   bone at -90    -> socket at (1, 1, 0) -> the marker is off to the
//                             side, so the centre pixel is background
//
// Both frames are also compared to a control run with the camera DETACHED, in
// which nothing moves and the two frames are identical — otherwise "the frames
// differ" would be satisfied by any per-frame noise at all.
//
// Runs with QT_QPA_PLATFORM=offscreen and a reachable DISPLAY (Vulkan).

#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/socket.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

#include "../skeletal/armrig.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static Colour centre(const Image &i) { return i.at(i.width / 2, i.height / 2); }
static bool isRed(const Colour &c) { return c.r > 0.5f && c.g < 0.3f && c.b < 0.3f; }
static void show(const char *tag, const Image &i)
{
    const Colour c = centre(i);
    std::printf("    %-38s centre %3.0f %3.0f %3.0f\n", tag, c.r * 255, c.g * 255, c.b * 255);
}
static double imageDiff(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) return 1.0;
    double sum = 0.0;
    for (size_t i = 0; i < a.rgba.size(); ++i)
        sum += std::fabs(double(a.rgba[i]) - double(b.rgba[i]));
    return sum / double(a.rgba.size() ? a.rgba.size() : 1) / 255.0;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_socket_render-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("sockets", 96, 96, Colour(0, 0, 0.4f));
    Scene *target = engine->createScene("sockets");
    CHECK(view && target, "offscreen view + engine scene");
    if (!view || !target) return 1;
    view->setScene(target);
    // Flat white ambient: the assertion is WHERE the camera is looking, so the
    // less the lighting contributes the fewer ways this can fail for the wrong
    // reason.
    target->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));

    // ---- the document ----------------------------------------------------
    auto doc = iris::Scene::create();
    auto arm = armrig::buildArmNode(armrig::buildArmMesh(), "arm");
    doc->getRootNode()->addChild(arm);
    auto clip = armrig::buildSwingClip(-90.0f, 1.0f);
    clip->setName("Swing");
    clip->setLooping(false);
    arm->addAnimation(clip);
    arm->setAnimation(clip);
    arm->applyDefaultPose();

    // The marker the socketed camera is aimed at: an unmistakable red cube,
    // four units down -Z from the socket's t=0 position.
    auto marker = iris::MeshNode::create();
    marker->setName("marker");
    marker->setMesh(":assets/models/cube.obj");
    CHECK(!!marker->getMesh(), "the marker cube loaded");
    {
        const float r = marker->getMeshRadius();
        const float s = r > 0.0f ? 1.0f / r : 1.0f;
        marker->setLocalScale(iris::Vec3(s, s, s));
    }
    {
        auto mat = iris::PbrMaterial::create();
        mat->setValue("baseColor", QColor(0, 0, 0));
        mat->setValue("emissiveColor", QColor(255, 0, 0));
        mat->setValue("emissiveIntensity", 1.0f);
        marker->setMaterial(mat);
    }
    marker->setLocalPos(iris::Vec3(0, 2, -4));
    doc->getRootNode()->addChild(marker);

    // The rider.
    auto cam = iris::CameraNode::create();
    cam->setName("headcam");
    cam->nearClip = 0.05f;
    doc->getRootNode()->addChild(cam);

    SceneMirror mirror(target);
    mirror.setSource(doc);

    // One STEP = set the clock, then sync+render TWICE. The pose read-back is
    // one frame behind by construction (socket.h): the first pair puts the new
    // pose in the engine, the second resolves the socket onto it. A production
    // frame has the same one-frame lag and nobody can see it at 60 Hz; a test
    // that asserts an exact position has to close it explicitly.
    Image img;
    const auto step = [&](float t, Image &out) {
        doc->updateSceneAnimation(t);
        for (int i = 0; i < 2; ++i) {
            mirror.sync();
            engine->renderOneFrame();
        }
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(out);
    };

    // ---- 1. no socket yet: the camera is where the document put it --------
    step(0.0f, img);
    CHECK(!cam->isSocketAttached(), "the camera rides nothing to start with");
    const iris::Vec3 unattached = cam->getGlobalPosition();
    CHECK(unattached.lengthSquared() < 1e-6f, "…and sits at the origin");

    // ---- 2. attach it to the head socket ---------------------------------
    iris::Socket socket;
    socket.name = "head";
    socket.boneName = "jointTip";
    socket.position = iris::Vec3(0, 1, 0);        // one unit along the BONE
    QString error;
    CHECK(arm->addSocket(socket, &error), "the head socket goes onto jointTip");
    if (!error.isEmpty()) std::printf("    %s\n", qUtf8Printable(error));
    CHECK(doc->attachToSocket(cam, arm->getGUID(), "head", &error), "the camera rides it");

    Image atRest;
    step(0.0f, atRest);
    show("t=0, camera on the head socket", atRest);
    const iris::Vec3 rest = cam->getGlobalPosition();
    std::printf("    socket world position t=0: %.3f %.3f %.3f\n",
                double(rest.x()), double(rest.y()), double(rest.z()));
    CHECK(std::fabs(rest.x()) < 1e-2f && std::fabs(rest.y() - 2.0f) < 1e-2f
              && std::fabs(rest.z()) < 1e-2f,
          "at rest the socket is at (0, 2, 0) — the bone's bind position plus the offset, "
          "resolved out of the ENGINE's pose");
    CHECK(isRed(centre(atRest)),
          "…and the shot through it is the red marker, dead centre");

    // ---- 3. play the clip: the bone moves, so the camera moves ------------
    Image atEnd;
    step(1.0f, atEnd);
    show("t=1, the bone has swung -90 degrees", atEnd);
    const iris::Vec3 swung = cam->getGlobalPosition();
    std::printf("    socket world position t=1: %.3f %.3f %.3f\n",
                double(swung.x()), double(swung.y()), double(swung.z()));
    CHECK(std::fabs(swung.x() - 1.0f) < 5e-2f && std::fabs(swung.y() - 1.0f) < 5e-2f,
          "the socket followed the ANIMATED bone to (1, 1, 0) — a -90 degree turn about Z "
          "maps the bone's local +Y onto world +X");
    CHECK((swung - rest).length() > 0.5f, "the camera really moved between the two frames");
    CHECK(!isRed(centre(atEnd)),
          "…and the shot through it is a DIFFERENT shot: the marker is no longer centred");
    const double moved = imageDiff(atRest, atEnd);
    std::printf("    mean per-channel difference between the two shots: %.4f\n", moved);
    CHECK(moved > 0.02,
          "the two frames differ — an agent looking through an avatar's head sees the rig move");

    // ---- 4. the control: detached, the same two times are the same shot ---
    // Without this, "the frames differ" is satisfied by any per-frame noise.
    CHECK(doc->detachFromSocket(cam), "detach");
    cam->setLocalPos(iris::Vec3(0, 2, 0));
    cam->setLocalRot(iris::Quat());
    Image ctrlA, ctrlB;
    step(0.0f, ctrlA);
    step(1.0f, ctrlB);
    const double still = imageDiff(ctrlA, ctrlB);
    std::printf("    detached, same two times: %.4f\n", still);
    CHECK(still < 1e-4,
          "with the camera DETACHED the same two clip times render the SAME pixels from it — "
          "so the difference above is the socket and not frame noise");
    CHECK(std::fabs(cam->getGlobalPosition().y() - 2.0f) < 1e-4f,
          "a detached camera keeps the transform it was given (nothing drives it any more)");

    // ---- 5. teardown, in the documented order ----------------------------
    mirror.setSource(iris::ScenePtr());
    engine->destroyView(view);
    engine->destroyScene(target);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
