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

// Specific headers only: the irisgl/IrisGL.h umbrella pulls in VR/libovr.
#include <functional>
#include "irisglfwd.h"
#include "scenegraph/scene.h"
#include "scenegraph/scenenode.h"
#include "scenegraph/lightnode.h"
#include "scenegraph/meshnode.h"
#include "graphics/mesh.h"
#include "graphics/shadowmap.h"
#include "graphics/texture2d.h"
#include "materials/defaultmaterial.h"
#include "materials/defaultskymaterial.h"

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
    CHECK(!!scene->skyMesh, "sky mesh loaded (CPU-side, buffers not uploaded)");
    CHECK(!!scene->skyMaterial, "sky material created (shader compiles lazily)");

    // --- LightNode: previously created a ShadowMap -> QOpenGLTexture in its ctor
    auto light = iris::LightNode::create();
    CHECK(!!light, "iris::LightNode constructed without GL");
    CHECK(light->shadowMap != nullptr, "light has a ShadowMap object");
    CHECK(!!light->shadowMap->shadowTexture, "shadow texture object exists");
    CHECK(light->shadowMap->shadowTexture->isDeferred(), "shadow texture creation is DEFERRED (no GL yet)");
    CHECK(light->shadowMap->shadowTexture->getWidth() == 2048, "deferred texture still reports its size");
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
    CHECK(tex->isDeferred(), "texture is deferred");
    CHECK(tex->getWidth() == 8 && tex->getHeight() == 8, "deferred texture reports image size");
    CHECK(tex->getTextureId() == 0, "getTextureId() is 0 (no GL) instead of crashing");
    tex->bind();            // must be a harmless no-op
    tex->bind(3);
    CHECK(true, "bind() without GL is a no-op");
    CHECK(!tex->ensureCreated(), "ensureCreated() reports false while no context exists");

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

    // --- Teardown with no GL must not crash either
    scene.reset(); light.reset(); meshNode.reset(); mat.reset(); tex.reset();
    CHECK(true, "document destroyed without GL");

    printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
