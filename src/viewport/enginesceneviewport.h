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
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
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
#include <QPointer>
#include <QPointF>
#include <QHash>

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
    iris::Vec3 orbitPivot() const;
    bool snapSelectionToFloor() override;

    iris::CameraNodePtr editorCamera() override { return mEditorCam; }
    void setEditorCamera(iris::CameraNodePtr camera) override;
    void resetEditorCam() override;
    void setFreeCameraMode() override;
    void setArcBallCameraMode() override;
    QString cameraMode() const override;
    bool setCameraView(const QString &view) override;
    QString cameraView() const override { return mCameraView; }
    bool setCameraPose(const EditorCameraPose &pose) override;
    bool frameNode(iris::SceneNodePtr sceneNode, const EditorFraming &framing) override;
    void setEditorData(EditorData *data) override;
    EditorData *getEditorData() override;

    // ---- Pilot mode + the selection PiP (CAMERAS_SPEC D3/D8) --------------
    bool pilotCamera(iris::CameraNodePtr camera) override;
    iris::CameraNodePtr pilotedCamera() const override { return mPilot; }
    bool pipEnabled() const override { return mPipEnabled; }
    void setPipEnabled(bool on) override;
    double pipSize() const override { return mPipSize; }
    void setPipSize(double fraction) override;

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
    /// F3 / editor.setOverlays({stats}) / the Preferences checkbox — all three
    /// arrive here. Was an EMPTY override for the whole life of the engine
    /// viewport, which is why editor.overlays() used to refuse a `stats` key by
    /// name: there was nothing behind it (STATS_OVERLAY_SPEC §1.1).
    ///
    /// Deliberately NOT hidden by Game View (G) or fullscreen (F11): those hide
    /// editor HELPERS, and a frame-time readout is a DIAGNOSTIC — "what is my
    /// frame time in the game view" is the question people actually ask.
    void setShowFps(bool value) override;
    bool getShowFps() const override { return mShowStats; }
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

    // ---- the loading / no-scene cover, drawn BY THE ENGINE ----
    // Owner decision D2 (STATS_OVERLAY_SPEC.md §6): the cover used to be a Qt
    // widget stacked over this one in its own native X window (the deleted
    // ViewportCover). It is now an overlay panel the engine draws into the same
    // frame it was going to present anyway — no second window, no stacking
    // order, no input region, no Qt clock.
    QString presentationState() const override;
    qulonglong framesPresented() const override;
    /// Bridges EngineViewWidget's own (non-virtual, and on the OTHER base) copy
    /// onto the interface — C++ does not override across hierarchies, and the
    /// shell holds an IEditorViewport*.
    QString viewCreationError() const override
    { return EngineViewWidget::viewCreationError(); }
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
    /// Recomputes the engine overlay — cover state AND stats readout — and
    /// pushes it at the View. Called once a frame (before the engine's frame,
    /// so it sees the presents already made) and at every event that can change
    /// the answer. Cheap: an unchanged desc is a no-op inside the engine, and
    /// no overlay change ever rebuilds a workspace.
    void refreshOverlay();
    /// The desc refreshOverlay pushes. Split out so the covered-present helper
    /// can force the Loading state before the state machine would report it.
    jahshaka::engine::ViewOverlayDesc overlayDesc() const;
    /// Presents `frames` frames THROUGH THIS VIEW, right now, synchronously,
    /// with the overlay already refreshed — the replacement for the Qt cover's
    /// repaint() (STATS_OVERLAY_SPEC §6.3).
    ///
    /// The Qt cover could paint synchronously because it was a Qt widget. An
    /// engine-drawn cover cannot: the caller is about to block this thread with
    /// a scene load, and the 16 ms driver tick that would draw the cover is
    /// queued behind it. So the cover's frames are drawn HERE, inline, exactly
    /// where repaint() used to be — and TWO of them, because a Vulkan present
    /// is queued and the first one is not yet on screen (the same reason
    /// kPresentsBeforeReveal is 2).
    ///
    /// Does nothing when there is no on-screen View to present into.
    void presentCovered(int frames = int(kPresentsBeforeReveal));
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
                              iris::Vec3 *hitPoint = nullptr, bool forcePickable = false);
    /// Where a dragged asset would land: the picked surface, else the ground plane.
    iris::Vec3 dropPositionAt(const QPointF &point);

