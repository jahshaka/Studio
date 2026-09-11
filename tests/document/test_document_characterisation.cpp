// Characterisation test: what the iris:: scene DOCUMENT is, field by field.
//
// It was born as "the document can be built with NO GL context" — step 1 of
// VIEWPORT_MIGRATION_PLAN.md, and the single biggest de-risk of the whole
// migration, because Studio keeps iris::Scene/SceneNode/MeshNode/LightNode as
// its document model and mirrors it into the engine, which only works if the
// document does not need OpenGL to exist. Back then Scene::Scene() compiled a
// shader, LightNode::LightNode() allocated a QOpenGLTexture, and Texture2D::load
// called qFatal without a current context.
//
// THAT PREMISE IS RETIRED (TEST_GATE_AUDIT.md §2, 2026-09-09). The legacy GL
// renderer was DELETED at step 14 on 2026-08-30: there is no GL in this tree to
// be accidentally touched, the suite's Qt6::OpenGL link bought nothing, and its
// "no GL context is current" precondition asserted something about a world that
// no longer exists. What the body has always actually done — and what it is
// named for now — is CHARACTERISE the document model: every node, every
// property, every default, in a suite that boots the NULL render system and
// runs with no display at all.
//
// Runs under QT_QPA_PLATFORM=offscreen. Framework-free; non-zero exit on failure.
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QFile>
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
#include "irisgl/core/properties/property.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicsproperties.h"
// The reparent command's cycle guard is header-only document logic (its
// undo/redo bodies live in the app; only the static guard is exercised here).
#include "commands/reparentscenenodecommand.h"
// The scene FILE format's retired-node-type table (header-only, no reader).
#include "io/sceneformat.h"

