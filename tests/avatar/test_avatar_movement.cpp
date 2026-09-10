// avatar.movement — AVATAR_LOCOMOTION_SPEC Stage 2, gates M1-M7.
//
// The movement component (§6) driven through the REAL seam: an iris::Scene, its
// iris::Environment, Environment::initializePhysicsWorldFromScene and
// Scene::update(dt) — i.e. exactly the chain PlayBack::update drives at Play.
// Nothing here reaches into the component's internals to move it; every gate
// writes the §5 input contract and reads the §5 output contract, which is what
// makes `avatar.input` (Stage 1) and the keyboard producer interchangeable.
//
// Runs offscreen with DISPLAY unset (the document graph boots Ogre's NULL
// render system). Framework-free; non-zero exit on failure.

#include <QGuiApplication>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/physics/avatarmovement.h"
#include "irisgl/document/scenegraph/simulationclock.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/core/geometry/trimesh.h"
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
// Fixtures. Every collider here is a plain STATIC physics body built by the
// production PhysicsHelper path, so what the sweeps see is what a real scene's
// geometry produces.

struct Rig
{
    iris::ScenePtr scene;
    QSharedPointer<iris::Environment> env;
};

static Rig makeScene()
{
    Rig r;
    r.scene = iris::Scene::create();
    r.env = r.scene->getPhysicsEnvironment();
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

/// A static box. btBoxShape(1,1,1) scaled by the node's local scale, so
/// `halfExtents` IS the local scale.
static iris::MeshNodePtr addBox(Rig &r, const char *name, const iris::Vec3 &pos,
                                const iris::Vec3 &halfExtents,
                                const iris::Quat &rot = iris::Quat())
{
    auto node = iris::MeshNode::create();
    node->setName(name);
    node->isPhysicsBody = true;
    node->physicsProperty.shape = iris::PhysicsCollisionShape::Cube;
    node->physicsProperty.type = iris::PhysicsType::Static;
    node->physicsProperty.objectMass = 0.0f;
    node->physicsProperty.isStatic = true;
    node->setLocalPos(pos);
    node->setLocalRot(rot);
    node->setLocalScale(halfExtents);
    r.scene->getRootNode()->addChild(node);
    return node;
}

static iris::SceneNodePtr addAvatar(Rig &r, const iris::Vec3 &pos,
                                    const iris::AvatarMovementParams *params = nullptr)
{
    auto node = iris::SceneNode::create();
    node->setName("Avatar");
    auto movement = iris::AvatarMovementPtr(new iris::AvatarMovement());
    if (params) movement->setParams(*params);
    node->setAvatarComponent(movement);
    node->setLocalPos(pos);
    r.scene->getRootNode()->addChild(node);
    return node;
}

static void startPlay(Rig &r)
{
    r.env->initializePhysicsWorldFromScene(r.scene->getRootNode());
    r.env->simulatePhysics();
}

/// One FRAME of play, exactly as PlayBack::update drives it.
static void frames(Rig &r, int n, float dt = 1.0f / 60.0f)
{
    for (int i = 0; i < n; ++i) r.scene->advance(dt);
}

static float planarDistance(const iris::Vec3 &a, const iris::Vec3 &b)
{
    const float dx = a.x() - b.x();
    const float dz = a.z() - b.z();
    return std::sqrt(dx * dx + dz * dz);
}

// ===========================================================================
// M1 — fixed-dt determinism and walk speed

/// Runs the M1 sequence and returns the avatar's final position. Same code both
/// times, so a difference between the two runs can only come from the
/// component's own state.
static iris::Vec3 m1Run(float *travelled)
{
    Rig r = makeScene();
    addGroundPlane(r);
    auto avatar = addAvatar(r, iris::Vec3(0, 2, 0));
    startPlay(r);

    // Settle first: the gate is about WALKING, not about the fall onto the
    // floor, and a run that starts mid-air spends its first frames in Falling.
    frames(r, 60);

    auto *m = avatar->avatar();
    const iris::Vec3 start = avatar->getGlobalPosition();
    m->setMoveInput(iris::Vec3(0, 0, -1));   // forward = world -Z
    frames(r, 60);
    const iris::Vec3 end = avatar->getGlobalPosition();
    if (travelled) *travelled = planarDistance(end, start);
    return end;
}

static void gateM1()
{
    section("M1 — fixed-dt run: displacement within 5% of walkSpeed, and bit-identical twice");

    float d1 = 0.0f, d2 = 0.0f;
    const iris::Vec3 a = m1Run(&d1);
    const iris::Vec3 b = m1Run(&d2);

    const float walkSpeed = iris::AvatarMovementParams().walkSpeed;   // 2.0 u/s
    const float ideal = walkSpeed * 1.0f;                             // 60 frames at 1/60
    const float errPct = std::fabs(d1 - ideal) / ideal * 100.0f;
    printf("      travelled %.6f u in 1.000 s (ideal %.3f, error %.2f%%)\n",
           double(d1), double(ideal), double(errPct));
    // The whole shortfall is the acceleration ramp: reaching 2 u/s at
    // 20 u/s^2 costs walkSpeed^2 / (2 * maxAcceleration) = 0.1 u of the first
    // second, which is 5% of the ideal at the continuous limit and 4.2% after
    // discrete integration. It is INSIDE the gate, but only just — a builder
    // who lowers maxAcceleration will see this fail, and that is the gate
    // doing its job rather than a flake.
    CHECK(errPct < 5.0f, "M1: planar displacement is within 5% of walkSpeed");

    // BIT-identical, not approximately equal.
    const bool identical = a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
    printf("      run A (%.9g, %.9g, %.9g)\n      run B (%.9g, %.9g, %.9g)\n",
           double(a.x()), double(a.y()), double(a.z()),
           double(b.x()), double(b.y()), double(b.z()));
    CHECK(identical, "M1: the same run twice is BIT-IDENTICAL");

    // Sub-stepping is what makes that true across frame rates: the same
    // WALL-CLOCK time at a different dt must not change the answer by much.
    {
        Rig r = makeScene();
        addGroundPlane(r);
        auto avatar = addAvatar(r, iris::Vec3(0, 2, 0));
        startPlay(r);
        frames(r, 30, 1.0f / 30.0f);
        auto *m = avatar->avatar();
        const iris::Vec3 start = avatar->getGlobalPosition();
        m->setMoveInput(iris::Vec3(0, 0, -1));
        frames(r, 30, 1.0f / 30.0f);
        const float d30 = planarDistance(avatar->getGlobalPosition(), start);
        printf("      at 30 fps: %.6f u (60 fps: %.6f u)\n", double(d30), double(d1));
        CHECK(std::fabs(d30 - d1) < 0.05f,
              "M1: half the frame rate, same second, same distance to within 0.05 u");
    }

    // The catch-up ceiling moved out of the component and into the document's
    // one clock (ENGINEERING_DEBT_SPEC A4.2): a 10 fps frame is 6 grid steps
    // of 1/60 s, and a stalled frame runs at most 8 and DROPS the rest rather
    // than integrating the whole stall at once — for every consumer at once.
    CHECK(iris::SimulationClock::kMaxStepsPerAdvance == 8 && iris::SimulationClock::kStepHz == 60,
          "M1: the catch-up bound is 8 steps of 1/60 s (SimulationClock)");
}

// ===========================================================================
// M2 — gravity settle, grounded, no sink

static void gateM2()
{
    section("M2 — gravity settle: grounded, resting at the capsule's height, no sink");

    Rig r = makeScene();
    addGroundPlane(r);
    auto avatar = addAvatar(r, iris::Vec3(0, 4, 0));
    startPlay(r);

    auto *m = avatar->avatar();
    CHECK(!m->state().grounded, "M2: an avatar dropped from 4 u starts NOT grounded");

    frames(r, 120);
    const float y = avatar->getGlobalPosition().y();
    printf("      settled at y = %.6f (capsule height %.3f, feet expected ~0)\n",
           double(y), double(m->params().capsuleHeight));
    CHECK(m->state().grounded, "M2: it is grounded after 2 s");
    CHECK(std::fabs(y) < 0.02f, "M2: its FEET rest on the plane (y within 0.02 of 0)");
    CHECK(y > -0.01f, "M2: it did not SINK below the floor");
    CHECK(std::fabs(m->state().verticalVelocity) < 1e-3f, "M2: vertical velocity settled to 0");
    CHECK(m->state().mode == iris::AvatarMovementMode::Walking, "M2: mode is Walking");
    CHECK(m->state().speed < 1e-3f, "M2: planar speed is 0 with no input");

    // And it STAYS there — a floor snap that creeps is the classic sink.
    frames(r, 240);
    const float y2 = avatar->getGlobalPosition().y();
    printf("      after 4 more seconds: y = %.6f (drift %.3g)\n", double(y2), double(y2 - y));
    CHECK(std::fabs(y2 - y) < 1e-3f, "M2: 4 more seconds of standing still does not drift");
}

// ===========================================================================
// M3 — jump: apex within 5% of ballistic, coyote honoured, a held key fires once

static void gateM3()
{
    section("M3 — jump: ballistic apex, coyote time, a held key fires ONCE");

    {
        Rig r = makeScene();
        addGroundPlane(r);
        auto avatar = addAvatar(r, iris::Vec3(0, 1, 0));
        startPlay(r);
        frames(r, 60);

        auto *m = avatar->avatar();
        const float g = -float(r.env->getWorld()->getGravity().y());
        const float v0 = m->params().jumpVelocity;
        const float predicted = v0 * v0 / (2.0f * g);

        const float base = avatar->getGlobalPosition().y();
        m->requestJump();
        float apex = base;
        for (int i = 0; i < 180; ++i) {
            r.scene->advance(1.0f / 60.0f);
            apex = std::max(apex, avatar->getGlobalPosition().y());
        }
        const float reached = apex - base;
        const float errPct = std::fabs(reached - predicted) / predicted * 100.0f;
        printf("      gravity %.3f, jumpVelocity %.3f -> ballistic apex %.4f, reached %.4f (%.2f%%)\n",
               double(g), double(v0), double(predicted), double(reached), double(errPct));
        CHECK(errPct < 5.0f, "M3: the apex is within 5% of the ballistic prediction");
        CHECK(m->state().grounded, "M3: and it landed again");
        CHECK(std::fabs(avatar->getGlobalPosition().y() - base) < 0.02f,
              "M3: landing returns it to the floor, not below it");
    }

    // COYOTE TIME: walk off a ledge and jump AFTER leaving the ground.
    {
        // A ledge: a box top at y = 1, ending at x = 1. Beyond it, the plane.
        Rig r = makeScene();
        addGroundPlane(r);
        addBox(r, "Ledge", iris::Vec3(-1, 0.5f, 0), iris::Vec3(2, 0.5f, 2));
        auto avatar = addAvatar(r, iris::Vec3(-1, 1.2f, 0));
        startPlay(r);
        frames(r, 60);
        auto *m = avatar->avatar();
        CHECK(m->state().grounded && std::fabs(avatar->getGlobalPosition().y() - 1.0f) < 0.02f,
              "M3: the avatar stands on the ledge at y = 1");

        // Walk off the edge (+X).
        m->setMoveInput(iris::Vec3(1, 0, 0));
        int framesUntilAirborne = 0;
        for (int i = 0; i < 240 && m->state().grounded; ++i) {
            r.scene->advance(1.0f / 60.0f);
            ++framesUntilAirborne;
        }
        CHECK(!m->state().grounded, "M3: it walked off the ledge and is airborne");
        // 3 frames = 0.05 s, comfortably inside the 0.12 s coyote window.
        m->setMoveInput(iris::Vec3());
        r.scene->advance(1.0f / 60.0f);
        r.scene->advance(1.0f / 60.0f);
        const float yBefore = avatar->getGlobalPosition().y();
        const float vyBefore = m->state().verticalVelocity;
        m->requestJump();
        r.scene->advance(1.0f / 60.0f);
        const float vyAfter = m->state().verticalVelocity;
        printf("      airborne %d frames, vy %.4f -> %.4f after the coyote jump (y %.4f)\n",
               framesUntilAirborne, double(vyBefore), double(vyAfter), double(yBefore));
        CHECK(vyAfter > vyBefore + 1.0f, "M3: a jump inside coyoteTime FIRES after leaving the ground");
    }

    // And OUTSIDE the window it does not.
    {
        Rig r = makeScene();
        addGroundPlane(r);
        addBox(r, "Ledge", iris::Vec3(-1, 0.5f, 0), iris::Vec3(2, 0.5f, 2));
        auto avatar = addAvatar(r, iris::Vec3(-1, 1.2f, 0));
        startPlay(r);
        frames(r, 60);
        auto *m = avatar->avatar();
        m->setMoveInput(iris::Vec3(1, 0, 0));
        for (int i = 0; i < 240 && m->state().grounded; ++i) r.scene->advance(1.0f / 60.0f);
        m->setMoveInput(iris::Vec3());
        // 0.25 s > coyoteTime 0.12 s
        for (int i = 0; i < 15; ++i) r.scene->advance(1.0f / 60.0f);
        const float vyBefore = m->state().verticalVelocity;
        m->requestJump();
        r.scene->advance(1.0f / 60.0f);
        const float vyAfter = m->state().verticalVelocity;
        printf("      0.25 s after the edge: vy %.4f -> %.4f\n", double(vyBefore), double(vyAfter));
        CHECK(vyAfter < vyBefore, "M3: a jump OUTSIDE coyoteTime does not fire");
    }

    // A HELD key fires exactly once (jumpCount 1, jumpReArm).
    {
        Rig r = makeScene();
        addGroundPlane(r);
        auto avatar = addAvatar(r, iris::Vec3(0, 1, 0));
        startPlay(r);
        frames(r, 60);
        auto *m = avatar->avatar();
        const float g = -float(r.env->getWorld()->getGravity().y());
        const float v0 = m->params().jumpVelocity;
        const float singleApex = v0 * v0 / (2.0f * g);
        const float base = avatar->getGlobalPosition().y();

        int launches = 0;
        float prevVy = m->state().verticalVelocity;
        float apex = base;
        for (int i = 0; i < 180; ++i) {
            m->requestJump();   // HELD, every single frame
            r.scene->advance(1.0f / 60.0f);
            const float vy = m->state().verticalVelocity;
            if (vy > prevVy + 1.0f) ++launches;
            prevVy = vy;
            apex = std::max(apex, avatar->getGlobalPosition().y());
        }
        printf("      held for 3 s: %d launches, apex %.4f (a single jump reaches %.4f)\n",
               launches, double(apex - base), double(singleApex));
        CHECK(apex - base < singleApex * 1.10f,
              "M3: a HELD jump key never stacks past one jump's apex");
        // It may re-fire after LANDING (that is a new press as far as a latch
        // can tell), but it must never double-fire in the air.
        CHECK(m->params().jumpCount == 1, "M3: jumpCount defaults to 1");
    }

    // Double jump: one integer, and it works.
    {
        iris::AvatarMovementParams p;
        p.jumpCount = 2;
        Rig r = makeScene();
        addGroundPlane(r);
        auto avatar = addAvatar(r, iris::Vec3(0, 1, 0), &p);
        startPlay(r);
        frames(r, 60);
        auto *m = avatar->avatar();
        const float base = avatar->getGlobalPosition().y();
        m->requestJump();
        for (int i = 0; i < 20; ++i) r.scene->advance(1.0f / 60.0f);
        m->requestJump();   // the second, in the air
        float apex = base;
        for (int i = 0; i < 200; ++i) {
            r.scene->advance(1.0f / 60.0f);
            apex = std::max(apex, avatar->getGlobalPosition().y());
        }
        const float g = -float(r.env->getWorld()->getGravity().y());
        const float single = m->params().jumpVelocity * m->params().jumpVelocity / (2.0f * g);
        printf("      jumpCount 2: apex %.4f vs a single jump's %.4f\n",
               double(apex - base), double(single));
        CHECK(apex - base > single * 1.2f, "M3: jumpCount 2 reaches higher than one jump can");
    }
}

// ===========================================================================
// M4 — a 0.3 u step is climbed, a 0.5 u step is not (maxStepHeight 0.4)

static void gateM4()
{
    section("M4 — step-up: 0.3 u climbed, 0.5 u refused (maxStepHeight 0.4)");

    auto walkAtStep = [](float stepHeight, float *finalX, float *finalY) {
        Rig r = makeScene();
        addGroundPlane(r);
        // Front face at x = 1, top at y = stepHeight, and LONG (out to x = 13)
        // so a 4 s walk lands on it rather than walking off its far side.
        addBox(r, "Step", iris::Vec3(7, stepHeight * 0.5f, 0),
               iris::Vec3(6, stepHeight * 0.5f, 3));
        auto avatar = addAvatar(r, iris::Vec3(0, 1, 0));
        startPlay(r);
        frames(r, 60);
        avatar->avatar()->setMoveInput(iris::Vec3(1, 0, 0));
        frames(r, 240);
        *finalX = avatar->getGlobalPosition().x();
        *finalY = avatar->getGlobalPosition().y();
    };

    float x = 0, y = 0;
    walkAtStep(0.3f, &x, &y);
    printf("      0.3 u step: ended at x = %.4f, y = %.4f\n", double(x), double(y));
    CHECK(x > 1.5f, "M4: a 0.3 u step is CLIMBED (the avatar is past the face)");
    CHECK(std::fabs(y - 0.3f) < 0.03f, "M4: and it is standing on top of it (y ~ 0.3)");

    walkAtStep(0.5f, &x, &y);
    printf("      0.5 u step: ended at x = %.4f, y = %.4f\n", double(x), double(y));
    CHECK(x < 1.0f, "M4: a 0.5 u step is NOT climbed (blocked short of the face)");
    CHECK(std::fabs(y) < 0.03f, "M4: and the avatar is still on the ground (y ~ 0)");
}

// ===========================================================================
// M5 — a 60 deg slope slides, a 30 deg slope holds

static void gateM5()
{
    section("M5 — slopes: 60 deg slides, 30 deg holds (walkableFloorAngle 45)");

    auto standOnSlope = [](float degrees, float *drift, bool *grounded) {
        Rig r = makeScene();
        // A rotated slab. Its top surface passes through (0, 0.5 / cos(a), 0),
        // so the avatar is dropped just above that.
        const float rad = degrees * 3.14159265358979f / 180.0f;
        addBox(r, "Ramp", iris::Vec3(0, 0, 0), iris::Vec3(6, 0.5f, 6),
               iris::Quat::fromAxisAndAngle(iris::Vec3(0, 0, 1), degrees));
        const float surfaceY = 0.5f / std::cos(rad);
        auto avatar = addAvatar(r, iris::Vec3(0, surfaceY + 0.2f, 0));
        startPlay(r);
        frames(r, 60);
        const iris::Vec3 settled = avatar->getGlobalPosition();
        frames(r, 120);
        *drift = planarDistance(avatar->getGlobalPosition(), settled);
        *grounded = avatar->avatar()->state().grounded;
    };

    float drift = 0;
    bool grounded = false;

    standOnSlope(30.0f, &drift, &grounded);
    printf("      30 deg: grounded %d, planar drift over 2 s = %.4f u\n", int(grounded), double(drift));
    CHECK(grounded, "M5: a 30 deg slope is walkable — the avatar is grounded on it");
    CHECK(drift < 0.05f, "M5: and it HOLDS (planar drift under 0.05 u in 2 s)");

    standOnSlope(60.0f, &drift, &grounded);
    printf("      60 deg: grounded %d, planar drift over 2 s = %.4f u\n", int(grounded), double(drift));
    CHECK(!grounded, "M5: a 60 deg slope is not walkable — the avatar is not grounded");
    CHECK(drift > 0.5f, "M5: and it SLIDES down it");
}

// ===========================================================================
// M6 — wall slide: into a wall at 45 deg yields motion ALONG it

static void gateM6()
{
    section("M6 — wall slide: a 45 deg approach yields along-wall motion, not a stop");

    Rig r = makeScene();
    addGroundPlane(r);
    // A wall facing -X, its face at x = 2.
    addBox(r, "Wall", iris::Vec3(3, 2, 0), iris::Vec3(1, 2, 8));
    auto avatar = addAvatar(r, iris::Vec3(0, 1, 0));
    startPlay(r);
    frames(r, 60);

    auto *m = avatar->avatar();
    const iris::Vec3 start = avatar->getGlobalPosition();
    // 45 degrees into the wall: half toward +X (blocked), half toward +Z (free).
    const float s = std::sqrt(0.5f);
    m->setMoveInput(iris::Vec3(s, 0, s));
    frames(r, 180);
    const iris::Vec3 end = avatar->getGlobalPosition();

    const float alongWall = end.z() - start.z();
    const float intoWall = end.x() - start.x();
    printf("      moved %.4f u ALONG the wall (+Z) and %.4f u INTO it (+X); "
           "the face is at x = 2, capsule radius %.2f\n",
           double(alongWall), double(intoWall), double(m->params().capsuleRadius));
    CHECK(intoWall > 1.0f && end.x() < 2.0f,
          "M6: the avatar advanced to the wall and stopped short of its face");
    CHECK(alongWall > 1.5f, "M6: and it kept SLIDING along the wall rather than stopping");
    CHECK(m->state().speed > 0.5f, "M6: it is still moving at the end of the run");

    // Straight into the wall: it stops, and it does not jitter through.
    {
        Rig r2 = makeScene();
        addGroundPlane(r2);
        addBox(r2, "Wall", iris::Vec3(3, 2, 0), iris::Vec3(1, 2, 8));
        auto a2 = addAvatar(r2, iris::Vec3(0, 1, 0));
        startPlay(r2);
        frames(r2, 60);
        a2->avatar()->setMoveInput(iris::Vec3(1, 0, 0));
        frames(r2, 300);
        const float x = a2->getGlobalPosition().x();
        printf("      head-on for 5 s: stopped at x = %.4f (face 2.0 - radius 0.30 = 1.70)\n",
               double(x));
        CHECK(x < 2.0f, "M6: a head-on run never penetrates the wall");
        CHECK(x > 1.5f, "M6: and it does reach it");
    }
}

// ===========================================================================
// M7 — delete a moving avatar mid-play: no stale broadphase entry, ASan clean

static void gateM7()
{
    section("M7 — delete mid-play: no world object, no stale entry, no use-after-free");

    Rig r = makeScene();
    addGroundPlane(r);

    const int objectsBefore = r.env->getWorld()->getNumCollisionObjects();
    auto avatar = addAvatar(r, iris::Vec3(0, 2, 0));
    auto second = addAvatar(r, iris::Vec3(3, 2, 0));
    startPlay(r);
    const int objectsWithAvatars = r.env->getWorld()->getNumCollisionObjects();
    printf("      collision objects: %d before the avatars, %d after (+1 ground body)\n",
           objectsBefore, objectsWithAvatars);
    CHECK(r.env->avatarCount() == 2, "M7: both avatars registered at play start");
    // The whole architectural claim of L2, asserted: the component owns NOTHING
    // in the world, so two avatars add zero collision objects beyond the
    // ground body the scene already had.
    CHECK(objectsWithAvatars == objectsBefore + 1,
          "M7: an avatar adds NO collision object to the world (Bullet is query-only)");

    // Both step — no `break` (§4.4 defect 1).
    avatar->avatar()->setMoveInput(iris::Vec3(0, 0, -1));
    second->avatar()->setMoveInput(iris::Vec3(0, 0, -1));
    frames(r, 120);
    const float movedA = std::fabs(avatar->getGlobalPosition().z());
    const float movedB = std::fabs(second->getGlobalPosition().z());
    printf("      after 2 s both walked: A %.4f u, B %.4f u\n", double(movedA), double(movedB));
    CHECK(movedA > 1.0f && movedB > 1.0f, "M7: BOTH avatars stepped, not just the first");

    // DELETE one, mid-play, while it is moving — the production shape of it
    // (SceneEditService::deleteNode): out of the registry, out of the tree,
    // and then the last reference goes.
    const QString goneGuid = avatar->getGUID();
    r.scene->removeNode(avatar);
    avatar->removeFromParent();
    avatar.reset();   // the document's last reference — the node is destroyed here

    // The world must survive the very next step.
    frames(r, 60);
    printf("      after the delete: %d avatars registered, %d collision objects\n",
           r.env->avatarCount(), r.env->getWorld()->getNumCollisionObjects());
    CHECK(r.env->avatarCount() == 1, "M7: the deleted avatar was pruned from the registry");
    CHECK(r.env->getWorld()->getNumCollisionObjects() == objectsWithAvatars,
          "M7: and the world's object count is unchanged — no stale broadphase entry");
    CHECK(second->avatar()->state().grounded || second->getGlobalPosition().y() < 2.0f,
          "M7: the surviving avatar is still being stepped");

    // Explicit removal is idempotent and tolerates a guid nobody registered.
    r.env->removeAvatarFromWorld(goneGuid);
    r.env->removeAvatarFromWorld("not-a-guid");
    CHECK(r.env->avatarCount() == 1, "M7: removeAvatarFromWorld is idempotent and tolerant");

    // Tearing the world down while an avatar is still registered must not
    // leave the component holding a dangling world pointer.
    r.env->destroyPhysicsWorld();
    CHECK(r.env->avatarCount() == 0, "M7: destroyPhysicsWorld clears the avatar registry");
    r.env->createPhysicsWorld();
}

// ===========================================================================
// The knob set and the capsule fit — the surface `avatar.movement` /
// `avatar.setMovement` report, asserted at the component before the verb.

static void gateKnobs()
{
    section("knobs — the §6.2 defaults, clamping, and the R4 capsule fit");

    iris::AvatarMovement m;
    const auto d = m.params();
    CHECK(d.walkSpeed == 2.0f && d.runSpeed == 5.5f, "knobs: walk 2.0 / run 5.5");
    CHECK(d.maxAcceleration == 20.0f && d.brakingDeceleration == 25.0f, "knobs: accel 20 / braking 25");
    CHECK(d.groundFriction == 8.0f, "knobs: groundFriction 8");
    CHECK(d.jumpVelocity == 6.0f && d.jumpCount == 1, "knobs: jumpVelocity 6, jumpCount 1");
    CHECK(d.coyoteTime == 0.12f && d.jumpReArm == 0.1f, "knobs: coyote 0.12 s, re-arm 0.1 s");
    CHECK(d.airControl == 0.35f && d.gravityScale == 1.0f, "knobs: airControl 0.35, gravityScale 1");
    CHECK(d.maxStepHeight == 0.4f && d.walkableFloorAngle == 45.0f, "knobs: step 0.4, floor angle 45");
    CHECK(d.orientRotationToMovement && d.rotationRate == 540.0f, "knobs: orient on, 540 deg/s");
    CHECK(d.capsuleAuto, "knobs: the capsule auto-fits by default");

    // Clamped, not refused.
    iris::AvatarMovementParams bad;
    bad.walkSpeed = -3.0f;
    bad.airControl = 4.0f;
    bad.walkableFloorAngle = 120.0f;
    bad.capsuleRadius = 2.0f;
    bad.capsuleHeight = 0.5f;
    bad.jumpCount = -2;
    m.setParams(bad);
    const auto c = m.params();
    printf("      clamped: walkSpeed %.2f, airControl %.2f, floorAngle %.1f, capsule r %.2f h %.2f, jumps %d\n",
           double(c.walkSpeed), double(c.airControl), double(c.walkableFloorAngle),
           double(c.capsuleRadius), double(c.capsuleHeight), c.jumpCount);
    CHECK(c.walkSpeed == 0.0f, "knobs: a negative walkSpeed clamps to 0");
    CHECK(c.airControl == 1.0f, "knobs: airControl clamps to 1");
    CHECK(c.walkableFloorAngle == 89.0f, "knobs: walkableFloorAngle clamps to 89, never 90");
    CHECK(c.capsuleHeight >= 2.0f * c.capsuleRadius, "knobs: the capsule is at least a sphere");
    CHECK(c.jumpCount == 0, "knobs: a negative jumpCount clamps to 0");

    // The unit disc: a diagonal must not walk 1.41x.
    {
        Rig r = makeScene();
        addGroundPlane(r);
        auto avatar = addAvatar(r, iris::Vec3(0, 1, 0));
        startPlay(r);
        frames(r, 60);
        auto *mv = avatar->avatar();
        mv->setMoveInput(iris::Vec3(1, 0, 1));   // length sqrt(2)
        CHECK(std::fabs(mv->input().move.length() - 1.0f) < 1e-5f,
              "knobs: moveInput is clamped to the unit disc on the way in");
        const iris::Vec3 start = avatar->getGlobalPosition();
        frames(r, 120);
        const float d2 = planarDistance(avatar->getGlobalPosition(), start);
        printf("      diagonal input travelled %.4f u in 2 s (walkSpeed 2.0)\n", double(d2));
        CHECK(d2 < 2.0f * 2.0f + 0.05f, "knobs: a diagonal walks at walkSpeed, not 1.41x it");
    }
}


// ===========================================================================
// Step DOWN, orient-to-movement, and the §6.3 collision content — the rest of
// the step order, asserted beside the numbered gates it belongs to.

static void gateStepDown()
{
    section("step-down — walking off a small drop re-plants instead of launching");

    Rig r = makeScene();
    addGroundPlane(r);
    // A platform 0.3 u high, ending at x = 1. Walking off it is a drop INSIDE
    // maxStepHeight, so the capsule must be re-planted, not launched.
    addBox(r, "Platform", iris::Vec3(-3, 0.15f, 0), iris::Vec3(4, 0.15f, 3));
    auto avatar = addAvatar(r, iris::Vec3(-3, 1, 0));
    startPlay(r);
    frames(r, 60);
    auto *m = avatar->avatar();
    CHECK(std::fabs(avatar->getGlobalPosition().y() - 0.3f) < 0.03f,
          "step-down: the avatar stands on the 0.3 u platform");

    m->setMoveInput(iris::Vec3(1, 0, 0));
    bool everAirborne = false;
    float minY = 10.0f;
    for (int i = 0; i < 240; ++i) {
        r.scene->advance(1.0f / 60.0f);
        if (!m->state().grounded) everAirborne = true;
        minY = std::min(minY, avatar->getGlobalPosition().y());
    }
    printf("      after the drop: y = %.4f, ever airborne %d, lowest y %.4f\n",
           double(avatar->getGlobalPosition().y()), int(everAirborne), double(minY));
    CHECK(!everAirborne, "step-down: it never left the ground crossing a 0.3 u drop");
    CHECK(std::fabs(avatar->getGlobalPosition().y()) < 0.03f, "step-down: and it is on the floor now");
    CHECK(minY > -0.01f, "step-down: it never dipped below the floor on the way");
}

static void gateOrient()
{
    section("orient-to-movement — the wrapper yaws toward the velocity at rotationRate");

    Rig r = makeScene();
    addGroundPlane(r);
    auto avatar = addAvatar(r, iris::Vec3(0, 1, 0));
    startPlay(r);
    frames(r, 60);
    auto *m = avatar->avatar();

    // Forward is the node's -Z, so walking toward -Z must leave the yaw at 0.
    m->setMoveInput(iris::Vec3(0, 0, -1));
    frames(r, 60);
    float yaw = avatar->getGlobalRotation().toEulerAngles().y();
    printf("      walking -Z: yaw %.2f deg\n", double(yaw));
    CHECK(std::fabs(yaw) < 1.0f, "orient: walking world -Z leaves the yaw at 0 (forward is -Z)");

    // Turn to +X: 90 degrees away. At 540 deg/s that is 1/6 of a second.
    m->setMoveInput(iris::Vec3(1, 0, 0));
    frames(r, 60);
    yaw = avatar->getGlobalRotation().toEulerAngles().y();
    printf("      walking +X: yaw %.2f deg (expected 90)\n", double(yaw));
    CHECK(std::fabs(std::fabs(yaw) - 90.0f) < 3.0f, "orient: walking +X yaws 90 degrees");

    // Turned OFF, the yaw is left alone.
    {
        iris::AvatarMovementParams p;
        p.orientRotationToMovement = false;
        Rig r2 = makeScene();
        addGroundPlane(r2);
        auto a2 = addAvatar(r2, iris::Vec3(0, 1, 0), &p);
        startPlay(r2);
        frames(r2, 60);
        a2->avatar()->setMoveInput(iris::Vec3(1, 0, 0));
        frames(r2, 120);
        const float y2 = a2->getGlobalRotation().toEulerAngles().y();
        printf("      orientRotationToMovement off: yaw %.2f deg\n", double(y2));
        CHECK(std::fabs(y2) < 0.01f, "orient: with the knob off the yaw never moves");
    }

    // The rate is a CAP, not a snap: at 45 deg/s a 90 degree turn cannot
    // complete inside half a second.
    {
        iris::AvatarMovementParams p;
        p.rotationRate = 45.0f;
        Rig r3 = makeScene();
        addGroundPlane(r3);
        auto a3 = addAvatar(r3, iris::Vec3(0, 1, 0), &p);
        startPlay(r3);
        frames(r3, 60);
        a3->avatar()->setMoveInput(iris::Vec3(0, 0, -1));
        frames(r3, 30);
        a3->avatar()->setMoveInput(iris::Vec3(1, 0, 0));
        frames(r3, 30);   // 0.5 s at 45 deg/s = 22.5 degrees, not 90
        const float y3 = std::fabs(a3->getGlobalRotation().toEulerAngles().y());
        printf("      rotationRate 45 deg/s after 0.5 s: yaw %.2f deg (a snap would read 90)\n",
               double(y3));
        CHECK(y3 > 5.0f && y3 < 45.0f, "orient: rotationRate CAPS the turn, it never snaps");
    }
}

static void gateCollisionContent()
{
    section("collision content (§6.3 option C) — a plain mesh is solid, and only when it must be");

    // A real mesh with real triangles: the bundled 2x2x2 cube.
    auto mesh = iris::Mesh::loadMesh(":assets/models/cube.obj");
    CHECK(!!mesh && mesh->getTriMesh() && !mesh->getTriMesh()->triangles.isEmpty(),
          "collision: the bundled cube.obj loads with triangles");
    if (!mesh) return;

    auto build = [&mesh](bool withAvatar, bool collisionOn, int *objectsAdded,
                         float *finalX) {
        Rig r = makeScene();
        addGroundPlane(r);
        auto wall = iris::MeshNode::create();
        wall->setName("Crate");
        wall->setMesh(mesh);
        wall->setCollisionEnabled(collisionOn);
        // NOT a physics body: this is the whole point — an ordinary imported
        // mesh the user never marked.
        wall->setLocalPos(iris::Vec3(3, 1, 0));
        wall->setLocalScale(iris::Vec3(1, 1, 4));
        r.scene->getRootNode()->addChild(wall);

        iris::SceneNodePtr avatar;
        if (withAvatar) avatar = addAvatar(r, iris::Vec3(0, 1, 0));

        const int before = r.env->getWorld()->getNumCollisionObjects();
        startPlay(r);
        *objectsAdded = r.env->getWorld()->getNumCollisionObjects() - before - 1;   // -1 ground
        if (avatar) {
            frames(r, 60);
            avatar->avatar()->setMoveInput(iris::Vec3(1, 0, 0));
            frames(r, 300);
            *finalX = avatar->getGlobalPosition().x();
        } else {
            *finalX = 0.0f;
        }
    };

    int added = 0;
    float x = 0;

    build(false, true, &added, &x);
    printf("      no avatar in the scene: %d implicit colliders built\n", added);
    CHECK(added == 0,
          "collision: a scene with NO avatar builds nothing — Play costs what it always did");

    build(true, true, &added, &x);
    printf("      with an avatar, collision ON: %d colliders, the avatar stopped at x = %.4f "
           "(crate face at x = 2)\n", added, double(x));
    CHECK(added == 1, "collision: the flagged mesh got exactly one static collider");
    CHECK(x < 2.0f && x > 1.0f, "collision: and the avatar cannot walk through it");

    build(true, false, &added, &x);
    printf("      with an avatar, collision OFF: %d colliders, the avatar reached x = %.4f\n",
           added, double(x));
    CHECK(added == 0, "collision: node.setCollision(false) builds no collider");
    CHECK(x > 4.0f, "collision: and the avatar walks straight through");

    // The default: a MESH is collidable, everything else is not.
    {
        auto meshNode = iris::MeshNode::create();
        auto empty = iris::SceneNode::create();
        CHECK(meshNode->isCollisionEnabled(), "collision: a mesh node defaults to ON");
        CHECK(!empty->isCollisionEnabled(), "collision: every other node type defaults to OFF");
    }
}

static void gateLifecycleExtras()
{
    section("stop restores the pre-play transform; duplicate DEEP-copies the component");

    {
        Rig r = makeScene();
        addGroundPlane(r);
        auto avatar = addAvatar(r, iris::Vec3(2, 1, -3));
        const iris::Vec3 authored = avatar->getGlobalPosition();
        startPlay(r);
        frames(r, 60);
        avatar->avatar()->setMoveInput(iris::Vec3(1, 0, 0));
        frames(r, 120);
        const iris::Vec3 walked = avatar->getGlobalPosition();
        CHECK(planarDistance(walked, authored) > 1.0f, "stop-restore: the avatar walked away first");

        // Exactly what PlayBack does on stop.
        r.env->stopPhysics();
        r.env->stopSimulation();
        r.env->restoreNodeTransformations(r.scene->getRootNode());
        const iris::Vec3 back = avatar->getGlobalPosition();
        printf("      authored (%.3f, %.3f, %.3f) -> walked (%.3f, %.3f, %.3f) -> restored (%.3f, %.3f, %.3f)\n",
               double(authored.x()), double(authored.y()), double(authored.z()),
               double(walked.x()), double(walked.y()), double(walked.z()),
               double(back.x()), double(back.y()), double(back.z()));
        CHECK(planarDistance(back, authored) < 1e-4f && std::fabs(back.y() - authored.y()) < 1e-4f,
              "stop-restore: Stop puts the avatar back on its pre-play transform (§2 step 5)");
    }

    {
        Rig r = makeScene();
        addGroundPlane(r);
        iris::AvatarMovementParams p;
        p.walkSpeed = 4.25f;
        p.jumpCount = 3;
        auto avatar = addAvatar(r, iris::Vec3(0, 1, 0), &p);
        auto copy = avatar->duplicate();
        CHECK(!!copy && copy->hasAvatarComponent(),
              "duplicate: a copy of an avatar IS an avatar");
        if (copy && copy->hasAvatarComponent()) {
            CHECK(copy->avatar() != avatar->avatar(),
                  "duplicate: and it carries its OWN component, not a shared pointer to one");
            CHECK(std::fabs(copy->avatar()->params().walkSpeed - 4.25f) < 1e-5f
                      && copy->avatar()->params().jumpCount == 3,
                  "duplicate: with the original's knobs copied across");
            // Prove they are independent: change one, the other must not move.
            iris::AvatarMovementParams q = copy->avatar()->params();
            q.walkSpeed = 1.0f;
            copy->avatar()->setParams(q);
            CHECK(std::fabs(avatar->avatar()->params().walkSpeed - 4.25f) < 1e-5f,
                  "duplicate: writing the copy's knobs does not touch the original's");
        }
    }

    // The COLLISION override travels with a duplicate too.
    //
    // Asserted on a plain SceneNode rather than a MeshNode ON PURPOSE:
    // `MeshNode::createDuplicate` dereferences `this->material` unguarded
    // (irisgl/document/scenegraph/meshnode.cpp:547), so duplicating a MeshNode
    // that carries no material SEGFAULTS. That is a pre-existing defect in
    // shared code, not this lane's, and it is reported rather than patched in
    // passing — every MeshNode the app builds has a material, so it is only
    // reachable from a synthetic node like the one this test would have used.
    {
        Rig r = makeScene();
        auto node = iris::SceneNode::create();
        CHECK(!node->isCollisionEnabled(), "duplicate: the node starts at its type default (off)");
        node->setCollisionEnabled(true);
        r.scene->getRootNode()->addChild(node);
        auto copy = node->duplicate();
        CHECK(copy && copy->isCollisionEnabled(),
              "duplicate: the collision override travels with the copy");
    }
}

// ===========================================================================

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("avatar-movement-ogre.log");
    if (!graph.require()) return 1;

    gateKnobs();
    gateM1();
    gateM2();
    gateM3();
    gateM4();
    gateM5();
    gateM6();
    gateM7();
    gateStepDown();
    gateOrient();
    gateCollisionContent();
    gateLifecycleExtras();

    printf("\n%s: %d failure(s)\n", failures ? "avatar.movement FAILED" : "avatar.movement OK",
           failures);
    return failures ? 1 : 0;
}
