#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "bridge/enginematerialpreviewscene.h"

#include <QFileInfo>

#include "irisgl/mirror/scenemirror.h"
#include "viewport/previeworbit.h"
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
    : EnginePreviewScene(engine, "matpreview", sceneworkers::Tier::Preview)
{
    buildDocument();
}

EngineMaterialPreviewScene::~EngineMaterialPreviewScene()
{
    // The base destructor cannot run the hooks (see enginepreviewscene.h).
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
    key->setLocalRot(iris::Quat::fromEulerAngles(45, 45, 0));
    key->intensity = 0.86f;
    key->isBuiltIn = true;
    mDocument->rootNode->addChild(key);

    auto fill = iris::LightNode::create();
    fill->setLightType(iris::LightType::Point);
    fill->setName("matpreview-fill");
    fill->setLocalPos(iris::Vec3(-3, 0, 3));
    fill->color = QColor(255, 255, 255);
    fill->intensity = 0.5f;
    fill->isBuiltIn = true;
    mDocument->rootNode->addChild(fill);

    // The legacy camera sat at (2,0,3) looking at the origin; a touch of height
    // keeps the Plane primitive from being edge-on at first sight.
    mCamera = iris::CameraNode::create();
    mCamera->setLocalPos(iris::Vec3(2, 1.2f, 3));
    mCamera->lookAt(iris::Vec3(0, 0, 0));
    mDocument->setCamera(mCamera);

    mDocument->setSkyColor(QColor(125, 125, 125));   // SceneWidget's initial clearColor
    mDocument->setAmbientColor(QColor(190, 190, 190));
    mDocument->fogEnabled = false;

    mCamera->update(0);
    mDocument->refresh();

    // Orbit around the ORIGIN from where the camera stands (not PreviewOrbit::
    // adopt, which would put the pivot one radius ahead of the camera).
    mOrbit.pivot = iris::Vec3(0, 0, 0);
    mOrbit.distFromPivot = mCamera->getLocalPos().length();
    float pitch, yaw, roll;
    mCamera->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
    mOrbit.set(yaw, pitch);
    mOrbit.apply(mCamera);

    mMaterial = iris::DefaultMaterial::create();
    setPreviewMesh(PreviewMesh::Sphere);
}

void EngineMaterialPreviewScene::configureScene(Scene *scene)
{
    scene->setAmbient(Colour(0.45f, 0.45f, 0.45f), Colour(0.30f, 0.30f, 0.30f));
}

void EngineMaterialPreviewScene::configureMirror(SceneMirror *mirror)
{
    mirror->setSource(mDocument);
}

void EngineMaterialPreviewScene::configureView(View *view)
{
    view->setShadows(false);
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
    node->setLocalPos(iris::Vec3(0, 0, 0));
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

// ---- orbit camera: the shared PreviewOrbit (viewport/previeworbit.h) ----

void EngineMaterialPreviewScene::mouseDown(Qt::MouseButton b) { mOrbit.mouseDown(b); }

void EngineMaterialPreviewScene::mouseUp(Qt::MouseButton b) { mOrbit.mouseUp(b); }

void EngineMaterialPreviewScene::mouseMove(int dx, int dy)
{
    mOrbit.drag(mCamera, dx, dy, 0.01f);
    mOrbit.apply(mCamera);
}

void EngineMaterialPreviewScene::orbit(float yawDegrees, float pitchDegrees)
{
    mOrbit.orbit(yawDegrees, pitchDegrees);
    mOrbit.apply(mCamera);
}

void EngineMaterialPreviewScene::wheel(int delta)
{
    // The zoom POLICY is this dock's own (previeworbit.h): a primitive filling
    // the frame stops half a unit out, never at the pivot.
    const float zoomSpeed = 0.01f;
    mOrbit.distFromPivot += -delta * zoomSpeed;
    if (mOrbit.distFromPivot < 0.5f) mOrbit.distFromPivot = 0.5f;
    mOrbit.apply(mCamera);
}

void EngineMaterialPreviewScene::step(float dt, int width, int height)
{
    mOrbit.advance();
    mOrbit.apply(mCamera);

    mDocument->advance(dt);
    pushFrame(mCamera, width, height);
}