#include "../support/documentgraph.h"
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // v1 INTERIM (SPECS/SCENEGRAPH_SPEC.md §3): a document node IS an engine
    // node now, so even a document-only suite needs an engine. Declared here,
    // before anything builds a document, and destroyed last.
    enginetest::DocumentGraph graph("document-no-gl-ogre.log");
    if (!graph.require()) return 1;
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
        ++count; for (auto &c : n->children()) walk(c);
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
        body->setLocalPos(iris::Vec3(0, 10, 0));
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
        for (int i = 0; i < 60; ++i) physScene->advance(1.0f / 60.0f);  // syncFrame's editor-mode stepper
        CHECK(body->getLocalPos().y() < y0 - 0.5f, "simulate: dynamic body fell under gravity via scene->update");
        env->restartPhysics();
        env->restoreNodeTransformations(physScene->getRootNode());
        CHECK(std::fabs(body->getLocalPos().y() - y0) < 0.01f, "restart: node transform restored");
        env->stopPhysics();
        CHECK(!env->isSimulating(), "stop: simulation flag cleared");
        physScene.reset(); body.reset();
    }

    // --- A1.4: a physics body on a node that is NOT a mesh must not crash.
    // PhysicsHelper::createPhysicsBody used to open with an unguarded
    // `sceneNode.staticCast<iris::MeshNode>()` and the three geometry shapes
    // then called getMesh() on it. The UI only sets isPhysicsBody on meshes —
    // but SceneReader reads "physicsObject" for EVERY node kind, so a
    // hand-authored file (which is exactly what this block builds) reached it.
    {
        auto physScene = iris::Scene::create();

        // (i) a LIGHT asking for a convex hull — the crash case
        auto lamp = iris::LightNode::create();
        lamp->setGUID("a14-light-hull");
        lamp->setLocalPos(iris::Vec3(0, 3, 0));
        lamp->isPhysicsBody = true;
        lamp->physicsProperty.objectMass = 1.0f;
        lamp->physicsProperty.shape = iris::PhysicsCollisionShape::ConvexHull;
        lamp->physicsProperty.type = iris::PhysicsType::RigidBody;
        physScene->getRootNode()->addChild(lamp);

        // (ii) a plain empty asking for a triangle mesh
        auto empty = iris::SceneNode::create();
        empty->setGUID("a14-empty-trimesh");
        empty->isPhysicsBody = true;
        empty->physicsProperty.objectMass = 0.0f;
        empty->physicsProperty.shape = iris::PhysicsCollisionShape::TriangleMesh;
        empty->physicsProperty.type = iris::PhysicsType::Static;
        physScene->getRootNode()->addChild(empty);

        // (iii) a camera asking for a compound (no mesh anywhere in its subtree)
        auto cam = iris::CameraNode::create();
        cam->setGUID("a14-camera-compound");
        cam->isPhysicsBody = true;
        cam->physicsProperty.objectMass = 0.0f;
        cam->physicsProperty.shape = iris::PhysicsCollisionShape::Compound;
        cam->physicsProperty.type = iris::PhysicsType::Static;
        physScene->getRootNode()->addChild(cam);

        // (iv) a real MeshNode with NO mesh loaded — same hole, mesh-typed
        auto emptyMesh = iris::MeshNode::create();
        emptyMesh->setGUID("a14-meshnode-no-mesh");
        emptyMesh->isPhysicsBody = true;
        emptyMesh->physicsProperty.objectMass = 1.0f;
        emptyMesh->physicsProperty.shape = iris::PhysicsCollisionShape::TriangleMesh;
        emptyMesh->physicsProperty.type = iris::PhysicsType::RigidBody;
        physScene->getRootNode()->addChild(emptyMesh);

        // (v) the shapes that never needed a mesh must still work on a non-mesh
        auto sphereLight = iris::LightNode::create();
        sphereLight->setGUID("a14-light-sphere");
        sphereLight->setLocalPos(iris::Vec3(2, 8, 0));
        sphereLight->isPhysicsBody = true;
        sphereLight->physicsProperty.objectMass = 1.0f;
        sphereLight->physicsProperty.shape = iris::PhysicsCollisionShape::Sphere;
        sphereLight->physicsProperty.type = iris::PhysicsType::RigidBody;
        physScene->getRootNode()->addChild(sphereLight);

        auto env = physScene->getPhysicsEnvironment();
        env->initializePhysicsWorldFromScene(physScene->getRootNode());
        CHECK(true, "A1.4: a non-mesh node with a mesh-shaped physics body does not crash");
        CHECK(env->hashBodies.contains("a14-light-hull")
                  && env->hashBodies.contains("a14-empty-trimesh")
                  && env->hashBodies.contains("a14-camera-compound")
                  && env->hashBodies.contains("a14-meshnode-no-mesh"),
              "A1.4: every degraded body still exists in the world (constraints and "
              "transform sync keep resolving)");
        CHECK(env->hashBodies.contains("a14-light-sphere"),
              "A1.4: the mesh-free shapes still build on a non-mesh node");

        // it still simulates: the degraded bodies collide with nothing, the
        // sphere falls.
        env->simulatePhysics();
        const float y0 = sphereLight->getLocalPos().y();
        for (int i = 0; i < 60; ++i) physScene->advance(1.0f / 60.0f);
        CHECK(sphereLight->getLocalPos().y() < y0 - 0.5f,
              "A1.4: the world still steps with degraded bodies in it");
        env->stopPhysics();
        env->destroyPhysicsWorld();
        physScene.reset();
    }

    // --- A1.5: Scene::removeNode's typed registries. The four reverse lookups
    // became O(1) guid removals; the SEMANTICS asserted here are what must not
    // change — the node leaves every registry it was in, its children leave
    // with it, and removing a node the scene never had touches nothing.
    {
        auto s2 = iris::Scene::create();
        auto lamp = iris::LightNode::create();       lamp->setGUID("a15-light");
        auto mesh2 = iris::MeshNode::create();       mesh2->setGUID("a15-mesh");
        auto parts = iris::ParticleSystemNode::create(); parts->setGUID("a15-particles");
        auto cam2 = iris::CameraNode::create();      cam2->setGUID("a15-camera");
        auto child = iris::MeshNode::create();       child->setGUID("a15-child");

        s2->getRootNode()->addChild(lamp);
        s2->getRootNode()->addChild(mesh2);
        s2->getRootNode()->addChild(parts);
        s2->getRootNode()->addChild(cam2);
        mesh2->addChild(child);

        CHECK(s2->lights.contains("a15-light") && s2->meshes.contains("a15-mesh")
                  && s2->particleSystems.contains("a15-particles")
                  && s2->cameras.contains("a15-camera")
                  && s2->meshes.contains("a15-child"),
              "A1.5: addNode registers each node under its own guid");

        // A node the scene never saw: removing it must not evict anything —
        // and specifically not the entry keyed with the EMPTY string, which is
        // what `hash.remove(hash.key(missing))` did (QHash::key() returns a
        // default-constructed QString when the value is absent). The unnamed
        // light below is registered under "" precisely so that bug has
        // something to destroy.
        auto unnamed = iris::LightNode::create(); unnamed->setGUID(QString());
        s2->getRootNode()->addChild(unnamed);
        CHECK(s2->lights.contains(QString()),
              "A1.5: a guid-less light registers under the empty key");
        auto stranger = iris::LightNode::create(); stranger->setGUID("a15-stranger");
        const int lightsBefore = s2->lights.size();
        s2->removeNode(stranger);
        CHECK(s2->lights.size() == lightsBefore && s2->lights.contains("a15-light"),
              "A1.5: removing an unregistered node evicts nothing");
        CHECK(s2->lights.contains(QString()),
              "A1.5: and specifically not the empty-keyed entry (the old reverse "
              "lookup removed it)");

        s2->removeNode(lamp);
        CHECK(!s2->lights.contains("a15-light") && !s2->nodes.contains("a15-light"),
              "A1.5: a light leaves both the light registry and the node map");

        s2->removeNode(parts);
        CHECK(!s2->particleSystems.contains("a15-particles"), "A1.5: particles deregister");

        CHECK(s2->setActiveCamera("a15-camera"), "A1.5: the camera can be made active");
        s2->removeNode(cam2);
        CHECK(!s2->cameras.contains("a15-camera"), "A1.5: cameras deregister");
        CHECK(s2->getActiveCameraGuid().isEmpty(),
              "A1.5: deleting the ACTIVE camera clears the active guid");

        s2->removeNode(mesh2);
        CHECK(!s2->meshes.contains("a15-mesh") && !s2->meshes.contains("a15-child"),
              "A1.5: removeNode recurses — the child left the mesh registry too");
        CHECK(!s2->nodes.contains("a15-child"), "A1.5: and the node map");
        s2.reset();
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

        plain.reset(); refLight.reset(); refMesh.reset(); refCam.reset();
        refParticles.reset();
    }

    // --- Transform propagation through the ONE tree -------------------------
    // SPECS/SCENEGRAPH_SPEC.md D2: there are no document dirty flags and no
    // cached matrices any more — a node's world transform is Ogre's, resolved
    // on demand (Node::_getFullTransformUpdated) for the readers that need one
    // between frames and by the engine's threaded SIMD pass inside the frame.
    // The three assertions this block used to carry about CACHE STALENESS (an
    // idle update leaving a hand-corrupted matrix alone, and setTransformDirty
    // repairing it) describe a mechanism that no longer exists and are gone
    // with it; every assertion about what a mutator MEANS is kept, and they now
    // hold without any update() call at all.
    {
        auto approx = [](const iris::Vec3 &a, const iris::Vec3 &b) {
            return (a - b).length() < 1e-4f;
        };
        auto worldPos = [](const iris::SceneNodePtr &n) {
            return n->getGlobalTransform().column(3).toVector3D();
        };

        auto tScene = iris::Scene::create();
        auto tRoot  = tScene->getRootNode();
        auto mid    = iris::SceneNode::create();
        auto leaf   = iris::SceneNode::create();
        tRoot->addChild(mid);
        mid->addChild(leaf);

        mid->setLocalPos(iris::Vec3(10, 0, 0));
        leaf->setLocalPos(iris::Vec3(0, 5, 0));
        tScene->refresh();
        CHECK(approx(worldPos(leaf), iris::Vec3(10, 5, 0)),
              "invalidation: a fresh hierarchy composes on the first update");

        // The case the flags cannot get from setTransformDirty alone: a node
        // moves and its DESCENDANTS' world transforms must follow. The dirty
        // flag propagates upward; only update() can push it down.
        mid->setLocalPos(iris::Vec3(-4, 0, 0));
        tScene->refresh();
        CHECK(approx(worldPos(leaf), iris::Vec3(-4, 5, 0)),
              "invalidation: moving a parent refreshes the whole subtree below it");

        // Nothing moved: the same answer, and the value the caller gets back is
        // a COPY — writing to it cannot corrupt anything (the old
        // getGlobalTransform() handed out a reference to a member cache and
        // wrote it from a read; audit F2).
        tScene->refresh();
        CHECK(approx(worldPos(leaf), iris::Vec3(-4, 5, 0)),
              "propagation: an idle update changes nothing");
        leaf->getGlobalTransform().translate(iris::Vec3(100, 100, 100));
        CHECK(approx(worldPos(leaf), iris::Vec3(-4, 5, 0)),
              "propagation: the world transform is a value, not a writable cache");

        // Every remaining mutator, each asserted through the cache.
        leaf->setLocalRot(iris::Quat::fromEulerAngles(0, 90, 0));
        leaf->setLocalPos(iris::Vec3(0, 0, 3));
        tScene->refresh();
        CHECK(approx(worldPos(leaf), iris::Vec3(-4, 0, 3)),
              "invalidation: setLocalPos/setLocalRot");

        leaf->rotate(iris::Quat::fromEulerAngles(0, 90, 0));
        mid->setLocalScale(iris::Vec3(2, 2, 2));
        tScene->refresh();
        CHECK(approx(worldPos(leaf), iris::Vec3(-4, 0, 6)),
              "invalidation: rotate() and setLocalScale()");

        iris::Mat4 lt; lt.setToIdentity(); lt.translate(iris::Vec3(1, 1, 1));
        leaf->setLocalTransform(lt);
        tScene->refresh();
        CHECK(approx(worldPos(leaf), iris::Vec3(-2, 2, 2)),
              "invalidation: setLocalTransform()");

        // SCENE_STATIC (SPECS/SCENEGRAPH_SPEC.md §6), the four rules in
        // nodegraph.h, each pinned. This is the whole of the swap's per-frame
        // headroom: Ogre's transform and bounds passes skip the static memory
        // manager entirely on frames nothing in it changed.
        {
            // RULE 2 — a static node's parent must be static or be the scene
            // ROOT. `host` hangs off the root, so it may be static; `stat`
            // under a DYNAMIC host may not.
            auto host = iris::SceneNode::create();
            tRoot->addChild(host, false);
            auto stat = iris::SceneNode::create();
            host->addChild(stat, false);
            CHECK(!stat->staticHint(), "static: a fresh node is dynamic");
            stat->setStaticHint(true);
            CHECK(!stat->isStaticInGraph(),
                  "static: refused under a dynamic parent (rule 2 — it would freeze there)");
            stat->_clearStaticHint();

            CHECK(host->isStaticEligible(), "static: a plain empty is eligible");
            host->setStaticHint(true);
            CHECK(host->isStaticInGraph(), "static: a child of the scene root may be static");

            // RULE 1 — static is a SUBTREE property: marking `host` took its
            // whole subtree with it, because Ogre gives a child its parent's
            // memory-manager class and propagates that down on every reparent.
            CHECK(stat->isStaticInGraph(), "static: the whole subtree went with it (rule 1)");

            // ...and a static subtree still resolves its world transforms.
            host->setStaticHint(false);
            host->setLocalPos(iris::Vec3(5, 0, 0));
            host->setStaticHint(true);
            stat->setStaticHint(false);
            stat->setLocalPos(iris::Vec3(1, 0, 0));
            stat->setStaticHint(true);
            tScene->refresh();
            CHECK(approx(worldPos(stat), iris::Vec3(6, 0, 0)),
                  "static: a static subtree resolves against its parent");

            // RULE 4 — MOVING A STATIC NODE PROMOTES IT, and clears the hint.
            // (v1 answered a static move with notifyStaticDirty, which costs a
            // whole static pass on the next frame — every frame, for a node
            // being dragged.)
            stat->setLocalPos(iris::Vec3(2, 0, 0));
            CHECK(!stat->isStaticInGraph() && !stat->staticHint(),
                  "static: a transform write demotes the node and clears the hint (rule 4)");
            tScene->refresh();
            CHECK(approx(worldPos(stat), iris::Vec3(7, 0, 0)),
                  "static: ...and the moved node lands where it was put");

            // RULE 3 / eligibility — node kinds whose engine attachment cannot
            // change memory-manager class refuse the hint outright.
            auto lamp = iris::LightNode::create();
            tRoot->addChild(lamp, false);
            CHECK(!lamp->isStaticEligible(), "static: a light is never eligible");
            lamp->setStaticHint(true);
            CHECK(!lamp->staticHint() && !lamp->isStaticInGraph(),
                  "static: setStaticHint on a light changes nothing");
            lamp->removeFromParent();

            // Eligibility also refuses anything that is going to move: a
            // physics body, a socket rider, an animated node.
            auto body = iris::SceneNode::create();
            tRoot->addChild(body, false);
            body->isPhysicsBody = true;
            CHECK(!body->isStaticEligible(), "static: a physics body is never eligible");
            body->isPhysicsBody = false;
            body->setSocketAttachment("owner-guid", "head");
            CHECK(!body->isStaticEligible(), "static: a socket rider is never eligible");
            body->setSocketAttachment(QString(), QString());
            CHECK(body->isStaticEligible(), "static: ...and eligible again once neither holds");
            body->removeFromParent();

            // THE DEFAULT POLICY: applyStaticDefaults marks a whole freshly
            // added branch wherever rule 2 and eligibility allow.
            auto branch = iris::SceneNode::create();
            auto twig = iris::SceneNode::create();
            branch->addChild(twig, false);
            tRoot->addChild(branch, false);
            CHECK(!branch->isStaticInGraph(), "static: a freshly added branch is dynamic");
            branch->applyStaticDefaults();
            CHECK(branch->isStaticInGraph() && twig->isStaticInGraph(),
                  "static: applyStaticDefaults marks the whole eligible branch");

            // ...and an INELIGIBLE node inside it is SKIPPED, not a veto: one
            // lamp in a building must not deny the building its classification.
            auto lampBranch = iris::SceneNode::create();
            auto innerLamp = iris::LightNode::create();
            auto sibling = iris::SceneNode::create();
            lampBranch->addChild(innerLamp, false);
            lampBranch->addChild(sibling, false);
            tRoot->addChild(lampBranch, false);
            lampBranch->applyStaticDefaults();
            CHECK(lampBranch->isStaticInGraph() && sibling->isStaticInGraph(),
                  "static: a light inside a branch does not veto the branch");
            CHECK(!innerLamp->isStaticInGraph(),
                  "static: ...and the light itself stays dynamic");
            lampBranch->removeFromParent();
            branch->removeFromParent();

            host->setStaticHint(false);
            host->removeFromParent();
        }

        // setGlobalPos/setGlobalRot on a node WITH a parent, and on one
        // without (the root).
        leaf->setGlobalPos(iris::Vec3(7, 7, 7));
        tScene->refresh();
        CHECK(approx(worldPos(leaf), iris::Vec3(7, 7, 7)), "invalidation: setGlobalPos() under a parent");

        tRoot->setGlobalPos(iris::Vec3(0, 1, 0));
        tScene->refresh();
        CHECK(approx(worldPos(tRoot), iris::Vec3(0, 1, 0)),
              "invalidation: setGlobalPos() on a parentless node (the early-return path)");
        CHECK(approx(worldPos(leaf), iris::Vec3(7, 8, 7)),
              "...and it carried the subtree with it");

        tRoot->setGlobalRot(iris::Quat::fromEulerAngles(0, 180, 0));
        tScene->refresh();
        CHECK(approx(worldPos(leaf), iris::Vec3(-7, 8, -7)),
              "invalidation: setGlobalRot() on a parentless node");
        tRoot->setLocalRot(iris::Quat());
        tRoot->setLocalPos(iris::Vec3());

        // Reparenting: the world transform changes even when the local one does
        // not, and insertChild's keepTransform branch re-expresses the world
        // transform in the new parent's space.
        auto other = iris::SceneNode::create();
        other->setLocalPos(iris::Vec3(0, 0, 20));
        tRoot->addChild(other);
        tScene->refresh();
        const iris::Vec3 before = worldPos(leaf);
        other->addChild(leaf, true);                     // keepTransform
        tScene->refresh();
        CHECK(approx(worldPos(leaf), before),
              "invalidation: a keepTransform reparent leaves the node where it was");
        other->addChild(mid, false);                     // no keepTransform
        tScene->refresh();
        CHECK(approx(worldPos(mid), iris::Vec3(0, 0, 20) + mid->getLocalPos()),
              "invalidation: a plain reparent recomposes against the new parent");

        // Losing a parent: the local transform becomes the global one.
        mid->removeFromParent();
        mid->update(0.0f);
        CHECK(approx(worldPos(mid), mid->getLocalPos()),
              "invalidation: removeFromParent() recomposes the orphan");

        // The property-animation path (which now goes through the setters).
        {
            auto animated = iris::SceneNode::create();
            tRoot->addChild(animated);
            auto anim = iris::Animation::create("t");
            auto *pa = new iris::Vector3DPropertyAnim();
            pa->setName("position");
            for (int axis = 0; axis < 3; ++axis) {
                pa->getKeyFrame(axis)->addKey(0.0f, 0.0);
                pa->getKeyFrame(axis)->addKey(axis == 0 ? 9.0f : 0.0f, 1.0);
            }
            anim->addPropertyAnim(pa);
            anim->setLooping(false);          // sample at t=1 == the last key
            animated->setAnimation(anim);
            tScene->refresh();
            tScene->updateSceneAnimation(1.0f);
            tScene->refresh();
            CHECK(approx(worldPos(animated), iris::Vec3(9, 0, 0)),
                  "invalidation: the property-animation path marks the node dirty");
        }

        // CameraNode::lookAt writes the node's TRS directly instead of going
        // through the setters. (ViewerNode::setViewScale was the other one; the
        // node type was removed by AVATAR_LOCOMOTION_SPEC Stage 0.)
        {
            auto cam = iris::CameraNode::create();
            cam->setLocalPos(iris::Vec3(0, 0, 5));
            cam->update(0.0f);
            cam->lookAt(iris::Vec3(0, 0, 0));
            cam->update(0.0f);
            const iris::Vec3 fwd = (cam->getGlobalTransform() * iris::Vec4(0, 0, -1, 0)).toVector3D();
            CHECK(fwd.z() < -0.9f, "invalidation: CameraNode::lookAt()");
        }

        // CameraNode::lookAt AT THE POLE (smoke L10 item 1). The basis was built
        // against a fixed world +Y, and a view straight down (or up) is parallel
        // to it: the cross product is zero, and the decomposed rotation was
        // whatever the degenerate matrix said — editor.setCamera({position:
        // {0,9,0}, lookAt: {0,0,0}}) pitched -36.87 degrees instead of -90.
        {
            const auto forwardOf = [](const iris::CameraNodePtr &c) {
                return c->getLocalRot().rotatedVector(iris::Vec3(0, 0, -1));
            };
            const auto near3 = [](const iris::Vec3 &a, const iris::Vec3 &b, float tol) {
                return (a - b).length() <= tol;
            };
            auto top = iris::CameraNode::create();
            top->setLocalPos(iris::Vec3(0, 9, 0));
            top->lookAt(iris::Vec3(0, 0, 0));
            float pitch = 0, yaw = 0, roll = 0;
            top->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
            std::printf("    straight down: pitch %.4f yaw %.4f roll %.4f\n",
                        double(pitch), double(yaw), double(roll));
            CHECK(std::fabs(pitch + 90.0f) < 1e-3f,
                  "lookAt straight DOWN pitches the camera -90 degrees (within 1e-3)");
            CHECK(near3(forwardOf(top), iris::Vec3(0, -1, 0), 1e-5f),
                  "...and it really looks down -Y");
            CHECK(near3(top->getLocalPos(), iris::Vec3(0, 9, 0), 1e-6f),
                  "...without moving the camera");
            // A fresh camera faces -Z, so the top of the frame is -Z: the same
            // pose the top axis view takes (yaw 0, pitch -90).
            CHECK(near3(top->getLocalRot().rotatedVector(iris::Vec3(0, 1, 0)),
                        iris::Vec3(0, 0, -1), 1e-5f),
                  "...with the camera's former heading (-Z) at the top of the frame");

            auto bottom = iris::CameraNode::create();
            bottom->setLocalPos(iris::Vec3(0, -4, 0));
            bottom->lookAt(iris::Vec3(0, 3, 0));
            bottom->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
            CHECK(std::fabs(pitch - 90.0f) < 1e-3f,
                  "lookAt straight UP pitches the camera +90 degrees (within 1e-3)");
            CHECK(near3(forwardOf(bottom), iris::Vec3(0, 1, 0), 1e-5f), "...and looks up +Y");

            // The heading SURVIVES the pole: a camera turned 45 degrees and then
            // aimed straight down keeps its right-hand side where it was, which
            // is what "look down from where I stand" means — not a snap to +X.
            auto turned = iris::CameraNode::create();
            turned->setLocalPos(iris::Vec3(2, 5, -1));
            turned->setLocalRot(iris::Quat::fromEulerAngles(0.0f, 45.0f, 0.0f));
            const iris::Vec3 rightBefore = turned->getLocalRot().rotatedVector(iris::Vec3(1, 0, 0));
            turned->lookAt(iris::Vec3(2, -3, -1));
            CHECK(near3(forwardOf(turned), iris::Vec3(0, -1, 0), 1e-5f),
                  "a yawed camera aimed straight down looks down");
            CHECK(near3(turned->getLocalRot().rotatedVector(iris::Vec3(1, 0, 0)), rightBefore, 1e-5f),
                  "...and keeps its heading (its right vector is unchanged)");

            // Off the pole NOTHING moved: the ordinary case still builds its
            // basis against world +Y (the default editor camera's pose, and so
            // the self-test's pixels, ride this path).
            auto ordinary = iris::CameraNode::create();
            ordinary->setLocalPos(iris::Vec3(0, 3, 12));
            ordinary->lookAt(iris::Vec3(0, 0, 0));
            iris::Mat4 ref;
            ref.setToIdentity();
            ref.lookAt(iris::Vec3(0, 3, 12), iris::Vec3(0, 0, 0), iris::Vec3(0, 1, 0));
            ref = ref.inverted();
            auto oldPath = iris::CameraNode::create();   // what lookAt did before, verbatim
            oldPath->setLocalTransform(ref);
            const iris::Quat refRot = oldPath->getLocalRot();
            const iris::Quat got = ordinary->getLocalRot();
            CHECK(got.x() == refRot.x() && got.y() == refRot.y() && got.z() == refRot.z() &&
                      got.scalar() == refRot.scalar(),
                  "an off-pole lookAt is BIT-IDENTICAL to the fixed +Y basis it always used");
        }

        tScene->cleanup();
    }

    // --- A model read from a Qt RESOURCE is dispatched by its extension ------
    // (smoke L10 item 4). Mesh::loadMesh reads a ":/" path into memory and hands
    // assimp the bytes; with no extension hint assimp can only SNIFF the first
    // 200 bytes for a format keyword, so a comment block above `mtllib` made
    // the default scene's ground unloadable ("error parsing file") on
    // 2026-09-11. The fixture's first ~800 bytes are comments with no OBJ
    // keyword in them.
    {
        const QString res = QStringLiteral(":/document-fixtures/commented_header.obj");
        QFile probe(res);
        CHECK(probe.open(QIODevice::ReadOnly) && probe.read(200).indexOf("mtllib") < 0,
              "fixture: the resource exists and its first 200 bytes hold no format keyword");
        auto mesh = iris::Mesh::loadMesh(res);
        CHECK(!mesh.isNull() && mesh->numVerts > 0,
              "an OBJ resource with 300+ bytes of leading comments LOADS (extension hint)");
        auto node = iris::MeshNode::create();
        node->setMesh(res);
        CHECK(!node->getMesh().isNull(),
              "...through MeshNode::setMesh too — the default scene's ground path");
    }

    // --- Retired node types (AVATAR_LOCOMOTION_SPEC Stage 0) ---------------
    // `viewer` (iris::ViewerNode) was removed with its 2016 character
    // controller. A file written by an older build can still carry one, and the
    // reader's contract for such a node is SKIP + log, never "guess a
    // substitute type" and never a crash — sceneformat::isRetiredNodeType is
    // the table it keys on, and SceneReader::readSceneNode returns null for a
    // match (both of its child loops, plus all four external call sites, take
    // the null).
    {
        CHECK(sceneformat::isRetiredNodeType(QStringLiteral("viewer")),
              "format: 'viewer' is a RETIRED node type -> the reader skips it");

        // Every type string the writer still emits must NOT be retired: a typo
        // in that table would silently delete every node of a live type from
        // every file that is opened.
        const char *live[] = { "empty", "mesh", "light", "camera",
                               "particle system", "decal" };
        bool anyLiveRetired = false;
        for (const char *t : live)
            if (sceneformat::isRetiredNodeType(QString::fromLatin1(t))) anyLiveRetired = true;
        CHECK(!anyLiveRetired,
              "format: no LIVE node type string is in the retired table");

        // An UNRECOGNISED type is not the same thing: it keeps the format's
        // long-standing tolerance and reads as an Empty placeholder.
        CHECK(!sceneformat::isRetiredNodeType(QStringLiteral("something-newer")),
              "format: an unknown type is not 'retired' (it still reads as empty)");
        CHECK(!sceneformat::isRetiredNodeType(QString()),
              "format: an absent type string is not 'retired'");
    }

    // --- Platform deep audit 2026-09-10, document-layer defects (B5.1, B5.2, B8.1, B11.1)
    {
        // B8.1: a skeleton is no longer a strong-pointer cycle — dropping the
        // last reference frees every bone.
        QWeakPointer<iris::Bone> observed;
        {
            auto skel = iris::Skeleton::create();
            auto root = iris::Bone::create("root");
            auto tip  = iris::Bone::create("tip");
            root->addChild(tip);
            skel->addBone(root); skel->addBone(tip);
            observed = root;
            CHECK(tip->parent() == root, "bone: parent() reads the weak back-link");
        }
        CHECK(observed.isNull(), "bone: the skeleton is freed when its last reference drops (no parent/child cycle)");

        // B5.1: a MeshNode is born with meshIndex 0, not whatever the stack held.
        auto fresh = iris::MeshNode::create();
        CHECK(fresh->meshIndex == 0, "meshnode: meshIndex initialised to 0 at birth");

        // B5.2: duplicating a physics body keeps its physics.
        auto body = iris::MeshNode::create();
        body->isPhysicsBody = true;
        body->physicsProperty.objectMass = 3.5f;
        body->physicsProperty.type = iris::PhysicsType::RigidBody;
        auto copy = body->duplicate();
        CHECK(copy && copy->isPhysicsBody && std::fabs(copy->physicsProperty.objectMass - 3.5f) < 1e-6f
                  && copy->physicsProperty.type == iris::PhysicsType::RigidBody,
              "duplicate: a rigid body's copy is a rigid body with the same mass and type");

        // B11.1: a fresh Environment is not simulating (the flag used to be an
        // uninitialised read).
        iris::Environment env;
        CHECK(!env.isSimulating(), "environment: isSimulating() is false at birth");
    }

    // --- SMOKE_FIX S11: the material's PROPERTY ROWS are what gets SAVED, so a
    //     row that disagrees with its field is a scene that reopens wrong.
    //
    // The owner's floor checkers were squashed after a reopen. Node scale was
    // (1,1,1) in the file; what had drifted was the material's V tiling.
    // `setValue("textureScale", 4)` writes BOTH fields (the uniform overload)
    // but used to sync only the row it was NAMED with, so the live material
    // tiled 4x4 while SceneWriter serialized textureScale 4, textureScaleV 1 —
    // and the reader, applying rows in order, produced (4, 1) on reopen. Every
    // scene built by createDefaultScene was written that way.
    {
        auto pbr = iris::PbrMaterial::create();
        auto row = [&](const char *name) {
            for (auto prop : pbr->properties)
                if (prop->name == QLatin1String(name)) return prop->getValue().toFloat();
            return -1.0f;
        };
        CHECK(std::fabs(row("textureScale") - 1.0f) < 1e-6f &&
              std::fabs(row("textureScaleV") - 1.0f) < 1e-6f,
              "uv rows: a fresh PbrMaterial is (1, 1) in its rows");

        pbr->setValue(QStringLiteral("textureScale"), 4.0f);
        CHECK(std::fabs(pbr->textureScale - 4.0f) < 1e-6f &&
              std::fabs(pbr->textureScaleV - 4.0f) < 1e-6f,
              "uv fields: setValue(textureScale, 4) is uniform tiling");
        CHECK(std::fabs(row("textureScale") - 4.0f) < 1e-6f &&
              std::fabs(row("textureScaleV") - 4.0f) < 1e-6f,
              "uv rows: ...and BOTH rows follow, so the saved file says (4, 4)");

        // The deliberate non-uniform case still works, and still SAVES: a
        // caller sets V after U, which is the order both readers apply.
        pbr->setValue(QStringLiteral("textureScaleV"), 1.0f);
        CHECK(std::fabs(pbr->textureScale - 4.0f) < 1e-6f &&
              std::fabs(pbr->textureScaleV - 1.0f) < 1e-6f &&
              std::fabs(row("textureScale") - 4.0f) < 1e-6f &&
              std::fabs(row("textureScaleV") - 1.0f) < 1e-6f,
              "uv rows: an explicit (4, 1) survives in the fields AND the rows");

        // The two-argument setter is the same contract from C++.
        pbr->setTextureScale(2.0f, 3.0f);
        CHECK(std::fabs(row("textureScale") - 2.0f) < 1e-6f &&
              std::fabs(row("textureScaleV") - 3.0f) < 1e-6f,
              "uv rows: setTextureScale(u, v) writes both rows too");
    }

    // --- Teardown with no GL must not crash either
    scene.reset(); light.reset(); meshNode.reset(); mat.reset(); tex.reset();
    CHECK(true, "document destroyed without GL");

    printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
