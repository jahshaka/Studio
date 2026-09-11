#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "bridge/engineassetscene.h"

#include <QColor>
#include <QFileInfo>
#include <QtMath>

#include "irisgl/mirror/scenemirror.h"
#include "viewport/previewframing.h"
#include "viewport/previeworbit.h"
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

} // namespace

EngineAssetScene::EngineAssetScene(const std::shared_ptr<Engine> &engine)
    : EnginePreviewScene(engine, "assets", sceneworkers::Tier::Preview)
{
    buildDocument();
}

EngineAssetScene::~EngineAssetScene()
{
    // The base destructor cannot run the hooks (see enginepreviewscene.h).
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
    mDocument->refresh();

    // Adopt the camera at the legacy orbit radius, then move the pivot to the
    // origin at the preview distance (AssetViewer did exactly this).
    mOrbit.distFromPivot = 15;
    orbitFromCamera();
    mOrbit.pivot = iris::Vec3(0, 0, 0);
    mOrbit.distFromPivot = 5;
    mOrbit.rotationSpeed = 0.5f;
}

void EngineAssetScene::configureScene(Scene *scene)
{
    scene->setAmbient(Colour(0.45f, 0.45f, 0.45f), Colour(0.30f, 0.30f, 0.30f));
}

void EngineAssetScene::configureMirror(SceneMirror *mirror)
{
    mirror->setSource(mDocument);
}

void EngineAssetScene::configureView(View *view)
{
    view->setShadows(mShadows);
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

    // THE FRAMING is preview::frameSubject — the editor's F, shared with the
    // thumbnail renderer (viewport/previewframing.h). The clip planes come
    // with it: the framing distance grows with the subject, and a large model
    // whose far plane did not follow rendered nothing at all (ASSETS_AUDIT.md
    // finding 3: any radius over ~170 vanished).
    const preview::Framing framing = preview::frameSubject(node, mCamera->effectiveFovDegrees());
    mSubjectRadius = framing.radius;
    mCamera->nearClip = framing.nearClip;
    mCamera->farClip = framing.farClip;

    if (!viewed) {
        mLookAt = framing.target;
        // Straight out of the box's centre, at the framing distance: the old
        // (0, centre.y, 12) put the camera on the world's Z axis whatever the
        // subject's x/z, so a model authored away from its origin was previewed
        // from an angle and off-centre (the same defect as the thumbnail's).
        mLocalPos = framing.target + iris::Vec3(0, 0, framing.distance);
        mLocalRot = iris::Vec3(0, 0, 0);
    }
    mDistanceFromPivot = framing.distance;
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
        if (mFloor) mFloor->setVisible(false);
        mShadows = false;
        break;
    case 2:
        mDocument->fogEnabled = false;
        mDocument->shadowEnabled = false;
        mDocument->setSkyColor(QColor(82, 82, 82));
        if (mFloor) mFloor->setVisible(false);
        mShadows = false;
        break;
    case 3:
        mDocument->setSkyColor(QColor(25, 25, 25));
        mDocument->fogEnabled = true;          // exponential, at the document default density
        mDocument->fogColor = QColor(25, 25, 25);
        mDocument->shadowEnabled = true;
        if (mFloor) mFloor->setVisible(true);
        mShadows = true;
        break;
    default:
        return;
    }
    if (view()) view()->setShadows(mShadows);
}

// ---- orbit camera: the shared PreviewOrbit (viewport/previeworbit.h) ----

void EngineAssetScene::orbitFromCamera()
{
    mOrbit.adopt(mCamera);
    mOrbit.apply(mCamera);
}

void EngineAssetScene::applyClipPlanes()
{
    // mDistanceFromPivot may come from stored scene properties (orientCamera),
    // not only from setSubject's framing: re-derive planes that contain both
    // the orbit distance and the subject.
    const float dist = qMax(mDistanceFromPivot, mOrbit.distFromPivot);
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
    mOrbit.pivot = mLookAt;
    mOrbit.distFromPivot = mDistanceFromPivot;
    mOrbit.rotationSpeed = 0.5f;
    applyClipPlanes();
    mOrbit.apply(mCamera);
}

