// avatar.possession — AVATAR_LOCOMOTION_SPEC Stage 3, gates P1-P6.
//
// Possession (§8.4), the camera-relative input routing it decides, the
// spring-arm follow camera (§8.5) and the `playMode` world setting — driven
// through the REAL seam: an iris::Scene, its Environment, Scene::setPlaying and
// Scene::update, i.e. exactly the chain PlayBack drives at Play. Nothing here
// reaches into the possession object to move an avatar: every gate writes the
// gameplay INPUT STATE (iris::InputSystem — the same struct `avatar.input` and
// the keyboard producer write) and reads the §5 output contract.
//
// Runs offscreen with DISPLAY unset (the document graph boots Ogre's NULL
// render system). Framework-free; non-zero exit on failure.

#include <QGuiApplication>
#include <QString>
#include <QStringList>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/input/inputmap.h"
#include "irisgl/document/input/possession.h"
#include "irisgl/document/physics/avatarmovement.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("ok:   %s\n", msg); \
         else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void section(const char *name) { printf("\n---- %s ----\n", name); }

// ---------------------------------------------------------------------------
// The log tap (gate P4). iris::irisLog goes to qInfo(), so a message handler is
// the honest way to assert "and says so in the log" rather than asserting on a
// flag that only the test can see.

static QStringList sLog;
static QtMessageHandler sPrevHandler = nullptr;

static void logTap(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    sLog.append(msg);
    if (sPrevHandler) sPrevHandler(type, ctx, msg);
}

// ---------------------------------------------------------------------------
// Fixtures — the same shape tests/avatar/test_avatar_movement.cpp builds, plus
// an editor camera (the node the follow arm writes) and the play flag.

struct Rig
{
    iris::ScenePtr scene;
    QSharedPointer<iris::Environment> env;
    iris::CameraNodePtr camera;
};

static Rig makeScene()
{
    Rig r;
    r.scene = iris::Scene::create();
    r.env = r.scene->getPhysicsEnvironment();
    // The editor camera. The viewport installs one on every real scene
    // (EngineSceneViewport::setScene -> Scene::setCamera); the follow arm
    // writes THIS node, which is why the arm needs no second driver installed
    // beside PlayBack's controller path.
    r.camera = iris::CameraNode::create();
    r.camera->setLocalPos(iris::Vec3(0, 5, 14));
    r.scene->setCamera(r.camera);
    return r;
}

static iris::MeshNodePtr addGroundPlane(Rig &r)
{
    auto node = iris::MeshNode::create();
    node->setName("Ground");
    node->isPhysicsBody = true;
    node->physicsProperty.shape = iris::PhysicsCollisionShape::Plane;
    node->physicsProperty.type = iris::PhysicsType::Static;
    node->physicsProperty.objectMass = 0.0f;
    node->physicsProperty.isStatic = true;
    r.scene->getRootNode()->addChild(node);
    return node;
}

static iris::SceneNodePtr addAvatar(Rig &r, const char *name, const iris::Vec3 &pos,
                                    const iris::SceneNodePtr &parent = iris::SceneNodePtr())
{
    auto node = iris::SceneNode::create();
    node->setName(QLatin1String(name));
    node->setAvatarComponent(iris::AvatarMovementPtr(new iris::AvatarMovement()));
    node->setLocalPos(pos);
    if (parent) parent->addChild(node);
    else r.scene->getRootNode()->addChild(node);
    return node;
}

/// A plain node — something for the document-order walk to step over.
static iris::SceneNodePtr addEmpty(Rig &r, const char *name,
                                   const iris::SceneNodePtr &parent = iris::SceneNodePtr())
{
    auto node = iris::SceneNode::create();
    node->setName(QLatin1String(name));
    if (parent) parent->addChild(node);
    else r.scene->getRootNode()->addChild(node);
    return node;
}

/// Play, exactly as PlayBack::playScene enters it: the document's play flag
/// (which is the possession EDGE) and then the physics world.
static void startPlay(Rig &r)
{
    r.scene->setPlaying(true);
    r.env->initializePhysicsWorldFromScene(r.scene->getRootNode());
    r.env->simulatePhysics();
}

static void frames(Rig &r, int n, float dt = 1.0f / 60.0f)
{
    for (int i = 0; i < n; ++i) r.scene->update(dt);
}

