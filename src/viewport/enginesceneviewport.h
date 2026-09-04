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
class ViewportCover;
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
#include <QPointer>
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
    QImage takeScreenshot(int width, int height, bool postFx) override;
    int sampleCount() const override
    { return view() ? int(view()->sampleCount()) : 1; }
    bool isOffscreen() const override
    { return view() ? view()->isOffscreen() : true; }
    /// The size of what is actually being rendered into, in pixels — View::
    /// width()/height() report the live render target (the swapchain for an
    /// on-screen view), never the size this widget last pushed down. That is
    /// what makes a resize assertion mean something: while these returned our
    /// own request the selftest's post-resize check compared the pushed values
    /// with themselves and could not fail (deep audit area 7 F3).
    QSize renderTargetSize() const override
    { return view() ? QSize(int(view()->width()), int(view()->height())) : QSize(); }
    int shadowResolution() const override
    { return mEngine ? int(mEngine->shadowResolution()) : 0; }
    int activePlanarReflectors() const override
    { return (view() && view()->scene()) ? view()->scene()->activePlanarReflectors() : 0; }
    bool planarReflectorAccepted(iris::SceneNodePtr node) const override;
    void renderFrames(int n) override;
    void renderFrames(int n, float dt) override;

    void begin() override;
    void end() override;
    bool isInitialized() override { return view() != nullptr; }
    void cleanup() override;
    void clearScene() override;

    // ---- the loading / no-scene cover (viewportcover.h) ----
    /// The cover this viewport drives. It is a SIBLING widget (same layout cell,
    /// never a child — a child of a setUpdatesEnabled(false) widget can never
    /// repaint), created and owned by whoever builds the editor page. Null in
    /// sessions that have no cover; every cover call is then a no-op.
    void setCover(ViewportCover *cover) override;
    QString presentationState() const override;
    qulonglong framesPresented() const override;
    void beginSceneLoad(const QString &title = QString()) override;
    void coverIfNotPresenting() override;
    void primeSceneGeometry() override;
    void primeSceneEnvironment() override;
    unsigned warmUpShaders() override;
    /// Sentinel for mWarmUpIdleAt meaning "always run the warm-up": no real
    /// compile count can equal it.
    static constexpr unsigned kWarmUpAlwaysRun = ~0u;
    /// The engine's total compile count when a warm-up last found nothing to
    /// do. While the count still reads this, another warm-up would be 250 ms of
    /// UI block for zero shaders — see warmUpShaders() for the measurements.
    unsigned mWarmUpIdleAt = kWarmUpAlwaysRun;
    /// Recomputes the cover's state from the view's present count. Called once
    /// a frame (before the engine's frame, so it sees the presents already
    /// made) and at every event that can change the answer.
    void updateCover();
    /// Frames presented since the CURRENT world was bound to this viewport.
    /// Not simply View::framesPresented(): a project close/open reuses the
    /// engine scene (MainWindow::closeProject leaves it bound), so the engine's
    /// own counter does not restart — the viewport has to remember where the
    /// new world started.
    qulonglong presentsSinceBind() const;
    /// Presented frames a scene needs before the cover comes down. Two, not
    /// one: a Vulkan present is queued, so the frame counted first is not
    /// guaranteed to be the one the compositor is showing.
    static constexpr unsigned long long kPresentsBeforeReveal = 2;


    /// Pushes document -> engine and the editor camera -> view. Called before every frame.
    /// One document->engine sync. `dt` >= 0 overrides the wall clock: that is
    /// what makes editor.frame(n, dt) deterministic in PLAY mode, where the
    /// document's animation clock is advanced by dt.
    void syncFrame(float dtOverride = -1.0f);

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
    /// The View was rebuilt on a new native window (EngineViewWidget::
    /// recreateViewForNewWindow): re-attach the engine scene, which belonged to
    /// the old View, and restart the present accounting the cover reads.
    void viewRecreated() override;

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
    /// Not owned (see setCover): a sibling widget in the same layout cell.
    QPointer<ViewportCover> mCover;
    /// View::framesPresented() at the moment the current world was bound
    /// (setScene/clearScene/beginSceneLoad) — the zero of presentsSinceBind.
    qulonglong mPresentBaseline = 0;
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
