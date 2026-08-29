#include "enginesceneviewport.h"

#include <QShowEvent>
#include <QMouseEvent>
#include "../editor/scenepicker.h"
#include "../editor/translationgizmo.h"
#include "../editor/rotationgizmo.h"
#include "../editor/scalegizmo.h"
#include "../editor/gizmooverlay.h"
#include "../editor/cameracontrollerbase.h"
#include "../editor/editorcameracontroller.h"
#include "../editor/orbitalcameracontroller.h"
#include <QWheelEvent>
#include <QKeyEvent>
#include <QVector3D>
#include <QQuaternion>

#include "enginerenderdriver.h"
#include "../engine/scenemirror.h"
#include "../editor/editordata.h"
#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/cameranode.h"

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

iris::SceneNodePtr EngineSceneViewport::pickAt(const QPointF &point, bool selectRootObject)
{
    if (!mScene || !mEditorCam) return iris::SceneNodePtr();
    QVector3D a, b;
    ScenePicker::screenSegment(mEditorCam, width(), height(), point, a, b);
    const auto hits = ScenePicker::pickAll(mScene, a, b, mEditorCam->getGlobalPosition());
    const ScenePick best = ScenePicker::nearest(hits);
    return ScenePicker::resolveRootSelection(best.node, mSelectedNode, selectRootObject);
}

void EngineSceneViewport::mousePressEvent(QMouseEvent *e)
{
    EngineViewWidget::mousePressEvent(e);
    setFocus();
    mMousePos = mPrevMousePos = e->position(); mHaveMouse = true;
    if (e->button() == Qt::LeftButton) {
        QVector3D rayPos, rayDir, viewDir;
        const bool haveRay = mouseRay(rayPos, rayDir, viewDir);
        // A hit on the active gizmo starts a drag and keeps the selection.
        if (haveRay && mSelectedNode && mGizmo && mGizmo->isHit(rayPos, rayDir)) {
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
    EngineViewWidget::mouseMoveEvent(e);
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
    EngineViewWidget::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && mGizmo && mGizmo->isDragging()) mGizmo->endDragging();
    if (mCamController) mCamController->onMouseUp(e->button());
}

void EngineSceneViewport::wheelEvent(QWheelEvent *e)
{
    if (mCamController) mCamController->onMouseWheel(e->angleDelta().y());
}

void EngineSceneViewport::keyPressEvent(QKeyEvent *e)
{
    if (mCamController) mCamController->onKeyPressed(static_cast<Qt::Key>(e->key()));
}

void EngineSceneViewport::keyReleaseEvent(QKeyEvent *e)
{
    if (mCamController) mCamController->keyReleaseEvent(e);
}

void EngineSceneViewport::setScene(iris::ScenePtr scene)
{
    mScene = scene;
    mSelectedNode.clear();
    if (mMirror) mMirror->setSource(scene);
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

void EngineSceneViewport::focusOnNode(iris::SceneNodePtr sceneNode)
{
    if (!sceneNode || !mEditorCam) return;
    sceneNode->update(0.0f);
    const QVector3D target = sceneNode->getGlobalPosition();
    const QVector3D dir = (mEditorCam->getGlobalPosition() - target).normalized();
    mEditorCam->setLocalPos(target + dir * 5.0f);
    mEditorCam->lookAt(target);
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
        mShowDebugDraw = data->showDebugDrawFlags;
    }
}

EditorData *EngineSceneViewport::getEditorData()
{
    if (!mEditorData) mEditorData = new EditorData();
    mEditorData->editorCamera = mEditorCam;
    mEditorData->showLightWires = mShowLightWires;
    mEditorData->showDebugDrawFlags = mShowDebugDraw;
    return mEditorData;
}

void EngineSceneViewport::syncFrame()
{
    if (!mActive || !view()) return;
    if (!ensureEngineScene()) return;
    const float dt = float(mFrameTimer.restart()) / 1000.0f;
    if (mCamController) mCamController->update(dt);
    if (mEditorCam) { mEditorCam->setAspectRatio(height() ? float(width()) / float(height()) : 1.0f); }
    if (mMirror) {
        mMirror->setLightWires(mShowLightWires);
        mMirror->setHighlightedNode(mSelectedNode);
        mMirror->sync();
    }
    if (mGizmo && mEditorCam && mSelectedNode) mGizmo->updateSize(mEditorCam);
    if (mOverlay) {
        QVector3D rayPos, rayDir, viewDir;
        mouseRay(rayPos, rayDir, viewDir);
        mOverlay->update(mSelectedNode ? mGizmo : nullptr, rayPos, rayDir, viewDir);
    }
    if (mMirror) mMirror->applySky(view());
    if (mMirror && mEditorCam) mMirror->applyCamera(mEditorCam, view());
}

QImage EngineSceneViewport::takeScreenshot(QSize dimension)
{
    return takeScreenshot(dimension.width(), dimension.height());
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
    if (mMirror) { mMirror->sync(); if (mEditorCam) mMirror->applyCamera(mEditorCam, shot); }
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