static float planarDistance(const iris::Vec3 &a, const iris::Vec3 &b)
{
    const float dx = a.x() - b.x();
    const float dz = a.z() - b.z();
    return std::sqrt(dx * dx + dz * dz);
}

/// Degrees between two planar vectors, ignoring Y. -1 when either is degenerate.
static float planarAngleBetween(const iris::Vec3 &a, const iris::Vec3 &b)
{
    const float la = std::sqrt(a.x() * a.x() + a.z() * a.z());
    const float lb = std::sqrt(b.x() * b.x() + b.z() * b.z());
    if (la < 1e-4f || lb < 1e-4f) return -1.0f;
    float c = (a.x() * b.x() + a.z() * b.z()) / (la * lb);
    c = c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
    return std::acos(c) * 180.0f / 3.14159265358979f;
}

/// Back to a clean gameplay input state between gates — the whole point of
/// InputSystem::resetForTest.
static void resetInput() { iris::InputSystem::instance().resetForTest(); }

// ===========================================================================
// P1 — possess / unpossess / switch: input reaches EXACTLY one avatar

static void gateP1()
{
    section("P1 — one slot: input reaches exactly one avatar, the other's moveInput is zero");
    resetInput();

    Rig r = makeScene();
    addGroundPlane(r);
    auto a = addAvatar(r, "A", iris::Vec3(-3, 2, 0));
    auto b = addAvatar(r, "B", iris::Vec3(3, 2, 0));
    startPlay(r);
    frames(r, 60);                                  // settle both onto the floor

    auto *possession = r.scene->getPossession();
    CHECK(possession != nullptr, "P1: the scene owns a possession slot");
    CHECK(possession->possessed().isNull(),
          "P1: nobody is possessed in the default 'explorer' play mode");

    CHECK(possession->possess(a), "P1: possess(A) succeeds");
    CHECK(possession->possessed() == a, "P1: possessed() is A");
    CHECK(possession->possessedGuid() == a->getGUID(), "P1: possessedGuid() is A's");

    // The stick, written the way `avatar.input({move:{x:0,y:1}})` writes it.
    iris::InputSystem::instance().setMove(0.0f, 1.0f);
    const iris::Vec3 aStart = a->getGlobalPosition();
    const iris::Vec3 bStart = b->getGlobalPosition();
    frames(r, 60);

    const float aMoved = planarDistance(a->getGlobalPosition(), aStart);
    const float bMoved = planarDistance(b->getGlobalPosition(), bStart);
    printf("      A travelled %.4f u, B travelled %.4f u\n", double(aMoved), double(bMoved));
    CHECK(aMoved > 1.5f, "P1: the possessed avatar walked");
    CHECK(bMoved < 0.001f, "P1: the UNPOSSESSED avatar did not move");
    CHECK(b->avatar()->state().moveInput.length() == 0.0f,
          "P1: the unpossessed avatar's moveInput is ZERO");
    CHECK(a->avatar()->state().moveInput.length() > 0.9f,
          "P1: the possessed avatar's moveInput is the full stick");
    // §8.4: "Unpossessed avatars keep running" — the component must still be
    // stepping (grounded, settled on the floor), not frozen.
    CHECK(b->avatar()->state().grounded,
          "P1: the unpossessed avatar is still STEPPING (grounded, idling — not frozen)");

    // The SWITCH. possess(B) implicitly unpossesses A.
    CHECK(possession->possess(b), "P1: possess(B) while A is possessed");
    CHECK(possession->possessed() == b, "P1: possessed() is now B");
    CHECK(a->avatar()->input().move.length() == 0.0f,
          "P1: the released avatar's input was CLEARED by the switch");

    const iris::Vec3 aSwitch = a->getGlobalPosition();
    const iris::Vec3 bSwitch = b->getGlobalPosition();
    frames(r, 60);
    const float aAfter = planarDistance(a->getGlobalPosition(), aSwitch);
    const float bAfter = planarDistance(b->getGlobalPosition(), bSwitch);
    printf("      after the switch: A drifted %.4f u (braking), B travelled %.4f u\n",
           double(aAfter), double(bAfter));
    CHECK(bAfter > 1.5f, "P1: after the switch the input drives B");
    CHECK(aAfter < 0.15f, "P1: A brakes to a stop rather than walking on");
    CHECK(a->avatar()->state().speed < 0.01f, "P1: A's published speed is back to zero");

    // UNPOSSESS: the slot empties and the last avatar's input goes with it.
    CHECK(possession->unpossess(), "P1: unpossess() releases B");
    CHECK(possession->possessed().isNull(), "P1: possessed() is null");
    CHECK(b->avatar()->input().move.length() == 0.0f, "P1: B's input was cleared too");
    CHECK(!possession->unpossess(),
          "P1: a SECOND unpossess() is false and harmless (the idempotence P6 depends on)");

    const iris::Vec3 bIdle = b->getGlobalPosition();
    frames(r, 60);
    CHECK(planarDistance(b->getGlobalPosition(), bIdle) < 0.15f,
          "P1: with nobody possessed the held stick moves nothing");

    // A node with no avatar component is refused rather than silently possessed.
    auto plain = addEmpty(r, "NotAnAvatar");
    CHECK(!possession->possess(plain), "P1: possessing a node with no avatar component is refused");
    CHECK(!possession->possess(iris::SceneNodePtr()), "P1: possessing null is refused");
    CHECK(possession->possessed().isNull(), "P1: ...and neither refusal armed the slot");
}

