#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/enginesceneviewport.h"
#include "viewport/viewportcover.h"

#include <QShowEvent>
#include <QMouseEvent>
#include "viewport/scenepicker.h"
#include "player/playback.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/physics/environment.h"
#include "viewport/translationgizmo.h"
#include "viewport/rotationgizmo.h"
#include "viewport/scalegizmo.h"
#include "viewport/gizmooverlay.h"
#include "viewport/cameracontrollerbase.h"
#include "viewport/editorcameracontroller.h"
#include "viewport/orbitalcameracontroller.h"
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include "data/constants.h"
#include "shell/mainwindow.h"
#include "data/project.h"
#include "data/database/database.h"
#include "io/assetmanager.h"
#include "ui/panels/scenehierarchywidget.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "commands/changematerialpropertycommand.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "irisgl/core/math/intersectionhelper.h"
#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"

#include "viewport/enginerenderdriver.h"
#include "viewport/previewframing.h"
#include "viewport/snapsettings.h"
#include "services/engineerrorpump.h"
#include "services/loadtimeline.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "services/sceneeditservice.h"
#include "commands/transformscenenodecommand.h"
#include <QUndoStack>
#include "irisgl/mirror/scenemirror.h"
#include "viewport/editordata.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"

using namespace jahshaka::engine;

EngineSceneViewport::EngineSceneViewport(const std::shared_ptr<Engine> &engine,
                                         EngineRenderDriver *driver, QWidget *parent)
    : EngineViewWidget(parent), mEngine(engine), mDriver(driver)
{
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);          // hover highlights gizmo handles
    mTranslateGizmo = new TranslationGizmo();
    mRotateGizmo    = new RotationGizmo();
    mScaleGizmo     = new ScaleGizmo();
    mGizmo = mTranslateGizmo;
    mFreeCam  = new EditorCameraController(this);
    mOrbitCam = new OrbitalCameraController(this);
    mPlayback = new PlayBack();
    mPlayback->setEditorViewport(this);
    mPlayback->init();                 // GL-free init: physics, animation, controllers
    resetEditorCam();
    setCameraController(mFreeCam);
    mFrameTimer.start();
    if (mDriver)
        // A lambda, not a direct member connect: syncFrame grew a default
        // argument (the fixed-dt override) and Qt's new-style connect refuses a
        // slot that takes more arguments than the signal provides.
        connect(mDriver, &EngineRenderDriver::beforeFrame, this, [this]() {
            syncFrame();
            // The cover comes down here, one frame BEHIND the present that
            // earned it: framesPresented counts frames already on screen.
            updateCover();
        });
}

EngineSceneViewport::~EngineSceneViewport()
{
    cleanup();
    delete mFreeCam; delete mOrbitCam;
    delete mPlayback;
    delete mTranslateGizmo; delete mRotateGizmo; delete mScaleGizmo;
}

void EngineSceneViewport::setCameraController(CameraControllerBase *c)
{
    if (mCamController && mCamController != c) mCamController->end();
    mCamController = c;
    if (mCamController) {
        mCamController->resetMouseStates();
        mCamController->setCamera(mEditorCam);
        mCamController->start();
    }
}

void EngineSceneViewport::setFreeCameraMode()   { setCameraController(mFreeCam); }
void EngineSceneViewport::setArcBallCameraMode() { setCameraController(mOrbitCam); }
QString EngineSceneViewport::cameraMode() const
{ return mCamController == mOrbitCam ? QStringLiteral("orbit") : QStringLiteral("free"); }

// Views dropdown / view.* shortcuts / editor.setView verb: snap to a canonical
// view. Every view remembers its camera between visits (per viewport session):
// switching saves the outgoing view's camera and restores the incoming one's —
// perspective keeps its full free/orbit pose across trips into ortho views,
// each ortho view keeps its own pan + zoom. A first visit to an axis view gets
// the standard framing (the orbital controller animates there via its lerp;
// the free camera turns in place). Re-picking the current view re-snaps it.
bool EngineSceneViewport::setCameraView(const QString &view)
{
    struct AxisView { const char *name; float yaw; float pitch; };
    static const AxisView axisViews[] = {
        { "top", 0.f, -90.f }, { "bottom", 0.f, 90.f },
        { "left", 90.f, 0.f }, { "right", -90.f, 0.f },
        { "front", 0.f, 0.f }, { "back", 180.f, 0.f },
    };
    const AxisView *axis = nullptr;
    for (const auto &v : axisViews)
        if (view == QLatin1String(v.name)) { axis = &v; break; }
    const bool persp = (view == QLatin1String("perspective"));
    if (!axis && !persp) return false;   // unknown name: refuse before touching state

    // Save the outgoing view's camera — but not on a re-pick of the current
    // view, which must re-snap (the pre-memory behavior), not restore what
    // was saved a moment ago.
    const bool switching = (view != mCameraView);
    if (switching) saveViewState();

    const auto projection = persp ? iris::CameraProjection::Perspective
                                  : iris::CameraProjection::Orthogonal;
    if (mEditorCam) mEditorCam->setProjection(projection);
    if (mScene && mScene->camera && mScene->camera != mEditorCam)
        mScene->camera->setProjection(projection);

    if (switching && restoreViewState(view)) {
        mCameraView = view;
        return true;
    }

    // First visit (or a re-pick): the standard framing. "perspective" keeps
    // the current orientation — it only ever lands here before its pose has
    // been saved once, i.e. when it IS the current pose already.
    if (axis) {
        if (mCamController == mOrbitCam && mOrbitCam)
            mOrbitCam->setAxisView(axis->yaw, axis->pitch);
        else if (mFreeCam)
            mFreeCam->setAxisView(axis->yaw, axis->pitch);
    }
    mCameraView = view;
    return true;
}

void EngineSceneViewport::saveViewState()
{
    if (!mEditorCam) return;
    ViewCameraState s;
    s.pos = mEditorCam->getLocalPos();
    s.rot = mEditorCam->getLocalRot();
    s.orthoSize = mEditorCam->orthoSize;
    if (mOrbitCam) s.distFromPivot = mOrbitCam->distFromPivot;
    mViewStates.insert(mCameraView, s);
}

bool EngineSceneViewport::restoreViewState(const QString &view)
{
    const auto it = mViewStates.constFind(view);
    if (it == mViewStates.constEnd() || !mEditorCam) return false;
    const ViewCameraState &s = *it;
    mEditorCam->setLocalPos(s.pos);
    mEditorCam->setLocalRot(s.rot);
    mEditorCam->setOrthagonalZoom(s.orthoSize);   // ortho zoom; inert in perspective
    mEditorCam->update(0);
    // Resync the active controller with the restored pose — the same resync
    // focusOnNode/setEditorData use. The free cam re-derives yaw/pitch (and
    // wheel zoom from orthoSize); the orbital cam re-derives its pivot from
    // the restored pose + orbit distance, targets matched so nothing lerps.
    if (mCamController == mOrbitCam && mOrbitCam) {
        mOrbitCam->distFromPivot = s.distFromPivot;
        mOrbitCam->setCamera(mEditorCam);
    } else if (mCamController) {
        mCamController->setCamera(mEditorCam);
    }
    return true;
}

void EngineSceneViewport::clearViewStates()
{
    mViewStates.clear();
    mCameraView = QStringLiteral("perspective");
}

// The resync every camera mover in this file owes the active controller.
// OrbitalCameraController::setCamera() DECOMPOSES the camera's rotation into
// pitch/yaw and then REBUILDS the pose from pivot + orbit distance
// (updateCameraRot) — which round-trips exactly for a roll-free rotation and
// silently drops any roll otherwise. The free controller does the same to the
// rotation and leaves the position alone.
void EngineSceneViewport::resyncCameraController(float orbitDistance)
{
    if (!mCamController || !mEditorCam) return;
    if (mCamController == mOrbitCam && mOrbitCam) {
        if (orbitDistance > 0.0f) mOrbitCam->distFromPivot = orbitDistance;
        mOrbitCam->setCamera(mEditorCam);
    } else {
        mCamController->setCamera(mEditorCam);
    }
}

