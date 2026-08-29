#include "enginethumbnailrenderer.h"

#include <QColor>
#include <QQuaternion>
#include <QVector3D>
#include <QtMath>
#include <cstring>

#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/scenegraph/lightnode.h"
#include "irisgl/src/materials/defaultmaterial.h"
#include "engine/scenemirror.h"
#include "previewframing.h"

using namespace jahshaka::engine;

EngineThumbnailRenderer::EngineThumbnailRenderer(const std::shared_ptr<Engine> &engine)
    : mEngine(engine)
{
}

EngineThumbnailRenderer::~EngineThumbnailRenderer()
{
    release();
}

Colour EngineThumbnailRenderer::backgroundColour()
{
    // The legacy generator cleared to (25, 25, 25).
    return Colour(25 / 255.0f, 25 / 255.0f, 25 / 255.0f, 1.0f);
}

void EngineThumbnailRenderer::release()
{
    auto engine = mEngine.lock();
    if (mMirror) {
        if (engine && mScene) mMirror->setSource(nullptr);
        mMirror.reset();
    }
    if (engine) {
        if (mView)  { mView->setScene(nullptr); engine->destroyView(mView); }
        if (mScene) engine->destroyScene(mScene);
    }
    mView = nullptr;
    mScene = nullptr;
    mSphere.reset();
}

bool EngineThumbnailRenderer::ensureResources(QSize size)
{
    auto engine = mEngine.lock();
    if (!engine || size.width() <= 0 || size.height() <= 0) return false;

    if (!mView) {
        // The View must exist before the Scene (Engine.h: ORDER MATTERS).
        mView = engine->createOffscreenView("thumbs", unsigned(size.width()), unsigned(size.height()),
                                            backgroundColour());
        if (!mView) return false;
        mView->setEnabled(false);
    }
    if (!mScene) {
        mScene = engine->createScene("thumbs");
        if (!mScene) return false;
        mScene->setAmbient(Colour(0.45f, 0.45f, 0.45f), Colour(0.30f, 0.30f, 0.30f));
        mView->setScene(mScene);
        mMirror.reset(new SceneMirror(mScene));
        mMirror->setLightWires(false);   // a thumbnail never shows editor light helpers
    }
    if (mView->width() != unsigned(size.width()) || mView->height() != unsigned(size.height()))
        mView->resize(unsigned(size.width()), unsigned(size.height()));
    return true;
}

iris::ScenePtr EngineThumbnailRenderer::buildPreviewScene(iris::CameraNodePtr &cameraOut)
{
    auto scene = iris::Scene::create();
    scene->setSkyColor(QColor(25, 25, 25, 0));
    scene->setAmbientColor(QColor(190, 190, 190));
    scene->fogEnabled = false;
    scene->shadowEnabled = false;

    auto dlight = iris::LightNode::create();
    dlight->color = QColor(255, 255, 240);
    dlight->intensity = 0.76f;
    dlight->setLightType(iris::LightType::Directional);
    dlight->setName("Key Light");
    dlight->setLocalRot(QQuaternion::fromEulerAngles(45, 45, 0));
    dlight->setShadowMapType(iris::ShadowMapType::None);
    scene->rootNode->addChild(dlight);

    auto plight = iris::LightNode::create();
    plight->color = QColor(210, 210, 255);
    plight->intensity = 0.47f;
    plight->setLightType(iris::LightType::Point);
    plight->setName("Rim Light");
    plight->setLocalPos(QVector3D(0, 0, -3));
    plight->setShadowMapType(iris::ShadowMapType::None);
    scene->rootNode->addChild(plight);

    cameraOut = iris::CameraNode::create();
    cameraOut->setLocalPos(QVector3D(1, 1, 5));
    cameraOut->lookAt(QVector3D(0, 0.5f, 0));
    cameraOut->update(0);
    return scene;
}