// ===========================================================================
// P2 — camera-relative input: yaw the follow camera 90 deg, the motion rotates

/// One run: possess, force the arm's yaw, hold the same stick, report where the
/// avatar went. Same code every time, so a difference can only be the yaw.
static iris::Vec3 p2Run(float yawDegrees, iris::Vec3 *cameraPos = nullptr)
{
    resetInput();
    Rig r = makeScene();
    addGroundPlane(r);
    auto avatar = addAvatar(r, "A", iris::Vec3(0, 2, 0));
    startPlay(r);
    frames(r, 60);

    auto *possession = r.scene->getPossession();
    possession->possess(avatar);
    possession->setYaw(yawDegrees);

    const iris::Vec3 start = avatar->getGlobalPosition();
    iris::InputSystem::instance().setMove(0.0f, 1.0f);   // "forward" on the stick
    frames(r, 60);
    if (cameraPos) *cameraPos = possession->cameraPosition();
    return avatar->getGlobalPosition() - start;
}

static void gateP2()
{
    section("P2 — camera-relative: yaw the camera 90 deg, the same stick walks 90 deg round");
    resetInput();

    const iris::Vec3 at0 = p2Run(0.0f);
    const iris::Vec3 at90 = p2Run(90.0f);
    const iris::Vec3 at180 = p2Run(180.0f);
    printf("      yaw   0: (%.4f, %.4f, %.4f)\n", double(at0.x()), double(at0.y()), double(at0.z()));
    printf("      yaw  90: (%.4f, %.4f, %.4f)\n", double(at90.x()), double(at90.y()), double(at90.z()));
    printf("      yaw 180: (%.4f, %.4f, %.4f)\n", double(at180.x()), double(at180.y()), double(at180.z()));

    // At yaw 0 "forward" is world -Z — the scene's forward, the axis the
    // movement component's own convention is written against.
    CHECK(at0.z() < -1.5f && std::fabs(at0.x()) < 0.05f,
          "P2: at yaw 0 the stick's forward is world -Z");

    const float a90 = planarAngleBetween(at0, at90);
    const float a180 = planarAngleBetween(at0, at180);
    printf("      angle(0, 90) = %.3f deg, angle(0, 180) = %.3f deg\n", double(a90), double(a180));
    CHECK(std::fabs(a90 - 90.0f) < 1.0f, "P2: yawing the camera 90 deg rotates the motion 90 deg");
    CHECK(std::fabs(a180 - 180.0f) < 1.0f, "P2: ...and 180 deg rotates it 180 deg");
    CHECK(std::fabs(at90.length() - at0.length()) < 0.02f,
          "P2: the rotation is a rotation — the DISTANCE walked does not change");

    // The routing maths in isolation, so a failure above can be told apart from
    // a movement-component failure.
    {
        Rig r = makeScene();
        auto *possession = r.scene->getPossession();
        possession->setYaw(0.0f);
        const iris::Vec3 f = possession->moveToWorld(0.0f, 1.0f);
        possession->setYaw(90.0f);
        const iris::Vec3 f90 = possession->moveToWorld(0.0f, 1.0f);
        CHECK(std::fabs(f.z() + 1.0f) < 1e-4f && std::fabs(f.x()) < 1e-4f,
              "P2: moveToWorld(0,1) at yaw 0 is (0,0,-1)");
        CHECK(std::fabs(f90.x() + 1.0f) < 1e-4f && std::fabs(f90.z()) < 1e-4f,
              "P2: moveToWorld(0,1) at yaw 90 is (-1,0,0)");
        CHECK(possession->moveToWorld(0.0f, 0.0f).length() == 0.0f,
              "P2: no stick is no direction (never a spurious forward)");
    }

    // THE LOOK PRODUCER drives that yaw — the mouse path, not just setYaw.
    {
        resetInput();
        Rig r = makeScene();
        addGroundPlane(r);
        auto avatar = addAvatar(r, "A", iris::Vec3(0, 2, 0));
        startPlay(r);
        auto *possession = r.scene->getPossession();
        possession->possess(avatar);
        possession->setYaw(0.0f);
        const float sens = possession->follow().yawSensitivity;
        // Exactly the call PlayBack::mouseMoveEvent makes.
        iris::InputSystem::instance().mouseMoved(-90.0f / sens, 0.0f);
        frames(r, 1);
        printf("      yaw after a %.0f px mouse delta: %.3f deg\n", double(-90.0f / sens),
               double(possession->yaw()));
        CHECK(std::fabs(possession->yaw() - 90.0f) < 0.5f,
              "P2: the Look action drives the arm's yaw through the mouse producer");
        CHECK(iris::InputSystem::instance().state().look.x == 0.0f,
              "P2: ...and the look accumulator was DRAINED by the consumer");

        // Pitch is clamped, and Y-only look never touches yaw.
        possession->setPitch(0.0f);
        iris::InputSystem::instance().mouseMoved(0.0f, 100000.0f);
        frames(r, 1);
        CHECK(std::fabs(possession->pitch() - possession->follow().maxPitch) < 0.001f,
              "P2: pitch is clamped to the arm's limit, however far the mouse goes");
    }
}