// editor.setCamera: place the camera outright. Every field is optional, so
// "move here, keep looking the same way" is a position on its own.
bool EngineSceneViewport::setCameraPose(const EditorCameraPose &pose)
{
    if (!mEditorCam) return false;
    if (pose.hasPosition) mEditorCam->setLocalPos(pose.position);
    if (pose.hasLookAt) {
        mEditorCam->lookAt(pose.lookAt);
        // A target beyond the far plane renders as an empty frame and looks
        // like the verb did nothing — grow the plane to contain it, the same
        // adaptation focusOnNode makes (previewframing.h's rationale).
        const float dist = mEditorCam->getLocalPos().distanceToPoint(pose.lookAt);
        mEditorCam->farClip = qMax(mEditorCam->farClip, dist * 2.0f);
    } else if (pose.hasRotation) {
        mEditorCam->setLocalRot(pose.rotation);
    }
    if (pose.fovDegrees > 0.0f) mEditorCam->angle = pose.fovDegrees;
    mEditorCam->update(0.0f);
    // The arcball keeps its orbit distance; its pivot is re-derived from the
    // new pose inside setCamera().
    resyncCameraController();
    return true;
}

// editor.frameNode: focusOnNode's framing maths with the view direction taken
// from {yaw, pitch} instead of from wherever the camera happens to be. The
// yaw/pitch convention is the arcball's own (updateCameraRot): the eye sits at
// pivot + Quat::fromEulerAngles(pitch, yaw, 0) * (0,0,1) * distance, which is
// also what setCameraView's axis table uses (top = yaw 0, pitch -90).
bool EngineSceneViewport::frameNode(iris::SceneNodePtr sceneNode, const EditorFraming &framing)
{
    if (!sceneNode || !mEditorCam) return false;
    sceneNode->update(0.0f);

    iris::Vec3 target = sceneNode->getGlobalPosition();
    float radius = 1.0f;
    const iris::AABB bounds = preview::worldBoundingBox(sceneNode);
    if (bounds.getMin().x() <= bounds.getMax().x()) {   // non-empty (meshes exist)
        target = bounds.getCenter();
        radius = qMax(0.05f, bounds.getSize().length() * 0.5f);
    }
    const float dist = framing.distance > 0.0f
                           ? framing.distance
                           : qMax(1.0f, preview::framingDistance(radius, mEditorCam->angle));

    // Missing yaw/pitch = "keep looking from where I look now", which makes
    // frameNode(id) the verb form of the F key.
    float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
    mEditorCam->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
    if (framing.hasYaw) yaw = framing.yawDegrees;
    if (framing.hasPitch) pitch = framing.pitchDegrees;

    const iris::Quat rot = iris::Quat::fromEulerAngles(pitch, yaw, 0.0f);
    const iris::Vec3 offset = rot.rotatedVector(iris::Vec3(0, 0, 1));
    mEditorCam->setLocalPos(target + offset * dist);
    mEditorCam->setLocalRot(rot);
    float nearClip, farClip;
    preview::clipPlanesForFraming(dist, radius, nearClip, farClip);
    mEditorCam->farClip = qMax(mEditorCam->farClip, farClip);
    mEditorCam->update(0.0f);

    // The arcball adopts THIS pivot: hand it the distance we framed at and let
    // setCamera() re-derive the pivot, which lands back on `target` exactly
    // (the pose above is already the controller's own orbit formula).
    resyncCameraController(dist);
    mLastOrbitPivot = target;   // a following Alt+drag orbits what we framed
    return true;
}

void EngineSceneViewport::setActiveGizmo(Gizmo *g)
{
    if (mGizmo == g) return;
    if (mGizmo && mGizmo->isDragging()) mGizmo->endDragging();
    mGizmo = g;
    if (mGizmo) { if (mSelectedNode) mGizmo->setSelectedNode(mSelectedNode); else mGizmo->clearSelectedNode(); }
}
void EngineSceneViewport::setGizmoLoc()   { setActiveGizmo(mTranslateGizmo); }
void EngineSceneViewport::setGizmoRot()   { setActiveGizmo(mRotateGizmo); }
void EngineSceneViewport::setGizmoScale() { setActiveGizmo(mScaleGizmo); }
void EngineSceneViewport::setGizmoTransformToLocal()
{
    mTranslateGizmo->setTransformSpace(GizmoTransformSpace::Local);
    mRotateGizmo->setTransformSpace(GizmoTransformSpace::Local);
    mScaleGizmo->setTransformSpace(GizmoTransformSpace::Local);
}
void EngineSceneViewport::setGizmoTransformToGlobal()
{
    mTranslateGizmo->setTransformSpace(GizmoTransformSpace::Global);
    mRotateGizmo->setTransformSpace(GizmoTransformSpace::Global);
    mScaleGizmo->setTransformSpace(GizmoTransformSpace::Global);
}

bool EngineSceneViewport::mouseRay(iris::Vec3 &rayPos, iris::Vec3 &rayDir, iris::Vec3 &viewDir) const
{
    if (!mEditorCam) return false;
    viewDir = mEditorCam->getGlobalRotation().rotatedVector(iris::Vec3(0, 0, -1));
    if (!mHaveMouse) return false;
    iris::Vec3 a, b;
    ScenePicker::screenSegment(mEditorCam, width(), height(), mMousePos, a, b);
    rayPos = a; rayDir = (b - a).normalized();
    return true;
}

bool EngineSceneViewport::ensureEngineScene()
{
    if (mEngineScene) return true;
    if (!mEngine || !view()) return false;
    mEngineScene = mEngine->createScene("editor-" + std::to_string(++mViewSerial) + "-" +
                                        std::to_string(reinterpret_cast<uintptr_t>(this)));
    if (!mEngineScene) return false;
    mEngineScene->setAmbient(Colour(0.25f, 0.27f, 0.32f), Colour(0.15f, 0.15f, 0.18f));
    view()->setScene(mEngineScene);
    view()->setShadows(true);           // directional PSSM; lights opt in via the document
    mMirror.reset(new SceneMirror(mEngineScene));
    mOverlay.reset(new GizmoOverlay(mEngineScene));
    if (mScene) mMirror->setSource(mScene);
    return true;
}

void EngineSceneViewport::viewRecreated()
{
    // The old View took its camera, workspace and scene binding with it.
    if (view() && mEngineScene) {
        view()->setScene(mEngineScene);
        view()->setShadows(true);
    }
    // A fresh View starts its present count at zero, so the baseline must too —
    // otherwise presentsSinceBind() reads a subtraction of a larger number and
    // the cover never comes down again.
    mPresentBaseline = 0;
    updateCover();
}

void EngineSceneViewport::showEvent(QShowEvent *e)
{
    EngineViewWidget::showEvent(e);
    // The native window exists now: bind a View to it, then the engine scene.
    if (!view() && mEngine)
        createView(mEngine, "editor-viewport-" + QString::number(reinterpret_cast<uintptr_t>(this)),
                   Colour(0.10f, 0.11f, 0.14f));
    ensureEngineScene();
    // Becoming visible with nothing presented yet is exactly the moment the
    // stale pixels underneath would show through.
    updateCover();
}

