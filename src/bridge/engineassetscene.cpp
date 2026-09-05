#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "bridge/engineassetscene.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <QColor>
#include <QFileInfo>
#include <QtMath>

#include "irisgl/mirror/scenemirror.h"
#include "viewport/previewframing.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/geometry/boundingsphere.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"

using namespace jahshaka::engine;

namespace {

const char *kFloorName = "ae98cx7u_floor";

// World-space bounds: mesh AABBs through the full global transform (scale and
// rotation included — ASSETS_AUDIT.md finding 4; the legacy getNodeBoundingBox
// only offset by position, framing a 0.0143-scaled model at its unscaled radius).
iris::AABB nodeBoundingBox(iris::SceneNodePtr node)
{
    return preview::worldBoundingBox(node);
}

float lerp(float a, float b, float t) { return a * (1 - t) + b * t; }

} // namespace

EngineAssetScene::EngineAssetScene(const std::shared_ptr<Engine> &engine)
    : mEngine(engine)
{
    buildDocument();
}

EngineAssetScene::~EngineAssetScene()
{
    release();
}

void EngineAssetScene::buildDocument()
{
    // AssetViewer::initializeGL, minus the GL: the same lights, floor, sky and camera.
    mDocument = iris::Scene::create();
    mDocument->shadowEnabled = true;

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    dlight->setName("ae98cx7u");
    dlight->color = QColor(255, 255, 240);
    dlight->setLocalRot(iris::Quat::fromEulerAngles(45, 45, 0));
    dlight->intensity = 0.76f;
    dlight->setShadowMapType(iris::ShadowMapType::Soft);
    dlight->isBuiltIn = true;
    mDocument->rootNode->addChild(dlight);

    auto plight = iris::LightNode::create();
    plight->setLightType(iris::LightType::Point);
    plight->setName("ae98cx7u");
    plight->setLocalPos(iris::Vec3(0, 0, -3));
    plight->color = QColor(210, 210, 255);
    plight->intensity = 0.47f;
    plight->setShadowMapType(iris::ShadowMapType::Soft);
    plight->setShadowMapResolution(2048);
    plight->isBuiltIn = true;
    mDocument->rootNode->addChild(plight);

    // The floor is a resource of the app; headless tests have no floor, which is fine.
    auto floor = iris::MeshNode::create();
    floor->setMesh(":/models/ground.obj");
    if (floor->getMesh()) {
        floor->setLocalPos(iris::Vec3(0, -5, 0));   // legacy: below the default plane reset
        floor->setName(kFloorName);
        floor->setPickable(false);
        floor->isBuiltIn = true;
        floor->setFaceCullingMode(iris::FaceCullingMode::None);
        floor->setShadowCastingEnabled(true);
        // The legacy floor is Default.shader (a CustomMaterial) with the tile texture;
        // the mirror renders DefaultMaterial, so the floor is that here.
        auto m = iris::DefaultMaterial::create();
        m->setDiffuseColor(QColor(255, 255, 255));
        const QString tile = IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png");
        if (QFileInfo(tile).isFile()) m->setDiffuseTexture(iris::Texture2D::load(tile));
        m->setTextureScale(4.0f);
        floor->setMaterial(m);
        mDocument->rootNode->addChild(floor);
        mFloor = floor;
    }

    mCamera = iris::CameraNode::create();
    mCamera->setLocalPos(iris::Vec3(5, 6, 12));
    mCamera->lookAt(iris::Vec3(0, 0.5f, 0));
    mDocument->setCamera(mCamera);

    mDocument->setSkyColor(QColor(25, 25, 25));
    mDocument->setAmbientColor(QColor(190, 190, 190));
    mDocument->fogColor = QColor(25, 25, 25);
    mDocument->fogEnabled = false;
    mDocument->shadowEnabled = true;

    mCamera->update(0);
    mDocument->update(0);

    // OrbitalCameraController: setCamera (pivot from the camera), then the
    // preview pivot/distance, rotation speed .5.
    mDistFromPivot = 15;
    orbitFromCamera();
    mPivot = iris::Vec3(0, 0, 0);
    mDistFromPivot = 5;
    mRotationSpeed = 0.5f;
}