// ===========================================================================
// P3 — playMode('third-person') auto-possesses the FIRST avatar, deterministically

static void gateP3()
{
    section("P3 — 'third-person' auto-possesses the first avatar in document order");
    resetInput();

    // The document order the claim is about: depth-first from the root,
    // children in order. `First` is nested one level down and `Second` is a
    // direct child added AFTER it — so a walk that is breadth-first, or that
    // skips subtrees, picks the wrong one and this gate says so.
    struct Names { QString possessed; QString firstWalk; };
    auto run = [](Names *out) {
        Rig r = makeScene();
        addGroundPlane(r);
        auto group = addEmpty(r, "Group");
        addEmpty(r, "Prop", group);
        auto first = addAvatar(r, "First", iris::Vec3(0, 2, 0), group);
        Q_UNUSED(first);
        addAvatar(r, "Second", iris::Vec3(4, 2, 0));
        addAvatar(r, "Third", iris::Vec3(8, 2, 0));

        r.scene->setPlayMode(iris::ScenePlayMode::ThirdPerson);
        startPlay(r);
        auto *possession = r.scene->getPossession();
        out->possessed = possession->possessed() ? possession->possessed()->getName() : QString();
        auto fa = iris::AvatarPossession::firstAvatar(r.scene->getRootNode());
        out->firstWalk = fa ? fa->getName() : QString();
        CHECK(!possession->fellBackToExplorer(), "P3: a scene WITH an avatar does not fall back");
    };

    Names a, b;
    run(&a);
    run(&b);
    printf("      run A possessed '%s' (firstAvatar walk: '%s')\n",
           qPrintable(a.possessed), qPrintable(a.firstWalk));
    printf("      run B possessed '%s' (firstAvatar walk: '%s')\n",
           qPrintable(b.possessed), qPrintable(b.firstWalk));
    CHECK(a.possessed == QLatin1String("First"),
          "P3: play auto-possessed the first avatar in DOCUMENT order (depth-first)");
    CHECK(a.firstWalk == QLatin1String("First"),
          "P3: ...and firstAvatar() names the same node");
    CHECK(a.possessed == b.possessed,
          "P3: the choice is DETERMINISTIC — the same scene twice picks the same avatar");

    // 'explorer' and 'camera' possess nobody, which is what makes third-person
    // a decision rather than a default.
    for (auto mode : { iris::ScenePlayMode::Explorer, iris::ScenePlayMode::Camera }) {
        Rig r = makeScene();
        addGroundPlane(r);
        addAvatar(r, "A", iris::Vec3(0, 2, 0));
        r.scene->setPlayMode(mode);
        startPlay(r);
        CHECK(r.scene->getPossession()->possessed().isNull(),
              mode == iris::ScenePlayMode::Explorer
                  ? "P3: 'explorer' possesses nobody, even with an avatar in the scene"
                  : "P3: 'camera' possesses nobody, even with an avatar in the scene");
    }

    // Switching a RUNNING scene re-arms possession without a stop/start.
    {
        Rig r = makeScene();
        addGroundPlane(r);
        auto avatar = addAvatar(r, "A", iris::Vec3(0, 2, 0));
        startPlay(r);
        CHECK(r.scene->getPossession()->possessed().isNull(), "P3: starts unpossessed in explorer");
        r.scene->setPlayMode(iris::ScenePlayMode::ThirdPerson);
        CHECK(r.scene->getPossession()->possessed() == avatar,
              "P3: switching a PLAYING scene to third-person possesses on the spot");
    }

    // The stable strings the file carries.
    CHECK(QString::fromLatin1(iris::playModeName(iris::ScenePlayMode::Explorer)) == "explorer" &&
              QString::fromLatin1(iris::playModeName(iris::ScenePlayMode::ThirdPerson)) == "third-person" &&
              QString::fromLatin1(iris::playModeName(iris::ScenePlayMode::Camera)) == "camera",
          "P3: the serialized mode names are the three the verb documents");
    iris::ScenePlayMode parsed = iris::ScenePlayMode::Explorer;
    CHECK(iris::playModeFromName("Third-Person", parsed) && parsed == iris::ScenePlayMode::ThirdPerson,
          "P3: parsing is case-insensitive");
    CHECK(!iris::playModeFromName("cinematic", parsed),
          "P3: an unknown mode name is refused, not guessed");
    CHECK(parsed == iris::ScenePlayMode::ThirdPerson,
          "P3: ...and a refused parse leaves the out-param alone");
    CHECK(iris::Scene::create()->getPlayMode() == iris::ScenePlayMode::Explorer,
          "P3: a new scene defaults to 'explorer'");
}