iris::SceneNodePtr EngineSceneViewport::pickAt(const QPointF &point, bool selectRootObject,
                                                iris::Vec3 *hitPoint, bool forcePickable)
{
    if (!mScene || !mEditorCam) return iris::SceneNodePtr();
    iris::Vec3 a, b;
    ScenePicker::screenSegment(mEditorCam, width(), height(), point, a, b);
    const auto hits = ScenePicker::pickAll(mScene, a, b, mEditorCam->getGlobalPosition(), forcePickable);
    const ScenePick best = ScenePicker::nearest(hits);
    if (hitPoint && best.node) *hitPoint = best.hitPoint;
    return ScenePicker::resolveRootSelection(best.node, mSelectedNode, selectRootObject);
}

iris::Vec3 EngineSceneViewport::dropPositionAt(const QPointF &point)
{
    iris::Vec3 hit;
    if (pickAt(point, false, &hit, true)) return hit;
    // Ground plane (y = 0), like the legacy viewport's sceneFloor.
    if (!mEditorCam) return iris::Vec3();
    iris::Vec3 a, b;
    ScenePicker::screenSegment(mEditorCam, width(), height(), point, a, b);
    const iris::Plane floor = iris::IntersectionHelper::computePlaneND(iris::Vec3(100, 0, 100), iris::Vec3(-100, 0, 100), iris::Vec3(-100, 0, -100));
    float t; iris::Vec3 q;
    if (iris::IntersectionHelper::intersectSegmentPlane(a, a + (b - a).normalized() * 1024.0f, floor, t, q)) return q;
    return iris::Vec3();
}

// ---- drag and drop from the asset panel: ported from SceneViewWidget ----------------

static QMap<int, QVariant> dragRoleData(const QMimeData *mime)
{
    QByteArray encoded = mime->data("application/x-qabstractitemmodeldatalist");
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QMap<int, QVariant> roleDataMap;
    while (!stream.atEnd()) stream >> roleDataMap;
    return roleDataMap;
}

void EngineSceneViewport::dragEnterEvent(QDragEnterEvent *event)
{
    if (mHierarchyDragSource && event->source() == mHierarchyDragSource) {
        event->ignore();
        return;
    }
    if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist"))
        event->acceptProposedAction();
}

void EngineSceneViewport::dragLeaveEvent(QDragLeaveEvent *)
{
    if (mDragPreviewNode) {
        mDragPreviewNode.staticCast<iris::MeshNode>()->setMaterial(mDragOriginalMaterial);
        mDragPreviewNode.reset(); mDragOriginalMaterial.reset(); mDragWasHit = false;
    }
}

void EngineSceneViewport::dragMoveEvent(QDragMoveEvent *event)
{
    const QMap<int, QVariant> role = dragRoleData(event->mimeData());
    const int type = role.value(0).toInt();
    if (type == static_cast<int>(ModelTypes::Material)) {
        // Hover preview: temporarily apply the dragged material to the mesh under the pointer.
        iris::SceneNodePtr node = pickAt(event->position(), false);
        if (node && node->getSceneNodeType() != iris::SceneNodeType::Mesh) node.reset();
        if (mDragPreviewNode && mDragPreviewNode != node) {
            mDragPreviewNode.staticCast<iris::MeshNode>()->setMaterial(mDragOriginalMaterial);
            mDragPreviewNode.reset(); mDragOriginalMaterial.reset(); mDragWasHit = false;
        }
        if (node && !mDragWasHit) {
            mDragWasHit = true;
            mDragPreviewNode = node;
            auto meshNode = node.staticCast<iris::MeshNode>();
            mDragOriginalMaterial = meshNode->getMaterial();
            for (Asset *asset : AssetManager::getAssets()) {
                if (asset->assetGuid == role.value(3).toString()) {
                    // Project materials hydrate as MaterialPtr (PBR-aware);
                    // the built-in defaults from trigger() still store a
                    // CustomMaterialPtr — accept both.
                    const QVariant value = asset->getValue();
                    auto material = value.value<iris::MaterialPtr>();
                    if (!material) material = value.value<iris::CustomMaterialPtr>();
                    if (material) meshNode->setMaterial(material);
                }
            }
        }
    } else if (type == static_cast<int>(ModelTypes::Object) || type == static_cast<int>(ModelTypes::ParticleSystem)
               || type == static_cast<int>(ModelTypes::Texture)) {
        // Texture too (IMAGE_PLANE_SPEC §2): an image dropped on empty space
        // becomes an image plane at the drop point, so the drag must track it
        // exactly like Object drags.
        mDragScenePos = dropPositionAt(event->position());
    }
    event->acceptProposedAction();
}

void EngineSceneViewport::dropEvent(QDropEvent *event)
{
    const QMap<int, QVariant> role = dragRoleData(event->mimeData());
    const int type = role.value(0).toInt();
    if (type == static_cast<int>(ModelTypes::ParticleSystem)) {
        emit mEvents.addDroppedParticleSystem(true, mDragScenePos, role.value(3).toString(), role.value(1).toString());
    } else if (type == static_cast<int>(ModelTypes::Object)) {
        if (Constants::Reserved::DefaultPrimitives.contains(role.value(3).toString())) {
            emit mEvents.addPrimitive(Constants::Reserved::DefaultPrimitives.value(role.value(3).toString()));
            return;
        }
        emit mEvents.addDroppedMesh(QDir(mProject->getProjectFolder()).filePath(role.value(2).toString()),
                                    true, mDragScenePos, role.value(3).toString(), role.value(1).toString());
    } else if (type == static_cast<int>(ModelTypes::Material)) {
        if (mDragPreviewNode && mMainWindow) {
            auto target = mDragPreviewNode;
            // Put the original material back BEFORE the real apply: the hover
            // preview borrowed a shared AssetManager instance, and the undoable
            // apply must capture (and on undo restore) the true original.
            target.staticCast<iris::MeshNode>()->setMaterial(mDragOriginalMaterial);
            // Select the drop TARGET first, then apply. The old order applied
            // the preset to whatever was selected before the drag — usually a
            // different node, or a container the apply silently refused — while
            // the leaked preview material made the drop LOOK successful. The
            // document never held the material, so it vanished on reopen.
            mMainWindow->sceneNodeSelected(target);
            mMainWindow->applyMaterialPreset(role.value(3).toString());
        }
        mDragPreviewNode.reset(); mDragOriginalMaterial.reset(); mDragWasHit = false;
    } else if (type == static_cast<int>(ModelTypes::Texture)) {
        // IMAGE_PLANE_SPEC §2: on a mesh the image retextures it; on empty
        // space it spawns an image plane at the tracked drop point.
        const QString textureGuid = role.value(3).toString();
        iris::SceneNodePtr node = pickAt(event->position(), false);
        if (node && node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto meshNode = node.staticCast<iris::MeshNode>();
            // Bytes resolve pin-first through the CAS — the flat
            // projectFolder join this branch used pointed at a folder the
            // pin world no longer populates (the drop was dead code).
            QString texPath;
            if (mDatabase) {
                QSqlDatabase conn = QSqlDatabase::database();
                texPath = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                                  mProject ? mProject->getProjectGuid() : QString(),
                                                  textureGuid);
            }
            if (texPath.isEmpty()) return;
            if (auto pbr = meshNode->getMaterial().dynamicCast<iris::PbrMaterial>()) {
                // The PBR repair: baseColorMap, undoable like the panel edit.
                QVariant oldMap;
                for (auto *prop : pbr->properties)
                    if (prop->name == QStringLiteral("baseColorMap")) { oldMap = prop->getValue(); break; }
                if (mServices && mServices->undo) {
                    mServices->undo->push(new ChangeMaterialPropertyCommand(
                        pbr, QStringLiteral("baseColorMap"), oldMap, texPath));
                } else {
                    pbr->setValue(QStringLiteral("baseColorMap"), texPath);
                }
                if (mMainWindow) mMainWindow->sceneNodeSelected(node);
            } else if (auto mat = meshNode->getMaterial().dynamicCast<iris::CustomMaterial>()) {
                // The legacy CustomMaterial slot path stays for old materials.
                if (!mat->firstTextureSlot().isEmpty()) {
                    mat->setValue(mat->firstTextureSlot(), texPath);
                    if (mMainWindow) mMainWindow->sceneNodeSelected(node);
                }
            }
        } else {
            emit mEvents.addDroppedImagePlane(mDragScenePos, textureGuid);
        }
    } else if (type == static_cast<int>(ModelTypes::Sky)) {
        if (!mScene || !mDatabase) return;
        const QString skyGuid = role.value(3).toString();
        const QJsonObject skyDefinition = QJsonDocument::fromJson(mDatabase->fetchAssetData(skyGuid)).object();
        const QJsonObject skyProperties = QJsonDocument::fromJson(mDatabase->fetchAsset(skyGuid).properties).object();
        const int skyTypeIndex = skyProperties.value("sky").toObject().value("type").toInt();
        mScene->skyData.insert(mScene->skyTypeToStr[skyTypeIndex], skyDefinition);
        mScene->skyType = static_cast<iris::SkyType>(skyTypeIndex);
        emit mEvents.changeSkyFromAssetWidget(skyTypeIndex);
    }
}