protected:
    void showEvent(QShowEvent *) override;
    /// A RESIZE THROWS THE COVER AWAY, and that is why this override exists.
    /// The cover lives in a presented frame, and an on-screen resize rebuilds
    /// the swapchain — the frame that carried it is gone and the region shows
    /// whatever Qt last put there. A Qt-painted cover never had this problem
    /// (it just repainted); this is the engine-drawn equivalent, and without it
    /// the very first layout after the view is created (160x120 -> the real
    /// size) leaves the editor page on its own watermark for as long as the UI
    /// thread is busy. Only pays anything while a cover is actually up.
    void resizeEvent(QResizeEvent *) override;
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
        iris::Vec3 pos;
        iris::Quat rot;
        float orthoSize = 10.0f;      // ortho zoom (CameraNode::orthoSize)
        float distFromPivot = 15.0f;  // orbital controller's orbit distance
    };
    /// Pushes the selection preview inset for this frame (CAMERAS_SPEC D3).
    /// Called once per syncFrame, right after the camera push.
    void syncPip();
    /// Snapshot the current camera under the CURRENT view's key (mCameraView).
    void saveViewState();
    /// Restore `view`'s saved camera, resyncing the active controller.
    /// False when the view has never been visited (caller applies the default).
    bool restoreViewState(const QString &view);
    void clearViewStates();
    /// The three lines every camera mover in here ends with: hand the moved
    /// camera back to the ACTIVE controller so its yaw/pitch (free) or pivot
    /// (arcball) are re-derived. Without it the first mouse move snaps the
    /// camera back to where the controller still thinks it is. `orbitDistance`
    /// is what the arcball should adopt (<= 0 = keep the current one).
    void resyncCameraController(float orbitDistance = -1.0f);
    /// V-hold vertex snapping during a translate drag (EDITOR_SHORTCUTS_SPEC §4).
    bool snapDragToVertexUnderCursor();
    void setCameraController(CameraControllerBase *c);
    /// Mouse ray for the current pointer position (false if the pointer never entered).
    bool mouseRay(iris::Vec3 &rayPos, iris::Vec3 &rayDir, iris::Vec3 &viewDir) const;

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
    iris::Vec3 mDragScenePos;
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
    /// F3 / editor.setOverlays({stats}) / Preferences show_fps.
    bool mShowStats = false;
    /// The readout's text, rebuilt at most every kStatsRefreshMs. A number that
    /// changes 62 times a second is unreadable, and each rebuild costs a string
    /// format, a boundary crossing and a TextArea re-layout (§5.1 asks for
    /// 4-10 Hz; this is 5).
    mutable QStringList mStatsLines;
    mutable QElapsedTimer mStatsClock;
    static constexpr qint64 kStatsRefreshMs = 200;
    /// Whether the last refreshOverlay left a cover up — the rising edge that
    /// makes a raise synchronous (see refreshOverlay).
    bool mCoverUp = false;
    /// Re-entrancy guard: presentCovered refreshes first, and refreshOverlay
    /// presents on a rising edge.
    bool mPresentingCover = false;
    /// What the last INLINE covered present actually put on screen, and at what
    /// target size. Presenting the same thing again costs a full frame of the
    /// scene and shows the user nothing new.
    jahshaka::engine::ViewOverlayDesc mLastPresentedCover;
    unsigned mLastPresentedW = 0, mLastPresentedH = 0;
    qulonglong mLastPresentedEpoch = 0;
    /// A MONOTONIC count of frames drawn through this viewport by anybody — the
    /// driver's ticks, editor.frame(), and presentCovered itself. Deliberately
    /// not View::framesPresented, which resets on every scene bind.
    qulonglong mFrameEpoch = 0;
    /// A world is on its way but nothing of it has presented yet. Set by
    /// beginSceneLoad and cleared when the view starts presenting: the state
    /// machine alone cannot tell "no world open" from "a world is loading",
    /// because at beginSceneLoad time the OLD world is still bound (or none is).
    bool mSceneLoadPending = false;
    /// The world's name, shown under "Loading world…" — beginSceneLoad's argument.
    QString mLoadingTitle;
    /// View::framesPresented() at the moment the current world was bound
    /// (setScene/clearScene/beginSceneLoad) — the zero of presentsSinceBind.
    qulonglong mPresentBaseline = 0;
    iris::SceneNodePtr mSelectedNode;
    /// Alt+LMB orbit pivot when nothing is selected: the last focus point
    /// (F on a node), else the world origin.
    iris::Vec3 mLastOrbitPivot;
    iris::CameraNodePtr mEditorCam;
    /// THE CAMERA THIS VIEWPORT IS DRIVING (CAMERAS_SPEC D8). The explorer,
    /// unless a scene camera is being piloted — and then everything that used
    /// to say mEditorCam has to say this instead, or the pick rays, the orbit
    /// pivot and F-focus would all be computed from a camera nobody is looking
    /// through. That sweep is what phase 3 is; every remaining mEditorCam here
    /// is deliberate (the explorer's own state: per-view memory, resets, the
    /// EditorData round trip).
    iris::CameraNodePtr viewCamera() const { return mPilot ? mPilot : mEditorCam; }
    iris::CameraNodePtr mPilot;
    /// The piloted camera's transform when piloting STARTED, so ejecting can
    /// push ONE undo command for the whole flight (a per-mouse-event command
    /// would bury the stack).
    iris::Vec3 mPilotStartPos;
    iris::Quat mPilotStartRot;
    /// Preferences (persisted): the selection preview inset and its width as a
    /// fraction of the viewport. Defaults match CAMERAS_SPEC D3 — on, and a bit
    /// under a third of the width, bottom-right.
    bool   mPipEnabled = true;
    double mPipSize = 0.28;
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