bool EngineAssetScene::attach(View *view)
{
    auto engine = mEngine.lock();
    if (!engine || !view) return false;
    if (mScene && mView == view) return true;
    if (mScene && mView != view) {
        if (mView) mView->setScene(nullptr);
    } else if (!mScene) {
        mScene = engine->createScene("assets-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        if (!mScene) return false;
        mScene->setAmbient(Colour(0.45f, 0.45f, 0.45f), Colour(0.30f, 0.30f, 0.30f));
        mMirror.reset(new SceneMirror(mScene));
        mMirror->setLightWires(false);          // a preview never shows editor wires
        mMirror->setSource(mDocument);
    }
    mView = view;
    mView->setScene(mScene);
    mView->setShadows(mShadows);
    return true;
}

void EngineAssetScene::release()
{
    auto engine = mEngine.lock();
    if (mMirror) {
        if (engine && mScene) mMirror->setSource(nullptr);
        mMirror.reset();
    }
    if (engine && mScene) {
        if (mView && mView->scene() == mScene) mView->setScene(nullptr);
        engine->destroyScene(mScene);
    }
    mScene = nullptr;
    mView = nullptr;
}

iris::MeshPtr EngineAssetScene::previewSphere()
{
    if (mSphere) return mSphere;
    mSphere = iris::Mesh::loadMesh(":/content/primitives/hp_sphere.obj");
    if (!mSphere) mSphere = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/hp_sphere.obj"));
    if (!mSphere) mSphere = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/sphere.obj"));
    return mSphere;
}

iris::SceneNodePtr EngineAssetScene::subject() const
{
    if (!mDocument->rootNode->hasChildren()) return iris::SceneNodePtr();
    auto last = mDocument->rootNode->children().last();
    return last->isBuiltIn ? iris::SceneNodePtr() : last;
}

void EngineAssetScene::clearSubject()
{
    // AssetViewer::clearScene: the last child of the root goes unless it is built in.
    if (auto s = subject()) s->removeFromParent();
}

void EngineAssetScene::setSubject(iris::SceneNodePtr node, bool viewed, bool isOnGround)
{
    if (!node) return;

    // AssetViewer::addNodeToScene. The legacy code measured the box BEFORE
    // dropping the node onto the floor and then framed that stale centre; here
    // the box is measured where the node ends up, so the camera looks at it.
    if (isOnGround) {
        node->setLocalPos(iris::Vec3(0, 0, 0));
        node->update(0);
        auto aabb = nodeBoundingBox(node);
        node->setLocalPos(iris::Vec3(0, -aabb.getMin().y() - 5, 0));
    }

    if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        if (!meshNode->getMaterial()) meshNode->setMaterial(iris::DefaultMaterial::create());
    }

    clearSubject();
    mDocument->rootNode->addChild(node);
    node->update(0);

    auto aabb = nodeBoundingBox(node);
    iris::BoundingSphere bound = aabb.getMinimalEnclosingSphere();
    if (bound.radius <= 0.0f) { bound.pos = node->getGlobalPosition(); bound.radius = 1; }
    const float dist = preview::framingDistance(bound.radius, mCamera->angle);

    // The framing distance grows with the subject; the clip planes must follow
    // it or a large model sits entirely beyond its own far plane and renders
    // nothing (ASSETS_AUDIT.md finding 3: any radius over ~170 vanished).
    mSubjectRadius = bound.radius;
    preview::clipPlanesForFraming(dist, mSubjectRadius, mCamera->nearClip, mCamera->farClip);

    if (!viewed) {
        mLookAt = bound.pos;
        mLocalPos = iris::Vec3(0, bound.pos.y(), 12);
        mLocalRot = iris::Vec3(0, 0, 0);
    }
    mDistanceFromPivot = dist;
}

iris::SceneNodePtr EngineAssetScene::setMaterialSubject(iris::MaterialPtr material, const QString &name)
{
    auto matball = iris::MeshNode::create();
    auto sphere = previewSphere();
    if (sphere) matball->setMesh(sphere);
    matball->setLocalPos(iris::Vec3(0, 0, 0));
    matball->setName(name);
    matball->setPickable(false);
    matball->setFaceCullingMode(iris::FaceCullingMode::None);
    matball->setShadowCastingEnabled(true);
    if (material) matball->setMaterial(material);
    setSubject(matball, false, false);
    return matball;
}

