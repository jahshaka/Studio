#include "viewport/enginesceneviewport.h"

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
#include "irisgl/core/math/intersectionhelper.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include <QVector3D>
#include <QQuaternion>

#include "viewport/enginerenderdriver.h"
#include "viewport/previewframing.h"
#include "viewport/snapsettings.h"
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
        connect(mDriver, &EngineRenderDriver::beforeFrame, this, &EngineSceneViewport::syncFrame);
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

bool EngineSceneViewport::mouseRay(QVector3D &rayPos, QVector3D &rayDir, QVector3D &viewDir) const
{
    if (!mEditorCam) return false;
    viewDir = mEditorCam->getGlobalRotation().rotatedVector(QVector3D(0, 0, -1));
    if (!mHaveMouse) return false;
    QVector3D a, b;
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

void EngineSceneViewport::showEvent(QShowEvent *e)
{
    EngineViewWidget::showEvent(e);
    // The native window exists now: bind a View to it, then the engine scene.
    if (!view() && mEngine)
        createView(mEngine, "editor-viewport-" + QString::number(reinterpret_cast<uintptr_t>(this)),
                   Colour(0.10f, 0.11f, 0.14f));
    ensureEngineScene();
}

iris::SceneNodePtr EngineSceneViewport::pickAt(const QPointF &point, bool selectRootObject,
                                                QVector3D *hitPoint, bool forcePickable)
{
    if (!mScene || !mEditorCam) return iris::SceneNodePtr();
    QVector3D a, b;
    ScenePicker::screenSegment(mEditorCam, width(), height(), point, a, b);
    const auto hits = ScenePicker::pickAll(mScene, a, b, mEditorCam->getGlobalPosition(), forcePickable);
    const ScenePick best = ScenePicker::nearest(hits);
    if (hitPoint && best.node) *hitPoint = best.hitPoint;
    return ScenePicker::resolveRootSelection(best.node, mSelectedNode, selectRootObject);
}

QVector3D EngineSceneViewport::dropPositionAt(const QPointF &point)
{
    QVector3D hit;
    if (pickAt(point, false, &hit, true)) return hit;
    // Ground plane (y = 0), like the legacy viewport's sceneFloor.
    if (!mEditorCam) return QVector3D();
    QVector3D a, b;
    ScenePicker::screenSegment(mEditorCam, width(), height(), point, a, b);
    const iris::Plane floor = iris::IntersectionHelper::computePlaneND(QVector3D(100, 0, 100), QVector3D(-100, 0, 100), QVector3D(-100, 0, -100));
    float t; QVector3D q;
    if (iris::IntersectionHelper::intersectSegmentPlane(a, a + (b - a).normalized() * 1024.0f, floor, t, q)) return q;
    return QVector3D();
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
                    auto material = asset->getValue().value<iris::CustomMaterialPtr>();
                    if (material) meshNode->setMaterial(material);
                }
            }
        }
    } else if (type == static_cast<int>(ModelTypes::Object) || type == static_cast<int>(ModelTypes::ParticleSystem)) {
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
            mMainWindow->applyMaterialPreset(role.value(3).toString());
            mMainWindow->sceneNodeSelected(mDragPreviewNode);
        }
        mDragPreviewNode.reset(); mDragOriginalMaterial.reset(); mDragWasHit = false;
    } else if (type == static_cast<int>(ModelTypes::Texture)) {
        iris::SceneNodePtr node = pickAt(event->position(), false);
        if (node && node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto mat = node.staticCast<iris::MeshNode>()->getMaterial().dynamicCast<iris::CustomMaterial>();
            if (mat && !mat->firstTextureSlot().isEmpty()) {
                mat->setValue(mat->firstTextureSlot(), QDir(mProject->getProjectFolder()).filePath(role.value(1).toString()));
                if (mMainWindow) mMainWindow->sceneNodeSelected(node);
            }
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
        QVector3D rayPos, rayDir, viewDir;
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
        QVector3D rayPos, rayDir, viewDir;
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
    if (mCamController) mCamController->onKeyPressed(static_cast<Qt::Key>(e->key()));
}

void EngineSceneViewport::keyReleaseEvent(QKeyEvent *e)
{
    if (mPlaying && mPlayback) { mPlayback->keyReleaseEvent(e); return; }
    if (e->isAutoRepeat()) return;
    if (mCamController) mCamController->keyReleaseEvent(e);
}

void EngineSceneViewport::focusOutEvent(QFocusEvent *e)
{
    // Keys released while another widget has focus never reach us — drop the
    // held set so fly keys cannot stick down.
    if (mCamController) mCamController->clearKeys();
    EngineViewWidget::focusOutEvent(e);
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
    if (mPlaying) stopPlayingScene();
    mScene = scene;
    mSelectedNode.clear();
    if (mPlayback && scene) {
        // Like the legacy viewport: the editor camera doubles as the play camera.
        if (mEditorCam) scene->setCamera(mEditorCam);
        mPlayback->setScene(scene);
    }
    if (mMirror) mMirror->setSource(scene);
}

void EngineSceneViewport::startPlayingScene()
{
    if (!mScene || !mPlayback) return;
    if (!mPlaying) mPlayback->playScene();
    mPlaying = true;
}

void EngineSceneViewport::pausePlayingScene()
{
    mPlaying = false;                 // time is not reset, like the legacy viewport
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

    QVector3D target = sceneNode->getGlobalPosition();
    float radius = 1.0f;
    const iris::AABB bounds = preview::worldBoundingBox(sceneNode);
    if (bounds.getMin().x() <= bounds.getMax().x()) {   // non-empty (meshes exist)
        target = bounds.getCenter();
        radius = qMax(0.05f, bounds.getSize().length() * 0.5f);
    }
    const float dist = qMax(1.0f, preview::framingDistance(radius, mEditorCam->angle));

    QVector3D dir = (mEditorCam->getGlobalPosition() - target).normalized();
    if (dir.isNull()) dir = QVector3D(0.45f, 0.45f, 0.77f);
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
    mEditorCam = iris::CameraNode::create();
    mEditorCam->setLocalPos(QVector3D(0, 5, 14));
    mEditorCam->lookAt(QVector3D(0, 0, 0));
    mEditorCam->angle = 45.0f;
    mEditorCam->nearClip = 0.1f;
    mEditorCam->farClip = 1000.0f;
    if (mCamController) mCamController->setCamera(mEditorCam);
}

void EngineSceneViewport::setEditorData(EditorData *data)
{
    mEditorData = data;
    if (data) {
        if (data->editorCamera) mEditorCam = data->editorCamera;
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

void EngineSceneViewport::syncFrame()
{
    if (!mActive || !view()) return;
    if (!ensureEngineScene()) return;
    const float dt = float(mFrameTimer.restart()) / 1000.0f;
    if (mPlaying && mPlayback) {
        iris::Viewport vp; vp.width = width(); vp.height = height(); vp.pixelRatioScale = 1.0f;
        mPlayback->update(vp, dt);        // physics, animation, play controllers move the document
    } else if (mCamController) {
        mCamController->update(dt);
    }
    // Emitters animate in the editor exactly as in the legacy viewport (which
    // ticks the WHOLE scene each frame, sceneviewwidget.cpp). Here only the
    // particle nodes tick: a full scene update would also run physics while not
    // playing. Play mode already ticks them via PlayBack::update -> scene->update.
    // Exception: "Simulate physics" (startPhysicsSimulation) — then the full
    // scene update runs, stepping Bullet and writing body transforms back onto
    // the document nodes; it also ticks the emitters, so skip the explicit
    // particle loop that frame to avoid double-ticking them.
    if (!mPlaying && mScene) {
        if (mScene->getPhysicsEnvironment()->isSimulating()) {
            mScene->update(dt);
        } else {
            for (const auto &ps : mScene->particleSystems)
                if (ps) ps->update(dt);
        }
    }
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
        QVector3D rayPos, rayDir, viewDir;
        mouseRay(rayPos, rayDir, viewDir);
        mOverlay->update((helpers && mSelectedNode) ? mGizmo : nullptr, rayPos, rayDir, viewDir);
    }
    if (mMirror) mMirror->applySky(view());
    if (mMirror) mMirror->applyEnvironment(view(), mEngine.get());
    if (mMirror && mEditorCam) mMirror->applyCamera(mEditorCam, view());
}

QImage EngineSceneViewport::takeScreenshot(QSize dimension)
{
    return takeScreenshot(dimension.width(), dimension.height());
}

void EngineSceneViewport::renderFrames(int n)
{
    // editor.frame(n): the deterministic document→engine sync + render pattern of
    // the headless suites, synchronously — scripts step exact frames instead of
    // sleeping against the driver timer.
    if (!mEngine) return;
    for (int i = 0; i < n; ++i) {
        syncFrame();
        mEngine->renderOneFrame();
    }
}

QImage EngineSceneViewport::takeScreenshot(int width, int height)
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
    if (view()) view()->setEnabled(true);
}

void EngineSceneViewport::end()
{
    mActive = false;
    if (view()) view()->setEnabled(false);
}

void EngineSceneViewport::cleanup()
{
    mActive = false;
    if (mOverlay) { mOverlay->clear(); mOverlay.reset(); }
    if (mMirror) { mMirror->setSource(nullptr); mMirror.reset(); }
    if (mEngineScene && mEngine) { mEngine->destroyScene(mEngineScene); mEngineScene = nullptr; }
    destroyView();
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
    const QVector3D pos = mSelectedNode->getGlobalPosition();
    const float bottom = hasBounds ? bounds.getMin().y() : pos.y();
    const QVector3D centre = hasBounds ? bounds.getCenter() : pos;

    // Straight down from just under the selection's own bounds, so its own
    // meshes can never be the hit.
    const QVector3D start(centre.x(), bottom - 0.001f, centre.z());
    const QVector3D end = start + QVector3D(0.0f, -10000.0f, 0.0f);
    const auto hits = ScenePicker::pickAll(mScene, start, end, start, true, false, false);
    float targetY = 0.0f;                       // fallback: the y = 0 plane
    bool found = false;
    for (const auto &h : hits) {
        if (!h.node) continue;
        bool own = false;                       // ignore the selection's own subtree
        for (iris::SceneNode *n = h.node.data(); n; n = n->parent.data())
            if (n == mSelectedNode.data()) { own = true; break; }
        if (own) continue;
        if (!found || h.hitPoint.y() > targetY) { targetY = h.hitPoint.y(); found = true; }
    }

    const float delta = targetY - bottom;
    if (std::abs(delta) < 1e-5f) return true;   // already on the floor

    const QVector3D oldLocalPos = mSelectedNode->getLocalPos();
    const QQuaternion rot = mSelectedNode->getLocalRot();
    const QVector3D scale = mSelectedNode->getLocalScale();
    mSelectedNode->setGlobalPos(pos + QVector3D(0.0f, delta, 0.0f));
    const QVector3D newLocalPos = mSelectedNode->getLocalPos();
    if (mServices && mServices->undo) {
        mServices->undo->push(new TransformSceneNodeCommand(mSelectedNode,
                                                            oldLocalPos, rot, scale,
                                                            newLocalPos, rot, scale));
    }
    return true;
}