static void collectBoundingSpheres(iris::SceneNodePtr node, QList<iris::BoundingSphere> &spheres)
{
    if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        if (meshNode->getMesh()) spheres.append(meshNode->getTransformedBoundingSphere());
    }
    for (auto child : node->children) collectBoundingSpheres(child, spheres);
}

static void frameCamera(iris::CameraNodePtr cam, iris::SceneNodePtr subject)
{
    // Same framing as the legacy generator: merge the subject's bounding spheres
    // and back the camera off along +Z until the sphere fits the vertical FOV.
    QList<iris::BoundingSphere> spheres;
    collectBoundingSpheres(subject, spheres);
    iris::BoundingSphere bound;
    if (spheres.count() == 0) {
        bound.pos = QVector3D(0, 0, 0);
        bound.radius = 1;
    } else if (spheres.count() == 1) {
        bound = spheres[0];
    } else {
        bound.pos = QVector3D(0, 0, 0);
        bound.radius = 1;
        for (auto &sphere : spheres) bound = iris::BoundingSphere::merge(bound, sphere);
    }
    const float dist = preview::framingDistance(bound.radius, cam->angle);
    // The clip planes must follow the framing distance: a large model (cm-scaled
    // glb) framed at ~2.9 * radius sat beyond the default farClip of 500 and
    // rendered a blank thumbnail (ASSETS_AUDIT.md finding 3).
    preview::clipPlanesForFraming(dist, bound.radius, cam->nearClip, cam->farClip);
    cam->setLocalPos(QVector3D(0, bound.pos.y(), dist));
    cam->lookAt(bound.pos);
    cam->update(0);
}

QImage EngineThumbnailRenderer::renderNode(iris::SceneNodePtr subject, QSize size)
{
    if (!subject) return QImage();
    iris::CameraNodePtr cam;
    auto document = buildPreviewScene(cam);
    document->rootNode->addChild(subject);
    frameCamera(cam, subject);
    QImage img = render(document, cam, size);
    subject->removeFromParent();   // the caller keeps its node; the document dies here
    return img;
}

QImage EngineThumbnailRenderer::renderMaterial(iris::MaterialPtr material, QSize size)
{
    if (!mSphere) {
        mSphere = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/sphere.obj"));
        if (!mSphere) return QImage();
    }
    auto node = iris::MeshNode::create();
    node->setMesh(mSphere);
    node->setMaterial(material ? material : iris::DefaultMaterial::create().staticCast<iris::Material>());

    iris::CameraNodePtr cam;
    auto document = buildPreviewScene(cam);
    document->rootNode->addChild(node);
    const float dist = 1.2f / qTan(qDegreesToRadians(cam->angle / 2.0f));
    cam->setLocalPos(QVector3D(0, 0, dist));
    cam->lookAt(QVector3D(0, 0, 0));
    cam->update(0);
    return render(document, cam, size);
}

QImage EngineThumbnailRenderer::render(iris::ScenePtr document, iris::CameraNodePtr camera, QSize size)
{
    auto engine = mEngine.lock();
    if (!engine || !ensureResources(size)) return QImage();

    document->update(0);
    mMirror->setSource(document);
    mMirror->sync();
    // Background from the document's sky (buildPreviewScene's 25,25,25 for asset
    // previews; a real scene's sky colour matches the viewport). Ambient stays the
    // renderer's own studio lighting — deliberately not applyEnvironment.
    mMirror->applySky(mView);
    mMirror->applyCamera(camera, mView);

    mView->setEnabled(true);
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    Image img;
    const bool ok = mView->readPixels(img);
    mView->setEnabled(false);

    // Nothing leaks across requests: drop every mirrored node, mesh and material.
    mMirror->setSource(nullptr);

    QImage result;
    if (ok && img.width && img.height) {
        result = QImage(int(img.width), int(img.height), QImage::Format_RGBA8888);
        for (unsigned y = 0; y < img.height; ++y)
            std::memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    }
    return result;
}