void EngineAssetScene::setSkyColor(const QColor &c)
{
    mDocument->setSkyColor(c);
}

void EngineAssetScene::setBackdrop(unsigned int id)
{
    switch (id) {
    case 1:
        mDocument->fogEnabled = false;
        mDocument->shadowEnabled = false;
        mDocument->setSkyColor(QColor(25, 25, 25));
        if (mFloor) mFloor->hide();
        mShadows = false;
        break;
    case 2:
        mDocument->fogEnabled = false;
        mDocument->shadowEnabled = false;
        mDocument->setSkyColor(QColor(82, 82, 82));
        if (mFloor) mFloor->hide();
        mShadows = false;
        break;
    case 3:
        mDocument->setSkyColor(QColor(25, 25, 25));
        mDocument->fogEnabled = true;          // exponential, at the document default density
        mDocument->fogColor = QColor(25, 25, 25);
        mDocument->shadowEnabled = true;
        if (mFloor) mFloor->show();
        mShadows = true;
        break;
    default:
        return;
    }
    if (mView) mView->setShadows(mShadows);
}

// ---- orbit camera: OrbitalCameraController in preview mode ----

void EngineAssetScene::orbitFromCamera()
{
    // OrbitalCameraController::setCamera: pivot ahead of the camera, yaw/pitch from it.
    auto viewVec = mCamera->getLocalRot().rotatedVector(iris::Vec3(0, 0, -1));
    mPivot = mCamera->getLocalPos() + viewVec * mDistFromPivot;
    float roll;
    mCamera->getLocalRot().getEulerAngles(&mPitch, &mYaw, &roll);
    mTargetYaw = mYaw;
    mTargetPitch = mPitch;
    updateCameraRot();
}

void EngineAssetScene::updateCameraRot()
{
    auto rot = iris::Quat::fromEulerAngles(mPitch, mYaw, 0);
    auto localPos = rot.rotatedVector(iris::Vec3(0, 0, 1));
    mCamera->setLocalPos(mPivot + localPos * mDistFromPivot);
    mCamera->setLocalRot(rot);
    mCamera->update(0);
}

void EngineAssetScene::applyClipPlanes()
{
    // mDistanceFromPivot may come from stored scene properties (orientCamera),
    // not only from setSubject's framing: re-derive planes that contain both
    // the orbit distance and the subject.
    const float dist = qMax(mDistanceFromPivot, mDistFromPivot);
    preview::clipPlanesForFraming(dist, qMax(mSubjectRadius, 1.0f),
                                  mCamera->nearClip, mCamera->farClip);
}

void EngineAssetScene::resetCamera()
{
    // AssetViewer::resetViewerCamera
    mCamera->setLocalPos(mLocalPos);
    mCamera->setLocalRot(iris::Quat::fromEulerAngles(mLocalRot));
    mCamera->lookAt(mLookAt);
    mCamera->update(0);

    orbitFromCamera();
    mPivot = mLookAt;
    mDistFromPivot = mDistanceFromPivot;
    mRotationSpeed = 0.5f;
    applyClipPlanes();
    updateCameraRot();
}

void EngineAssetScene::resetCameraAfter()
{
    // AssetViewer::resetViewerCameraAfter
    mCamera->setLocalPos(mLocalPos);
    mCamera->setLocalRot(iris::Quat::fromEulerAngles(mLocalRot));
    mCamera->update(0);

    mDistFromPivot = mDistanceFromPivot;
    orbitFromCamera();
    mRotationSpeed = 0.5f;
    applyClipPlanes();
}

void EngineAssetScene::orientCamera(iris::Vec3 pos, iris::Vec3 localRot, float distanceFromPivot)
{
    mLocalPos = pos;
    mLocalRot = localRot;
    mDistanceFromPivot = distanceFromPivot;
    resetCameraAfter();
}

QJsonObject EngineAssetScene::sceneProperties() const
{
    auto vec3 = [](const iris::Vec3 &v) {
        QJsonObject o; o["x"] = v.x(); o["y"] = v.y(); o["z"] = v.z(); return o;
    };
    QJsonObject cameraObj;
    cameraObj["pos"] = vec3(mCamera->getLocalPos());
    cameraObj["distFromPivot"] = mDistFromPivot;
    cameraObj["rot"] = vec3(mCamera->getLocalRot().toEulerAngles());
    QJsonObject properties;
    properties["camera"] = cameraObj;
    return properties;
}

