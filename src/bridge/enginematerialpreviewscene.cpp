#include "bridge/enginematerialpreviewscene.h"

#include <cstdint>
#include <string>
#include <QFileInfo>
#include <QQuaternion>

#include "irisgl/mirror/scenemirror.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"

using namespace jahshaka::engine;

namespace {

const char *kSubjectName = "matpreview-primitive";

float lerp(float a, float b, float t) { return a * (1 - t) + b * t; }

// The legacy SceneWidget's primitives (MaterialHelper::assetPath ->
// app/shadergraph/<file>); app/content/primitives is the fallback.
const char *meshFile(PreviewMesh mesh)
{
    switch (mesh) {
    case PreviewMesh::Sphere:   return "lowpoly_sphere.obj";
    case PreviewMesh::Cube:     return "cube.obj";
    case PreviewMesh::Plane:    return "plane.obj";
    case PreviewMesh::Cylinder: return "cylinder.obj";
    case PreviewMesh::Capsule:  return "capsule.obj";
    case PreviewMesh::Torus:    return "torus.obj";
    }
    return "lowpoly_sphere.obj";
}

} // namespace

EngineMaterialPreviewScene::EngineMaterialPreviewScene(const std::shared_ptr<Engine> &engine)
    : mEngine(engine)
{
    buildDocument();
}

EngineMaterialPreviewScene::~EngineMaterialPreviewScene()
{
    release();
}

void EngineMaterialPreviewScene::buildDocument()
{
    // SceneWidget::start, minus the GL: primitive at the origin, lights around
    // it, the grey clear colour. Lit like the assets preview (a key directional
    // and a fill point) so PBR materials read; no floor, no shadows.
    mDocument = iris::Scene::create();
    mDocument->shadowEnabled = false;

    auto key = iris::LightNode::create();
    key->setLightType(iris::LightType::Directional);
    key->setName("matpreview-key");
    key->color = QColor(255, 255, 240);
    key->setLocalRot(QQuaternion::fromEulerAngles(45, 45, 0));
    key->intensity = 0.86f;
    key->isBuiltIn = true;
    mDocument->rootNode->addChild(key);

    auto fill = iris::LightNode::create();
    fill->setLightType(iris::LightType::Point);
    fill->setName("matpreview-fill");
    fill->setLocalPos(QVector3D(-3, 0, 3));
    fill->color = QColor(255, 255, 255);
    fill->intensity = 0.5f;
    fill->isBuiltIn = true;
    mDocument->rootNode->addChild(fill);

    // The legacy camera sat at (2,0,3) looking at the origin; a touch of height
    // keeps the Plane primitive from being edge-on at first sight.
    mCamera = iris::CameraNode::create();
    mCamera->setLocalPos(QVector3D(2, 1.2f, 3));
    mCamera->lookAt(QVector3D(0, 0, 0));
    mDocument->setCamera(mCamera);

    mDocument->setSkyColor(QColor(125, 125, 125));   // SceneWidget's initial clearColor
    mDocument->setAmbientColor(QColor(190, 190, 190));
    mDocument->fogEnabled = false;

    mCamera->update(0);
    mDocument->update(0);

    // Orbit around the origin from where the camera stands.
    mPivot = QVector3D(0, 0, 0);
    mDistFromPivot = mCamera->getLocalPos().length();
    float roll;
    mCamera->getLocalRot().getEulerAngles(&mPitch, &mYaw, &roll);
    mTargetYaw = mYaw;
    mTargetPitch = mPitch;
    updateCameraRot();

    mMaterial = iris::DefaultMaterial::create();
    setPreviewMesh(PreviewMesh::Sphere);
}

