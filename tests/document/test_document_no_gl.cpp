// Characterisation test: the iris:: scene DOCUMENT can be built with NO GL context.
//
// This is step 1 of VIEWPORT_MIGRATION_PLAN.md and the single biggest de-risk of
// the whole migration: Studio keeps iris::Scene/SceneNode/MeshNode/LightNode as its
// document model and mirrors it into the engine. That only works if the document
// no longer needs OpenGL to exist. Before this change, Scene::Scene() compiled a
// shader, LightNode::LightNode() allocated a QOpenGLTexture, and Texture2D::load
// called qFatal without a current context.
//
// Runs under QT_QPA_PLATFORM=offscreen. Framework-free; non-zero exit on failure.
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include <functional>
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicsproperties.h"
// The reparent command's cycle guard is header-only document logic (its
// undo/redo bodies live in the app; only the static guard is exercised here).
#include "commands/reparentscenenodecommand.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    CHECK(QOpenGLContext::currentContext() == nullptr, "precondition: no GL context is current");

    // --- Scene: previously loaded a sky mesh and compiled a sky shader in its ctor
    auto scene = iris::Scene::create();
    CHECK(!!scene, "iris::Scene constructed without GL");
    CHECK(!!scene->getRootNode(), "scene has a root node");
    CHECK(scene->skyType == iris::SkyType::SINGLE_COLOR, "sky defaults to single colour (document field)");

    // --- LightNode: previously created a ShadowMap -> QOpenGLTexture in its ctor
    auto light = iris::LightNode::create();
    CHECK(!!light, "iris::LightNode constructed without GL");
    CHECK(light->shadowMap != nullptr, "light has a ShadowMap object");
    CHECK(light->shadowMap->resolution == 2048, "shadow map settings carry a resolution (document data)");
    scene->getRootNode()->addChild(light);

    // --- MeshNode + mesh from a bundled OBJ: previously fine on CPU, buffers upload at draw
    auto meshNode = iris::MeshNode::create();
    meshNode->setMesh(":assets/models/sky.obj");
    CHECK(!!meshNode->getMesh(), "mesh loaded from resources without GL");
    scene->getRootNode()->addChild(meshNode);

    // --- Materials: DefaultMaterial builds its shader source; compile is lazy
    auto mat = iris::DefaultMaterial::create();
    CHECK(!!mat, "DefaultMaterial constructed without GL");
    meshNode->setMaterial(mat);

    // --- Texture2D::load: previously qFatal("Failed to get QOpenGLFunctions_3_2_Core")
    QImage img(8, 8, QImage::Format_RGBA8888); img.fill(Qt::red);
    auto tex = iris::Texture2D::create(img);
    CHECK(!!tex, "Texture2D::create(QImage) without GL returns an object");
    CHECK(tex->getWidth() == 8 && tex->getHeight() == 8, "texture asset reports image size");

    // --- Cubemap: previously returned null with no context (silently dropping the sky)
    auto cube = iris::Texture2D::createCubeMap(":assets/models/sky.obj", ":x", ":x", ":x", ":x", ":x");
    CHECK(cube.isNull(), "cubemap with unreadable faces still returns null (validation kept)");

    // --- Document traversal works: this is what SceneMirror will walk
    int count = 0;
    std::function<void(iris::SceneNodePtr)> walk = [&](iris::SceneNodePtr n) {
        ++count; for (auto &c : n->children) walk(c);
    };
    walk(scene->getRootNode());
    CHECK(count == 3, "root + light + mesh = 3 nodes");
    CHECK(light->getSceneNodeType() == iris::SceneNodeType::Light, "light node type");
    CHECK(meshNode->getSceneNodeType() == iris::SceneNodeType::Mesh, "mesh node type");

    // --- Hierarchy-panel reparent guard (ASSET_ADD_AUDIT D2): pure document logic
    {
        auto a = iris::SceneNode::create();
        auto b = iris::SceneNode::create();
        auto c = iris::SceneNode::create();
        scene->getRootNode()->addChild(a);
        a->addChild(b);
        b->addChild(c);
        CHECK(!ReparentSceneNodeCommand::wouldCreateCycle(c, a), "moving a leaf up its chain is allowed");
        CHECK(!ReparentSceneNodeCommand::wouldCreateCycle(b, scene->getRootNode()), "reparenting to the root is allowed");
        CHECK(ReparentSceneNodeCommand::wouldCreateCycle(a, c), "dropping a node into its own descendant is refused");
        CHECK(ReparentSceneNodeCommand::wouldCreateCycle(a, b), "direct child target is refused too");
        CHECK(ReparentSceneNodeCommand::wouldCreateCycle(a, a), "self-parenting is refused");
        CHECK(ReparentSceneNodeCommand::wouldCreateCycle(iris::SceneNodePtr(), a), "null dragged node is refused");
        CHECK(ReparentSceneNodeCommand::wouldCreateCycle(a, iris::SceneNodePtr()), "null target is refused");
        scene->getRootNode()->removeChild(a);
    }

    // --- "Simulate physics" without play mode (PHYSICS_AUDIT 5.1): the exact call
    // sequence EngineSceneViewport::startPhysicsSimulation + syncFrame's stepper
    // performs — init world, simulatePhysics, tick scene->update, bodies move;
    // restart restores transforms; stop clears the flag. No GL, no viewport.
    {
        auto physScene = iris::Scene::create();
        auto body = iris::MeshNode::create();
        body->setGUID("simulate-test-sphere");
        body->setLocalPos(QVector3D(0, 10, 0));
        body->isPhysicsBody = true;
        body->physicsProperty.objectMass = 1.0f;
        body->physicsProperty.shape = iris::PhysicsCollisionShape::Sphere;
        body->physicsProperty.type = iris::PhysicsType::RigidBody;
        physScene->getRootNode()->addChild(body);

        auto env = physScene->getPhysicsEnvironment();
        env->initializePhysicsWorldFromScene(physScene->getRootNode());
        env->simulatePhysics();
        CHECK(env->isSimulating(), "simulate: environment reports simulating without play mode");
        const float y0 = body->getLocalPos().y();
        for (int i = 0; i < 60; ++i) physScene->update(1.0f / 60.0f);  // syncFrame's editor-mode stepper
        CHECK(body->getLocalPos().y() < y0 - 0.5f, "simulate: dynamic body fell under gravity via scene->update");
        env->restartPhysics();
        env->restoreNodeTransformations(physScene->getRootNode());
        CHECK(std::fabs(body->getLocalPos().y() - y0) < 0.01f, "restart: node transform restored");
        env->stopPhysics();
        CHECK(!env->isSimulating(), "stop: simulation flag cleared");
        physScene.reset(); body.reset();
    }

    // --- Teardown with no GL must not crash either
    scene.reset(); light.reset(); meshNode.reset(); mat.reset(); tex.reset();
    CHECK(true, "document destroyed without GL");

    printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
