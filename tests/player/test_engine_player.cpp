// The player on the engine, headless: a document with a cube and a scene camera
// goes through EnginePlayerScene (player Scene + SceneMirror + PlayBack) into an
// offscreen View. The cube must be visible from the scene camera; then playing
// the scene with a dynamic physics body must move it (transform) and change the
// picture (pixels); stopping must restore the transform.
#include <QGuiApplication>
#include <QColor>
#include <QVector3D>
#include <cmath>
#include <cstdio>
#include <string>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "jahshaka/engine/Engine.h"
#include "player/engineplayerscene.h"
#include "player/playback.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const Colour kBackground(0, 0, 1);
static bool isBackground(const Colour &c) { return c.b > 0.8f && c.r < 0.15f && c.g < 0.15f; }
static bool isMaterial(const Colour &c) { return c.r > 0.12f && c.r > c.b * 1.5f && c.r > c.g * 1.5f; }
static Colour at(const Image &i, int x, int y) { return i.at(unsigned(x), unsigned(y)); }
static void show(const char *tag, const Image &i, int x, int y)
{
    const Colour c = at(i, x, y), k = at(i, 2, 2);
    std::printf("    %-34s (%d,%d) %3.0f %3.0f %3.0f   corner %3.0f %3.0f %3.0f\n", tag, x, y,
                c.r*255, c.g*255, c.b*255, k.r*255, k.g*255, k.b*255);
}
static int maxAbsDiff(const Image &a, const Image &b)
{
    float d = 0;
    for (unsigned y = 0; y < a.height; ++y) for (unsigned x = 0; x < a.width; ++x) {
        const Colour p = a.at(x, y), q = b.at(x, y);
        d = std::max({d, std::fabs(p.r - q.r), std::fabs(p.g - q.g), std::fabs(p.b - q.b)});
    }
    return int(d * 255);
}
// Renders `frames` engine frames after one player step each, returns the last readback.
static Image render(EnginePlayerScene &player, Engine &engine, View *view, int frames, float dt)
{
    for (int i = 0; i < frames; ++i) {
        player.step(dt, int(view->width()), int(view->height()));
        engine.renderOneFrame();
    }
    Image img;
    view->readPixels(img);
    return img;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_engine_player-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // The editor viewport exists first in the app; the player is the SECOND view
    // and the second scene on the same engine.
    const int W = 128, H = 128;
    View *editorView = engine->createOffscreenView("editor", 64, 64, Colour(0, 0, 0));
    Scene *editorScene = engine->createScene("editor");
    editorView->setScene(editorScene);
    View *view = engine->createOffscreenView("player", W, H, kBackground);
    CHECK(view != nullptr, "offscreen player view");
    if (!view) return 1;

    // ---- the document: a lit cube 2 units above the origin, a scene camera looking at it ----
    auto doc = iris::Scene::create();
    doc->setSkyColor(QColor(0, 0, 255));     // flat sky -> the view's clear colour
    doc->setAmbientColor(QColor(90, 90, 90));
    auto light = iris::LightNode::create();
    light->setLightType(iris::LightType::Directional);
    light->setName("sun");
    light->color = QColor(255, 255, 255);
    light->intensity = 1.0f;
    light->setLocalRot(QQuaternion::fromEulerAngles(-60, 30, 0));
    doc->getRootNode()->addChild(light);

    auto cube = iris::MeshNode::create();
    cube->setName("cube");
    cube->setMesh(":assets/models/cube.obj");
    auto orange = iris::DefaultMaterial::create();
    orange->setDiffuseColor(QColor(204, 76, 51));
    cube->setMaterial(orange);
    CHECK(!!cube->getMesh(), "cube.obj loaded into the document (no GL)");
    const float r = cube->getMeshRadius();
    const float s = r > 0.0f ? 1.0f / r : 1.0f;
    cube->setLocalScale(QVector3D(s, s, s));
    const QVector3D startPos(0, 2, 0);
    cube->setLocalPos(startPos);
    // A dynamic rigid body: gravity pulls it down once the scene plays.
    cube->isPhysicsBody = true;
    cube->physicsProperty.type = iris::PhysicsType::RigidBody;
    cube->physicsProperty.shape = iris::PhysicsCollisionShape::Cube;
    cube->physicsProperty.isStatic = false;
    cube->physicsProperty.objectMass = 1.0f;
    doc->getRootNode()->addChild(cube);

    auto camera = iris::CameraNode::create();
    camera->setLocalPos(QVector3D(0, 2, 7));
    camera->lookAt(startPos);
    camera->angle = 45.0f;
    camera->nearClip = 0.1f;
    camera->farClip = 100.0f;
    doc->update(0);

    {
        EnginePlayerScene player(engine);
        CHECK(player.attach(view), "player scene attached to the view");
        CHECK(view->scene() == player.engineScene(), "the view renders the PLAYER scene");
        CHECK(player.engineScene() != editorScene, "the player scene is a second scene on the engine");

        player.setDocument(doc, camera);
        CHECK(doc->getCamera() == camera, "the document's scene camera is the play camera");
        CHECK(player.camera() == camera, "EnginePlayerScene drives the view from it");

        // ---- 1. visible from the scene camera, at rest ----
        player.begin();
        Image rest = render(player, *engine, view, 3, 1.0f / 60.0f);
        CHECK(rest.width == unsigned(W) && rest.height == unsigned(H), "readback has the view's size");
        show("cube at rest, scene camera", rest, W / 2, H / 2);
        CHECK(isMaterial(at(rest, W / 2, H / 2)), "cube is visible at the centre from the scene camera");
        CHECK(isBackground(at(rest, 2, 2)), "corner is the document's sky colour");
        CHECK(isBackground(at(rest, W / 2, H - 4)), "bottom of the view is sky (nothing there yet)");
        CHECK(!player.isPlaying(), "not playing before play()");
        const QVector3D before = cube->getLocalPos();
        CHECK((before - startPos).length() < 1e-4f, "stepping while stopped does not move the cube");

        // ---- 2. play: physics moves the body, the pixels follow ----
        player.play();
        CHECK(player.isPlaying(), "play() starts the scene");
        Image falling = render(player, *engine, view, 60, 1.0f / 60.0f);   // ~1 s of simulation
        const QVector3D after = cube->getLocalPos();
        std::printf("    cube y: %.3f -> %.3f after 60 frames\n", double(before.y()), double(after.y()));
        CHECK(after.y() < before.y() - 0.5f, "physics body fell (transform changed)");
        show("cube after 60 frames", falling, W / 2, H / 2);
        const int diff = maxAbsDiff(rest, falling);
        std::printf("    max |rest - falling| = %d\n", diff);
        CHECK(diff > 40, "the picture changed while playing (pixels)");
        CHECK(isBackground(at(falling, W / 2, 4)), "top of the view is sky: the cube has left the centre going down");

        // ---- 3. stop: transforms restored, picture back to the first frame ----
        player.stop();
        CHECK(!player.isPlaying(), "stop() ends the scene");
        const QVector3D restored = cube->getLocalPos();
        CHECK((restored - startPos).length() < 1e-3f, "stop() restores the cube's transform");
        Image again = render(player, *engine, view, 3, 1.0f / 60.0f);
        const int back = maxAbsDiff(rest, again);
        std::printf("    max |rest - after stop| = %d\n", back);
        CHECK(back <= 8, "after stop the picture matches the first frame");
        CHECK(isMaterial(at(again, W / 2, H / 2)), "cube is back at the centre");

        // ---- 4. end(): the scene camera is restored to what begin() saw ----
        camera->setLocalPos(QVector3D(5, 5, 5));
        player.end();
        CHECK((camera->getLocalPos() - QVector3D(0, 2, 7)).length() < 1e-4f, "end() restores the scene camera transform");

        player.release();
        CHECK(view->scene() == nullptr, "release() detaches the player scene from the view");
    }

    engine->destroyView(view);
    engine->destroyView(editorView);
    engine->destroyScene(editorScene);
    engine.reset();
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
