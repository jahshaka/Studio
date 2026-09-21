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
#include "irisgl/core/geometry/aabb.h"
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
class MaterialPreviewService;
class QMimeData;
class PlayBack;
class EditorCameraController;
class OrbitalCameraController;
#include <QElapsedTimer>
#include <QPointer>
#include <QPointF>
#include <QHash>
#include <functional>

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
    /// The ONE engine scene and the ONE mirror (IEditorViewport's note): the
    /// Player page draws through these, as a second view. BOTH ARE PURE READS —
    /// null until something has asked for the scene to be BUILT, which is
    /// ensureEngineScene() and nothing else. They are read per frame.
    jahshaka::engine::Scene *engineScene() override { return mEngineScene; }
    SceneMirror *sceneMirror() override { return mMirror.get(); }
    /// IEditorViewport: build the one scene now if it does not exist, for a
    /// caller that draws it and cannot wait for this widget's show event (the
    /// Player page, the editor's VR preview). See the definition.
    bool ensureEngineScene() override;
    float measuredExposureScale() const override {
        return view() ? view()->measuredExposureScale() : 0.0f;
    }
    void setSelectedNode(iris::SceneNodePtr sceneNode) override;
    void setSelectedSet(const QList<iris::SceneNodePtr> &nodes) override;
    void clearSelectedNode() override;
    void focusOnNode(iris::SceneNodePtr sceneNode) override;
    void focusOnSelection() override;
    /// Where an Alt+drag starting at this pixel orbits around: the point under
    /// the cursor, else the view ray at the current working distance (§353).
    iris::Vec3 altOrbitPivotAt(const QPointF &point);
    /// Hands the gizmo the D5-reduced selection set (empty for a single node).
    void pushGizmoGroup();
    /// The union of the selection set's world bounds (a member with no meshes
    /// contributes its origin). False when the set is empty.
    bool selectionBounds(iris::AABB &out) const;
    /// F's framing over a point + radius; `orbitNode` (nullable) is what the
    /// orbital controller re-pivots on when there is a single subject.
    void focusOnTarget(const iris::Vec3 &target, float radius,
                       const iris::SceneNodePtr &orbitNode);
    bool snapSelectionToFloor() override;
    /// One member's drop onto the surface below it (snapSelectionToFloor runs
    /// it per member inside one macro).
    bool snapNodeToFloor(const iris::SceneNodePtr &node);

    iris::CameraNodePtr editorCamera() override { return mEditorCam; }
    void setEditorCamera(iris::CameraNodePtr camera) override;
    void resetEditorCam() override;
    void setFreeCameraMode() override;
    void setArcBallCameraMode() override;
    QString cameraMode() const override;
    bool setCameraView(const QString &view) override;
    QString cameraView() const override { return mCameraView; }
    bool cameraRotationLocked() const override;
    QString gridPlane() const override { return mGridPlanePushed; }
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
    void onCameraSpeedChanged() override { emit mEvents.cameraSpeedChanged(); }

private:
    /// The wide-aspect FRAMING HOLD aspect for the camera this view is
    /// currently rendering: the policy value (16:9) for the EXPLORER, zero
    /// (off) for a piloted scene camera (viewport/freecamerapolicy.h).
    float freeCameraFramingAspect() const;
    /// Takes ownership of the explorer camera and STAMPS THE FREE-CAMERA POLICY
    /// ON IT (freecamerapolicy.h). Every assignment to `mEditorCam` goes through
    /// here — a fresh camera, a project's remembered one, a scripted one — so
    /// there is exactly one place where "this camera is a free explorer" is
    /// said, and the document's projection (i.e. every pick ray) and the engine's
    /// cannot end up describing two different frusta.
    void adoptEditorCamera(iris::CameraNodePtr camera);