void EngineSceneViewport::mousePressEvent(QMouseEvent *e)
{
    // Accept explicitly: an ignored press propagates to sceneContainer, whose
    // MainWindow::eventFilter re-sends it here — an infinite loop (each round
    // re-picked and rebuilt the property panel). The legacy widget accepts too.
    e->accept();
    setFocus();
    if (mPlaying && mPlayback) { mPlayback->mousePressEvent(e); return; }
    mMousePos = mPrevMousePos = e->position(); mHaveMouse = true;
    if (e->button() == Qt::LeftButton) {
        iris::Vec3 rayPos, rayDir, viewDir;
        const bool haveRay = mouseRay(rayPos, rayDir, viewDir);
        // A hit on the active gizmo starts a drag and keeps the selection.
        if (haveRay && mSelectedNode && mGizmo && mGizmo->isHit(rayPos, rayDir)) {
            // Alt+drag duplicates first, then drags the COPY — one undo macro
            // covers duplicate + move (EDITOR_SHORTCUTS_SPEC §4).
            if ((e->modifiers() & Qt::AltModifier) && mServices && mServices->sceneEdit &&
                mServices->undo && mServices->undo->stack() && mSelectedNode->isDuplicable()) {
                mServices->undo->stack()->beginMacro(QStringLiteral("Duplicate + Move"));
                iris::SceneNodePtr copy = mServices->sceneEdit->duplicateNode(mSelectedNode);
                if (copy) {
                    mAltDragMacroOpen = true;
                    setSelectedNode(copy);              // re-points the gizmo too
                    emit mEvents.sceneNodeSelected(copy);
                } else {
                    mServices->undo->stack()->endMacro();
                }
            }
            mGizmo->startDragging(rayPos, rayDir, viewDir);
        } else if (e->modifiers() & Qt::AltModifier) {
            // Alt+LMB anywhere BUT the gizmo orbits around the selection
            // (Maya/Unreal). The gizmo hit-test above ran first on purpose:
            // Alt ON the gizmo keeps meaning duplicate-while-transforming.
            // Orbiting must not re-pick, so the selection is left alone.
            if (mCamController) mCamController->setAltOrbit(true, orbitPivot());
        } else {
            iris::SceneNodePtr picked = pickAt(e->position(), true);
            setSelectedNode(picked);
            emit mEvents.sceneNodeSelected(picked);
        }
    }
    if (mCamController) mCamController->onMouseDown(e->button());
}

void EngineSceneViewport::mouseMoveEvent(QMouseEvent *e)
{
    e->accept();
    if (mPlaying && mPlayback) { mPlayback->mouseMoveEvent(e); return; }
    mMousePos = e->position(); mHaveMouse = true;
    const QPointF dir = mMousePos - mPrevMousePos;
    mPrevMousePos = mMousePos;
    if (mGizmo && mGizmo->isDragging()) {
        // V held during a translate drag: snap the dragged node's pivot to the
        // nearest vertex of the triangle under the cursor on OTHER meshes
        // (EDITOR_SHORTCUTS_SPEC §4). No target under the cursor -> plain drag.
        if (mVertexSnapHeld && mGizmo == mTranslateGizmo && snapDragToVertexUnderCursor())
            return;
        iris::Vec3 rayPos, rayDir, viewDir;
        if (mouseRay(rayPos, rayDir, viewDir)) mGizmo->drag(rayPos, rayDir, viewDir);
        return;
    }
    if (mCamController) mCamController->onMouseMove(-int(dir.x()), -int(dir.y()));
}

void EngineSceneViewport::mouseReleaseEvent(QMouseEvent *e)
{
    e->accept();
    if (mPlaying && mPlayback) { mPlayback->mouseReleaseEvent(e); return; }
    if (e->button() == Qt::LeftButton && mGizmo && mGizmo->isDragging()) mGizmo->endDragging();
    // The Alt+drag macro closes AFTER endDragging pushed its transform command,
    // so duplicate + move undo as one step.
    if (e->button() == Qt::LeftButton && mAltDragMacroOpen) {
        mAltDragMacroOpen = false;
        if (mServices && mServices->undo && mServices->undo->stack())
            mServices->undo->stack()->endMacro();
    }
    // Alt+LMB orbit ends with the drag (the free camera returns to fly).
    if (e->button() == Qt::LeftButton && mCamController && mCamController->isAltOrbiting())
        mCamController->setAltOrbit(false, iris::Vec3());
    if (mCamController) mCamController->onMouseUp(e->button());
}

void EngineSceneViewport::mouseDoubleClickEvent(QMouseEvent *e)
{
    // QWidget's default forwards to mousePressEvent (a second pick + panel rebuild).
    e->accept();
    if (mPlaying && mPlayback) mPlayback->mouseDoubleClickEvent(e);
}

void EngineSceneViewport::wheelEvent(QWheelEvent *e)
{
    e->accept();
    if (mPlaying && mPlayback) { mPlayback->wheelEvent(e); return; }
    if (mCamController) mCamController->onMouseWheel(e->angleDelta().y());
}

void EngineSceneViewport::keyPressEvent(QKeyEvent *e)
{
    if (mPlaying && mPlayback) { mPlayback->keyPressEvent(e); return; }
    // Auto-repeat presses would be harmless (the key is already in the held
    // set) but auto-repeat RELEASES would clear it mid-hold — skip both.
    if (e->isAutoRepeat()) return;
    if (e->key() == Qt::Key_V) mVertexSnapHeld = true;
    if (mCamController) mCamController->onKeyPressed(static_cast<Qt::Key>(e->key()));
}

void EngineSceneViewport::keyReleaseEvent(QKeyEvent *e)
{
    if (mPlaying && mPlayback) { mPlayback->keyReleaseEvent(e); return; }
    if (e->isAutoRepeat()) return;
    if (e->key() == Qt::Key_V) mVertexSnapHeld = false;
    if (mCamController) mCamController->keyReleaseEvent(e);
}

void EngineSceneViewport::focusOutEvent(QFocusEvent *e)
{
    // Keys released while another widget has focus never reach us — drop the
    // held set so fly keys cannot stick down.
    if (mCamController) mCamController->clearKeys();
    mVertexSnapHeld = false;
    EngineViewWidget::focusOutEvent(e);
}

