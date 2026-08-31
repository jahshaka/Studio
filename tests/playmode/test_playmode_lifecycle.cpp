// Play-mode lifecycle, document-level and GL-free: how the physics world is
// built when a scene starts playing, how it is torn down, what removing a
// viewer does to the document, and PlayBack's play/pause/resume/stop machine.
//
// Every check here is a regression guard for a verified defect:
//   1. initializePhysicsWorldFromScene read isActiveCharacterController out of
//      EVERY node through an unguarded staticCast<ViewerNode> — a heap
//      over-read on smaller node types (ASan aborts on the Empty node below)
//      that could spawn a character controller on a mesh or a light.
//   2. pause only cleared the viewport's flag, so resuming re-entered
//      playScene(): the animation clock reset, the mid-play pose was saved over
//      the pre-play originals, and a second copy of every rigid body and
//      character controller was added to the world.
//   3. Scene::removeNode's viewer branch walked its iterator to constEnd() and
//      dereferenced it whenever a viewer other than the active one was removed.
//   4. removeCharacterControllerFromWorld deleted the controller while bullet
//      still held its ghost object in the broadphase and its action in the
//      world's action list.
#include <QGuiApplication>
#include <QQuaternion>
#include <QVector3D>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/charactercontroller.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "player/playback.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static iris::MeshNodePtr makeBody(const QVector3D &pos, float mass)
{
    auto node = iris::MeshNode::create();
    node->setName("body");
    node->setLocalPos(pos);
    node->setLocalScale(QVector3D(2, 2, 2));
    node->setLocalRot(QQuaternion::fromEulerAngles(0, 30, 0));
    node->isPhysicsBody = true;
    node->physicsProperty.type = iris::PhysicsType::RigidBody;
    // A sphere needs no mesh data, so this suite stays free of asset loading.
    node->physicsProperty.shape = iris::PhysicsCollisionShape::Sphere;
    node->physicsProperty.isStatic = false;
    node->physicsProperty.objectMass = mass;
    return node;
}

static bool sameTransform(const iris::SceneNodePtr &node, const QVector3D &pos,
                          const QQuaternion &rot, const QVector3D &scale)
{
    return (node->getLocalPos() - pos).length() < 1e-4f &&
           (node->getLocalRot() - rot).length() < 1e-4f &&
           (node->getLocalScale() - scale).length() < 1e-4f;
}

// ---------------------------------------------------------------- defect 1 --
static void testPhysicsInitIgnoresNonViewers()
{
    std::printf("\n-- a scene of plain nodes never gains a character controller --\n");
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();

    root->addChild(makeBody(QVector3D(0, 10, 0), 1.0f));
    root->addChild(iris::LightNode::create());
    // An Empty node is SMALLER than a ViewerNode: the old unguarded
    // staticCast<ViewerNode> read past the end of this allocation.
    auto empty = iris::SceneNode::create();
    empty->addChild(iris::SceneNode::create());
    root->addChild(empty);
    root->addChild(iris::CameraNode::create());
    root->addChild(iris::MeshNode::create());

    auto env = scene->getPhysicsEnvironment();
    env->initializePhysicsWorldFromScene(root);

    CHECK(env->characterControllers.isEmpty(),
          "mesh/light/camera/empty nodes create no character controller");
    CHECK(env->getActiveCharacterController() == nullptr,
          "no active character controller for a viewer-less scene");
    CHECK(env->hashBodies.size() == 1, "exactly the one rigid body was added");
    CHECK(env->getWorld()->getNumCollisionObjects() == 1,
          "the world holds exactly one collision object");
}

// ------------------------------------------------------------- defects 1+4 --
static void testCharacterControllerLifecycle()
{
    std::printf("\n-- character controllers: only flagged viewers, clean removal --\n");
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();
    root->addChild(makeBody(QVector3D(0, 10, 0), 1.0f));

    auto passive = iris::ViewerNode::create();
    passive->setActiveCharacterController(false);
    root->addChild(passive);

    auto driver = iris::ViewerNode::create();
    driver->setActiveCharacterController(true);
    root->addChild(driver);

    auto env = scene->getPhysicsEnvironment();
    env->initializePhysicsWorldFromScene(root);

    CHECK(env->characterControllers.size() == 1,
          "only the viewer with the flag set gets a controller");
    CHECK(env->characterControllers.contains(driver->getGUID()),
          "and it is that viewer's controller");
    CHECK(env->getActiveCharacterController() != nullptr, "it became the active controller");

    const int withController = env->getWorld()->getNumCollisionObjects();
    CHECK(withController == 2, "body + ghost object are both in the world");

    // Removal must mirror the registration: action list AND broadphase.
    env->removeCharacterControllerFromWorld(driver->getGUID());
    CHECK(env->getWorld()->getNumCollisionObjects() == withController - 1,
          "removal takes the ghost object out of the world");
    CHECK(env->characterControllers.isEmpty(), "the controller is gone from the hash");
    CHECK(env->getActiveCharacterController() == nullptr,
          "the active controller pointer does not dangle");

    // Stepping now walks the world's action list and broadphase: with the freed
    // controller still registered this is a use-after-free (ASan catches it).
    env->simulatePhysics();
    for (int i = 0; i < 30; ++i) env->stepSimulation(1.0f / 60.0f);
    CHECK(true, "the world steps cleanly after a controller was removed");

    // Teardown with a live controller must not double-free the ghost object
    // (destroyPhysicsWorld deletes every collision object in the array).
    env->addCharacterControllerToWorldUsingNode(driver);
    CHECK(env->characterControllers.size() == 1, "controller re-added for the teardown check");
    env->restartPhysics();
    CHECK(env->characterControllers.isEmpty(), "restartPhysics drops the controllers with the world");
    CHECK(env->getActiveCharacterController() == nullptr, "and clears the active one");
}

