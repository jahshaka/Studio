// document.physics_restart — the restart half of the fixed-clock determinism
// proof (ENGINEERING_DEBT_SPEC A4.2). Six rigid bodies dropped into a pile,
// stepped on the SimulationClock grid, then the world torn down and rebuilt
// the way PlayBack::stopScene / playScene do it, four times over. Every cycle
// must land every body on the same bits. No renderer: the document graph's
// headless engine (tests/support/documentgraph.h).
//
// The scripted suite (scripting.e2e.fixed_clock) found the piled-bodies case
// alternating between TWO end states across play restarts (A == C, B == D,
// A != B) while non-interacting bodies were exact — so the alternation was in
// the contact solve, not in the clock or the transforms. This suite isolates
// the Environment + Bullet from the app and pins the fix.

#include <QGuiApplication>
#include <QString>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/simulationclock.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

struct Saved { iris::Vec3 pos, scale; iris::Quat rot; };

static QString stateOf(const std::vector<iris::MeshNodePtr> &bodies)
{
    QString s;
    for (const auto &b : bodies) {
        const iris::Vec3 p = b->getLocalPos();
        const iris::Quat q = b->getLocalRot();
        s += QString::asprintf("%.9g %.9g %.9g | %.9g %.9g %.9g %.9g\n",
                               double(p.x()), double(p.y()), double(p.z()),
                               double(q.scalar()), double(q.x()), double(q.y()), double(q.z()));
    }
    return s;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("physics-restart-ogre.log");
    if (!graph.ok()) { std::printf("FAIL: headless engine: %s\n", graph.error().c_str()); return 1; }

    auto scene = iris::Scene::create();
    auto ground = iris::MeshNode::create();
    ground->setGUID("restart-ground");
    ground->isPhysicsBody = true;
    ground->physicsProperty.objectMass = 0.0f;
    ground->physicsProperty.type = iris::PhysicsType::Static;
    ground->physicsProperty.shape = iris::PhysicsCollisionShape::Plane;
    scene->getRootNode()->addChild(ground);

    // A pile: six spheres and boxes dropped onto one spot so every body
    // touches several others (the case that alternated).
    struct Seed { bool sphere; float x, y, z, rx, ry, rz, mass; };
    const Seed seeds[] = {
        { false, -0.3f, 3.0f,  0.0f, 30, 15, 40, 1.0f },
        { false,  0.2f, 5.0f,  0.1f, 45, 45, 10, 1.5f },
        { true,   0.1f, 7.0f, -0.2f,  0,  0,  0, 2.0f },
        { false, -0.1f, 9.0f,  0.2f, 70, 20, 65, 2.5f },
        { true,   0.3f, 11.0f, 0.0f,  0,  0,  0, 3.0f },
        { false,  0.0f, 13.0f, 0.3f, 10, 80, 25, 3.5f },
    };
    std::vector<iris::MeshNodePtr> bodies;
    std::vector<Saved> rest;
    int k = 0;
    for (const Seed &s : seeds) {
        auto b = iris::MeshNode::create();
        b->setGUID(QString("restart-body-%1").arg(k++));
        b->setLocalPos(iris::Vec3(s.x, s.y, s.z));
        b->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(s.rx, s.ry, s.rz)));
        b->setLocalScale(iris::Vec3(0.5f, 0.5f, 0.5f));
        b->isPhysicsBody = true;
        b->physicsProperty.objectMass = s.mass;
        b->physicsProperty.objectRestitution = 0.3f;
        b->physicsProperty.type = iris::PhysicsType::RigidBody;
        b->physicsProperty.shape = s.sphere ? iris::PhysicsCollisionShape::Sphere
                                            : iris::PhysicsCollisionShape::Cube;
        scene->getRootNode()->addChild(b);
        bodies.push_back(b);
        rest.push_back({ b->getLocalPos(), b->getLocalScale(), b->getLocalRot() });
    }

    auto env = scene->getPhysicsEnvironment();
    const QString restText = stateOf(bodies);

    // One play/stop cycle, exactly the PlayBack sequence: init world +
    // simulate, advance N grid steps, then restartPhysics + the environment's
    // restore + the exact local restore, and the clock back to zero.
    auto cycle = [&](int steps) {
        env->initializePhysicsWorldFromScene(scene->getRootNode());
        env->simulatePhysics();
        scene->simulationClock().reset();
        for (int i = 0; i < steps; ++i) scene->advance(1.0f / 60.0f);
        const QString out = stateOf(bodies);
        env->restartPhysics();
        env->restoreNodeTransformations(scene->getRootNode());
        for (size_t i = 0; i < bodies.size(); ++i) {
            bodies[i]->setLocalPos(rest[i].pos);
            bodies[i]->setLocalRot(rest[i].rot);
            bodies[i]->setLocalScale(rest[i].scale);
        }
        scene->simulationClock().reset();
        return out;
    };

    std::vector<QString> runs;
    for (int r = 0; r < 4; ++r) {
        runs.push_back(cycle(120));
        CHECK(stateOf(bodies) == restText, "the stop restores every body to its rest transform exactly");
    }
    std::printf("run 0:\n%s", runs[0].toUtf8().constData());
    // Did they pile? Every body ended below its drop height and near the origin.
    bool piled = true;
    {
        const QStringList lines = runs[0].split('\n', Qt::SkipEmptyParts);
        for (int i = 0; i < lines.size(); ++i) {
            const QStringList f = lines[i].split(' ');
            const double y = f[1].toDouble();
            if (!(y < seeds[i].y - 1.0)) piled = false;
        }
    }
    CHECK(piled, "every body fell at least a metre (the pile formed)");
    for (int r = 1; r < 4; ++r) {
        const bool same = runs[r] == runs[0];
        if (!same) std::printf("run %d differs:\n%s", r, runs[r].toUtf8().constData());
        CHECK(same, QString("run %1 lands every body on the same bits as run 0").arg(r).toUtf8().constData());
    }
    // And the frame-rate half, in the same process: 60 x 1/30 buys the same 120 steps.
    {
        env->initializePhysicsWorldFromScene(scene->getRootNode());
        env->simulatePhysics();
        scene->simulationClock().reset();
        for (int i = 0; i < 60; ++i) scene->advance(1.0f / 30.0f);
        const QString out = stateOf(bodies);
        CHECK(scene->simulationClock().steps() == 120, "60 frames of 1/30 s bought 120 steps");
        CHECK(out == runs[0], "…and land every body on run 0's bits");
        env->restartPhysics();
        env->restoreNodeTransformations(scene->getRootNode());
    }

    std::printf(failures ? "document.physics_restart: %d FAILED\n" : "document.physics_restart: all ok\n", failures);
    return failures ? 1 : 0;
}
