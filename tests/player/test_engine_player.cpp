// The player on the engine, headless: a document with a cube and a scene camera
// goes through EnginePlayerScene (player Scene + SceneMirror + PlayBack) into an
// offscreen View. The cube must be visible from the scene camera; then playing
// the scene with a dynamic physics body must move it (transform) and change the
// picture (pixels); stopping must restore the transform.
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QColor>
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
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/core/viewport.h"
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


// ---------------------------------------------------------------------------
// MIGRATED from the deleted `player.lifecycle` suite (AVATAR_LOCOMOTION_SPEC
// Stage 0). That suite had five cases; three were about the ViewerNode /
// CharacterController machinery Stage 0 removes and died with it. These two are
// NOT — they guard two separately verified defects that have nothing to do with
// viewers — so they moved here, to the other suite that links PlayBack and the
// physics Environment, rather than being thrown away with the file.
// ---------------------------------------------------------------------------

static iris::MeshNodePtr lifecycleBody(const iris::Vec3 &pos, float mass)
{
    auto node = iris::MeshNode::create();
    node->setName("body");
    node->setLocalPos(pos);
    node->setLocalScale(iris::Vec3(2, 2, 2));
    node->setLocalRot(iris::Quat::fromEulerAngles(0, 30, 0));
    node->isPhysicsBody = true;
    node->physicsProperty.type = iris::PhysicsType::RigidBody;
    // A sphere needs no mesh data, so this case stays free of asset loading.
    node->physicsProperty.shape = iris::PhysicsCollisionShape::Sphere;
    node->physicsProperty.isStatic = false;
    node->physicsProperty.objectMass = mass;
    return node;
}

/// A mesh node carrying real triangle data, so the mesh-backed collision shapes
/// (ConvexHull / TriangleMesh / Compound) can be built without loading an asset.
static iris::MeshNodePtr lifecycleMeshBody(iris::PhysicsCollisionShape shape, int triangles = 64)
{
    auto mesh = iris::Mesh::create();
    mesh->triMesh = new iris::TriMesh();
    for (int i = 0; i < triangles; ++i) {
        const float t = float(i);
        mesh->triMesh->addTriangle(iris::Vec3(t, 0, 0), iris::Vec3(0, t, 0), iris::Vec3(0, 0, t));
    }

    auto node = iris::MeshNode::create();
    node->setName("meshbody");
    node->setMesh(mesh);
    node->setLocalPos(iris::Vec3(0, 5, 0));
    node->isPhysicsBody = true;
    node->physicsProperty.type = iris::PhysicsType::RigidBody;
    node->physicsProperty.shape = shape;
    node->physicsProperty.isStatic = false;
    node->physicsProperty.objectMass = 1.0f;
    return node;
}

static bool lifecycleSameTransform(const iris::SceneNodePtr &node, const iris::Vec3 &pos,
                                   const iris::Quat &rot, const iris::Vec3 &scale)
{
    return (node->getLocalPos() - pos).length() < 1e-4f &&
           (node->getLocalRot() - rot).length() < 1e-4f &&
           (node->getLocalScale() - scale).length() < 1e-4f;
}