// ---------------------------------------------------------------- defect 3 --
static void testViewerRemoval()
{
    std::printf("\n-- removing viewers from the document --\n");
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();

    auto v1 = iris::ViewerNode::create();
    auto v2 = iris::ViewerNode::create();
    auto v3 = iris::ViewerNode::create();
    root->addChild(v1);
    root->addChild(v2);
    root->addChild(v3);

    CHECK(scene->viewers.count() == 3, "three viewers in the document");
    CHECK(scene->getActiveVrViewer() == v1, "the first viewer added is the active one");

    // The past-the-end dereference: removing a viewer that is NOT the active one.
    scene->removeNode(v3);
    CHECK(scene->viewers.count() == 2, "the removed viewer left the viewer list");
    CHECK(scene->getActiveVrViewer() == v1, "removing a non-active viewer leaves the active one alone");

    scene->removeNode(v2);
    CHECK(scene->viewers.count() == 1, "second non-active viewer removed");
    CHECK(scene->getActiveVrViewer() == v1, "the active viewer is still the active one");

    // Removing the ACTIVE viewer promotes a remaining one.
    auto v4 = iris::ViewerNode::create();
    root->addChild(v4);
    CHECK(scene->getActiveVrViewer() == v1, "a newly added viewer does not steal the active slot");
    scene->removeNode(v1);
    CHECK(scene->viewers.count() == 1, "the active viewer left the viewer list");
    CHECK(scene->getActiveVrViewer() == v4, "the remaining viewer becomes active");

    scene->removeNode(v4);
    CHECK(scene->viewers.isEmpty(), "no viewers left");
    CHECK(!scene->getActiveVrViewer(), "and no active viewer either");
}

// ---------------------------------------------------------------- defect 2 --
static void testPlayPauseResumeStop()
{
    std::printf("\n-- play / pause / resume / stop --\n");
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();

    const QVector3D startPos(0, 10, 0);
    auto body = makeBody(startPos, 1.0f);
    root->addChild(body);
    const QQuaternion startRot = body->getLocalRot();
    const QVector3D startScale = body->getLocalScale();

    auto camera = iris::CameraNode::create();
    camera->setLocalPos(QVector3D(0, 2, 10));
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
    const QVector3D fell = body->getLocalPos();
    CHECK(fell.y() < startPos.y() - 0.05f, "the body falls while playing");

    // ---- pause: frozen, and nothing added ----
    playback.pause();
    CHECK(playback.isScenePaused(), "pause() pauses");
    CHECK(playback.isScenePlaying(), "a paused scene is still IN play mode");
    const QVector3D atPause = body->getLocalPos();
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
    const QVector3D beforeRePlay = body->getLocalPos();
    playback.playScene();
    CHECK(objects() == playingObjects, "playScene() while already playing changes nothing");
    CHECK((body->getLocalPos() - beforeRePlay).length() < 1e-6f, "and does not move the document");

    // ---- stop: the ORIGINAL pre-play transform comes back ----
    playback.stopScene();
    CHECK(!playback.isScenePlaying() && !playback.isScenePaused(), "stopScene() stops");
    std::printf("    body y: start %.3f, at pause %.3f, restored %.3f\n",
                double(startPos.y()), double(atPause.y()), double(body->getLocalPos().y()));
    CHECK(sameTransform(body, startPos, startRot, startScale),
          "stop restores the ORIGINAL pre-play transform (not the pose at the last resume)");

    // ---- and the whole cycle runs again from a clean world ----
    playback.playScene();
    CHECK(objects() == playingObjects, "replaying builds the same world, not a bigger one");
    step(10);
    playback.pause();
    playback.stopScene();
    CHECK(sameTransform(body, startPos, startRot, startScale), "stop from PAUSED also restores the original");
    CHECK(!playback.isScenePaused(), "and clears the paused state");
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    testPhysicsInitIgnoresNonViewers();
    testCharacterControllerLifecycle();
    testViewerRemoval();
    testPlayPauseResumeStop();

    std::printf("\n%s\n", failures == 0 ? "play-mode lifecycle: all checks passed"
                                        : "play-mode lifecycle: FAILURES");
    return failures == 0 ? 0 : 1;
}
