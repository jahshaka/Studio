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
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include <functional>
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/core/properties/property.h"
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

    // --- Reflection round-trip (IRISGL_ARCHITECTURE_AUDIT 3.1): every field the
    // node types newly reflect must be reachable through all three methods —
    // advertised by getProperties(), written by setPropertyValue(), read back
    // unchanged by getPropertyValue(). Document-only; no engine, no GL.
    {
        auto names = [](iris::SceneNodePtr n) {
            QSet<QString> out;
            const auto props = n->getProperties();
            for (auto *p : props) { out.insert(p->name); delete p; }
            return out;
        };
        auto advertises = [&](iris::SceneNodePtr n, const QStringList &keys, const QString &what) {
            const QSet<QString> have = names(n);
            QStringList missing;
            for (const auto &k : keys) if (!have.contains(k)) missing << k;
            CHECK(missing.isEmpty(),
                  qPrintable(QString("%1: getProperties() advertises %2 (missing: %3)")
                                 .arg(what).arg(keys.join(", ")).arg(missing.join(", "))));
        };
        auto roundTrip = [](iris::SceneNodePtr n, const QString &key, const QVariant &v,
                            const QString &what) {
            const bool set = n->setPropertyValue(key, v);
            const QVariant back = n->getPropertyValue(key);
            CHECK(set && back.isValid() && back.toString() == v.toString(),
                  qPrintable(QString("%1: %2 round-trips (wrote %3, read %4)")
                                 .arg(what).arg(key).arg(v.toString()).arg(back.toString())));
        };
        auto readOnly = [](iris::SceneNodePtr n, const QString &key, const QVariant &v,
                           const QString &what) {
            const QVariant before = n->getPropertyValue(key);
            const bool set = n->setPropertyValue(key, v);
            CHECK(!set && n->getPropertyValue(key).isValid() &&
                      n->getPropertyValue(key).toString() == before.toString(),
                  qPrintable(QString("%1: %2 is readable but refuses writes").arg(what).arg(key)));
        };

        // SceneNode
        auto plain = iris::SceneNode::create();
        advertises(plain, { "name", "visible", "castShadow", "pickable" }, "SceneNode");
        roundTrip(plain, "name", QString("renamed"), "SceneNode");
        roundTrip(plain, "visible", false, "SceneNode");
        roundTrip(plain, "castShadow", false, "SceneNode");
        roundTrip(plain, "pickable", false, "SceneNode");
        CHECK(!plain->getPropertyValue("noSuchProperty").isValid(),
              "SceneNode: unknown property reads as an invalid QVariant");
        CHECK(!plain->setPropertyValue("noSuchProperty", 1),
              "SceneNode: unknown property refuses writes");

        // LightNode
        auto refLight = iris::LightNode::create();
        advertises(refLight, { "lightType", "shadowColor", "shadowAlpha", "shadowMapType",
                               "shadowMapResolution", "shadowBias", "doubleSided",
                               "accurate", "iconSize", "name", "visible" }, "LightNode");
        roundTrip(refLight, "lightType", int(iris::LightType::Spot), "LightNode");
        roundTrip(refLight, "shadowAlpha", 0.25f, "LightNode");
        roundTrip(refLight, "shadowMapType", int(iris::ShadowMapType::VerySoft), "LightNode");
        roundTrip(refLight, "shadowMapResolution", 1024, "LightNode");
        roundTrip(refLight, "shadowBias", 0.05f, "LightNode");
        roundTrip(refLight, "doubleSided", true, "LightNode");
        roundTrip(refLight, "accurate", true, "LightNode");
        roundTrip(refLight, "iconSize", 1.5f, "LightNode");
        refLight->setPropertyValue("shadowColor", QColor(10, 20, 30));
        CHECK(refLight->getPropertyValue("shadowColor").value<QColor>() == QColor(10, 20, 30),
              "LightNode: shadowColor round-trips");
        CHECK(refLight->getPropertyValue("intensity").isValid(),
              "LightNode: the pre-existing keys still resolve");

        // MeshNode — meshPath/meshIndex are deliberately read-only
        auto refMesh = iris::MeshNode::create();
        refMesh->setMesh(QString(":assets/models/sky.obj"));
        advertises(refMesh, { "meshPath", "meshIndex", "faceCullingMode", "name" }, "MeshNode");
        roundTrip(refMesh, "faceCullingMode", int(iris::FaceCullingMode::Front), "MeshNode");
        readOnly(refMesh, "meshPath", QString("/somewhere/else.obj"), "MeshNode");
        readOnly(refMesh, "meshIndex", 7, "MeshNode");
        CHECK(refMesh->getPropertyValue("meshPath").toString() == QString(":assets/models/sky.obj"),
              "MeshNode: meshPath reads the loaded path");

        // CameraNode
        auto refCam = iris::CameraNode::create();
        advertises(refCam, { "aspectRatio", "angle", "nearClip", "farClip", "orthoSize",
                             "projMode", "vrViewScale" }, "CameraNode");
        roundTrip(refCam, "aspectRatio", 1.5f, "CameraNode");
        roundTrip(refCam, "angle", 60.0f, "CameraNode");
        roundTrip(refCam, "nearClip", 0.25f, "CameraNode");
        roundTrip(refCam, "farClip", 250.0f, "CameraNode");
        roundTrip(refCam, "orthoSize", 4.0f, "CameraNode");
        roundTrip(refCam, "vrViewScale", 3.0f, "CameraNode");
        roundTrip(refCam, "projMode", int(iris::CameraProjection::Orthogonal), "CameraNode");
        CHECK(refCam->isPerspective == false,
              "CameraNode: setting projMode keeps isPerspective in lock-step");

        // ParticleSystemNode — exactly the keys SceneWriter::writeParticleData writes
        auto refParticles = iris::ParticleSystemNode::create();
        advertises(refParticles, { "particlesPerSecond", "particleScale", "dissipate",
                                   "dissipateInv", "gravityComplement", "randomRotation",
                                   "blendMode", "lifeLength", "speed", "texture", "visible" },
                   "ParticleSystemNode");
        roundTrip(refParticles, "particlesPerSecond", 48.0f, "ParticleSystemNode");
        roundTrip(refParticles, "particleScale", 2.0f, "ParticleSystemNode");
        roundTrip(refParticles, "gravityComplement", 0.5f, "ParticleSystemNode");
        roundTrip(refParticles, "lifeLength", 3.0f, "ParticleSystemNode");
        roundTrip(refParticles, "speed", 7.0f, "ParticleSystemNode");
        roundTrip(refParticles, "dissipate", false, "ParticleSystemNode");
        roundTrip(refParticles, "dissipateInv", true, "ParticleSystemNode");
        roundTrip(refParticles, "randomRotation", false, "ParticleSystemNode");
        roundTrip(refParticles, "blendMode", false, "ParticleSystemNode");
        readOnly(refParticles, "texture", QString("/some/other.png"), "ParticleSystemNode");

        // ViewerNode
        auto refViewer = iris::ViewerNode::create();
        advertises(refViewer, { "viewScale", "activeCharacterController" }, "ViewerNode");
        roundTrip(refViewer, "viewScale", 3.5f, "ViewerNode");
        roundTrip(refViewer, "activeCharacterController", true, "ViewerNode");
        CHECK(qFuzzyCompare(refViewer->getLocalScale().x(), 3.5f),
              "ViewerNode: viewScale also drives the node scale (its own setter)");

        plain.reset(); refLight.reset(); refMesh.reset(); refCam.reset();
        refParticles.reset(); refViewer.reset();
    }

    // --- Teardown with no GL must not crash either
    scene.reset(); light.reset(); meshNode.reset(); mat.reset(); tex.reset();
    CHECK(true, "document destroyed without GL");

    printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