// The V-hold vertex snap: pick under the cursor against every OTHER mesh
// (document CPU picking already reports the hit triangle), then move the
// dragged node's pivot to the hit triangle's nearest corner. Runs inside the
// live translate drag, so endDragging()'s undo command covers it.
bool EngineSceneViewport::snapDragToVertexUnderCursor()
{
    if (!mSelectedNode || !mScene || !mEditorCam || !mHaveMouse) return false;
    iris::Vec3 a, b;
    ScenePicker::screenSegment(mEditorCam, width(), height(), mMousePos, a, b);
    // refreshTransforms = false: this runs on every mouse move inside a live
    // translate drag, and the mirror's sync() already updated the document's
    // global transforms this frame. The update is a full recursive walk.
    const auto hits = ScenePicker::pickAll(mScene, a, b, mEditorCam->getGlobalPosition(),
                                           true, false, false, true, false);
    ScenePick best;
    for (const auto &h : hits) {
        if (!h.node || h.triangleIndex < 0) continue;
        bool own = false;                       // never snap to the dragged subtree
        for (auto n = h.node; n; n = n->getParent())
            if (n.data() == mSelectedNode.data()) { own = true; break; }
        if (own) continue;
        if (!best.node || h.distanceFromCameraSqrd < best.distanceFromCameraSqrd) best = h;
    }
    if (!best.node) return false;
    auto meshNode = best.node.staticCast<iris::MeshNode>();
    auto mesh = meshNode->getMesh();
    if (!mesh || !mesh->getTriMesh() ||
        best.triangleIndex >= mesh->getTriMesh()->triangles.size())
        return false;
    const iris::Triangle &tri = mesh->getTriMesh()->triangles[best.triangleIndex];
    const iris::Mat4 &xf = meshNode->globalTransform;
    const iris::Vec3 corners[3] = { xf * tri.a, xf * tri.b, xf * tri.c };
    iris::Vec3 vertex = corners[0];
    for (int i = 1; i < 3; ++i)
        if ((corners[i] - best.hitPoint).lengthSquared() <
            (vertex - best.hitPoint).lengthSquared())
            vertex = corners[i];
    mSelectedNode->setGlobalPos(vertex);
    if (mServices && mServices->sceneEdit) mServices->sceneEdit->notifyTransformChanged();
    return true;
}

bool EngineSceneViewport::event(QEvent *e)
{
    // The Unreal rule: while the right mouse button is held in free-camera
    // mode, W/A/S/D/Q/E belong to the fly camera — accept the ShortcutOverride
    // so the window-wide W/E/R gizmo shortcuts don't fire and the raw key
    // events reach keyPressEvent instead (EDITOR_SHORTCUTS_SPEC §2).
    if (e->type() == QEvent::ShortcutOverride && mCamController == mFreeCam &&
        mFreeCam && mFreeCam->isFlying()) {
        const int key = static_cast<QKeyEvent *>(e)->key();
        switch (key) {
        case Qt::Key_W: case Qt::Key_A: case Qt::Key_S: case Qt::Key_D:
        case Qt::Key_Q: case Qt::Key_E: case Qt::Key_Shift:
            e->accept();
            return true;
        default:
            break;
        }
    }
    return EngineViewWidget::event(e);
}

void EngineSceneViewport::setScene(iris::ScenePtr scene)
{
    // A PAUSED scene is still playing as far as PlayBack is concerned (mPlaying
    // is false but the physics world and saved transforms are live) — it has to
    // be stopped before the document underneath it is swapped.
    if (mPlaying || (mPlayback && mPlayback->isScenePlaying())) stopPlayingScene();
    mScene = scene;
    mSelectedNode.clear();
    // After clearScene() the engine scene is gone but the view survives;
    // rebuild the scene-scoped objects now (mirror/overlay pick up mScene).
    if (scene && view() && !mEngineScene) ensureEngineScene();
    if (mPlayback && scene) {
        // Like the legacy viewport: the editor camera doubles as the play camera.
        if (mEditorCam) scene->setCamera(mEditorCam);
        mPlayback->setScene(scene);
    }
    if (mMirror) mMirror->setSource(scene);
    // A new document scene means no frame of IT has presented yet, even when
    // the engine scene underneath is the same object (close/open reuses it).
    mPresentBaseline = view() ? qulonglong(view()->framesPresented()) : 0;
    updateCover();
}

void EngineSceneViewport::startPlayingScene()
{
    if (!mScene || !mPlayback) return;
    // playScene() knows the difference between a cold start and a resume; the
    // viewport flag only says whether syncFrame drives the simulation.
    if (!mPlaying) mPlayback->playScene();
    mPlaying = true;
}

void EngineSceneViewport::pausePlayingScene()
{
    if (!mPlaying) return;
    mPlaying = false;                 // time is not reset, like the legacy viewport
    // The pause is a state on PlayBack too, otherwise the next
    // startPlayingScene() re-entered play: animation clock back to zero, the
    // mid-play pose saved over the originals, and a second set of rigid bodies
    // and character controllers added to the physics world.
    if (mPlayback) mPlayback->pause();
}

void EngineSceneViewport::stopPlayingScene()
{
    if (!mPlaying && !(mPlayback && mPlayback->isScenePlaying())) return;
    mPlaying = false;
    if (mPlayback) mPlayback->stopScene();
    if (mScene) mScene->updateSceneAnimation(0.0f);
}

// "Simulate physics" — run the document's physics world in the editor, without
// entering play mode. Same three calls as the legacy viewport
// (sceneviewwidget.cpp); the per-frame stepping happens in syncFrame.
void EngineSceneViewport::startPhysicsSimulation()
{
    if (!mScene) return;
    mScene->getPhysicsEnvironment()->initializePhysicsWorldFromScene(mScene->getRootNode());
    mScene->getPhysicsEnvironment()->simulatePhysics();
}

void EngineSceneViewport::restartPhysicsSimulation()
{
    if (!mScene) return;
    mScene->getPhysicsEnvironment()->restartPhysics();
    mScene->getPhysicsEnvironment()->restoreNodeTransformations(mScene->getRootNode());
}

void EngineSceneViewport::stopPhysicsSimulation()
{
    if (!mScene) return;
    mScene->getPhysicsEnvironment()->stopPhysics();
}

// The setter does NOT emit: MainWindow calls it in response to sceneNodeSelected,
// so emitting here would loop. Only picking (below) announces a selection.
void EngineSceneViewport::setSelectedNode(iris::SceneNodePtr sceneNode)
{
    mSelectedNode = sceneNode;
    if (mGizmo) { if (sceneNode) mGizmo->setSelectedNode(sceneNode); else mGizmo->clearSelectedNode(); }
}

void EngineSceneViewport::clearSelectedNode()
{
    setSelectedNode(iris::SceneNodePtr());
}

// F / editor.focusSelection(): frame the node Unreal-style — keep the current
// view direction, back off far enough for the node's world bounds to fill the
// view (preview framing math), and adapt the far plane so a huge subject can
// never clip away (EDITOR_SHORTCUTS_SPEC §2).
void EngineSceneViewport::focusOnNode(iris::SceneNodePtr sceneNode)
{
    if (!sceneNode || !mEditorCam) return;
    sceneNode->update(0.0f);

    iris::Vec3 target = sceneNode->getGlobalPosition();
    float radius = 1.0f;
    const iris::AABB bounds = preview::worldBoundingBox(sceneNode);
    if (bounds.getMin().x() <= bounds.getMax().x()) {   // non-empty (meshes exist)
        target = bounds.getCenter();
        radius = qMax(0.05f, bounds.getSize().length() * 0.5f);
    }
    const float dist = qMax(1.0f, preview::framingDistance(radius, mEditorCam->angle));

    iris::Vec3 dir = (mEditorCam->getGlobalPosition() - target).normalized();
    if (dir.isNull()) dir = iris::Vec3(0.45f, 0.45f, 0.77f);
    mEditorCam->setLocalPos(target + dir * dist);
    mEditorCam->lookAt(target);
    float nearClip, farClip;
    preview::clipPlanesForFraming(dist, radius, nearClip, farClip);
    mEditorCam->farClip = qMax(mEditorCam->farClip, farClip);
    mEditorCam->update(0.0f);

    // Resync the active controller with the moved camera (free cam re-derives
    // yaw/pitch; the orbital cam re-derives its pivot and orbit distance).
    if (mCamController == mOrbitCam && mOrbitCam) mOrbitCam->focusOnNode(sceneNode);
    else if (mCamController) mCamController->setCamera(mEditorCam);
}