// ===========================================================================
// P4 — 'third-person' with no avatar falls back to explorer AND SAYS SO

static void gateP4()
{
    section("P4 — 'third-person' with no avatar falls back to 'explorer' and logs it");
    resetInput();

    sLog.clear();
    Rig r = makeScene();
    addGroundPlane(r);                       // a scene, just no character in it
    r.scene->setPlayMode(iris::ScenePlayMode::ThirdPerson);
    startPlay(r);
    frames(r, 10);

    auto *possession = r.scene->getPossession();
    CHECK(possession->possessed().isNull(), "P4: nobody is possessed");
    CHECK(possession->fellBackToExplorer(), "P4: the fallback is recorded, not silent");

    bool logged = false;
    for (const QString &line : sLog)
        if (line.contains("third-person") && line.contains("explorer")) logged = true;
    for (const QString &line : sLog)
        if (line.contains("third-person")) printf("      log: %s\n", qPrintable(line));
    CHECK(logged, "P4: ...and it SAYS SO in the log (silence is the hour-costing failure mode)");

    // The user's setting is NOT rewritten: adding a character later must just
    // work, without the scene having quietly demoted itself to explorer.
    CHECK(r.scene->getPlayMode() == iris::ScenePlayMode::ThirdPerson,
          "P4: the serialized setting survives the fallback");

    // Add a character mid-play and re-arm: the promise the previous line makes.
    auto avatar = addAvatar(r, "LateArrival", iris::Vec3(0, 2, 0));
    r.scene->getPossession()->onPlayStarted();
    CHECK(r.scene->getPossession()->possessed() == avatar,
          "P4: a character added later is possessed at the next arming");
    CHECK(!r.scene->getPossession()->fellBackToExplorer(),
          "P4: ...and the fallback flag clears");
}

// ===========================================================================
// P5 — possess while W is HELD does not leave the old avatar walking

