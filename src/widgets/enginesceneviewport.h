#ifndef ENGINESCENEVIEWPORT_H
#define ENGINESCENEVIEWPORT_H

// The engine-backed editor viewport: IEditorViewport implemented on jahshaka::engine.
//
// Holds the document (iris::ScenePtr) exactly like SceneViewWidget does, mirrors it
// into an engine Scene through SceneMirror every frame, and drives the view's
// camera from the document's editor camera. Never includes Ogre or GL.
//
// Step 6 skeleton: rendering, document sync, camera, selection state and screenshots
// work; gizmo drawing, picking and physics hooks arrive in later plan steps and are
// explicit no-ops here (documented per method).
#include <memory>
#include "engineviewwidget.h"
#include "../editor/ieditorviewport.h"
#include "jahshaka/engine/Engine.h"

class SceneMirror;
class EngineRenderDriver;
class EditorData;
class Gizmo;
class TranslationGizmo;
class RotationGizmo;
class ScaleGizmo;
class GizmoOverlay;
class CameraControllerBase;
class EditorCameraController;
class OrbitalCameraController;
#include <QElapsedTimer>
#include <QPointF>

class EngineSceneViewport : public EngineViewWidget, public IEditorViewport
{
    Q_OBJECT
public:
    EngineSceneViewport(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                        EngineRenderDriver *driver, QWidget *parent = nullptr);
    ~EngineSceneViewport() override;

    QWidget *asWidget() override { return this; }
    EditorViewportEvents *events() override { return &mEvents; }
    void setMainWindow(MainWindow *window) override { mMainWindow = window; }
    void setDatabase(Database *db) override { mDatabase = db; }

    void setScene(iris::ScenePtr scene) override;
    iris::ScenePtr getScene() override { return mScene; }
    void setSelectedNode(iris::SceneNodePtr sceneNode) override;
    void clearSelectedNode() override;
    void focusOnNode(iris::SceneNodePtr sceneNode) override;

    iris::CameraNodePtr editorCamera() override { return mEditorCam; }
    void setEditorCamera(iris::CameraNodePtr camera) override;
    void resetEditorCam() override;
    void setFreeCameraMode() override;
    void setArcBallCameraMode() override;
    void setEditorData(EditorData *data) override;
    EditorData *getEditorData() override;

    void setViewportMode(ViewportMode mode) override { mViewportMode = mode; }
    ViewportMode getViewportMode() override { return mViewportMode; }
    bool isVrSupported() override { return false; }
    void setWindowSpace(WindowSpaces) override {}
    void setSceneMode(SceneMode) override {}
    void enterEditorMode() override {}
    void enterPlayerMode() override {}

    void setGizmoLoc() override;
    void setGizmoRot() override;
    void setGizmoScale() override;
    void setGizmoTransformToLocal() override;
    void setGizmoTransformToGlobal() override;
    Gizmo *activeGizmo() const { return mGizmo; }

    void startPlayingScene() override {}          // TODO(step 11): player on the engine
    void pausePlayingScene() override {}
    void stopPlayingScene() override {}
    void startPhysicsSimulation() override {}
    void restartPhysicsSimulation() override {}
    void stopPhysicsSimulation() override {}
    void addBodyToWorld(btRigidBody *, const iris::SceneNodePtr &) override {}
    void removeBodyFromWorld(btRigidBody *) override {}
    void removeBodyFromWorld(const QString &) override {}

    bool getShowLightWires() const override { return mShowLightWires; }
    void setShowLightWires(bool value) override { mShowLightWires = value; }
    bool getShowDebugDrawFlags() const override { return mShowDebugDraw; }
    void setShowDebugDrawFlags(bool value) override { mShowDebugDraw = value; }
    void setShowFps(bool) override {}
    void setShowPerspeciveLabel(bool) override {}
    QImage takeScreenshot(int width = 1920, int height = 1080) override;
    QImage takeScreenshot(QSize dimension) override;

    void begin() override;
    void end() override;
    bool isInitialized() override { return view() != nullptr; }
    void cleanup() override;

    void beginResourceLoad() override {}          // the document is GL-free
    void endResourceLoad() override {}
    iris::ForwardRendererPtr getRenderer() const override { return iris::ForwardRendererPtr(); }

    /// Pushes document -> engine and the editor camera -> view. Called before every frame.
    void syncFrame();

    /// Picks the document object under a viewport pixel (legacy selection rule).
    iris::SceneNodePtr pickAt(const QPointF &point, bool selectRootObject = true);

protected:
    void showEvent(QShowEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;

private:
    bool ensureEngineScene();
    void setActiveGizmo(Gizmo *g);
    void setCameraController(CameraControllerBase *c);
    /// Mouse ray for the current pointer position (false if the pointer never entered).
    bool mouseRay(QVector3D &rayPos, QVector3D &rayDir, QVector3D &viewDir) const;

    TranslationGizmo *mTranslateGizmo = nullptr;
    RotationGizmo    *mRotateGizmo = nullptr;
    ScaleGizmo       *mScaleGizmo = nullptr;
    Gizmo            *mGizmo = nullptr;
    std::unique_ptr<GizmoOverlay> mOverlay;
    EditorCameraController  *mFreeCam = nullptr;
    OrbitalCameraController *mOrbitCam = nullptr;
    CameraControllerBase    *mCamController = nullptr;
    QPointF mMousePos, mPrevMousePos;
    bool mHaveMouse = false;
    QElapsedTimer mFrameTimer;

    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    EngineRenderDriver *mDriver = nullptr;
    jahshaka::engine::Scene *mEngineScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;
    EditorViewportEvents mEvents;
    MainWindow *mMainWindow = nullptr;
    Database *mDatabase = nullptr;
    iris::ScenePtr mScene;
    iris::SceneNodePtr mSelectedNode;
    iris::CameraNodePtr mEditorCam;
    EditorData *mEditorData = nullptr;
    ViewportMode mViewportMode = ViewportMode::Editor;
    bool mShowLightWires = true;
    bool mShowDebugDraw = false;
    bool mActive = false;
    unsigned mViewSerial = 0;
};

#endif // ENGINESCENEVIEWPORT_H