void EngineSceneViewport::focusOnSelection()
{
    if (mSelectedNode) focusOnNode(mSelectedNode);
    if (mSelectedNode) mLastOrbitPivot = orbitPivot();   // F then Alt+drag orbits there
}

iris::Vec3 EngineSceneViewport::orbitPivot() const
{
    // Alt+LMB orbits around the SELECTION's centre (its world bounding-box
    // centre when it has meshes, else its origin). With nothing selected we
    // fall back to the last focus point, and finally to the world origin.
    if (mSelectedNode) {
        mSelectedNode->update(0.0f);
        const iris::AABB bounds = preview::worldBoundingBox(mSelectedNode);
        if (bounds.getMin().x() <= bounds.getMax().x())   // non-empty (meshes exist)
            return bounds.getCenter();
        return mSelectedNode->getGlobalPosition();
    }
    return mLastOrbitPivot;
}

QString EngineSceneViewport::gizmoMode() const
{
    if (mGizmo == mRotateGizmo) return QStringLiteral("rotate");
    if (mGizmo == mScaleGizmo)  return QStringLiteral("scale");
    return QStringLiteral("translate");
}

void EngineSceneViewport::setEditorCamera(iris::CameraNodePtr camera)
{
    if (camera) mEditorCam = camera;
    if (mCamController) mCamController->setCamera(mEditorCam);
}

void EngineSceneViewport::resetEditorCam()
{
    clearViewStates();   // a fresh camera invalidates every remembered view pose
    mEditorCam = iris::CameraNode::create();
    mEditorCam->setLocalPos(iris::Vec3(0, 5, 14));
    mEditorCam->lookAt(iris::Vec3(0, 0, 0));
    mEditorCam->angle = 45.0f;
    mEditorCam->nearClip = 0.1f;
    mEditorCam->farClip = 1000.0f;
    if (mCamController) mCamController->setCamera(mEditorCam);
}

void EngineSceneViewport::setEditorData(EditorData *data)
{
    mEditorData = data;
    if (data) {
        if (data->editorCamera) {
            // Project open: another scene's camera — its remembered view
            // poses do not apply (per-view memory is per scene session).
            if (data->editorCamera != mEditorCam) clearViewStates();
            mEditorCam = data->editorCamera;
        }
        mShowLightWires = data->showLightWires;
        mShowGrid = data->showGrid;
        mShowDebugDraw = data->showDebugDrawFlags;
    }
    // The controller must steer the SAME camera the view renders; without this a
    // project load leaves the mouse driving the old, no-longer-rendered camera.
    if (mCamController) mCamController->setCamera(mEditorCam);
    if (mPlayback && mScene && mEditorCam) mScene->setCamera(mEditorCam);
}

EditorData *EngineSceneViewport::getEditorData()
{
    if (!mEditorData) mEditorData = new EditorData();
    mEditorData->editorCamera = mEditorCam;
    mEditorData->showLightWires = mShowLightWires;
    mEditorData->showGrid = mShowGrid;
    mEditorData->showDebugDrawFlags = mShowDebugDraw;
    return mEditorData;
}

void EngineSceneViewport::syncFrame(float dtOverride)
{
    if (!mActive || !view()) return;
    if (!ensureEngineScene()) return;
    // The wall clock, unless a caller supplied a step. mFrameTimer is restarted
    // either way: after a fixed-dt frame the NEXT free-running frame must not
    // charge the document for the time the scripted one took.
    const float wall = float(mFrameTimer.restart()) / 1000.0f;
    const float dt = dtOverride >= 0.0f ? dtOverride : wall;
    if (mPlaying && mPlayback) {
        iris::Viewport vp; vp.width = width(); vp.height = height(); vp.pixelRatioScale = 1.0f;
        mPlayback->update(vp, dt);        // physics, animation, play controllers move the document
    } else if (mCamController) {
        mCamController->update(dt);
    }
    // Emitters used to be ticked here, one document node at a time, because the
    // document owned a CPU particle simulator. It does not any more
    // (PARTICLES_FX2_SPEC): the engine simulates every particle inside
    // renderOneFrame, in the editor and in play mode alike, and the document
    // only says WHAT to emit and how fast the clock runs. The physics branch
    // stays — that really is a document-side simulation.
    if (!mPlaying && mScene && mScene->getPhysicsEnvironment()->isSimulating())
        mScene->update(dt);
    if (mEditorCam) { mEditorCam->setAspectRatio(height() ? float(width()) / float(height()) : 1.0f); }
    // G (Game View) hides every in-viewport editor helper; play mode hides the
    // grid too (the Unreal look), while the other helpers keep their existing
    // play behaviour.
    const bool helpers = !mGameView;
    if (mMirror) {
        mMirror->setLightWires(mShowLightWires && helpers);
        mMirror->setHighlightWireframe(mSelectionWireframe);
        // No selection outline for the World root (the whole scene would glow)
        // or for the built-in ground plane — owner ask 2026-08-31. Selection,
        // gizmo and property panel still work on both.
        iris::SceneNodePtr highlight = helpers ? mSelectedNode : iris::SceneNodePtr();
        if (highlight && ((mScene && highlight == mScene->getRootNode()) || highlight->isBuiltIn))
            highlight.reset();
        mMirror->setHighlightedNode(highlight);
        // Grid spacing = the translate snap size ([ and ] re-space it live).
        mMirror->setGrid(mShowGrid && helpers && !mPlaying, SnapSettings::translateSize());
        mMirror->sync();
    }
    if (mGizmo && mEditorCam && mSelectedNode) mGizmo->updateSize(mEditorCam);
    if (mOverlay) {
        iris::Vec3 rayPos, rayDir, viewDir;
        mouseRay(rayPos, rayDir, viewDir);
        mOverlay->update((helpers && mSelectedNode) ? mGizmo : nullptr, rayPos, rayDir, viewDir);
    }
    if (mMirror) mMirror->applySky(view());
    if (mMirror) mMirror->applyEnvironment(view(), mEngine.get());
    if (mMirror && mEditorCam) mMirror->applyCamera(mEditorCam, view());
    // A SCRIPTED step (editor.frame(n, dt)) has to be deterministic for the
    // particles too. They are simulated inside the engine now, from the
    // backend's own frame-time source, so a dt this viewport applies to the
    // document would otherwise leave the flame running on the wall clock — and
    // an offscreen frame takes about a millisecond, so 15 scripted frames would
    // buy 15 ms of fire and photograph an empty scene. Pushed AFTER
    // applyEnvironment, which owns the scene's normal time scale (the two
    // settings cancel each other inside the backend, so exactly one of them may
    // be live at a time).
    if (mEngine) {
        if (dtOverride >= 0.0f) mEngine->setFixedFrameDelta(dtOverride);
        else if (mEngine->fixedFrameDelta() > 0.0f)
            mEngine->setParticleTimeScale(mScene ? mScene->particleTimeScale : 1.0f);
    }
}

QImage EngineSceneViewport::takeScreenshot(QSize dimension)
{
    return takeScreenshot(dimension.width(), dimension.height());
}