// ------------------------------------------------- deep audit 2026-09, F3 --
// Every collision shape, every compound child and every btTriangleMesh built
// for a rigid body used to leak on each play/stop cycle: storeCollisionShape()
// had ZERO call sites, so destroyPhysicsWorld's "delete collision shapes" loop
// iterated an array nothing ever filled. The Environment owns them now, and
// the counts are the proof: they rise with the bodies and fall to zero with
// the world, cycle after cycle, without accumulating.
static void testCollisionShapeOwnership()
{
    std::printf("\n-- collision shapes are owned by the world, and die with it --\n");
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();

    root->addChild(lifecycleBody(iris::Vec3(0, 10, 0), 1.0f));                     // sphere: 1 shape
    root->addChild(lifecycleMeshBody(iris::PhysicsCollisionShape::ConvexHull));   // 1 shape
    root->addChild(lifecycleMeshBody(iris::PhysicsCollisionShape::TriangleMesh)); // 1 shape + 1 interface

    // A compound whose children are mesh nodes: 1 compound + 3 children, and
    // one triangle-mesh interface behind every one of them.
    auto compound = lifecycleMeshBody(iris::PhysicsCollisionShape::Compound);
    for (int i = 0; i < 2; ++i) {
        auto part = lifecycleMeshBody(iris::PhysicsCollisionShape::TriangleMesh);
        // Parts of a compound are geometry, not bodies of their own — the
        // recursion in initializePhysicsWorldFromScene would give each its own
        // rigid body otherwise.
        part->isPhysicsBody = false;
        compound->addChild(part);
    }
    root->addChild(compound);

    auto env = scene->getPhysicsEnvironment();
    env->initializePhysicsWorldFromScene(root);

    const int shapes = env->ownedShapeCount();
    const int interfaces = env->ownedMeshInterfaceCount();
    std::printf("    after build: %d shapes, %d mesh interfaces, %d bodies\n",
                shapes, interfaces, env->hashBodies.size());
    CHECK(env->hashBodies.size() == 4, "four rigid bodies were built");
    CHECK(shapes >= 4, "the world owns at least one collision shape per body");
    CHECK(shapes > 4, "...and the compound's child shapes on top of that");
    CHECK(interfaces >= 3, "the triangle-mesh interfaces are owned too");

    // ---- the cycle. Rebuilding must not accumulate. ----
    for (int cycle = 0; cycle < 5; ++cycle) {
        env->restartPhysics();
        CHECK(env->ownedShapeCount() == 0 && env->ownedMeshInterfaceCount() == 0,
              cycle == 0 ? "restartPhysics released every shape and interface" : "...and again");
        env->initializePhysicsWorldFromScene(root);
        if (env->ownedShapeCount() != shapes || env->ownedMeshInterfaceCount() != interfaces) {
            std::printf("FAIL: cycle %d rebuilt %d shapes / %d interfaces (expected %d / %d)\n",
                        cycle, env->ownedShapeCount(), env->ownedMeshInterfaceCount(),
                        shapes, interfaces);
            ++failures;
        }
    }
    CHECK(env->ownedShapeCount() == shapes && env->ownedMeshInterfaceCount() == interfaces,
          "five play/stop cycles rebuild the SAME number of shapes — no unbounded growth");

    // Stepping the rebuilt world proves the shapes the bodies point at are the
    // live ones (a recycled address from a freed shape would fault here).
    env->simulatePhysics();
    for (int i = 0; i < 30; ++i) env->stepSimulation(1.0f / 60.0f);
    CHECK(true, "the rebuilt world steps cleanly");

    env->destroyPhysicsWorld();
    CHECK(env->ownedShapeCount() == 0, "destroyPhysicsWorld leaves no shape behind");
    CHECK(env->ownedMeshInterfaceCount() == 0, "...and no mesh interface either");
    env->createPhysicsWorld();   // the Environment destructor expects a world
}