public:

    void setWindowSpace(WindowSpaces) override {}
    void setSceneMode(SceneMode) override {}
    void enterEditorMode() override {}
    void enterPlayerMode() override {}

    void setGizmoLoc() override;
    void setGizmoRot() override;
    void setGizmoScale() override;
    void setGizmoTransformToLocal() override;
    void setGizmoTransformToGlobal() override;
    QString gizmoTransformSpace() const override;
    /// IEditorViewport: screen-space ring picking behind `editor.gizmoHitTest`
    /// (smoke S15) — the same pick the mouse takes, asked at a pixel.
    GizmoPickResult gizmoHitTest(const QPointF &point) const override;
    Gizmo *activeGizmo() const override { return mGizmo; }
    QString gizmoMode() const override;

    void startPlayingScene() override;            // play in place: PlayBack drives the document
    void pausePlayingScene() override;
    void stopPlayingScene() override;
    bool isPlaying() const override { return mPlaying; }
    QRect widgetRectInWindow() const override;
    bool playEjected() const override { return mPlayEjected; }
    /// A RUN EXISTS — not "a run is stepping" (PLAY-SELECT-1 fix round, F5).
    /// `mPlaying` goes FALSE on pause while the run, its physics world and its
    /// pre-play snapshot all live on, so every rule about what a run's edits
    /// are worth — the transient drag, the physics hand-over, the Alt+drag
    /// refusal, the eject verb — keys on this instead. PlayBack's own flag is
    /// the one that says it (isScenePlaying stays true through a pause,
    /// deliberately).
    bool playRunLive() const override;
    void setPlayEjected(bool ejected) override;
    QString playInputOwner() const override;
    void startPhysicsSimulation() override;    // simulate in place: steps the document's
    void restartPhysicsSimulation() override;  // physics world without entering play mode
    void stopPhysicsSimulation() override;

    bool getShowLightWires() const override { return mShowLightWires; }
    void setShowLightWires(bool value) override { mShowLightWires = value; }
    bool getShowGrid() const override { return mShowGrid; }
    void setShowGrid(bool value) override { mShowGrid = value; }
    bool getShowGiVolume() const override { return mShowGiVolume; }
    void setShowGiVolume(bool value) override { mShowGiVolume = value; }
    /// The shadow-atlas inspector (SHADOW_TOOLING_SPEC.md §4.4). Not persisted,
    /// like the GI volume beside it: a diagnostic you turn on to answer one
    /// question and turn off again.
    bool getShowShadowAtlas() const override { return mShowShadowAtlas; }
    void setShowShadowAtlas(bool value) override { mShowShadowAtlas = value; }
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
    QImage takeScreenshot(int width, int height, ScreenshotGrade grade) override;
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
    GiStatusInfo giStatus() const override;
    GiVoxelStatsInfo giVoxelStats(int cascade) override;
    ShadowStatusInfo shadowStatus() const override;
    bool planarReflectorAccepted(iris::SceneNodePtr node) const override;
    void renderFrames(int n) override;
    void renderFrames(int n, float dt) override;
    bool canRenderFrames() const override;
    MirrorStats mirrorStats() const override;
    RigStatsInfo rigStats() const override;
    QString dumpMaterial(const QString &nodeGuid) const override;

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
    /// The active camera controller's held-key set, by name, sorted (§356).
    QStringList heldFlyKeys() const override;
    bool flying() const override;
    /// The editor's VR preview (VR_SPEC §5 phase 4) — see IEditorViewport.
    void setVrPreviewStep(std::function<void()> step) override { mVrPreviewStep = std::move(step); }
    bool vrPreview() const override { return bool(mVrPreviewStep); }
    void setVrPreviewEnds(std::function<void()> ends) override
    { mVrPreviewEnds = std::move(ends); }
    /// Bridges EngineViewWidget's own (non-virtual, and on the OTHER base) copy
    /// onto the interface — C++ does not override across hierarchies, and the
    /// shell holds an IEditorViewport*.
    QString viewCreationError() const override
    { return EngineViewWidget::viewCreationError(); }
    void beginSceneLoad(const QString &title = QString()) override;
    void endSceneLoad() override;
    StreamingPending streamingPending() const override;
    QString coverState() const override;
    QString loadingIndicator() const override { return mStream.line; }
    void coverIfNotPresenting() override;
    void primeSceneGeometry() override;
    void primeSceneEnvironment() override;
    unsigned warmUpShaders() override;
    void recordWarmUpSet() override;
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

    // ---- streaming a world in (SPECS/OPEN_COVER_SPEC.md §2.1) -------------
    /// What the NEXT engine frame drawn for THIS viewport may put off. The rule
    /// lives here because this widget is the only object that knows both of its
    /// terms: whether the world has been revealed, and whether the engine still
    /// owes first-time work.
    ///
    /// THE CONTRACT, and it is the safety argument for the whole lane: THE
    /// DRIVER'S OWN TICKS ARE THE ONLY CALLER. A script's `editor.frame`, a
    /// screenshot, a thumbnail, a preview, the open runner's slice boundaries,
    /// the selftest and every pixel suite render `Complete` frames by not
    /// asking — so no gate and neither selftest hash can move. (The other half
    /// of the rule, "nothing of this world is on screen yet", is NOT a frame's
    /// property and is not here: it is `Scene::setLoading`, raised for the
    /// length of the load.)
    jahshaka::engine::FramePace driverFramePace() const;
    /// How many DRIVER frames after a reveal may still stream. A bound, not a
    /// budget: the engine's own `framePaceOwesWork` is what normally ends the
    /// window, and this stops a scene whose textures never arrive from leaving
    /// the frame-edge drain switched off for the life of the session.
    static constexpr int kStreamFramesAfterReveal = 240;


    /// Pushes document -> engine and the editor camera -> view. Called before every frame.
    /// One document->engine sync. `dt` >= 0 overrides the wall clock: that is
    /// what makes editor.frame(n, dt) deterministic in PLAY mode, where the
    /// document's animation clock is advanced by dt.
    void syncFrame(float dtOverride = -1.0f);

    /// Picks the document object under a viewport pixel (legacy selection rule).
    /// `hitPoint` receives the world-space hit when a node is returned.
    iris::SceneNodePtr pickAt(const QPointF &point, bool selectRootObject = true,
                              iris::Vec3 *hitPoint = nullptr, bool forcePickable = false);
    /// What a drop at this viewport pixel APPLIES TO: the node under the
    /// cursor, locked or not — `locked` (optional) says which, and a locked
    /// node refuses the drop by name rather than swallowing it. Null when the
    /// ray hits nothing.
    iris::SceneNodePtr dropTargetAt(const QPointF &point, bool *locked = nullptr) override;
    /// Toasts "<node> is locked — unlock it to apply <what>" and answers true
    /// when the drop must stop there.
    bool refuseDropOnLocked(const iris::SceneNodePtr &node, const QString &what);
    /// The material payload of a drag (a Material row OR a Materials-module
    /// Shader row), empty when the drag carries something else.
    static QString materialDragSource(const QMimeData *mime);
    /// The hover-preview service, or null in a host that has none.
    MaterialPreviewService *materialPreview() const;
    /// Where a dragged asset would land: the picked surface, else the ground plane.
    iris::Vec3 dropPositionAt(const QPointF &point);
    /// IEditorViewport: the same answer, for `editor.dropPointAt` and anything
    /// else that has to place where a user's drop would place.
    bool dropPointAt(const QPointF &point, iris::Vec3 *out) override;

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
    /// Driver frames left in the streaming window (kStreamFramesAfterReveal).
    int mStreamFramesLeft = 0;
    /// THE COVER PREFERENCE, LATCHED FOR THE LENGTH OF ONE LOAD
    /// (services/loadingcover.h, OPEN_COVER_SPEC §3). Read ONCE, in
    /// `beginSceneLoad`: a preference toggled mid-load must not make the panel
    /// appear and disappear between two frames of the same load.
    bool mCoverThisLoad = false;
    /// The indicator's readings, sampled once per driver tick (see
    /// `sampleStreamingWork`). Mutable state, not a computation: two of the
    /// three numbers are DIFFERENCES across frames and there is exactly one
    /// place per frame where a difference is meaningful.
    struct StreamSample {
        unsigned shadersLastFrame = 0;   ///< built by the frame just drawn
        unsigned shadersAtSample = 0;    ///< the engine's total at the last sample
        unsigned shadersAtLoad = 0;      ///< ...and when this load began
        unsigned texturesPeak = 0;       ///< the most materials waited on at once
        /// THE LINE ON SCREEN, and the frames it is held for. The counts
        /// FLICKER — the shader term is a per-frame rate and the arm's stage
        /// machine is idle between a stage finishing and the next being
        /// staged — so a line drawn from the raw reading blinks off and on
        /// again while a world is plainly still arriving (photographed: two
        /// frames of "Loading New World - shaders 4", then one blank, then
        /// "...lighting..."). It is therefore held for a short tail after the
        /// last frame that owed anything, and the tail is what makes it
        /// disappear ONCE, cleanly, when the world has settled.
        QString  line;
        int      holdTicks = 0;
    };
    /// How many frames the indicator survives its last owed work — ~0.2 s at
    /// 60 Hz. A bound on the flicker, not a timer for the line: the line goes
    /// when this runs out, which is a few frames after the world stops
    /// changing.
    static constexpr int kIndicatorHoldTicks = 12;
    StreamSample mStream;
    /// Reads `Engine::streamingWork()` and banks this frame's differences.
    /// Called before EVERY frame this viewport draws, so the line describes
    /// what the previous frame left owed. `driverFrame` is the safety rule:
    /// see the definition.
    void sampleStreamingWork(bool driverFrame);
    /// Composes the line from the CURRENT reading — the counts are the
    /// engine's and the world's name is the host's. Called once per drawn
    /// frame by `sampleStreamingWork`, never by the overlay (which reads the
    /// held line, so what is on screen and what `editor.viewportState()
    /// .indicator` reports are the same string).
    QString composeIndicatorLine(bool driverFrame) const;
    /// A world is on its way and none of it is on screen yet — the engine's
    /// half of `mSceneLoadPending`, and it needs to be its own flag because
    /// `clearScene` wipes that one while an open is in flight (see
    /// refreshOverlay). Drives `Scene::setLoading`.
    bool mWorldArriving = false;
    /// Binds this widget's View to the scene, with the view-side state that
    /// belongs to the editor's own picture (shadows, the VR helper channel).
    void bindViewToScene();
    void setActiveGizmo(Gizmo *g);

    /// The ONE grid push (visibility, plane, spacing, floor offset, colours)
    /// derived from the current canonical view — every mirror sync goes
    /// through it so the axis views cannot disagree about the grid.
    /// `helpers` is the caller's "editor helpers are drawable now" state.
    void pushGridForView(bool helpers);
    /// Every in-viewport editor helper (light/camera wires, selection outline,
    /// grid, GI boxes), pushed from the ONE place that knows what they should
    /// be. `helpers` false is Game View's answer — and, for the duration of one
    /// picture, a user's screenshot's (SS1). Reversible by construction: both
    /// callers push from the same owned fields.
    void pushEditorHelpers(bool helpers);

    /// Pushes cameraRotationLocked() onto BOTH camera controllers — the active
    /// one and the idle one, so switching camera mode inside an axis view
    /// cannot hand the user a controller that never heard about the lock.
    /// Called from every place that can change the answer: the view switch,
    /// the controller switch, pilot enter/leave and the scene reset.
    void applyRotationLock();

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
    /// WHERE THE PICTURE IS, in widget pixels: the whole widget, except while
    /// piloting a camera that CONSTRAINS its aspect — the engine letterboxes
    /// that one (chain::letterboxRect), so the image is the inner rectangle at
    /// the camera's authored aspect and the bars are not part of it. Every pick
    /// ray and the gizmo's pixel frame are built through this rectangle, so
    /// they unproject at the aspect that is actually on screen and never have
    /// to overwrite the camera's authored aspect to get one (lane L11).
    QRectF pictureRect() const;
    /// ScenePicker::screenSegment through pictureRect(): `point` in widget
    /// pixels, the segment through the picture under it.
    void pictureSegment(const iris::CameraNodePtr &cam, const QPointF &point,
                        iris::Vec3 &segStart, iris::Vec3 &segEnd) const;

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
    /// EJECTED (PLAY-SELECT-1, Unreal's F8): the run keeps simulating and the
    /// EDITOR has the input — every mouse gesture and every key comes back to
    /// this widget, and the possession arm stands down from the view camera so
    /// the editor's own fly moves the picture again. Cleared on every play
    /// start and every stop: it is a state of THIS run, not a preference.
    bool                     mPlayEjected = false;
    QPointF mMousePos, mPrevMousePos;
    bool mHaveMouse = false;
    // Drag-and-drop state. The MATERIAL HOVER PREVIEW is no longer here: it is
    // a service behind verbs (services/materialpreviewservice.h,
    // MATERIAL-PREVIEW-1), because it is an editor capability — the three
    // members that used to hold it inline are the reason a drop could leave a
    // borrowed material on a mesh and a scene close could strand one.
    iris::Vec3 mDragScenePos;
    /// The payload of the drag in progress, so dragMove does not re-decode and
    /// re-resolve the mime on every mouse move.
    QString mDragMaterialSource;
    QElapsedTimer mFrameTimer;

    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    /// A QPointer, NOT a raw one, and the reason is a use-after-free this lane
    /// tripped over (STATS-1, 2026-09-19): EngineHost::shutdown deletes the
    /// render driver at quit step 4 of 8, and the widget tree — this widget
    /// included — is destroyed at step 5, with ~EngineSceneViewport reaching
    /// through cleanup() -> clearScene() -> refreshOverlay() -> presentCovered()
    /// to `mDriver->noteExternalFrame()`. The call has always landed on freed
    /// memory; it went unnoticed only because the method used to be one
    /// QElapsedTimer::restart() into the corpse, which writes and returns. The
    /// moment it touched a container it aborted the process with `double free
    /// or corruption` at shutdown, in eleven suites at once. QPointer makes the
    /// `if (mDriver)` guards those call sites already carry TRUE.
    QPointer<EngineRenderDriver> mDriver;
    jahshaka::engine::Scene *mEngineScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;
    EditorViewportEvents mEvents;
    MainWindow *mMainWindow = nullptr;
    StudioServices *mServices = nullptr;      // undo + scene-edit for Alt+drag / snap-to-floor
    bool mAltDragMacroOpen = false;           // duplicate+move rides one undo macro
    /// IS THE GIZMO DRAG THIS VIEWPORT'S? (VR phase 4b stage 2.)
    ///
    /// There is ONE gizmo object per mode in the process and two hosts can
    /// reach it: this widget's mouse, and the wearer's controller through
    /// VrInteraction. Before this flag both handlers keyed on `Gizmo::
    /// isDragging()` alone, so a mouse MOVE over the viewport during a VR
    /// handle drag drove that drag with the desk's pixel ray (and overwrote its
    /// modifiers), and any stray click ENDED it — a second createUndoAction for
    /// one gesture. The rule is ownership: a drag belongs to whoever started
    /// it, this host drives and ends only its own, and the press refuses to
    /// take one somebody else is holding (VrInteraction::beginGizmoDrag refuses
    /// the mirror case).
    bool mMouseDrag = false;
    /// THE BUTTONS WHOSE PRESS THIS WIDGET TOOK (PLAY-SELECT-1 fix round, F2).
    /// A release belongs to whoever got the press, not to whatever the
    /// ownership predicate answers at release time: eject, un-eject and
    /// possession can all change that answer mid-gesture, and routing the
    /// release by the new answer strands the gizmo drag (never ended) or the
    /// run's own look (never released).
    Qt::MouseButtons mEditorButtons = Qt::NoButton;
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
    /// The whole selection SET, primary FIRST (EDITOR_MULTISELECT_SPEC §2.3).
    /// mSelectedNode stays the primary — the gizmo pivots on it, the pick
    /// drill-down rule anchors on it, and every single-node path keeps working
    /// — while the outline, the gizmo's group transform and the focus/orbit/
    /// floor unions read this.
    QList<iris::SceneNodePtr> mSelectedSet;
    /// THE LAST POINT F FRAMED (world origin until something is framed). Since
    /// §353 an Alt+drag orbits the point UNDER THE CURSOR, so this is no longer
    /// a pivot — it is the fallback DISTANCE: with nothing under the cursor the
    /// pivot is taken on the view ray at the camera's distance to this point,
    /// i.e. at the scale the user is already working in.
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
    /// THE CAMERA THE PICTURE IS TAKEN THROUGH — viewCamera() everywhere except
    /// inside a play run, where the document's one camera rule
    /// (iris::Scene::renderCamera) may hand the shot to the armed active camera
    /// or to a possession's spring arm. Every pick ray, every gizmo hit test
    /// and every gizmo size is built from THIS (PLAY-SELECT-1): a click during
    /// play that unprojected the explorer's frustum while the user was looking
    /// through the scene's camera would select whatever happened to be behind
    /// the cursor in a picture nobody could see. Outside play it is
    /// viewCamera() to the pointer — renderCamera's first term.
    iris::CameraNodePtr pickCamera() const;
    /// Whether a pick through `cam` must give the camera's authored aspect back
    /// when it is done with it (F1): true for every camera this viewport is
    /// only LOOKING THROUGH — an armed active camera, a possession's — and
    /// false for the explorer, whose aspect the frame tick owns.
    bool borrowsPickAspect(const iris::CameraNodePtr &cam) const;
    /// WHO OWNS THE POINTER while a run is in flight (PLAY-SELECT-1). True only
    /// while the run really consumes input — a POSSESSED avatar — and never
    /// while ejected. False is the ordinary case (explorer/camera play), and it
    /// is what gives a plain left click back to the editor's pick and gizmo.
    bool runOwnsPointer() const;
    /// The document's possession slot has somebody (the one state where the
    /// play controller is a real consumer of mouse and keys).
    bool playPossessing() const;
    /// Ends whatever gesture this widget is holding — a live gizmo drag (the
    /// run's way: no undo step, the bodies handed back), an open Alt+drag
    /// macro, and the buttons the camera controller believes are down. Called
    /// at an eject hand-over and at Stop, because a gesture cannot survive the
    /// moment its input owner changes underneath it (F2).
    void endEditorMouseGesture();
    /// A drag the RUN would win anyway is refused by name, with a toast (F6):
    /// a character's movement, a socket rider and an animation with real
    /// channels rewrite their node every frame of a run, so the gizmo cannot
    /// hold one. True = refused, and the press does nothing else.
    bool refuseDragOnDrivenNode();
    /// The selection as it stood at Stop, re-anchored on the restored document
    /// (PLAY-SELECT-1): the nodes that survived the run keep the selection, the
    /// outline and the properties column; the gizmo re-reads the poses the
    /// restore wrote.
    void refreshSelectionAfterPlay();
    /// A gizmo drag during a run takes the dragged rigid bodies away from
    /// Bullet for the length of the gesture and gives them back where the hand
    /// left them (see the definitions).
    void beginPlayDragOverPhysics();
    void endPlayDragOverPhysics();
    /// The bodies THIS drag took over — the exact set endPlayDragOverPhysics
    /// gives back, so a selection change mid-drag cannot strand a flag.
    QList<iris::SceneNodePtr> mPlayDragBodies;
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
    // Per-scene (EditorData). Default OFF (owner, 2026-09-06): scenes ship a
    // tiled floor, so the perspective grid is noise — and it was baking into
    // reflection probes. The canonical orthographic views force it on with a
    // view-facing plane regardless of this flag; see gridStateForView().
    bool mShowGrid = false;
    /// GI volume boxes (fix 9). Diagnostic, default off, not persisted — it is
    /// a thing you turn on while chasing a lighting question.
    bool mShowGiVolume = false;
    /// A VR session is previewing this viewport's scene: the wearer's per-frame
    /// step, which runs in the camera controller's place (IEditorViewport::
    /// setVrPreviewStep). Installed by the VR module for the life of a session;
    /// not persisted — a session does not survive a restart.
    std::function<void()> mVrPreviewStep;
    /// ...and "this preview cannot continue here" (finding 1; MIRROR-LIVE-1):
    /// called from clearScene() while the engine scene is still alive, and from
    /// end() when the page it is hosted on is left.
    std::function<void()> mVrPreviewEnds;
    bool mShowShadowAtlas = false;
    QString mCameraView = QStringLiteral("perspective"); // last canonical view requested
    /// The grid plane pushGridForView last PUSHED to the mirror (not what a
    /// caller asked for) — what editor.overlays().gridPlane reports, so a
    /// script can assert the axis views' grid orientation survives a pan.
    QString mGridPlanePushed = QStringLiteral("floor");
    QHash<QString, ViewCameraState> mViewStates; // per-view camera memory (session-only)
    bool mGameView = false;             // G: helpers hidden; never persisted
    bool mSelectionWireframe = false;   // false = silhouette outline (default)
    bool mShowDebugDraw = false;
    bool mActive = false;
    unsigned mViewSerial = 0;
};

#endif // ENGINESCENEVIEWPORT_H