static void gateP5()
{
    section("P5 — possess-while-held-W does not leave the old avatar walking");
    resetInput();

    Rig r = makeScene();
    addGroundPlane(r);
    auto a = addAvatar(r, "A", iris::Vec3(-4, 2, 0));
    auto b = addAvatar(r, "B", iris::Vec3(4, 2, 0));
    startPlay(r);
    frames(r, 60);

    auto *possession = r.scene->getPossession();
    possession->possess(a);

    // W goes DOWN through the keyboard producer — the real path, not setMove:
    // this gate is precisely about a key that is never released.
    const int wKey = iris::InputMap::keyFromName(QStringLiteral("W"));
    CHECK(wKey != 0, "P5: W is a key the input map knows");
    CHECK(iris::InputSystem::instance().keyPressed(wKey), "P5: W is bound to Move and is consumed");
    frames(r, 60);
    CHECK(a->avatar()->state().speed > 1.5f, "P5: A is walking under the held key");

    // ...and now the switch, with the key STILL DOWN.
    possession->possess(b);
    CHECK(iris::InputSystem::instance().state().move.y > 0.5f,
          "P5: the key is still held — the input state has not changed");
    CHECK(a->avatar()->input().move.length() == 0.0f,
          "P5: possession CLEARED A's input at the switch (the whole gate)");

    const iris::Vec3 aAt = a->getGlobalPosition();
    frames(r, 120);
    const float aDrift = planarDistance(a->getGlobalPosition(), aAt);
    printf("      A drifted %.4f u in 2 s after the switch (speed now %.5f)\n",
           double(aDrift), double(a->avatar()->state().speed));
    CHECK(aDrift < 0.15f, "P5: A brakes to a halt instead of walking forever");
    CHECK(a->avatar()->state().speed < 0.01f, "P5: A's speed is zero");
    CHECK(b->avatar()->state().speed > 1.5f, "P5: B is the one walking now");
    // §8.4 again: A must still be STEPPING, not frozen.
    CHECK(a->avatar()->state().grounded, "P5: A is idling on the floor, not frozen");

    // Releasing the key stops B, so the producer was never bypassed.
    iris::InputSystem::instance().keyReleased(wKey);
    frames(r, 120);
    CHECK(b->avatar()->state().speed < 0.01f, "P5: releasing W stops the possessed avatar");
    resetInput();
}

// ===========================================================================
// P6 — the idempotent-stop contract (§8.3 rule 1)

static void gateP6()
{
    section("P6 — stop(); stop(); leaves nobody possessed, not playing, and no error");
    resetInput();

    Rig r = makeScene();
    addGroundPlane(r);
    auto avatar = addAvatar(r, "A", iris::Vec3(0, 2, 0));
    r.scene->setPlayMode(iris::ScenePlayMode::ThirdPerson);
    startPlay(r);
    frames(r, 30);
    CHECK(r.scene->getPossession()->possessed() == avatar, "P6: playing, and A is possessed");
    CHECK(r.scene->isPlaying(), "P6: the document's play flag is on");

    // The camera the arm took over really moved — otherwise "it was restored"
    // is a check on a value that never changed.
    const iris::Vec3 followPos = r.camera->getLocalPos();
    printf("      follow camera at (%.3f, %.3f, %.3f)\n",
           double(followPos.x()), double(followPos.y()), double(followPos.z()));
    CHECK(planarDistance(followPos, iris::Vec3(0, 5, 14)) > 1.0f,
          "P6: the spring arm moved the editor camera during play");

    // THE GATE. `editor.stop()` deliberately has no early-out, so the second
    // call runs the whole stop path again; everything possession does hangs off
    // the TRANSITION, so the second one finds nothing to do.
    r.scene->setPlaying(false);
    r.scene->setPlaying(false);
    CHECK(r.scene->getPossession()->possessed().isNull(), "P6: possessed() == null");
    CHECK(r.scene->getPossession()->possessedGuid().isEmpty(), "P6: possessedGuid() is empty");
    CHECK(!r.scene->isPlaying(), "P6: playing() == false");
    CHECK(avatar->avatar()->input().move.length() == 0.0f, "P6: the released avatar's input is clear");
    CHECK(planarDistance(r.camera->getLocalPos(), iris::Vec3(0, 5, 14)) < 0.001f &&
              std::fabs(r.camera->getLocalPos().y() - 5.0f) < 0.001f,
          "P6: the editor camera is back where the arm found it");

    // A stop on a scene that never played, and a third stop after the second.
    r.scene->setPlaying(false);
    CHECK(r.scene->getPossession()->possessed().isNull(), "P6: a third stop is still clean");
    {
        Rig fresh = makeScene();
        fresh.scene->setPlaying(false);
        CHECK(!fresh.scene->isPlaying() && fresh.scene->getPossession()->possessed().isNull(),
              "P6: stopping a scene that never played is a no-op, not a fault");
    }

    // Play again after the double stop: the transition still arms.
    startPlay(r);
    CHECK(r.scene->getPossession()->possessed() == avatar,
          "P6: play after a double stop re-possesses — the edge still works");
    r.scene->setPlaying(false);
}