bool EngineSceneViewport::planarReflectorAccepted(iris::SceneNodePtr node) const
{
    // "No engine to ask" answers true: a caller must not report a failure it
    // cannot see (headless runs, the document-only stand-in viewport).
    if (!mMirror || node.isNull() || !view() || !view()->scene()) return true;
    const jahshaka::engine::NodeId id = mMirror->engineNode(node.data());
    if (!id) return true;   // not mirrored yet — the next sync decides
    return view()->scene()->nodePlanarReflector(id);
}

void EngineSceneViewport::renderFrames(int n)
{
    renderFrames(n, -1.0f);
}

void EngineSceneViewport::renderFrames(int n, float dt)
{
    // editor.frame(n, dt): the deterministic document→engine sync + render
    // pattern of the headless suites, synchronously — scripts step exact frames
    // instead of sleeping against the driver timer.
    //
    // WITHOUT `dt` this was only half deterministic: syncFrame pulls
    // mFrameTimer.restart(), so in PLAY mode each stepped frame advanced the
    // document's animation clock by however long the previous statement
    // happened to take. Every scripted play-mode assertion was therefore timing
    // dependent — the last thing standing between us and a play-mode pixel
    // gate, now that clip evaluation is the engine's and everything else in the
    // chain is driven by absolute time.
    if (!mEngine) return;
    for (int i = 0; i < n; ++i) {
        syncFrame(dt);
        mEngine->renderOneFrame();
        // The deterministic path bypasses EngineRenderDriver entirely, so it
        // has to drain the engine's error sink itself or a scripted/headless
        // run would be the one place failures stay silent — which is exactly
        // where the gates live (services/engineerrorpump.h).
        EngineErrorPump::instance().drain(mEngine.get());
    }
    // Scripted stepping is the deterministic path: editor.frame(2) must be
    // enough to take the cover down, exactly as two driver frames would.
    updateCover();
}

QImage EngineSceneViewport::takeScreenshot(int width, int height)
{
    return takeScreenshot(width, height, false);
}

QImage EngineSceneViewport::takeScreenshot(int width, int height, bool postFx)
{
    // Offscreen render of the same engine scene at the requested size, then readback.
    if (!mEngine || !mEngineScene || width <= 0 || height <= 0) return QImage();
    View *shot = mEngine->createOffscreenView("screenshot-" + std::to_string(++mViewSerial),
                                             unsigned(width), unsigned(height),
                                             Colour(0.10f, 0.11f, 0.14f));
    if (!shot) return QImage();
    shot->setScene(mEngineScene);
    if (mMirror) {
        mMirror->sync();
        // The shot view starts with a hardcoded background; give it the document
        // sky (flat colour) and world settings (shadows toggle) like the live view.
        // Textured skies are scene geometry and show up regardless.
        mMirror->applySky(shot);
        mMirror->applyEnvironment(shot);
        if (mEditorCam) mMirror->applyCamera(mEditorCam, shot);
        // applyEnvironment pushed the scene's post-fx description, which an
        // offscreen view ignores unless it is told otherwise. `postFx` is that
        // opt-in and the only place in the app that sets it: it makes the shot
        // match the viewport (tonemapped, bloomed, AA'd) at the cost of no
        // longer being a neutral, exactly-reproducible readback.
        if (postFx) {
            jahshaka::engine::PostFxDesc fx = shot->postFx();
            fx.allowOffscreen = true;
            shot->setPostFx(fx);
        }
    }
    for (int i = 0; i < 2; ++i) mEngine->renderOneFrame();
    Image img;
    QImage result;
    if (shot->readPixels(img) && img.width && img.height) {
        result = QImage(int(img.width), int(img.height), QImage::Format_RGBA8888);
        for (unsigned y = 0; y < img.height; ++y)
            memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    }
    mEngine->destroyView(shot);
    return result;
}

void EngineSceneViewport::begin()
{
    mActive = true;
    // Coming back from the player space, which drives its own engine scene
    // through its own mirror: re-push the world settings whose backend state is
    // process-wide (the HlmsPbs GI binding) instead of trusting a debounce that
    // was last true while a different scene owned the screen.
    if (mMirror) mMirror->invalidateEnvironment();
    if (view()) view()->setEnabled(true);
    updateCover();
}

void EngineSceneViewport::end()
{
    mActive = false;
    if (view()) view()->setEnabled(false);
}

// ---------------------------------------------------------------------------
// The cover (viewportcover.h): what this viewport looks like while the engine
// has no frame of the current world on screen.

void EngineSceneViewport::setCover(ViewportCover *cover)
{
    mCover = cover;
    updateCover();
}

qulonglong EngineSceneViewport::presentsSinceBind() const
{
    if (!view()) return 0;
    const qulonglong now = qulonglong(view()->framesPresented());
    // The engine resets its own counter when a scene is (re)bound; then `now`
    // is below the baseline and IS the answer.
    return now >= mPresentBaseline ? now - mPresentBaseline : now;
}

QString EngineSceneViewport::presentationState() const
{
    // No render target of our own, or one that never reaches the widget: the
    // cover has no business here and the honest answer is "offscreen".
    if (!view() || view()->isOffscreen()) return QStringLiteral("offscreen");
    if (!mScene) return QStringLiteral("noscene");
    return presentsSinceBind() >= kPresentsBeforeReveal
               ? QStringLiteral("presenting")
               : QStringLiteral("loading");
}

qulonglong EngineSceneViewport::framesPresented() const
{
    return presentsSinceBind();
}

void EngineSceneViewport::updateCover()
{
    if (!mCover) return;
    // The on-screen View could not be created and we are running on an offscreen
    // fallback: nothing will ever present into this widget, so the cover is the
    // permanent state here and it carries the engine's reason. Checked FIRST —
    // presentationState() would answer "offscreen", which maps to Presenting and
    // would hide the cover over a region the engine never writes.
    if (!viewCreationError().isEmpty()) {
        mCover->setSubtitle(viewCreationError());
        mCover->setState(ViewportCover::State::Failed);
        return;
    }
    const QString state = presentationState();
    if (state == QLatin1String("loading"))
        mCover->setState(ViewportCover::State::Loading);
    else if (state == QLatin1String("noscene"))
        mCover->setState(ViewportCover::State::NoScene);
    else
        mCover->setState(ViewportCover::State::Presenting);
}

void EngineSceneViewport::beginSceneLoad(const QString &title)
{
    // The world on screen (if any) is about to be replaced: nothing presented
    // from here on belongs to the old one.
    mPresentBaseline = view() ? qulonglong(view()->framesPresented()) : 0;
    if (!mCover) return;
    mCover->setSubtitle(title);
    // Loading, not the computed state: the caller is telling us a world is on
    // its way, and it is about to block this thread reading it. showNow paints
    // synchronously so the grey is on screen before that happens — a posted
    // paint would arrive after the load it exists to cover.
    mCover->showNow(ViewportCover::State::Loading);
}

void EngineSceneViewport::primeSceneGeometry()
{
    // The SLOW half of "opening a world" is not reading the document — it is
    // pushing it into the engine: every mesh, material and texture uploads on
    // the first SceneMirror::sync(). That used to happen on the first driver
    // tick AFTER the page switch, which is most of the time the viewport spent
    // with nothing of its own on screen. Doing it here pays for it while the
    // desktop page (and its progress dialog) is still what the user is looking
    // at, exactly as MainWindow::openProject's ordering intends.
    //
    // This is the same push the frame loop makes, from the same place between
    // frames — never inside one. It renders nothing: the view is disabled while
    // the editor page is hidden, and this deliberately does not enable it.
    // It is also entirely optional: on the very first open no View exists yet
    // (it is created when the page is first shown), so this returns and the
    // loading cover carries the wait instead.
    if (!mScene || !view() || !ensureEngineScene()) return;
    if (!mMirror) return;
    const bool helpers = !mGameView;
    mMirror->setLightWires(mShowLightWires && helpers);
    mMirror->setHighlightWireframe(mSelectionWireframe);
    mMirror->setGrid(mShowGrid && helpers, SnapSettings::translateSize());
    LoadTimeline::Accumulate mirror(QStringLiteral("engine:mirrorSync"));
    mMirror->sync();
}