bool EngineMaterialPreviewScene::attach(View *view)
{
    auto engine = mEngine.lock();
    if (!engine || !view) return false;
    if (mScene && mView == view) return true;
    if (mScene && mView != view) {
        if (mView) mView->setScene(nullptr);
    } else if (!mScene) {
        mScene = engine->createScene("matpreview-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        if (!mScene) return false;
        mScene->setAmbient(Colour(0.45f, 0.45f, 0.45f), Colour(0.30f, 0.30f, 0.30f));
        mMirror.reset(new SceneMirror(mScene));
        mMirror->setLightWires(false);          // a preview never shows editor wires
        mMirror->setSource(mDocument);
    }
    mView = view;
    mView->setScene(mScene);
    mView->setShadows(false);
    return true;
}

void EngineMaterialPreviewScene::release()
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

iris::MeshPtr EngineMaterialPreviewScene::meshFor(PreviewMesh mesh)
{
    auto &slot = mMeshes[int(mesh)];
    if (slot) return slot;
    const QString file = meshFile(mesh);
    QString path = IrisUtils::getAbsoluteAssetPath("app/shadergraph/" + file);
    if (!QFileInfo(path).isFile())
        path = IrisUtils::getAbsoluteAssetPath("app/content/primitives/" + file);
    slot = iris::Mesh::loadMesh(path);
    return slot;
}

void EngineMaterialPreviewScene::rebuildSubject()
{
    auto mesh = meshFor(mMesh);
    if (!mesh) return;
    // Mutate in place. This used to REPLACE the whole node on every Model-menu
    // pick, with the comment "SceneMirror only re-attaches mesh+material when
    // the node is new or its material pointer changed" — a true statement about
    // a mirror bug (Entry::meshPtr was written and never read), worked around
    // here instead of fixed. The mirror re-attaches on a mesh change now, so
    // the preview keeps one node for its whole life.
    if (mSubject && mSubject->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto node = mSubject.staticCast<iris::MeshNode>();
        node->setMesh(mesh);
        if (mMaterial) node->setMaterial(mMaterial);
        node->update(0);
        return;
    }
    if (mSubject) { mSubject->removeFromParent(); mSubject.reset(); }
    auto node = iris::MeshNode::create();
    node->setMesh(mesh);
    node->setName(kSubjectName);
    node->setLocalPos(QVector3D(0, 0, 0));
    node->setPickable(false);
    node->isBuiltIn = true;
    node->setFaceCullingMode(iris::FaceCullingMode::None);   // Plane reads from both sides
    if (mMaterial) node->setMaterial(mMaterial);
    mDocument->rootNode->addChild(node);
    node->update(0);
    mSubject = node;
}

void EngineMaterialPreviewScene::setMaterial(iris::MaterialPtr material)
{
    if (!material) return;
    mMaterial = material;
    if (mSubject && mSubject->sceneNodeType == iris::SceneNodeType::Mesh)
        mSubject.staticCast<iris::MeshNode>()->setMaterial(material);
}

bool EngineMaterialPreviewScene::setPreviewMesh(PreviewMesh mesh)
{
    if (!meshFor(mesh)) return false;
    mMesh = mesh;
    rebuildSubject();
    return mSubject != nullptr;
}

void EngineMaterialPreviewScene::setBackground(const QColor &colour)
{
    mDocument->setSkyColor(colour);
}

// ---- orbit camera: EngineAssetScene's maths ----

void EngineMaterialPreviewScene::updateCameraRot()
{
    auto rot = QQuaternion::fromEulerAngles(mPitch, mYaw, 0);
    auto localPos = rot.rotatedVector(QVector3D(0, 0, 1));
    mCamera->setLocalPos(mPivot + localPos * mDistFromPivot);
    mCamera->setLocalRot(rot);
    mCamera->update(0);
}

void EngineMaterialPreviewScene::mouseDown(Qt::MouseButton b)
{
    if (b == Qt::LeftButton) mLeftDown = true;
    if (b == Qt::RightButton) mRightDown = true;
    if (b == Qt::MiddleButton) mMiddleDown = true;
}

void EngineMaterialPreviewScene::mouseUp(Qt::MouseButton b)
{
    if (b == Qt::LeftButton) mLeftDown = false;
    if (b == Qt::RightButton) mRightDown = false;
    if (b == Qt::MiddleButton) mMiddleDown = false;
}

void EngineMaterialPreviewScene::mouseMove(int dx, int dy)
{
    if (mLeftDown || mRightDown) orbit(dx * mRotationSpeed, dy * mRotationSpeed);
    if (mMiddleDown) {
        const float dragSpeed = 0.01f;
        auto dir = mCamera->getLocalRot().rotatedVector(QVector3D(dx * dragSpeed, -dy * dragSpeed, 0));
        mPivot += dir;
    }
    updateCameraRot();
}

void EngineMaterialPreviewScene::orbit(float yawDegrees, float pitchDegrees)
{
    mYaw = mTargetYaw;
    mPitch = mTargetPitch;
    mYaw += yawDegrees;
    mPitch += pitchDegrees;
    mTargetYaw = mYaw;
    mTargetPitch = mPitch;
    updateCameraRot();
}

void EngineMaterialPreviewScene::wheel(int delta)
{
    const float zoomSpeed = 0.01f;
    mDistFromPivot += -delta * zoomSpeed;
    if (mDistFromPivot < 0.5f) mDistFromPivot = 0.5f;
    updateCameraRot();
}

void EngineMaterialPreviewScene::step(float dt, int width, int height)
{
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