// ===========================================================================
// The follow arm's shape (§8.5) — the claim that it rides the WRAPPER

static void gateFollowArm()
{
    section("follow camera — a spring arm on the WRAPPER, sized from the socket reference");
    resetInput();

    Rig r = makeScene();
    addGroundPlane(r);
    auto avatar = addAvatar(r, "A", iris::Vec3(0, 2, 0));
    startPlay(r);
    frames(r, 60);

    auto *possession = r.scene->getPossession();
    possession->possess(avatar);
    possession->setYaw(0.0f);
    possession->setPitch(0.0f);
    frames(r, 1);

    const iris::Vec3 pivot = avatar->getGlobalPosition();
    const iris::Vec3 cam = possession->cameraPosition();
    printf("      avatar (%.3f, %.3f, %.3f)  camera (%.3f, %.3f, %.3f)\n",
           double(pivot.x()), double(pivot.y()), double(pivot.z()),
           double(cam.x()), double(cam.y()), double(cam.z()));
    // Yaw 0, pitch 0: the camera sits armLength BEHIND (+Z) and heightOffset up.
    CHECK(cam.z() - pivot.z() > 1.0f, "arm: the camera sits behind the character");
    CHECK(cam.y() > pivot.y(), "arm: ...and above its feet");
    CHECK(std::fabs((cam.z() - pivot.z()) - possession->follow().armLength) < 0.01f,
          "arm: exactly armLength behind at yaw 0 / pitch 0");
    CHECK(std::fabs(r.camera->getLocalPos().z() - cam.z()) < 0.001f,
          "arm: the SCENE camera is what it writes (no second driver needed)");

    // It FOLLOWS: walk, and the offset from the character is unchanged.
    const iris::Vec3 before = cam - pivot;
    iris::InputSystem::instance().setMove(0.0f, 1.0f);
    frames(r, 60);
    const iris::Vec3 after = possession->cameraPosition() - avatar->getGlobalPosition();
    printf("      offset before (%.4f, %.4f, %.4f) after (%.4f, %.4f, %.4f)\n",
           double(before.x()), double(before.y()), double(before.z()),
           double(after.x()), double(after.y()), double(after.z()));
    CHECK((after - before).length() < 0.01f,
          "arm: the offset from the character is CONSTANT while it walks (a rigid arm, no bob)");
    CHECK(planarDistance(avatar->getGlobalPosition(), pivot) > 1.5f,
          "arm: ...and the character really moved under it");

    // The arm is sized from the character's capsule when it has no shoulder
    // socket (the fallback half of "read the socket once").
    {
        iris::AvatarMovementParams tall;
        tall.capsuleAuto = false;
        tall.capsuleHeight = 3.0f;
        avatar->avatar()->setParams(tall);
        possession->unpossess();
        possession->possess(avatar);
        printf("      capsuleHeight 3.0 -> heightOffset %.3f, armLength %.3f\n",
               double(possession->follow().heightOffset), double(possession->follow().armLength));
        CHECK(possession->follow().heightOffset > 2.0f,
              "arm: a taller character gets a higher pivot (not a hard-coded 1.6)");
    }
    resetInput();
}

// ===========================================================================

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    sPrevHandler = qInstallMessageHandler(logTap);
    enginetest::DocumentGraph graph("avatar-possession-ogre.log");
    if (!graph.require()) return 1;

    gateP1();
    gateP2();
    gateP3();
    gateP4();
    gateP5();
    gateP6();
    gateFollowArm();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL OK", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
