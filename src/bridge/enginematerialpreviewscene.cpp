#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "bridge/enginematerialpreviewscene.h"

#include "irisgl/core/irisutils.h"
#include "irisgl/import/graphicshelper.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

#include "irisgl/mirror/scenemirror.h"
#include "bridge/previewenvironment.h"
#include "bridge/secondarysurfacetonemap.h"
#include "viewport/freecamerapolicy.h"
#include "viewport/previewframing.h"
#include "viewport/previeworbit.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"

using namespace jahshaka::engine;

namespace {

const char *kSubjectName = "matpreview-primitive";

/// THE DOCK'S SUBJECT MESHES. Five of the six used to be loaded from
/// `app/shadergraph/`, where they sat as BYTE-IDENTICAL copies of the shipped
/// primitives (measured: cube, cone, plane, cylinder, capsule and torus matched
/// their `app/content/primitives/` twins exactly) — those six duplicate files are
/// DELETED and the dock reads the shipped ones, which is the same geometry it
/// always drew. The low-poly ball is its own mesh and stays where it is.
const char *meshFile(PreviewMesh mesh)
{
    switch (mesh) {
    case PreviewMesh::Sphere:   return "app/shadergraph/lowpoly_sphere.obj";
    case PreviewMesh::Cube:     return "app/content/primitives/cube.obj";
    case PreviewMesh::Plane:    return "app/content/primitives/plane.obj";
    case PreviewMesh::Cylinder: return "app/content/primitives/cylinder.obj";
    case PreviewMesh::Capsule:  return "app/content/primitives/capsule.obj";
    case PreviewMesh::Torus:    return "app/content/primitives/torus.obj";
    }
    return "app/shadergraph/lowpoly_sphere.obj";
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
    // THE STUDIO IS THE LIGHTING (MATPREVIEW-ENV-1, owner review R9). What
    // stood here was a HAND RIG — a warm key directional at 0.86 and a fill
    // point at 0.5 over a flat 125-grey sky — and every complaint in the
    // owner's report was a property of it: a flat sky is a uniform ball of
    // light, so a metal material mirrored a blank grey sphere ("weird mirrored
    // reflections"), and two point-like lights over an UNGRADED preview surface
    // clipped their speculars to white ("burnt-out lights"). Both lights and
    // the colour sky are GONE. The environment `previewenv::apply` binds is the
    // only light in this document, and it is the same one the thumbnail
    // renderer uses, so a material's tile and its preview are the same picture.
    mDocument = iris::Scene::create();
    previewenv::apply(mDocument);

    // The legacy camera sat at (2,0,3) looking at the origin; a touch of height
    // keeps the Plane primitive from being edge-on at first sight.
    mCamera = iris::CameraNode::create();
    mCamera->setLocalPos(iris::Vec3(2, 1.2f, 3));
    mCamera->lookAt(iris::Vec3(0, 0, 0));
    // THE FREE CAMERA'S FRAMING RULE, in a dock (freecamerapolicy.h): at or
    // below 16:9 the authored vertical angle is rendered exactly; wider, the
    // vertical narrows to hold the horizontal extent. reframe() asks for the
    // EFFECTIVE angle, so a Display dock dragged to any shape stays composed.
    mCamera->setFramingAspect(freecam::kFreeCameraFramingAspect);
    mDocument->setCamera(mCamera);

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

// THE FRAMING, RE-DONE FROM THE DOCK'S ACTUAL SHAPE (owner review R9: the
// preview sphere "is CLIPPED at the panel's right edge").
//
// The camera used to sit at a fixed 3.78 units whatever the dock was, so the
// subject's size on screen followed the dock's HEIGHT and its margin followed
// the dock's WIDTH: a narrow column cut the sphere off at both sides (measured:
// 274 silhouette pixels lost at 160x256) and a wide strip shrank it to a
// seventh of the frame. This frames by the subject's own radius against the
// SMALLER dimension of the frame (viewport/previewframing.h), so the margin is
// the same at every shape, and it re-runs whenever the dock is resized or the
// primitive changes. The user's own zoom rides ON TOP as a factor, so a
// resize keeps how close they had pulled in.
void EngineMaterialPreviewScene::reframe(int width, int height)
{
    if (!mCamera || !mSubject) return;
    const iris::AABB bounds = preview::worldBoundingBox(mSubject);
    const float radius = preview::subjectRadius(mSubject, bounds);
    const float aspect = height > 0 ? float(width) / float(height) : 1.0f;
    mCamera->setAspectRatio(aspect);
    mBaseDistance = preview::previewDistance(radius, mCamera->effectiveFovDegrees(), aspect);
    preview::clipPlanesForFraming(mBaseDistance * kMaxZoomOut, radius,
                                  mCamera->nearClip, mCamera->farClip);
    mFramedFor = QSize(width, height);
    mFramedRadius = radius;   // what the zoom factor below is a factor OF
    mOrbit.distFromPivot = mBaseDistance * mZoom;
    mOrbit.apply(mCamera);
}

void EngineMaterialPreviewScene::configureScene(Scene *scene)
{
    // The environment's own two halves — the nine diffuse bands and the
    // specular gain — instead of the flat hemisphere pair that used to stand
    // in for a sky here (previewenv::pushAmbient).
    previewenv::pushAmbient(scene);
}

void EngineMaterialPreviewScene::configureMirror(SceneMirror *mirror)
{
    mirror->setSource(mDocument);
}

void EngineMaterialPreviewScene::configureView(View *view)
{
    view->setShadows(false);
    // THE PREVIEW IS GRADED, at the studio's own manual exposure. Without this
    // the dock wrote raw linear radiance into an 8-bit surface — the same
    // defect the secondary-surface tonemap fixed for thumbnails in 2026-09-07
    // and which was still live here, and the reason a specular highlight was a
    // flat white hole instead of a highlight with a shoulder. It is the
    // thumbnail's grade, at the thumbnail's exposure, so the two surfaces
    // develop one picture.
    secondaryfx::apply(view, true, previewenv::exposureChain());
}

iris::MeshPtr EngineMaterialPreviewScene::meshFor(PreviewMesh mesh)
{
    auto &slot = mMeshes[int(mesh)];
    if (slot) return slot;
    // Parsed once per process through the importer's own parse entry point
    // (`Mesh::loadMesh` is deleted, ATOM P2): dock furniture, no cards, no
    // library. `mMeshes` is the cache — one parse per shape per session.
    const QList<iris::MeshPtr> meshes = iris::GraphicsHelper::loadAllMeshesFromFile(
        IrisUtils::getAbsoluteAssetPath(QString::fromLatin1(meshFile(mesh))));
    if (!meshes.isEmpty()) slot = meshes.first();
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
        mFramedFor = QSize();     // a new primitive is a new radius
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
    mFramedFor = QSize();
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
    // A ZOOM IS A FACTOR ON THE FRAMING, not an absolute distance: the framing
    // moves with the dock's shape (reframe), and a user who had pulled in
    // must stay pulled in across a resize. One notch is 120 units of angle
    // delta and moves the factor by ~10 %.
    mZoom *= std::pow(1.1f, -float(delta) / 120.0f);
    mZoom = std::max(kMinZoomIn, std::min(kMaxZoomOut, mZoom));
    mOrbit.distFromPivot = std::max(0.5f, mBaseDistance * mZoom);
    mOrbit.apply(mCamera);
}

void EngineMaterialPreviewScene::step(float dt, int width, int height)
{
    // THE FRAME'S SHAPE IS AN INPUT TO THE FRAMING, so it is read first — the
    // camera's effective vertical angle depends on it.
    if (mFramedFor != QSize(width, height)) reframe(width, height);
    mOrbit.advance();
    mOrbit.apply(mCamera);

    mDocument->advance(dt);
    // A preview is a host of the engine's ONE frame delta too (A4.2): the
    // editor stops pushing when its page is hidden (a paused play leaves 0 in
    // force), so a preview re-arms the grid step or its particles stay frozen.
    if (auto e = engine()) e->setFixedFrameDelta(jahshaka::engine::Engine::kDefaultFrameDelta);
    pushFrame(mCamera, width, height);
}
