#include "enginesceneviewport.h"

#include <QShowEvent>
#include <QMouseEvent>
#include "../editor/scenepicker.h"
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
    resetEditorCam();
    if (mDriver)
        connect(mDriver, &EngineRenderDriver::beforeFrame, this, &EngineSceneViewport::syncFrame);
}

EngineSceneViewport::~EngineSceneViewport()
{
    cleanup();
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
    if (e->button() != Qt::LeftButton) return;
    iris::SceneNodePtr picked = pickAt(e->position(), true);
    if (picked) setSelectedNode(picked); else clearSelectedNode();
}

void EngineSceneViewport::setScene(iris::ScenePtr scene)
{
    mScene = scene;
    mSelectedNode.clear();
    if (mMirror) mMirror->setSource(scene);
}

void EngineSceneViewport::setSelectedNode(iris::SceneNodePtr sceneNode)
{
    mSelectedNode = sceneNode;
    emit mEvents.sceneNodeSelected(sceneNode);
}

void EngineSceneViewport::clearSelectedNode()
{
    mSelectedNode.clear();
    emit mEvents.sceneNodeSelected(iris::SceneNodePtr());
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
}

void EngineSceneViewport::resetEditorCam()
{
    mEditorCam = iris::CameraNode::create();
    mEditorCam->setLocalPos(QVector3D(0, 5, 14));
    mEditorCam->lookAt(QVector3D(0, 0, 0));
    mEditorCam->angle = 45.0f;
    mEditorCam->nearClip = 0.1f;
    mEditorCam->farClip = 1000.0f;
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
    if (mMirror) mMirror->sync();
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
    if (mMirror) { mMirror->setSource(nullptr); mMirror.reset(); }
    if (mEngineScene && mEngine) { mEngine->destroyScene(mEngineScene); mEngineScene = nullptr; }
    destroyView();
}

IEditorViewport *createEngineSceneViewport(const std::shared_ptr<Engine> &engine,
                                           EngineRenderDriver *driver, QWidget *parent)
{
    return new EngineSceneViewport(engine, driver, parent);
}