void EngineAssetScene::mouseDown(Qt::MouseButton b)
{
    if (b == Qt::LeftButton) mLeftDown = true;
    if (b == Qt::RightButton) mRightDown = true;
    if (b == Qt::MiddleButton) mMiddleDown = true;
}

void EngineAssetScene::mouseUp(Qt::MouseButton b)
{
    if (b == Qt::LeftButton) mLeftDown = false;
    if (b == Qt::RightButton) mRightDown = false;
    if (b == Qt::MiddleButton) mMiddleDown = false;
}

void EngineAssetScene::mouseMove(int dx, int dy)
{
    // OrbitalCameraController::onMouseMove, previewMode: left or right drag orbits.
    if (mLeftDown || mRightDown) orbit(dx * mRotationSpeed, dy * mRotationSpeed);
    if (mMiddleDown) {
        const float dragSpeed = 0.01f;
        auto dir = mCamera->getLocalRot().rotatedVector(iris::Vec3(dx * dragSpeed, -dy * dragSpeed, 0));
        mPivot += dir;
    }
    updateCameraRot();
}

void EngineAssetScene::orbit(float yawDegrees, float pitchDegrees)
{
    mYaw = mTargetYaw;
    mPitch = mTargetPitch;
    mYaw += yawDegrees;
    mPitch += pitchDegrees;
    mTargetYaw = mYaw;
    mTargetPitch = mPitch;
    updateCameraRot();
}

void EngineAssetScene::wheel(int delta)
{
    const float zoomSpeed = 0.01f;
    mDistFromPivot += -delta * zoomSpeed;
    if (mDistFromPivot < 0) mDistFromPivot = 0;
    // Zooming out must never push the subject past the far plane.
    if (mDistFromPivot + 2.0f * mSubjectRadius > mCamera->farClip)
        preview::clipPlanesForFraming(mDistFromPivot, qMax(mSubjectRadius, 1.0f),
                                      mCamera->nearClip, mCamera->farClip);
    updateCameraRot();
}

void EngineAssetScene::step(float dt, int width, int height)
{
    // OrbitalCameraController::update
    mYaw = lerp(mYaw, mTargetYaw, 0.8f);
    mPitch = lerp(mPitch, mTargetPitch, 0.8f);
    updateCameraRot();

    mDocument->update(dt);
    mCamera->setAspectRatio(height > 0 ? float(width) / float(height) : 1.0f);
    if (mMirror && mView) {
        mMirror->sync();
        mMirror->applySky(mView);
        mMirror->applyCamera(mCamera, mView);
    }
}

QImage EngineAssetScene::toQImage(const Image &img)
{
    QImage result;
    if (img.width && img.height) {
        result = QImage(int(img.width), int(img.height), QImage::Format_RGBA8888);
        for (unsigned y = 0; y < img.height; ++y)
            std::memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    }
    return result;
}

QImage EngineAssetScene::renderImage(int width, int height)
{
    auto engine = mEngine.lock();
    if (!engine || width <= 0 || height <= 0) return QImage();
    const QColor c = mDocument->skyColor;
    View *shot = engine->createOffscreenView(
        "assets-shot-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + std::to_string(++mShotSerial),
        unsigned(width), unsigned(height), Colour(c.redF(), c.greenF(), c.blueF(), 1.0f));
    if (!shot) return QImage();
    // Not attached anywhere yet (the widget was never shown): the shot view is
    // the first view, which lets the scene be created (ORDER MATTERS).
    const bool temporary = !mScene;
    if (temporary && !attach(shot)) { engine->destroyView(shot); return QImage(); }
    shot->setScene(mScene);
    shot->setShadows(mShadows);
    mDocument->update(0);
    mCamera->setAspectRatio(float(width) / float(height));
    if (mMirror) {
        mMirror->sync();
        mMirror->applySky(shot);
        mMirror->applyCamera(mCamera, shot);
    }
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    Image img;
    QImage result;
    if (shot->readPixels(img)) result = toQImage(img);
    shot->setScene(nullptr);
    if (mView == shot) mView = nullptr;
    engine->destroyView(shot);
    return result;
}