void EngineSceneViewport::primeSceneEnvironment()
{
    // The other half (see primeSceneGeometry): sky, world settings, camera.
    // A separate event-loop turn in the threaded open.
    if (!mScene || !view() || !mMirror || !mEngineScene) return;
    {
        LoadTimeline::Accumulate sky(QStringLiteral("engine:applySky"));
        mMirror->applySky(view());
    }
    {
        // The world settings — and, at Epic, the VCT voxelize + light
        // injection, the shadow atlas resize and the post chain's shader
        // variants.
        LoadTimeline::Accumulate env(QStringLiteral("engine:applyEnvironment"));
        mMirror->applyEnvironment(view(), mEngine.get());
    }
    if (mEditorCam) mMirror->applyCamera(mEditorCam, view());
}

unsigned EngineSceneViewport::warmUpShaders()
{
    // The third prime (SHADER_CACHE_SPEC §5). Runs on the open path, after the
    // geometry and environment pushes and BEFORE the page is revealed — the
    // loading cover is up, so the frame the engine renders to build its shaders
    // is invisible.
    if (!mEngine || !view() || !mEngineScene) return 0;

    unsigned before = 0, cached = 0, expected = 0;
    mEngine->shaderBuildProgress(before, cached, expected);

    // THE BUDGET, and it is measured, not guessed (open.responsive, Showroom,
    // this box):
    //
    //   no warm-up          cold open worst UI gap 1691-1789 ms, warm 439-476
    //   warm-up every open  cold 1723-1761 ms,                    warm 646-717
    //
    // The cold open is unchanged — the compiles happened either way, and the
    // gap there is dominated by other stages. What the second open pays is
    // ~250 ms for a frame that compiles NOTHING: the Hlms shader cache is
    // PROCESS-wide, so once this process has built a world's shaders, opening
    // another world mostly finds them already there. 250 ms of UI block for
    // nothing is exactly what the 500 ms responsiveness budget exists to catch,
    // and widening that budget to accommodate a no-op would be the wrong trade.
    //
    // So: warm up while it is still paying. If a warm-up compiles nothing, note
    // the compile count and skip the next one — until something compiles for
    // any OTHER reason (new content in a later world), which moves the count
    // and re-arms this. Self-correcting in both directions, and no heuristic
    // about what a world contains.
    if (mWarmUpIdleAt == before) return 0;

    {
        LoadTimeline::Accumulate warm(QStringLiteral("engine:warmUpShaders"));
        view()->warmUpShaders();
    }
    unsigned after = 0;
    mEngine->shaderBuildProgress(after, cached, expected);
    mWarmUpIdleAt = (after == before) ? before : kWarmUpAlwaysRun;
    return after - before;
}

void EngineSceneViewport::coverIfNotPresenting()
{
    updateCover();
    if (mCover && mCover->isVisible()) mCover->repaint();
}

void EngineSceneViewport::cleanup()
{
    mActive = false;
    clearScene();
    destroyView();
}

void EngineSceneViewport::clearScene()
{
    // Project-swap teardown: destroy the scene-scoped objects (overlay, mirror,
    // engine scene) but keep the View — its native window and swapchain stay
    // valid across project close/open. Script sessions never leave the editor
    // page, so no showEvent would ever recreate a destroyed view (the
    // "engine viewport is not available after project.open" defect).
    // mActive is deliberately NOT cleared here: syncFrame's members are all
    // null-guarded, and clearing it would leave the viewport silently frozen
    // if no space switch follows (mActive belongs to begin()/end()).
    if (mOverlay) { mOverlay->clear(); mOverlay.reset(); }
    if (mMirror) { mMirror->setSource(nullptr); mMirror.reset(); }
    if (view()) {
        view()->setScene(nullptr);
        // The next project may not drive the background (only SINGLE_COLOR
        // skies do) — reset to the editor grey the view was created with.
        view()->setBackground(Colour(0.10f, 0.11f, 0.14f));
    }
    if (mEngineScene && mEngine) { mEngine->destroyScene(mEngineScene); mEngineScene = nullptr; }
    mScene.clear();
    mSelectedNode.clear();
    // No world bound: the cover says so (and the next bind starts from here).
    mPresentBaseline = view() ? qulonglong(view()->framesPresented()) : 0;
    updateCover();
}

IEditorViewport *createEngineSceneViewport(const std::shared_ptr<Engine> &engine,
                                           EngineRenderDriver *driver, QWidget *parent)
{
    return new EngineSceneViewport(engine, driver, parent);
}

void EngineSceneViewport::setServices(StudioServices *services)
{
    // The gizmos raise undo pushes / refreshes through the aggregate; the
    // viewport itself needs it for Alt+drag duplicate and End snap-to-floor.
    mServices = services;
    if (mTranslateGizmo) mTranslateGizmo->setServices(services);
    if (mRotateGizmo)    mRotateGizmo->setServices(services);
    if (mScaleGizmo)     mScaleGizmo->setServices(services);
}

// End / editor.snapToFloor(): drop the selection straight down onto the first
// scene surface BELOW its bounds; no hit means the y=0 ground plane (the
// dropPositionAt convention). Undoable (EDITOR_SHORTCUTS_SPEC §4).
bool EngineSceneViewport::snapSelectionToFloor()
{
    if (!mSelectedNode || !mScene) return false;
    mSelectedNode->update(0.0f);

    const iris::AABB bounds = preview::worldBoundingBox(mSelectedNode);
    const bool hasBounds = bounds.getMin().x() <= bounds.getMax().x();
    const iris::Vec3 pos = mSelectedNode->getGlobalPosition();
    const float bottom = hasBounds ? bounds.getMin().y() : pos.y();
    const iris::Vec3 centre = hasBounds ? bounds.getCenter() : pos;

    // Straight down from just under the selection's own bounds, so its own
    // meshes can never be the hit.
    const iris::Vec3 start(centre.x(), bottom - 0.001f, centre.z());
    const iris::Vec3 end = start + iris::Vec3(0.0f, -10000.0f, 0.0f);
    const auto hits = ScenePicker::pickAll(mScene, start, end, start, true, false, false);
    float targetY = 0.0f;                       // fallback: the y = 0 plane
    bool found = false;
    for (const auto &h : hits) {
        if (!h.node) continue;
        bool own = false;                       // ignore the selection's own subtree
        for (auto n = h.node; n; n = n->getParent())
            if (n.data() == mSelectedNode.data()) { own = true; break; }
        if (own) continue;
        if (!found || h.hitPoint.y() > targetY) { targetY = h.hitPoint.y(); found = true; }
    }

    const float delta = targetY - bottom;
    if (std::abs(delta) < 1e-5f) return true;   // already on the floor

    const iris::Vec3 oldLocalPos = mSelectedNode->getLocalPos();
    const iris::Quat rot = mSelectedNode->getLocalRot();
    const iris::Vec3 scale = mSelectedNode->getLocalScale();
    mSelectedNode->setGlobalPos(pos + iris::Vec3(0.0f, delta, 0.0f));
    const iris::Vec3 newLocalPos = mSelectedNode->getLocalPos();
    if (mServices && mServices->undo) {
        mServices->undo->push(new TransformSceneNodeCommand(mSelectedNode,
                                                            oldLocalPos, rot, scale,
                                                            newLocalPos, rot, scale));
    }
    return true;
}