void EngineAssetScene::resetCameraAfter()
{
    // AssetViewer::resetViewerCameraAfter
    mCamera->setLocalPos(mLocalPos);
    mCamera->setLocalRot(iris::Quat::fromEulerAngles(mLocalRot));
    mCamera->update(0);

    mOrbit.distFromPivot = mDistanceFromPivot;
    orbitFromCamera();
    mOrbit.rotationSpeed = 0.5f;
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
    cameraObj["distFromPivot"] = mOrbit.distFromPivot;
    cameraObj["rot"] = vec3(mCamera->getLocalRot().toEulerAngles());
    QJsonObject properties;
    properties["camera"] = cameraObj;
    return properties;
}

void EngineAssetScene::mouseDown(Qt::MouseButton b) { mOrbit.mouseDown(b); }

void EngineAssetScene::mouseUp(Qt::MouseButton b) { mOrbit.mouseUp(b); }

void EngineAssetScene::mouseMove(int dx, int dy)
{
    mOrbit.drag(mCamera, dx, dy, 0.01f);
    mOrbit.apply(mCamera);
}

void EngineAssetScene::orbit(float yawDegrees, float pitchDegrees)
{
    mOrbit.orbit(yawDegrees, pitchDegrees);
    mOrbit.apply(mCamera);
}

// FLY (smoke S7). The pivot carries the camera: orbitmath::applyPose parks the
// camera one orbit radius behind the pivot along the current look direction, so
// translating the pivot translates the camera by exactly the same vector and
// the orbit the user has set up survives the flight.
void EngineAssetScene::flyBy(const iris::Vec3 &worldDelta)
{
    if (worldDelta.isNull()) return;
    mOrbit.pivot += worldDelta;
    mLookAt = mOrbit.pivot;
    applyClipPlanes();
    mOrbit.apply(mCamera);
}

void EngineAssetScene::flyStep(const flystep::Keys &keys, float dt)
{
    if (!keys.any() || dt <= 0.0f) return;
    // The speed scales with the SUBJECT: a 50 m environment and a 10 cm prop
    // are both previewed here, and a fixed metres-per-second step is a crawl
    // in one and a jump across the other. The orbit distance is the framing's
    // own measure of "how big is what I am looking at".
    const float speed = qMax(0.5f, mOrbit.distFromPivot) * 1.2f;
    flyBy(flystep::delta(mCamera->getLocalRot(), keys, speed, dt));
}

iris::Vec3 EngineAssetScene::cameraPosition() const
{
    return mCamera ? mCamera->getLocalPos() : iris::Vec3();
}

iris::AABB EngineAssetScene::subjectBounds() const
{
    auto s = subject();
    if (!s) return iris::AABB();
    return preview::worldBoundingBox(s);
}

void EngineAssetScene::frameSubject()
{
    auto s = subject();
    if (!s) return;
    setSubject(s, false, false);   // re-measures and re-frames where it stands
    resetCamera();
}

void EngineAssetScene::wheel(int delta)
{
    // The zoom POLICY is the asset viewer's own (previeworbit.h): stop at the
    // pivot, and never let zooming out push the subject past the far plane.
    const float zoomSpeed = 0.01f;
    mOrbit.distFromPivot += -delta * zoomSpeed;
    if (mOrbit.distFromPivot < 0) mOrbit.distFromPivot = 0;
    if (mOrbit.distFromPivot + 2.0f * mSubjectRadius > mCamera->farClip)
        preview::clipPlanesForFraming(mOrbit.distFromPivot, qMax(mSubjectRadius, 1.0f),
                                      mCamera->nearClip, mCamera->farClip);
    mOrbit.apply(mCamera);
}

void EngineAssetScene::step(float dt, int width, int height)
{
    mOrbit.advance();
    mOrbit.apply(mCamera);

    mDocument->advance(dt);
    // A preview is a host of the engine's ONE frame delta too (A4.2): the
    // editor stops pushing when its page is hidden (a paused play leaves 0 in
    // force), so a preview re-arms the grid step or its particles stay frozen.
    if (auto e = engine()) e->setFixedFrameDelta(jahshaka::engine::Engine::kDefaultFrameDelta);
    pushFrame(mCamera, width, height);
}

void EngineAssetScene::prepareOffscreen(View *shot, int width, int height)
{
    (void)shot;   // the base has already made it the current view
    mDocument->refresh();
    pushFrame(mCamera, width, height);
}

QImage EngineAssetScene::renderImage(int width, int height)
{
    if (!mDocument) return QImage();
    const QColor c = mDocument->skyColor;
    return renderOffscreen("assets-shot", width, height,
                           Colour(c.redF(), c.greenF(), c.blueF(), 1.0f), mShadows);
}