// --------------------------------------------------------------------------
// pause only cleared the viewport's flag, so resuming re-entered playScene():
// the animation clock reset, the mid-play pose was saved over the pre-play
// originals, and a second copy of every rigid body was added to the world.
static void testPlayPauseResumeStop()
{
    std::printf("\n-- play / pause / resume / stop --\n");
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();

    const iris::Vec3 startPos(0, 10, 0);
    auto body = lifecycleBody(startPos, 1.0f);
    root->addChild(body);
    const iris::Quat startRot = body->getLocalRot();
    const iris::Vec3 startScale = body->getLocalScale();

    auto camera = iris::CameraNode::create();
    camera->setLocalPos(iris::Vec3(0, 2, 10));
    scene->setCamera(camera);
    scene->update(0);

    PlayBack playback;
    playback.init();
    playback.setScene(scene);

    iris::Viewport vp;
    vp.width = 64;
    vp.height = 64;
    vp.pixelRatioScale = 1.0f;
    auto step = [&](int frames) { for (int i = 0; i < frames; ++i) playback.update(vp, 1.0f / 60.0f); };
    auto objects = [&] { return scene->getPhysicsEnvironment()->getWorld()->getNumCollisionObjects(); };

    CHECK(!playback.isScenePlaying() && !playback.isScenePaused(), "starts stopped");

    playback.playScene();
    CHECK(playback.isScenePlaying(), "playScene() plays");
    CHECK(!playback.isScenePaused(), "and is not paused");
    const int playingObjects = objects();
    const int playingBodies = scene->getPhysicsEnvironment()->hashBodies.size();
    CHECK(playingBodies == 1, "one rigid body in the played world");

    step(30);
    const iris::Vec3 fell = body->getLocalPos();
    CHECK(fell.y() < startPos.y() - 0.05f, "the body falls while playing");

    // ---- pause: frozen, and nothing added ----
    playback.pause();
    CHECK(playback.isScenePaused(), "pause() pauses");
    CHECK(playback.isScenePlaying(), "a paused scene is still IN play mode");
    const iris::Vec3 atPause = body->getLocalPos();
    step(30);
    CHECK((body->getLocalPos() - atPause).length() < 1e-6f, "a paused scene is frozen");
    CHECK(objects() == playingObjects, "pausing adds nothing to the physics world");

    // ---- resume: through the same entry point the viewport uses ----
    playback.playScene();
    CHECK(!playback.isScenePaused(), "playScene() on a paused scene resumes it");
    CHECK(playback.isScenePlaying(), "still playing");
    CHECK(objects() == playingObjects, "resume does NOT re-initialize the physics world");
    CHECK(scene->getPhysicsEnvironment()->hashBodies.size() == playingBodies,
          "resume adds no duplicate rigid body");
    step(30);
    CHECK(body->getLocalPos().y() < atPause.y() - 0.05f, "resume continues the simulation");

    // ---- three more cycles: still no duplicates ----
    for (int cycle = 0; cycle < 3; ++cycle) {
        playback.pause();
        playback.playScene();
        step(5);
    }
    CHECK(objects() == playingObjects, "three pause/resume cycles duplicate nothing in the world");
    CHECK(scene->getPhysicsEnvironment()->hashBodies.size() == playingBodies,
          "three pause/resume cycles duplicate no rigid body");

    // A stray second playScene() while already playing must be a no-op too.
    const iris::Vec3 beforeRePlay = body->getLocalPos();
    playback.playScene();
    CHECK(objects() == playingObjects, "playScene() while already playing changes nothing");
    CHECK((body->getLocalPos() - beforeRePlay).length() < 1e-6f, "and does not move the document");

    // ---- stop: the ORIGINAL pre-play transform comes back ----
    playback.stopScene();
    CHECK(!playback.isScenePlaying() && !playback.isScenePaused(), "stopScene() stops");
    std::printf("    body y: start %.3f, at pause %.3f, restored %.3f\n",
                double(startPos.y()), double(atPause.y()), double(body->getLocalPos().y()));
    CHECK(lifecycleSameTransform(body, startPos, startRot, startScale),
          "stop restores the ORIGINAL pre-play transform (not the pose at the last resume)");

    // ---- and the whole cycle runs again from a clean world ----
    playback.playScene();
    CHECK(objects() == playingObjects, "replaying builds the same world, not a bigger one");
    step(10);
    playback.pause();
    playback.stopScene();
    CHECK(lifecycleSameTransform(body, startPos, startRot, startScale),
          "stop from PAUSED also restores the original");
    CHECK(!playback.isScenePaused(), "and clears the paused state");
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
    // The DOCUMENT's ambient is what lights this scene now: the player pushes
    // applyEnvironment, so the engine scene's hardcoded startup hemisphere is
    // overwritten on the first step (it used to survive, because the player
    // never pushed the world settings — the editor/player parity defect). 90
    // grey, the value this test used while the hardcoded hemisphere was in
    // force, leaves the cube at 30/255 red: still plainly the material, but
    // under isMaterial's floor. 140 restores the brightness the assertions were
    // written against, and is now an honest statement about the document.
    doc->setAmbientColor(QColor(140, 140, 140));
    auto light = iris::LightNode::create();
    light->setLightType(iris::LightType::Directional);
    light->setName("sun");
    light->color = QColor(255, 255, 255);
    light->intensity = 1.0f;
    light->setLocalRot(iris::Quat::fromEulerAngles(-60, 30, 0));
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
    cube->setLocalScale(iris::Vec3(s, s, s));
    const iris::Vec3 startPos(0, 2, 0);
    cube->setLocalPos(startPos);
    // A dynamic rigid body: gravity pulls it down once the scene plays.
    cube->isPhysicsBody = true;
    cube->physicsProperty.type = iris::PhysicsType::RigidBody;
    cube->physicsProperty.shape = iris::PhysicsCollisionShape::Cube;
    cube->physicsProperty.isStatic = false;
    cube->physicsProperty.objectMass = 1.0f;
    doc->getRootNode()->addChild(cube);

    auto camera = iris::CameraNode::create();
    camera->setLocalPos(iris::Vec3(0, 2, 7));
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
        const iris::Vec3 before = cube->getLocalPos();
        CHECK((before - startPos).length() < 1e-4f, "stepping while stopped does not move the cube");

        // ---- 2. play: physics moves the body, the pixels follow ----
        player.play();
        CHECK(player.isPlaying(), "play() starts the scene");
        Image falling = render(player, *engine, view, 60, 1.0f / 60.0f);   // ~1 s of simulation
        const iris::Vec3 after = cube->getLocalPos();
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
        const iris::Vec3 restored = cube->getLocalPos();
        CHECK((restored - startPos).length() < 1e-3f, "stop() restores the cube's transform");
        Image again = render(player, *engine, view, 3, 1.0f / 60.0f);
        const int back = maxAbsDiff(rest, again);
        std::printf("    max |rest - after stop| = %d\n", back);
        CHECK(back <= 8, "after stop the picture matches the first frame");
        CHECK(isMaterial(at(again, W / 2, H / 2)), "cube is back at the centre");

        // ---- 4. end(): the scene camera is restored to what begin() saw ----
        camera->setLocalPos(iris::Vec3(5, 5, 5));
        player.end();
        CHECK((camera->getLocalPos() - iris::Vec3(0, 2, 7)).length() < 1e-4f, "end() restores the scene camera transform");

        player.release();
        CHECK(view->scene() == nullptr, "release() detaches the player scene from the view");
    }

    // The two cases inherited from the deleted player.lifecycle suite. Document
    // + Bullet only, but they run here because a document node is an engine
    // node (SCENEGRAPH_SPEC D2) and this is where PlayBack is already linked.
    testCollisionShapeOwnership();
    testPlayPauseResumeStop();

    engine->destroyView(view);
    engine->destroyView(editorView);
    engine->destroyScene(editorScene);
    engine.reset();
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
