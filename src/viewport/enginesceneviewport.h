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
#include "viewport/engineviewwidget.h"
#include "viewport/ieditorviewport.h"
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
class PlayBack;
class EditorCameraController;
class OrbitalCameraController;
#include <QElapsedTimer>
#include <QPointF>
#include <QHash>
#include <QVector3D>
#include <QQuaternion>

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
    void setHierarchyDragSource(QWidget *source) override { mHierarchyDragSource = source; }
    void setServices(StudioServices *services) override;
    void setDatabase(Database *db) override { mDatabase = db; }
    void setProject(Project *project) override { mProject = project; }

    void setScene(iris::ScenePtr scene) override;
    iris::ScenePtr getScene() override { return mScene; }
    void setSelectedNode(iris::SceneNodePtr sceneNode) override;
    void clearSelectedNode() override;
    void focusOnNode(iris::SceneNodePtr sceneNode) override;
    void focusOnSelection() override;
    /// The point Alt+LMB orbits around: the selection's world bounding-box
    /// centre (its origin when it has no meshes), else the last focus point,
    /// else the world origin.
    QVector3D orbitPivot() const;
    bool snapSelectionToFloor() override;

    iris::CameraNodePtr editorCamera() override { return mEditorCam; }
    void setEditorCamera(iris::CameraNodePtr camera) override;
    void resetEditorCam() override;
    void setFreeCameraMode() override;
    void setArcBallCameraMode() override;
    QString cameraMode() const override;
    bool setCameraView(const QString &view) override;
    QString cameraView() const override { return mCameraView; }
    void setEditorData(EditorData *data) override;
    EditorData *getEditorData() override;

    void setWindowSpace(WindowSpaces) override {}
    void setSceneMode(SceneMode) override {}
    void enterEditorMode() override {}
    void enterPlayerMode() override {}

    void setGizmoLoc() override;
    void setGizmoRot() override;
    void setGizmoScale() override;
    void setGizmoTransformToLocal() override;
    void setGizmoTransformToGlobal() override;
    Gizmo *activeGizmo() const override { return mGizmo; }
    QString gizmoMode() const override;

    void startPlayingScene() override;            // play in place: PlayBack drives the document
    void pausePlayingScene() override;
    void stopPlayingScene() override;
    bool isPlaying() const { return mPlaying; }
    void startPhysicsSimulation() override;    // simulate in place: steps the document's
    void restartPhysicsSimulation() override;  // physics world without entering play mode
    void stopPhysicsSimulation() override;

    bool getShowLightWires() const override { return mShowLightWires; }
    void setShowLightWires(bool value) override { mShowLightWires = value; }
    bool getShowGrid() const override { return mShowGrid; }
    void setShowGrid(bool value) override { mShowGrid = value; }
    // G / editor.gameView: hide every in-viewport editor helper (grid, light
    // wires, outline, gizmo) — docks and toolbars untouched. Not persisted.
    void setGameView(bool enabled) override { mGameView = enabled; }
    bool isGameView() const override { return mGameView; }
    bool getSelectionWireframe() const override { return mSelectionWireframe; }
    void setSelectionWireframe(bool value) override { mSelectionWireframe = value; }
    bool getShowDebugDrawFlags() const override { return mShowDebugDraw; }
    void setShowDebugDrawFlags(bool value) override { mShowDebugDraw = value; }
    void setShowFps(bool) override {}
    void setShowPerspeciveLabel(bool) override {}
    QImage takeScreenshot(int width = 1920, int height = 1080) override;
    QImage takeScreenshot(QSize dimension) override;
    int sampleCount() const override
    { return view() ? int(view()->sampleCount()) : 1; }
    void renderFrames(int n) override;

    void begin() override;
    void end() override;
    bool isInitialized() override { return view() != nullptr; }
    void cleanup() override;


    /// Pushes document -> engine and the editor camera -> view. Called before every frame.
    void syncFrame();

    /// Picks the document object under a viewport pixel (legacy selection rule).
    /// `hitPoint` receives the world-space hit when a node is returned.
    iris::SceneNodePtr pickAt(const QPointF &point, bool selectRootObject = true,
                              QVector3D *hitPoint = nullptr, bool forcePickable = false);
    /// Where a dragged asset would land: the picked surface, else the ground plane.
    QVector3D dropPositionAt(const QPointF &point);

protected:
    void showEvent(QShowEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool event(QEvent *) override;

private:
    bool ensureEngineScene();
    void setActiveGizmo(Gizmo *g);

    /// Per-view camera memory (Views dropdown / editor.setView): each canonical
    /// view keeps its own camera between visits for the life of the viewport —
    /// perspective its full free/orbit pose, each ortho view its pan + zoom.
    /// Session-only by design (matches standard editors; serializing it into
    /// EditorData is a possible future option). Cleared on scene switch.
    struct ViewCameraState {
        QVector3D pos;
        QQuaternion rot;
        float orthoSize = 10.0f;      // ortho zoom (CameraNode::orthoSize)
        float distFromPivot = 15.0f;  // orbital controller's orbit distance
    };
    /// Snapshot the current camera under the CURRENT view's key (mCameraView).
    void saveViewState();
    /// Restore `view`'s saved camera, resyncing the active controller.
    /// False when the view has never been visited (caller applies the default).
    bool restoreViewState(const QString &view);
    void clearViewStates();
    /// V-hold vertex snapping during a translate drag (EDITOR_SHORTCUTS_SPEC §4).
    bool snapDragToVertexUnderCursor();
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
    PlayBack                *mPlayback = nullptr;
    bool                     mPlaying = false;
    QPointF mMousePos, mPrevMousePos;
    bool mHaveMouse = false;
    // drag-and-drop state (material hover preview), as in SceneViewWidget
    iris::SceneNodePtr mDragPreviewNode;
    iris::MaterialPtr mDragOriginalMaterial;
    bool mDragWasHit = false;
    QVector3D mDragScenePos;
    QElapsedTimer mFrameTimer;

    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    EngineRenderDriver *mDriver = nullptr;
    jahshaka::engine::Scene *mEngineScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;
    EditorViewportEvents mEvents;
    MainWindow *mMainWindow = nullptr;
    StudioServices *mServices = nullptr;      // undo + scene-edit for Alt+drag / snap-to-floor
    bool mAltDragMacroOpen = false;           // duplicate+move rides one undo macro
    bool mVertexSnapHeld = false;             // V held: translate drags snap to vertices
    QWidget *mHierarchyDragSource = nullptr;   // drags from the hierarchy tree are reparents, not spawns
    Database *mDatabase = nullptr;
    Project *mProject = nullptr;   // the live Project (Phase 4: was Globals::project)
    iris::ScenePtr mScene;
    iris::SceneNodePtr mSelectedNode;
    /// Alt+LMB orbit pivot when nothing is selected: the last focus point
    /// (F on a node), else the world origin.
    QVector3D mLastOrbitPivot;
    iris::CameraNodePtr mEditorCam;
    EditorData *mEditorData = nullptr;
    bool mShowLightWires = true;
    bool mShowGrid = true;              // per-scene (EditorData), default ON
    QString mCameraView = QStringLiteral("perspective"); // last canonical view requested
    QHash<QString, ViewCameraState> mViewStates; // per-view camera memory (session-only)
    bool mGameView = false;             // G: helpers hidden; never persisted
    bool mSelectionWireframe = false;   // false = silhouette outline (default)
    bool mShowDebugDraw = false;
    bool mActive = false;
    unsigned mViewSerial = 0;
};

#endif // ENGINESCENEVIEWPORT_H
