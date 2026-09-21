#ifndef IEDITORVIEWPORT_H
#define IEDITORVIEWPORT_H

// IEditorViewport — what the rest of Studio is allowed to ask of the editor viewport.
//
// VIEWPORT_MIGRATION_PLAN.md step 6. Two implementations:
//   SceneViewWidget       the legacy IrisGL/QOpenGLWidget viewport (wayland only)
//   EngineSceneViewport   the engine-backed viewport (Ogre-Next via jahshaka::engine)
// MainWindow, Globals, UiManager and the camera controllers hold this type, never a
// concrete widget, so the two can be swapped at runtime (--viewport=engine|legacy).
//
// The interface is exactly the surface Studio measured itself using (19 files); it
// is not a wish list. Legacy-only operations (GL context juggling around resource
// loads, the IrisGL renderer) are kept as explicit, nullable calls so the callers
// can be found and retired.
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QList>
#include <QPointF>
#include <QObject>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <functional>
#include "irisgl/irisglfwd.h"

class QWidget;
class MainWindow;
struct StudioServices;
class Database;
class Project;
class EditorData;
class Gizmo;
enum WindowSpaces : int;      // mainwindow.h
class SceneMirror;
namespace jahshaka { namespace engine { class Scene; } }
enum class SceneMode;         // playbackservice.h

/// Signals a viewport emits. A separate QObject so the interface itself stays a
/// plain abstract class (QOpenGLWidget and QWidget cannot both be an interface base).
class EditorViewportEvents : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    /// A builtin primitive dropped INTO the viewport, at the world point under
    /// the cursor (smoke S2 — the drop used to carry no position at all and
    /// every dropped cube landed in front of the camera).
    void addPrimitive(QString guid, iris::Vec3 position);
    void addDroppedMesh(QString path, bool ignore, iris::Vec3 position, QString guid, QString assetName);
    void addDroppedParticleSystem(bool ignore, iris::Vec3 position, QString guid, QString assetName);
    /// A Texture asset dropped on empty space — the shell spawns an image
    /// plane at the drop point (IMAGE_PLANE_SPEC option A).
    void addDroppedImagePlane(iris::Vec3 position, QString guid);
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);
    void updateToolbarButton();
    /// THE camera speed changed from INSIDE the viewport (the scroll wheel
    /// while flying). The shell shows the toast and re-syncs the toolbar's
    /// speed button; CameraSpeed already holds the new integer.
    void cameraSpeedChanged();
    void changeSkyFromAssetWidget(int index);
};

/// What `editor.setCamera` asks the viewport for (AI_SURFACE_PROGRAM_SPEC lane
/// B #3). Every field is optional so a caller can nudge one thing: "move here,
/// keep looking where I look" is `position` alone. Orientation comes from
/// EITHER `lookAt` (a world-space target) OR `rotation`, never both — the verb
/// refuses that ambiguity before it reaches here.
struct EditorCameraPose
{
    iris::Vec3 position;   bool hasPosition = false;
    iris::Vec3 lookAt;     bool hasLookAt = false;
    iris::Quat rotation;   bool hasRotation = false;
    /// Perspective field of view in degrees (iris::CameraNode::angle). <= 0
    /// leaves it alone; it is inert while the camera is orthographic.
    float fovDegrees = 0.0f;
};

/// What `editor.frameNode` asks for: a viewing direction on the sphere around
/// the node's world bounds. Absent yaw/pitch mean "keep the direction the
/// camera already looks from", which makes `frameNode(id)` the verb form of
/// the F key. `distance <= 0` means the bounds-derived framing distance.
struct EditorFraming
{
    float yawDegrees = 0.0f;   bool hasYaw = false;
    float pitchDegrees = 0.0f; bool hasPitch = false;
    float distance = 0.0f;
};

class IEditorViewport
{
public:
    virtual ~IEditorViewport() = default;

    /// The QWidget to place in a layout, and the signal hub to connect to.
    virtual QWidget *asWidget() = 0;
    virtual EditorViewportEvents *events() = 0;

    virtual void setMainWindow(MainWindow *window) = 0;
    /// The widget whose drags mean "reparent inside the hierarchy panel" —
    /// the viewport ignores those. Optional; headless viewports don't care.
    virtual void setHierarchyDragSource(QWidget *) {}
    /// The service aggregate the viewport's tools (gizmos) push undo commands
    /// and refresh notifications through. Optional; headless viewports don't care.
    virtual void setServices(StudioServices *) {}
    virtual void setDatabase(Database *db) = 0;
    /// The one live Project (Phase 4: was the Globals::project static). Optional;
    /// only the engine viewport's drag-drop paths read it.
    virtual void setProject(Project *) {}

    // ---- document ----
    virtual void setScene(iris::ScenePtr scene) = 0;
    virtual iris::ScenePtr getScene() = 0;

    /// THE ONE ENGINE SCENE, and the ONE SceneMirror that pushes the document
    /// into it (owner decision 2026-09-14, lane PLAYER-1).
    ///
    /// The Player page is a second VIEW on this scene, drawn by this mirror,
    /// with the editor's helper geometry masked out per view — not a second
    /// scene and not a second mirror. Both may be null: a headless stand-in
    /// viewport has neither, and the engine viewport has neither until its
    /// native window exists (they are built in its show event). A caller that
    /// gets null must stay inert rather than build its own.
    ///
    /// Borrowed, never owned. The viewport destroys both at teardown, so no
    /// caller may outlive it holding these.
    ///
    /// PURE READS, BOTH (SMOKE-FIX-1's fix round): whoever needs the scene to
    /// EXIST says so, once, with ensureEngineScene() below — these two are
    /// called from per-frame paths (VrApi::pushProxies rides the driver's
    /// beforeFrame), and a getter that builds an engine scene would build one
    /// in every windowed process on its first tick, editor or no editor.
    virtual jahshaka::engine::Scene *engineScene() { return nullptr; }
    virtual SceneMirror *sceneMirror() { return nullptr; }

    /// BUILD THE ONE SCENE NOW IF IT DOES NOT EXIST — the explicit ask, for the
    /// two callers that are entitled to make it: the Player page as it is
    /// entered (EnginePlayerView::adoptEditorScene, called from its show event
    /// and from start()) and the editor's VR preview as a session begins. Both
    /// draw THIS scene; neither can wait for the editor widget's show event,
    /// which may never come (the desktop tile's Play button, a `--vr` boot).
    ///
    /// False when the engine cannot make one yet — before any View exists in
    /// the process, which is the pin's own startup-order law answering for
    /// itself (Engine::createScene returns null until then). The mirror is
    /// created WITH the scene, so a true here means both reads above answer.
    /// A stand-in viewport has no engine and says false.
    virtual bool ensureEngineScene() { return false; }

    /// WHAT THE EDITOR'S ON-SCREEN VIEW HAS ACTUALLY GRADED WITH — the
    /// tonemapper's multiplier (View::measuredExposureScale), or 0 when there
    /// is nothing to read (no view, no HDR, a fixed grade, nothing presented).
    ///
    /// It exists for the Player page, which is a second view of the same scene
    /// with its own adaptation history: seeding that history from this value on
    /// entry is what stops the Player opening at the authored midpoint and
    /// walking to the room's real luminance in front of the user.
    ///
    /// COSTS A GPU STALL (a 1x1 readback with accurate tracking) — once per
    /// picture or per page entry, never per frame.
    virtual float measuredExposureScale() const { return 0.0f; }
    virtual void setSelectedNode(iris::SceneNodePtr sceneNode) = 0;
    /// The whole selection SET, primary first (EDITOR_MULTISELECT_SPEC §2.3) —
    /// the outline, the gizmo group and the focus/orbit/floor union read it.
    /// Optional: a headless stand-in viewport has neither outline nor gizmo.
    virtual void setSelectedSet(const QList<iris::SceneNodePtr> &) {}
    virtual void clearSelectedNode() = 0;
    virtual void focusOnNode(iris::SceneNodePtr sceneNode) = 0;
    /// WHERE A DROP AT THIS PIXEL LANDS: the surface under the cursor, else
    /// the y=0 ground plane, in world space. The viewport's own drag-drop
    /// paths and `editor.dropPointAt` are the same function, so a scripted
    /// placement and a user's drop agree by construction. False when the
    /// viewport has no camera (a headless stand-in never answers).
    virtual bool dropPointAt(const QPointF &, iris::Vec3 *) { return false; }
    /// The node a drop at that pixel would APPLY TO — the material and texture
    /// drops' target. `locked` (optional) receives whether that node is LOCKED
    /// (the hierarchy's lock icon, i.e. `pickable` false — the default floor
    /// ships that way): a locked node is under the cursor like any other and
    /// refuses the drop by name. Null in the document-only stand-ins.
    virtual iris::SceneNodePtr dropTargetAt(const QPointF &, bool * = nullptr)
    {
        return iris::SceneNodePtr();
    }

    /// F: frames the current selection (no-op without one). Only the engine
    /// viewport implements it (EDITOR_SHORTCUTS_SPEC §2).
    virtual void focusOnSelection() {}
    /// End: drops the selection onto the first scene surface below it (y=0
    /// plane fallback), undoable. Only the engine viewport implements it.
    virtual bool snapSelectionToFloor() { return false; }

    // ---- editor camera ----
    virtual iris::CameraNodePtr editorCamera() = 0;
    virtual void setEditorCamera(iris::CameraNodePtr camera) = 0;
    virtual void resetEditorCam() = 0;
    virtual void setFreeCameraMode() = 0;
    virtual void setArcBallCameraMode() = 0;
    /// The active camera controller: "free" (fly) or "orbit" (arcball).
    virtual QString cameraMode() const { return QStringLiteral("free"); }
    /// Snap the editor camera to a canonical view — "top", "bottom", "left",
    /// "right", "front", "back" (orthographic), or "perspective". Each view
    /// remembers its camera between visits (session-only): perspective its
    /// full pose, each ortho view its own pan + zoom; a first visit to an
    /// axis view gets the standard framing. Works in both camera modes.
    /// Returns false for an unknown name. Optional; headless viewports may
    /// leave it unimplemented.
    virtual bool setCameraView(const QString &) { return false; }
    /// The last canonical view requested via setCameraView ("perspective"
    /// until one is set). Purely informational — free orbiting afterwards
    /// does not reset it.
    virtual QString cameraView() const { return QStringLiteral("perspective"); }
    /// True while the editor camera's ROTATION is LOCKED to the current view:
    /// an axis view (top/bottom/left/right/front/back) that is not being
    /// piloted. Rotation GESTURES — the RMB look drag, the Alt+LMB orbit, the
    /// arcball's drag — do nothing while it is true; panning, zooming and the
    /// fly keys keep working, and the placement verbs (editor.setCamera,
    /// editor.frameNode) are unaffected. editor.setView("perspective") clears
    /// it and restores the remembered perspective pose. Reported by
    /// editor.camera().rotationLocked. Optional; stand-in viewports never lock.
    virtual bool cameraRotationLocked() const { return false; }
    /// Which plane the editor grid was last drawn in: "floor" (XZ — every
    /// perspective view and top/bottom), "frontXY" (front/back) or "sideYZ"
    /// (left/right). It follows the canonical VIEW, never the camera pose, so
    /// it cannot change under a pan. Reported by editor.overlays().gridPlane.
    virtual QString gridPlane() const { return QStringLiteral("floor"); }
    /// Place the editor camera directly (editor.setCamera). Implementations
    /// MUST resync the active camera controller afterwards — both existing
    /// camera movers (focusOnNode, restoreViewState) do, and a pose written
    /// without it snaps back on the first mouse move. Returns false when there
    /// is no editor camera. Optional; headless viewports may leave it out.
    virtual bool setCameraPose(const EditorCameraPose &) { return false; }
    /// Frame `node` from a chosen direction (editor.frameNode): focusOnNode's
    /// bounds maths with the view direction taken from `framing` instead of
    /// from the current camera. Same controller-resync obligation.
    virtual bool frameNode(iris::SceneNodePtr, const EditorFraming &) { return false; }

    // ---- Pilot mode + the selection PiP (CAMERAS_SPEC D3/D8) --------------
    /// PILOT a scene camera: the main view renders through it and the
    /// viewport's own navigation (RMB fly, orbit, F focus, the gizmos' pick
    /// rays) drives THAT camera's transform instead of the explorer's. Null
    /// returns to the explorer, leaving the camera wherever it was flown —
    /// piloting doubles as placement, which is the whole reason it exists.
    ///
    /// The move is undoable as ONE step, recorded when piloting ENDS: a
    /// continuous fly would otherwise push a command per mouse event and bury
    /// the undo stack.
    ///
    /// Returns false for a node that is not a camera of the current scene.
    /// Optional; headless viewports leave it unimplemented.
    virtual bool pilotCamera(iris::CameraNodePtr) { return false; }
    /// The camera being piloted, or null (the explorer).
    virtual iris::CameraNodePtr pilotedCamera() const { return iris::CameraNodePtr(); }

    /// The selection preview inset: whether it is drawn at all, and how wide
    /// it is as a FRACTION of the viewport's width (its height follows the
    /// camera's aspect). Persisted preferences; the inset itself only appears
    /// while a scene camera is selected and is hidden in Game View, in play,
    /// and while piloting that same camera.
    virtual bool pipEnabled() const { return false; }
    virtual void setPipEnabled(bool) {}
    virtual double pipSize() const { return 0.0; }
    virtual void setPipSize(double) {}

    /// The camera speed changed under the viewport's feet — the wheel stepped
    /// it while the camera was flying. The viewport shows the number briefly
    /// and tells the shell so the toolbar's speed button follows. CameraSpeed
    /// already holds the new value; this only announces it.
    virtual void onCameraSpeedChanged() {}
    virtual void setEditorData(EditorData *data) = 0;
    virtual EditorData *getEditorData() = 0;

    // ---- modes ----
    virtual void setWindowSpace(WindowSpaces windowSpace) = 0;
    virtual void setSceneMode(SceneMode sceneMode) = 0;
    virtual void enterEditorMode() = 0;
    virtual void enterPlayerMode() = 0;

    // ---- gizmos ----
    /// The live gizmo, or null. The camera controllers use it to refuse camera
    /// drags while a gizmo drag is in progress.
    virtual Gizmo *activeGizmo() const { return nullptr; }
    /// The active gizmo mode as a verb-friendly name: "translate" | "rotate" |
    /// "scale" (editor.gizmoMode; also drives the Space mode cycle).
    virtual QString gizmoMode() const { return QStringLiteral("translate"); }
    virtual void setGizmoLoc() = 0;
    virtual void setGizmoRot() = 0;
    virtual void setGizmoScale() = 0;
    virtual void setGizmoTransformToLocal() = 0;
    virtual void setGizmoTransformToGlobal() = 0;
    /// The gizmos' drag space as the verb surface spells it: "local" |
    /// "global" (editor.gizmoSpace). Only the engine viewport has gizmos; the
    /// stand-ins answer with the default.
    virtual QString gizmoTransformSpace() const { return QStringLiteral("global"); }

    /// WHAT THE ACTIVE GIZMO ANSWERS AT A PIXEL (smoke S15). The rotation
    /// gizmo picks in SCREEN SPACE — the cursor's distance from each ring's
    /// projected circle — so "is this ring clickable from here" is a number,
    /// and `editor.gizmoHitTest` is that number without a synthesized mouse
    /// event. `handle` is "x" | "y" | "z" | "screen" (the outer grey ring,
    /// which turns the node about the view direction — GIZMO-1 item 2) for a
    /// ring inside the pick tolerance and empty otherwise; `distancePx` is the
    /// distance to the NEAREST ring either way (-1 when nothing could be
    /// measured: no selection, no camera, or a gizmo that does not pick this
    /// way).
    struct GizmoPickResult
    {
        QString handle;
        float distancePx = -1.0f;
        float tolerancePx = 0.0f;
    };
    virtual GizmoPickResult gizmoHitTest(const QPointF &) const { return GizmoPickResult(); }

    // ---- play / physics ----
    virtual void startPlayingScene() = 0;
    virtual void pausePlayingScene() = 0;
    virtual void stopPlayingScene() = 0;
    /// The viewport's OWN play flag — the one its event handlers branch on.
    /// This is the truth; PlaybackService's copy delegates here (the two flags
    /// desynced once, 2026-09-05, and every editor click went to the player
    /// controller). Verb surface: editor.playing().
    virtual bool isPlaying() const = 0;
    /// EJECT (PLAY-SELECT-1, owner R13 — Unreal's F8). While a run owns the
    /// input (a possessed avatar), ejecting hands the mouse and the keyboard
    /// back to the editor WITHOUT stopping the simulation; un-ejecting gives
    /// them back. Idempotent, and meaningless outside play: setting it while
    /// nothing is playing is refused by the verb and ignored here. Stand-in
    /// viewports never eject.
    virtual bool playEjected() const { return false; }
    /// A RUN EXISTS — paused counts (PLAY-SELECT-1 fix round, F5). `isPlaying`
    /// answers "is it STEPPING", which pause makes false while the run, its
    /// physics world and its pre-play snapshot all live on; every rule about
    /// what a run's edits are worth needs this one instead.
    virtual bool playRunLive() const { return false; }
    virtual void setPlayEjected(bool) {}
    /// WHO OWNS A PLAIN LEFT CLICK RIGHT NOW: "controller" while a run is
    /// consuming input, "editor" otherwise (including outside play). The verb
    /// surface is editor.playInputOwner(); the routing in the engine viewport's
    /// event handlers is keyed on the SAME predicate, so the two cannot
    /// disagree — the lesson of the 2026-09-05 stuck-play defect.
    virtual QString playInputOwner() const { return QStringLiteral("editor"); }
    virtual void startPhysicsSimulation() = 0;
    virtual void restartPhysicsSimulation() = 0;
    virtual void stopPhysicsSimulation() = 0;

    // ---- overlays / output ----
    virtual bool getShowLightWires() const = 0;
    virtual void setShowLightWires(bool value) = 0;

    /// Ground grid (EDITOR_SHORTCUTS_SPEC §3): per-scene, default ON. Only the
    /// engine viewport draws it.
    virtual bool getShowGrid() const { return true; }
    virtual void setShowGrid(bool) {}

    /// The GI volume overlay (LIGHTING_FIX fix 9): wireframe boxes around the
    /// lit (voxel) volume and the reflection-probe region `world.giStatus()`
    /// reports. Default OFF — it is a diagnostic, not scenery. Only the engine
    /// viewport draws it, and only while GI is on.
    virtual bool getShowGiVolume() const { return false; }
    virtual void setShowGiVolume(bool) {}

    /// THE SHADOW-ATLAS INSPECTOR — a strip of thumbnails showing what the
    /// renderer rasterised into each rectangle of its one shadow atlas, with
    /// the light each map belongs to and whether that map is static
    /// (SPECS/SHADOW_TOOLING_SPEC.md §4.4). A diagnostic: never persisted, and
    /// drawn by the engine's own HUD rather than by Qt for the reason the
    /// loading cover is (the viewport is a native window).
    virtual bool getShowShadowAtlas() const { return false; }
    virtual void setShowShadowAtlas(bool) {}

    /// Game View (G): hides every in-viewport editor helper (grid, light
    /// wires, selection outline, gizmo). Docks/toolbars untouched, never
    /// persisted. Only the engine viewport implements it.
    virtual void setGameView(bool) {}
    virtual bool isGameView() const { return false; }

    /// Selection highlight style: false (default) = silhouette outline, true = the
    /// polygon wireframe. Only the engine viewport implements it; the legacy
    /// viewport keeps its own single style.
    virtual bool getSelectionWireframe() const { return false; }
    virtual void setSelectionWireframe(bool) {}
    virtual bool getShowDebugDrawFlags() const = 0;
    virtual void setShowDebugDrawFlags(bool value) = 0;
    /// The engine-drawn frame-stats readout (F3, `editor.setOverlays({stats})`,
    /// the Preferences `show_fps` row — one code path, three doors). SURVIVES
    /// Game View and fullscreen on purpose: it is a diagnostic, not an editor
    /// helper (STATS_OVERLAY_SPEC.md D3).
    virtual void setShowFps(bool value) = 0;
    virtual bool getShowFps() const { return false; }
    virtual void setShowPerspeciveLabel(bool value) = 0;
    virtual QImage takeScreenshot(int width = 1920, int height = 1080) = 0;
    virtual QImage takeScreenshot(QSize dimension) = 0;
    /// HOW A SCREENSHOT IS DEVELOPED — ONE FUNCTION, AN EXPLICIT MODE
    /// (owner, 2026-09-13: "match the screenshot to the scene properly", and
    /// "your pixel tests can have their own screenshot ... use the same
    /// function to get what you want and what the users want").
    ///
    /// The tension this enum exists to hold: the PIXEL SUITES want a plain,
    /// exact, ungraded readback and must keep getting exactly that, while a
    /// USER pressing Screenshot wants the picture they are looking at. So the
    /// plain picture is an EXPLICIT opt-in whose name says what it is, and the
    /// user's door asks for `Scene`.
    /// WHAT COLOUR SPACE EACH GRADE'S BYTES ARE IN — MEASURED, not assumed
    /// (PLAIN-GRADE-1, 2026-09-18; the render audit's ON-20 asked the question
    /// and left it open). The measurement, on the rig at 1920x1080 with a flat
    /// sky the user picked as #808080 (the document decodes a picked colour
    /// sRGB->linear, so the sky's radiance is 0.2159):
    ///
    ///   plain    55/255 = 0.2157 — THE LINEAR RADIANCE, un-encoded.
    ///   tonemap  50/255, scene 50/255, viewport 50/255 — the film curve's
    ///            output (Hable at the shipped constants, /f(11.2), then the
    ///            (x-0.5)*1.25+0.61 grade tail: 0.2159 x 1.3646 exposure
    ///            -> 0.1946 -> 50, to the byte).
    ///   THE WINDOW  (0x200011 grabbed with xwd while the same scene was on
    ///            screen) 50/255 — and with the sky at #8000C0 the window read
    ///            (50, 0, 114) against the scene grade's (50, 0, 114), THE SAME
    ///            BYTES.
    ///
    /// So: THE THREE GRADED ANSWERS ARE WHAT THE WINDOW SHOWS, bit for bit, and
    /// PLAIN IS NOT — Plain is one colour space away from the picture, by its
    /// own contract ("no post-processing at all"), and that difference is the
    /// whole reason every offscreen diagnosis in this tree has read "too dark".
    /// It is NOT a missing sRGB encode on the offscreen target: this engine's
    /// window swapchain is not sRGB either (the `gamma` parameter is passed on
    /// the non-Vulkan branch only, OgreEngine.cpp), HlmsPbs in this pin sets
    /// `hw_gamma_write` unconditionally so the shader never encodes, and the
    /// tonemapper's output IS the display code (which is why EXPOSURE-1's
    /// anchor solves to an output of exactly 0.18 for an 18 % card). Encoding
    /// the plain readback would therefore make it disagree with the window and
    /// with every engine-side offscreen suite at once.
    ///
    /// READ A PLAIN VALUE AS RADIANCE, then. `gradeEncoding()` below reports
    /// this per grade and every screenshot verb returns it beside the pixels,
    /// so a reader is told which of the two spaces it is holding.
    enum class ScreenshotGrade {
        /// NO POST-PROCESSING AT ALL: 1x MSAA, linear radiance clipped to 8
        /// bits, the same pixels on every machine and in every frame. THIS IS
        /// THE TEST PICTURE — every pixel assertion in the tree asserts it, and
        /// it is the default of `editor.screenshot` / `player.screenshot` for
        /// that reason. Script word "plain" (and "raw", the spelling the suites
        /// were written with; both mean this and always will).
        ///
        /// ITS BYTES ARE LINEAR RADIANCE (see the block above, measured): a
        /// floor at radiance 0.192 reads 49/255 here and NOT the 122/255 the
        /// same radiance would be as a display code. That is the contract, not
        /// a defect — it is what makes this grade an exactly reproducible
        /// measuring instrument — but it is also why a plain shot always looks
        /// darker than the editor: it is not a picture of the editor.
        Plain,
        /// THE THUMBNAIL PICTURE: the deterministic filmic grade and nothing
        /// else — no bloom, no AO, no SMAA, no SSR, exposure a constant
        /// (secondaryfx::apply). A photograph of the CONTENT that does not clip
        /// to white wherever the scene is bright, cheap enough for an import
        /// sweep of hundreds. Script word "tonemap".
        Tonemap,
        /// THE EDITOR'S OWN PICTURE, and the answer a user gets: the scene's
        /// WHOLE chain as the world has it — SSAO, SSR, bloom, SMAA, the looks
        /// stack, HDR and the tonemap — at the on-screen view's CURRENT
        /// MEASURED exposure, carried across as a constant
        /// (secondaryfx::applyScene). Matches the viewport, and is repeatable
        /// because the exposure stopped being a temporal filter on the way in.
        /// A world with HDR off gets an UNGRADED shot, like the viewport.
        /// Script word "scene".
        Scene,
        /// The scene's whole chain with its own ADAPTIVE exposure, RE-SEEDED
        /// from the description's exposure value. This is what `postFx: true`
        /// has always meant and it stays, because it is the door a PIPPED
        /// CAMERA's own exposure reaches a shot through (camera.screenshot,
        /// tests/cameras) — there is no on-screen view measuring THAT camera.
        /// For the editor camera prefer `Scene`: a two-frame view cannot
        /// converge, so this one grades at its seed. Script word "viewport".
        Viewport,
    };
    /// A viewport with no post chain (the headless stand-ins) has one picture
    /// and every grade is it. The boolean overload this used to delegate to is
    /// GONE (CLEANUP-1 item 5): there were three screenshot doors and two
    /// exposures, and a bool cannot name four grades.
    virtual QImage takeScreenshot(int width, int height, ScreenshotGrade grade) {
        (void)grade; return takeScreenshot(width, height);
    }

    /// THE SCRIPT SPELLINGS, in ONE place — "plain" (and "raw", the spelling
    /// the pixel suites were written with), "tonemap", "scene", "viewport".
    /// Every verb that takes a `grade` parses it through here, so a verb whose
    /// parser is narrower than its documentation cannot happen again (it did:
    /// player.screenshot's whole `scene` branch shipped unreachable once).
    /// False on an unknown word, with `grade` untouched.
    static bool gradeFromString(const QString &word, ScreenshotGrade *grade) {
        const QString w = word.trimmed().toLower();
        if (w == QLatin1String("plain") || w == QLatin1String("raw"))
            { if (grade) *grade = ScreenshotGrade::Plain; return true; }
        if (w == QLatin1String("tonemap"))
            { if (grade) *grade = ScreenshotGrade::Tonemap; return true; }
        if (w == QLatin1String("scene"))
            { if (grade) *grade = ScreenshotGrade::Scene; return true; }
        if (w == QLatin1String("viewport"))
            { if (grade) *grade = ScreenshotGrade::Viewport; return true; }
        return false;
    }
    /// The four words, for a verb's own error message and its docs.
    static QString gradeWords() { return QStringLiteral("plain | raw | tonemap | scene | viewport"); }

    /// THE GRADE'S SCRIPT WORD, for a verb's answer (the canonical spelling:
    /// "raw" and `false` both report "plain").
    static QString gradeName(ScreenshotGrade grade) {
        switch (grade) {
        case ScreenshotGrade::Tonemap:  return QStringLiteral("tonemap");
        case ScreenshotGrade::Scene:    return QStringLiteral("scene");
        case ScreenshotGrade::Viewport: return QStringLiteral("viewport");
        case ScreenshotGrade::Plain:    break;
        }
        return QStringLiteral("plain");
    }
    /// THE COLOUR SPACE OF THAT GRADE'S BYTES — "linear" or "display" (the
    /// enum's own measurement block above says how this was established, and
    /// that the display answer is the window's bytes exactly). Every screenshot
    /// verb returns it beside the pixels, because a number read in the wrong
    /// space is the one reading nobody notices is wrong: 0.2159 radiance is
    /// 55/255 here and 50/255 on screen, and neither is "too dark".
    static QString gradeEncoding(ScreenshotGrade grade) {
        return grade == ScreenshotGrade::Plain ? QStringLiteral("linear")
                                               : QStringLiteral("display");
    }

    /// The ACHIEVED anti-aliasing (MSAA) sample count of the viewport's render
    /// target — the driver may clamp what scene->antiAliasing requested. Only
    /// the engine viewport reports it; the legacy viewport has no MSAA (1).
    virtual int sampleCount() const { return 1; }

    /// True when the viewport renders to an offscreen texture rather than to the
    /// widget's own native window — i.e. the widget area stays blank. Document-only
    /// stand-in viewports are always offscreen; the engine viewport reports what it
    /// actually got, which is how --engine-selftest can assert that the on-screen
    /// path was taken at all without asserting pixels (MACOS_VIEWPORT_SPEC §5.1).
    virtual bool isOffscreen() const { return true; }
    /// The viewport's current render-target size, or an empty size when there is
    /// no render target. Used by the selftest to prove a resize was applied.
    virtual QSize renderTargetSize() const { return QSize(); }

    /// WHERE THIS WIDGET SITS INSIDE ITS TOP-LEVEL WINDOW, in window pixels
    /// (PLAY-SELECT-1). Every pixel-taking verb speaks the VIEWPORT's
    /// coordinates and a synthesised X click speaks the WINDOW's; this is the
    /// conversion, read from the live layout rather than guessed from dock
    /// sizes. An empty rect when there is no window (stand-ins, offscreen).
    virtual QRect widgetRectInWindow() const { return QRect(); }

    /// The shadow-map atlas base resolution the renderer is CURRENTLY using —
    /// global, one atlas for every light (VISUAL_PARITY_SPEC item 2). The scene
    /// field (0 = Auto) is a request; this is what came out of it. Only the
    /// engine viewport reports it; 0 means "no engine to ask".
    virtual int shadowResolution() const { return 0; }

    /// How many planar-reflection planes actually RENDERED last frame — the
    /// achieved number against the scene's budget, in the same "the renderer
    /// beats the request" spirit as sampleCount() and shadowResolution(). A
    /// plane off screen is culled and does not count. 0 means reflections are
    /// off, nothing has rendered yet, or there is no engine to ask.
    virtual int activePlanarReflectors() const { return 0; }

    /// What global illumination is ACHIEVING in the renderer, as opposed to
    /// what the document asked for (REFLECTIONS_ADOPTION_SPEC.md §3). Reported
    /// because the VCT+PCC hybrid can silently degrade to plain VCT — the probe
    /// arm logs a line and returns, and nothing downstream could tell. Read
    /// through world.giStatus().
    ///
    /// `available` false means there is no engine to ask (the document-only
    /// stand-in viewports); the other fields are then meaningless rather than
    /// merely zero, exactly like MirrorStats.
    struct GiStatusInfo {
        bool    available = false;
        QString mode;          ///< off | vct | vct_pcc_hybrid
        int     probeCount = 0;///< live parallax-corrected cubemap probes
        bool    pccBound = false;  ///< this scene's probe grid is bound to the PBR shader
        bool    vctBound = false;  ///< this scene's voxel lighting is bound to the PBR shader
        /// The RESOLVED boxes the last GI build actually used
        /// (REFLECTIONS_ADOPTION_SPEC.md P1a): the lit/voxel volume, and the
        /// reflection-probe region, which is deliberately a DIFFERENT and
        /// tighter box (the free space, no margin, pulled in to the room's
        /// walls). Both are null boxes when nothing is built. This is the only
        /// way to see what the auto-fit decided — the document's giBounds rows
        /// stay at zero for every scene that never pinned them.
        QVector3D boundsMin, boundsMax;
        /// METRES PER VOXEL of that volume — its largest axis over the tier's
        /// voxel resolution. The number that says whether this scene's GI means
        /// anything (a 1 km volume at 128^3 is 8 m per voxel); 0 with no volume.
        float voxelMetres = 0.0f;
        QVector3D probeRegionMin, probeRegionMax;
        /// What the probe captures RESOLVED to (P3a/P3b). Both document fields
        /// are tri-state with an "auto" that consults the quality dial, and the
        /// shadow half falls back when no shadow node exists — so the request
        /// alone never says what the reflections actually contain.
        bool probeHdr = false, probeShadows = false;
        int  probeCaptureSize = 0;
        /// How many candidate probes the renderer photographed and DROPPED
        /// because the box their six faces measured was no smaller, by volume,
        /// than the volume the renderer lit. `probeCount` 0 with this non-zero
        /// is the open-scene answer (the sky reflects); with it zero, in the
        /// hybrid, it is a build failure.
        int  probesDropped = 0;
        /// Material edits that CROSSED the reflection-probe gate on this scene
        /// (ogre-patch 0028): the one material edit that rebuilds a shader.
        /// Cumulative, never reset.
        unsigned probeGateCrossings = 0;
        /// How many probes the renderer re-captures per frame — the RESOLVED
        /// GI update budget (FIX WAVE B1/B2). 0 means GI is PAUSED: every
        /// reflection is frozen until world.refreshGi() asks for more. A CEILING:
        /// probes re-capture only while stale, so a change is caught up within
        /// ceil(probeCount / this) frames and a still scene spends nothing.
        int  probeUpdatesPerFrame = 0;
        /// The union of the probes' fitted parallax shapes (FIX WAVE A2). Must
        /// lie inside the probe region; a shape that escaped it is what makes
        /// reflections go black in hard-edged, cluster-shaped patches.
        QVector3D probeShapeMin, probeShapeMax;
        /// THE FORWARD+ PER-CELL CUBEMAP PROBE BUDGET, and the two per-probe
        /// readings the union above cannot give (2026-09-07 fix wave).
        ///
        /// `cubemapProbeSlotsPerCell` is what stopped the owner's "hard-edged
        /// black rectangles crawling over the metals": a cluster cell that sees
        /// more probes than this DROPS the rest silently, and the pixels whose
        /// probe was dropped fall through to cone tracing, which in an interior
        /// is black. Below `probeCount` means cells can still drop probes.
        ///
        /// `probesClampedToRegion` is how many probes the region clamp had to
        /// correct at the last build — i.e. how degenerate the 1x1
        /// averaged-depth shrink-fit was in this scene. Not itself an artifact
        /// (the clamp corrects it), but unlike the union check it CAN fire.
        /// `worstProbeShapeCellRatio` reports how far the worst probe's
        /// parallax box reaches past its own cell, as a multiple of that cell.
        int   cubemapProbeSlotsPerCell = 0;
        int   probesClampedToRegion = 0;
        float worstProbeShapeCellRatio = 0.0f;
        /// How many probes reach FURTHER past their own cell than the fit is
        /// allowed to (the engine's kProbeShapeCellAllowance). Non-zero is a
        /// defect and not a tuning matter — the shrink-fit has returned a box
        /// unrelated to the space that probe is responsible for. The engine has
        /// computed it since the 2026-09-07 fix wave and nothing reported it
        /// until render audit I-10 asked what giStatus was hiding.
        int   probesExceedingCell = 0;
        /// Whether the last full refresh RE-USED the voxel arm instead of
        /// rebuilding it from scratch (FIX WAVE B4).
        bool reusedLastRefresh = false;
        /// DDGI — the irradiance field (GI_UNIFIED_SPEC.md §4 P1). Same
        /// "achieved, not requested" contract as pccBound: the request can be
        /// refused for reasons no caller can see (no voxel volume to feed the
        /// field, DDGI media not staged, a construction that threw), so a
        /// scene asking for it and a scene getting it are two different
        /// readings. `ifdConverged` is false only while a progressive
        /// re-converge after a light move is still in flight — a field is
        /// converged on the frame it binds.
        bool ifdBound = false;
        int  ifdProbes = 0;
        bool ifdConverged = false;
        int  ifdProbesPerFrame = 0;
        /// WHERE THE FIELD IS — the corners of the volume its probes span. The
        /// scene's fitted box in the single-volume arm; cascade 0's box, which
        /// follows the camera, under a Photon cascade chain (PHOTON_SPEC E1).
        /// `ifdFollows` counts the re-placements onto cascade 0 since the last
        /// build: 0 while standing still, one per cascade-0 step while walking.
        QVector3D ifdMin;
        QVector3D ifdMax;
        quint64   ifdFollows = 0;
        /// THE PROBE CACHE (ENGINE_CACHE_POLICY_SPEC P1/P6/P7): probes
        /// re-capture only while stale. Captures the last rendered frame made,
        /// probes still owed a capture, the input that last staled the grid
        /// (none | rebuild | refresh | moved | light | material | sky | ambient
        /// | fog) with a serial per event, and the scene's
        /// from-scratch GI builds so far.
        int     probeCapturesLastFrame = 0;
        /// How many frames the probe budget has DEFERRED a capture for, over the
        /// scene's life (DRAG-1): a capture of something that is still moving is
        /// out of date before it is displayed, so the spend waits for the
        /// content to hold still. 0 for ever in a scene nothing moves in.
        quint64 probeCapturesDeferred = 0;
        int     staleProbes = 0;
        QString lastStaleReason = QStringLiteral("none");
        quint64 staleSerial = 0;
        quint64 rebuilds = 0;
        /// MOBILITY (SPECS/REALTIME_REFLECTIONS_SPEC.md §3.3.4). What the
        /// renderer has been told MOVES, which is what decides who may be baked
        /// into the room's lighting at all — so it belongs beside the probe and
        /// rebuild counters rather than in the mirror's own statistics.
        ///
        /// `movableItems`/`movableLights` are the engine's own records (the
        /// document's resolution reaches it through Scene::setNodeMovable).
        /// `mobilityMisses` counts objects that started moving DURING PLAY with
        /// nothing predicting they would — each keeps its old bounce light as a
        /// ghost until play stops, and each is worth marking Movable by hand;
        /// `lastMobilityMiss` names the last one. `mobilityRebuilds` counts GI
        /// rebuilds a mobility CHANGE caused: it is 0 in every scene today,
        /// because the renderer only records mobility so far (lane R1) and
        /// recording costs nothing — lane R2, which spends it on render
        /// channels, is where a flip can become a rebuild.
        int     movableItems = 0;
        int     movableLights = 0;
        quint64 mobilityMisses = 0;
        QString lastMobilityMiss;
        quint64 mobilityRebuilds = 0;
        /// PHOTON (SPECS/PHOTON_SPEC.md P0) — the live cascade chain, innermost
        /// first, empty in the single-volume arm. Per cascade: the half-extent
        /// and resolution it was built at, the metres per voxel that resolves
        /// to, the metres of camera travel between re-centres, the world centre
        /// it currently sits on, how many times it has been re-voxelised, how
        /// many rebuilds it owes, and the CPU cost of its last one.
        struct CascadeInfo {
            float     halfSize = 0.0f;
            int       resolution = 0;
            float     cell = 0.0f;
            float     step = 0.0f;
            /// The near-field radius this cascade guarantees, in metres
            /// (engine GiStatus::CascadeStatus::guaranteedRadius).
            float     guaranteedRadius = 0.0f;
            QVector3D centre;
            quint64   rebuilds = 0;
            int       pending = 0;
            int       items = 0;
            int       attached = 0;
            float     lastCpuMs = -1.0f;
            /// ATOM stage 1's far-field proxy, as this cascade spent it: the
            /// histogram of BAKED LOD LEVELS it voxelised (entry L = objects at
            /// level L, 0 the authored geometry) and the triangles those levels
            /// add up to.
            QVector<int> lodLevels;
            qint64    voxelTriangles = 0;
            /// How many compute dispatches that rebuild cost — the MATERIAL-COUNT
            /// half of its bill (ogre-patch 0065; engine GiStatus::CascadeStatus).
            qint64    voxelDispatches = 0;
        };
        QVector<CascadeInfo> cascades;
        /// Whole-chain rebuilds the teleport guards forced, and cascade
        /// rebuilds deferred because the frame's one-rebuild budget was spent.
        /// The chain is wanted but no view has tracked a camera yet, so there is
        /// nowhere honest to put it — `cascades` is empty for a REASON.
        bool    cascadesAwaitingCamera = false;
        /// The arm is waiting for an albedo/emissive texture that is still
        /// streaming (BOOTVOX-1): the build lands on the frame it arrives,
        /// bounded at 30 deferrals.
        bool    awaitingVoxelTextures = false;
        /// ATOM stage 1's far-field proxy, as APPLIED — whether the cascades
        /// are voxelising the baked LOD levels at all.
        bool    cascadeVoxelLod = true;
        /// Which column of the tier table the chain came from — true = the VR
        /// one, because the view driving GI is the headset's (V1-RIG item 4).
        bool    cascadeProfileVr = false;
        quint64 cascadeFullRebuilds = 0;
        quint64 cascadeDeferrals = 0;
        quint64 cascadeDirtyMajority = 0;
        /// Injection passes the last light tick spent over the chain (LAMPREST-2).
        int     chainSweeps = 0;
        /// Post-rebuild chain settles (LAMPREST-3): how many at-rest injections
        /// the cascade scheduler has run because a rebuild left the chain one
        /// Jacobi pass from its fixed point.
        long long chainSettles = 0;
        /// MOVER-1: how many objects ride the mover channel because they are
        /// being dragged right now, and how many such gestures have ENDED.
        int       dragMovers = 0;
        double    dragMoverGestures = 0.0;
        /// THE SURFACE CACHE (SURFACE-CACHE phase 2). Restated in plain types
        /// rather than carried as the engine's own `CardCacheStatus`, for the
        /// same reason every other field of this struct is: `ieditorviewport.h`
        /// is included by targets that do not build against the engine at all
        /// (the player's routing suite is one), and an engine include here is a
        /// dependency for every one of them. The engine's documentation of each
        /// counter is on `CardCacheStatus`.
        struct CardsInfo {
            bool     built = false;
            int      pageSize = 0, pages = 0, pagesUsed = 0, bytesPerTexel = 0;
            double   bytes = 0.0;
            QString  emissiveFormat;
            int      instances = 0, cards = 0;
            double   radius = 0.0;
            int      queue = 0, budgetTexels = 0, capturesLastFrame = 0, texelsLastFrame = 0;
            double   captures = 0.0;
            double   invalidTransform = 0.0, invalidMaterial = 0.0, invalidLight = 0.0;
            double   captureMs = 0.0;
            /// Phase 4's tables, as maintained (nothing binds them yet).
            int      cardRecords = 0, instanceSlots = 0;
        } cards;
        /// THE HARDWARE RAY-QUERY TIER (SPECS/PHOTON_SPEC.md §7 R1) — what the
        /// renderer HOLDS, reported here because it is read beside the GI
        /// figures and by the same "what it achieved, not what was asked for"
        /// contract. It is NOT global illumination: it is a geometry service
        /// (one acceleration structure over the scene's traceable Items) that
        /// GI's later stages are the first consumers of.
        ///
        /// `available` is the DEVICE's answer and nothing in the document can
        /// move it; `enabled` is ours — false with `available` true is the
        /// no-rays switch in force, which is how this machine renders the
        /// picture a machine without ray tracing gets.
        struct RayQueryInfo {
            bool    available = false;
            bool    enabled = false;
            int     blasCount = 0;   ///< bottom-level structures = unique traced meshes
            int     instances = 0;   ///< the traced set: NOT the scene's Item count
            int     triangles = 0;   ///< unique geometry, not instanced
            quint64 blasBytes = 0;
            quint64 tlasBytes = 0;
            float   tlasMs = -1.0f;  ///< GPU ms of the last top-level build/refit
            float   blasMs = -1.0f;  ///< GPU ms of the last bottom-level batch
            float   gatherMs = -1.0f;///< CPU ms of the last instance gather
            bool    lastWasRefit = false;
            quint64 tlasBuilds = 0;
            quint64 tlasRefits = 0;
            quint64 blasBuilds = 0;
            bool    reflect = false;  ///< the reflect trace ran this frame (needs an SSR chain)
            int     reflectRays = 0;  ///< rays the last reflect dispatch traced (its own resolution)
            float   reflectMs = -1.0f;///< GPU ms of that dispatch; -1 until measured
        };
        RayQueryInfo rayQuery;
        /// THE SCREEN-PROBE GATHER (SPECS/SCREEN_PROBE_GATHER_SPEC.md phase 1) —
        /// what the row RESOLVED to against this machine and what the last
        /// gathered frame did. `on` false with every number zero is the shipped
        /// state; `on` true with `running` false means no view of this scene
        /// carries the prepass the probes read their surfaces from.
        struct GatherInfo {
            bool  on = false;
            bool  running = false;
            int   stride = 0;        ///< pixels per probe, both axes
            int   octRes = 0;        ///< the octahedral map's resolution
            int   raysPerProbe = 0;  ///< octRes squared
            int   probesX = 0, probesY = 0, probes = 0;   ///< the uniform grid
            int   adaptive = 0, adaptiveCap = 0;          ///< ...and the extra probes
            double raysPerFrame = 0.0;
            int   targetW = 0, targetH = 0;
            double atlasBytes = 0.0; ///< the atlas + records + irradiance, resident
            float placeMs = -1.0f, traceMs = -1.0f, integrateMs = -1.0f;
            float cpuMs = -1.0f;     ///< the CPU cost of RECORDING the three jobs
        };
        GatherInfo gather;
    };
    virtual GiStatusInfo giStatus() const { return {}; }

    /// WHAT THE VOXEL LIGHTING VOLUME HOLDS (PHOTON-M3) — a TEST AND TOOL
    /// readback of one cascade's light volume, behind `world.giVoxelStats`.
    /// The engine flushes and downloads the whole volume for it, so it is
    /// never on a frame path; `available` false means there is no engine to
    /// ask, no VCT arm on the scene, or no such cascade.
    ///
    /// Every value is in the STORE's own normalised units (scene radiance
    /// times `multiplier`), because the question it answers is about the
    /// STORE: does the bounce's fixed point fit in the format.
    struct GiVoxelStatsInfo {
        bool    available = false;
        int     cascade = 0;
        int     width = 0, height = 0, depth = 0;
        QString format;            ///< the total volume's pixel format, Ogre's spelling
        float   formatMax = 0.0f;  ///< 1.0 for a UNORM store; 0 = a float one (no ceiling)
        float   multiplier = 0.0f; ///< k: a voxel holds k times the surface's radiance
        float   peak = 0.0f;       ///< peak channel of the TOTAL volume
        float   peakDirect = 0.0f; ///< ...and of the DIRECT one (<= 1/headroom by construction)
        double  meanLit = 0.0;     ///< mean channel maximum over lit voxels
        qint64  voxelsLit = 0;
        qint64  voxelsAtMax = 0;   ///< on the format's top bin: on a UNORM total this IS the clip
        qint64  directAtMax = 0;
        qint64  voxels = 0;
        qint64  voxelsAboveOne = 0;///< above 1.0 in store units — what an 8-bit store would clip
    };
    virtual GiVoxelStatsInfo giVoxelStats(int cascade) { (void)cascade; return {}; }

    /// WHAT THE SHADOW ATLAS IS, as opposed to what the scene asked for
    /// (SPECS/SHADOW_TOOLING_SPEC.md §7) — the same reading as giStatus() and
    /// for the same reason: the renderer has a FIXED number of point/spot
    /// shadow maps and silently drops the casters that do not fit, so nothing
    /// downstream could tell "this lamp casts no shadow" from "this lamp's
    /// shadow was dropped". `available` false means there is no engine to ask.
    struct ShadowMapEntry {
        int     slot = 0;        ///< 0 = the directional/PSSM slot, 1..N the focused maps
        QString node;            ///< the light's document guid, empty when it is not ours
        bool    isCached = false; ///< held by the lamp-map cache
        bool    dirty = false;
        bool    pssm = false;
        int     passesLastFrame = 0;
    };
    struct ShadowStatusInfo {
        bool available = false;
        int  resolution = 0;     ///< the atlas base size in force
        int  maps = 0;           ///< pssmSplits + focusedMaps
        int  pssmSplits = 0;
        int  focusedMaps = 0;    ///< point/spot maps the atlas has room for
        int  lightSlots = 0;     ///< 1 + focusedMaps: the length of `mapped`
        int  casters = 0;        ///< shadow-casting point/spot lights in the scene
        int  budget = 0;         ///< the effective ceiling (resolution-capped)
        int  requestedBudget = 0;
        int  atlasWidth = 0, atlasHeight = 0;
        qint64 atlasBytes = 0;          ///< view atlas + the planar mirrors' atlases
        qint64 reflectAtlasBytes = 0;
        qint64 probeAtlasBytes = 0;
        QVector<ShadowMapEntry> mapped;
        QStringList unmapped;    ///< guids of casters with no map — the silent failures
        /// Whether the *PassesLastFrame counters below are a MEASUREMENT at
        /// all. The engine's pass listeners are opt-in and the opt-in expires
        /// 120 rendered frames after the last read, so a plain 0 has two
        /// meanings; false here is "nobody was counting" (ShadowStatus::
        /// countersMeasured). world.shadowStatus reports them as null then.
        bool countersMeasured = false;
        int shadowPassesLastFrame = 0;
        int cachedMapRendersLastFrame = 0;
        int shaderLightMismatches = 0;   ///< the cache's self-check; 0 is the only healthy value
        int reflectPassesLastFrame = 0;
        int probePassesLastFrame = 0;
        int reflectLampPassesLastFrame = 0;
        int probeLampPassesLastFrame = 0;
        int cachedInstances = 0;
        int uncachedInstances = 0;
        bool viewCached = false;
        int mapsDirtiedLastFrame = 0;
        /// Cumulative atlas rebuilds (ShadowStatus::atlasRebuilds) — the hitch
        /// counter `world.shadowStatus().atlasRebuilds` reports.
        int atlasRebuilds = 0;
        /// Cumulative item visits by the lamp-map cache's caster walk
        /// (ShadowStatus::casterWalkItems) — the still-frame counter
        /// `world.shadowStatus().casterWalkItems` reports: it must not move on
        /// a scene nobody is touching.
        unsigned long long casterWalkItems = 0;
    };
    virtual ShadowStatusInfo shadowStatus() const { return {}; }

    /// Whether the renderer ACCEPTED this node as a planar-reflection plane.
    /// The plane, its size and its normal are derived from the mesh's own
    /// bounds, so geometry that is not plate-like is refused — and only the
    /// renderer has the bounds to judge it (the document model carries a
    /// bounding SPHERE, which cannot tell a plate from a ball). The document
    /// flag is the user's intent and is kept either way; this is what says
    /// whether the intent could be honoured. True when there is no engine to
    /// ask, so callers do not report a failure they cannot see.
    virtual bool planarReflectorAccepted(iris::SceneNodePtr node) const
    { Q_UNUSED(node); return true; }

    /// The viewport's document→engine mirror, as counters (see mirrorStats).
    /// `available` false means this viewport has no mirror to ask — the
    /// document-only stand-ins — and the counts are then meaningless rather
    /// than zero.
    struct MirrorStats {
        bool available = false;
        quint64 giPushes = 0;      ///< SceneMirror::giPushCount()
        quint64 giRefreshes = 0;   ///< SceneMirror::giRefreshCount()
        /// SceneMirror::giLightRefreshCount() — the CHEAP light-only re-injects
        /// that run while a light is being dragged, instead of the full
        /// re-solves the drag used to cost (REFLECTIONS_ADOPTION_SPEC.md P2).
        quint64 giLightRefreshes = 0;
        /// ...of which the ones taken AT REST — the tick that ends a movable
        /// lamp's travel and runs the scene's full bounce count (F1).
        quint64 giLightRefreshesAtRest = 0;
        // ---- MOBILITY (REALTIME_REFLECTIONS_SPEC §3.3, lane R1) -----------
        /// How many of the document's nodes resolved MOVABLE on the last sync —
        /// the mirror's own count, which is why it lives here and not in
        /// world.giStatus(): this is what the DOCUMENT decided, the giStatus
        /// counters are what the RENDERER recorded, and a disagreement between
        /// the two is a push that did not land.
        quint64 movableNodes = 0;
        // ---- THE MIRROR'S OWN COST (MIRROR_SCALE lane, 2026-09-13) --------
        /// How many nodes the last sync walked. The denominator for everything
        /// else here, and the one number that says how big the document the
        /// mirror is pushing every frame actually is.
        quint64 nodesVisited = 0;
        /// How many MATERIAL DESCRIPTIONS the last sync built (converted from
        /// the document into the renderer's parameters and texture binds). It
        /// must be ZERO on a still frame: a scene where it equals the material
        /// count every frame is the 52 ms-per-still-frame defect the render
        /// review measured on an 8,404-node lattice.
        quint64 materialBuilds = 0;
        /// How many nodes sit in a SCENE_STATIC memory manager, i.e. are OUT of
        /// Ogre's per-frame transform and bounds passes
        /// (iris::graph::staticNodeCount).
        ///
        /// IT IS A COUNT OF GRAPH NODES, NOT OF DOCUMENT NODES, and the two
        /// differ: the graph also holds nodes the ENGINE owns (a light's -Y
        /// adapter, a decal's projector box, helper wires) and they are counted
        /// too. On an 8,404-node lattice this reads 8,422 against a
        /// `nodesVisited` of 8,403. Read it as "how much of the scene graph is
        /// out of the per-frame passes", never as a share of nodesVisited.
        quint64 staticNodes = 0;
        /// How many times the mirror has re-derived the scene's static
        /// classification after the document went quiet. Moving a node demotes
        /// its subtree for the duration of the gesture; this is what puts it
        /// back, and without it a session's classification drains to nothing.
        quint64 staticRepromotions = 0;
        // ---- THE DIRTY SET (SPECS/DIRTY_SET_MIRROR_SPEC.md) ---------------
        /// How many nodes the document reported as CHANGED on the last sync —
        /// the size of the change list, before the visit. ZERO on a still
        /// frame however big the scene is, which is the whole contract: the
        /// mirror handles what moved, not what exists.
        quint64 dirtyNodes = 0;
        /// How many entries the last sync released because their nodes left
        /// the document (what replaced the per-frame stamp sweep).
        quint64 evictedNodes = 0;
        /// How many nodes the amortised VERIFIER re-checked on the last sync —
        /// the slow rotating re-read that catches a change the document failed
        /// to report (64 a sync by default, a full pass every ~2.2 s at 60 Hz).
        quint64 verifierVisits = 0;
        /// How many times it has FOUND one, ever. IT MUST BE ZERO: every catch
        /// is a missing mark in the document, named once in the log, and the
        /// screen is only right because the verifier healed it.
        quint64 verifierCatches = 0;
        /// How many engine pushes the node visits have made, ever.
        quint64 pushes = 0;
        /// "dirty" or "full" — which mode the last sync ran in. A full walk is
        /// the rare, explicit answer (a bind, a page switch, the play edge,
        /// JAH_MIRROR_VERIFY=full); "dirty" every other frame.
        QString walkMode;
    };
    /// How many times the mirror has pushed a NEW global-illumination
    /// configuration into the engine, and how many times it has asked for the
    /// existing one to be re-solved (SceneMirror::giPushCount /
    /// giRefreshCount). Both are expensive — a VCT re-solve tears the
    /// voxelizer down and rebuilds it from every item — and both are debounced,
    /// so "an idle scene re-solves ZERO times" is a contract of the mirror that
    /// is invisible in pixels and in the document. The mirror suite asserts it
    /// on a synthetic scene; this accessor is what lets the steady-state gate
    /// assert it on a REAL one, through the real app.
    virtual MirrorStats mirrorStats() const { return {}; }

    /// WHAT THE SCENE'S RIGS COST right now (AVATAR_RIG_PERF_SPEC §3.5), read
    /// through scene.rigStats().
    ///
    /// A character made of several skinned pieces used to cost one
    /// SkeletonInstance, one clip push and one WHOLE-RIG bone stream PER PIECE;
    /// on the character rig it costs one instance, one push, and each piece
    /// streams only its own bones. None of that is visible in the document or in
    /// pixels — a shared character and an unshared one render identically — so
    /// this is the only place the optimisation can be observed at all, which is
    /// exactly why it is a verb and not a log line.
    ///
    /// `available` false means there is no engine to ask (the document-only
    /// stand-in viewports), and the other fields are then meaningless rather
    /// than merely zero — the same contract MirrorStats has.
    struct RigStatsInfo {
        bool available = false;
        int rigged = 0;         ///< nodes carrying a skinned renderable
        int instances = 0;      ///< distinct SkeletonInstances behind them
        int shared = 0;         ///< nodes rendering from another node's instance
        int streamedBones = 0;  ///< bone matrices the Hlms streams per pass, summed
        quint64 clipPushes = 0; ///< SceneMirror::clipStatePushes(), cumulative
    };
    virtual RigStatsInfo rigStats() const { return {}; }

    /// DIAGNOSTIC: what the RENDERER's material for this document node
    /// actually ends up holding, as text (Scene::dumpMaterial). Empty when
    /// this viewport has no mirror, the node is not mirrored, or it carries no
    /// material.
    ///
    /// It exists because the hardest question in every material bug so far has
    /// been exactly this one: the document says one thing, the mirror
    /// translates it, applyPbr clamps and guards and reorders it, and until now
    /// the result was only visible under a debugger. Behind
    /// `material.dumpDatablock(nodeId)`.
    ///
    /// The format is the RENDERER'S and is not a material format — the document
    /// is the truth. Read it, do not parse it.
    virtual QString dumpMaterial(const QString &nodeGuid) const
    {
        Q_UNUSED(nodeGuid);
        return QString();
    }

    /// Deterministic frame stepping for scripts and tests (editor.frame(n)):
    /// document→engine sync + renderOneFrame, n times, synchronously — the exact
    /// pattern of the headless suites. Only the engine viewport implements it;
    /// the legacy viewport repaints on its own schedule.
    virtual void renderFrames(int n) { Q_UNUSED(n); }
    /// Steps `n` frames handing the document's simulation clock exactly `dt`
    /// seconds each, instead of however long the wall clock says (the clock
    /// turns either into whole grid steps — iris::SimulationClock). A negative
    /// `dt` means "use the wall clock" and is identical to renderFrames(n).
    virtual void renderFrames(int n, float dt) { Q_UNUSED(n); Q_UNUSED(dt); }
    /// CAN renderFrames() ACTUALLY DRAW? (lane OPEN-FRAMES-1) renderFrames is
    /// a no-op on a viewport with no engine, and silently: a caller that needs
    /// to know whether a frame really happened — the scene open's slice
    /// boundary, which falls back to the engine's bare resource advance when it
    /// cannot have one — has to ask first. False on every viewport that does
    /// not implement renderFrames at all.
    virtual bool canRenderFrames() const { return false; }


    // ---- the "nothing is presenting" cover ----
    // Drawn by the ENGINE since owner decision D2 (STATS_OVERLAY_SPEC.md §6):
    // the Qt widget that used to do it (ViewportCover, a second native X window
    // stacked over the viewport's) is gone. The state machine below is
    // unchanged — only its output device is.
    /// What the viewport is showing RIGHT NOW, as a verb-friendly name:
    ///   "presenting" — the engine's own frames are on screen
    ///   "loading"    — a world is bound but no frame of it has presented yet
    ///   "noscene"    — no world is open in this viewport
    ///   "offscreen"  — this viewport never renders to the widget (headless
    ///                  stand-ins, and the macOS offscreen fallback view)
    /// Read-only, and the thing editor.viewportState() reports.
    virtual QString presentationState() const { return QStringLiteral("offscreen"); }
    /// Frames the viewport's render target has actually drawn AND presented
    /// since the CURRENT WORLD was bound to this viewport — the honest "are
    /// there real pixels of this world on screen yet?" count. Built on
    /// View::framesPresented, but rebased per document scene: a project
    /// close/open reuses the engine scene, so the engine's own counter does
    /// not restart there.
    virtual qulonglong framesPresented() const { return 0; }
    /// THE OTHER HALF OF THAT QUESTION (lane STALE-VIEW-1, View::blankFrames
    /// Presented): frames this viewport presented with NO WORLD BOUND — its
    /// background cleared, and whatever it asked the HUD to draw over it.
    ///
    /// A viewport with no world used to present NOTHING, so the window kept the
    /// last frame it was given. Measured on the rig against the unmodified base
    /// (spikes/stale-view-1/): in a load IN PLACE that is the one to two frames
    /// (~20-60 ms) between the teardown and the moment the panel rebuild takes
    /// the native window off screen — after which the user sees the app's
    /// watermark for ~140 ms whatever the engine does (VIEW-REBUILD-1). The
    /// bigger half is the OTHER defect with the same cause: the "No world open"
    /// panel, raised by every close, changed not one pixel.
    ///
    /// This is the number that says the teardown reached the screen: a caller
    /// differences it across a load exactly as it differences `coversPresented`.
    /// Never reset.
    virtual qulonglong blankFramesPresented() const { return 0; }
    /// THE FLY'S HELD-KEY SET, by name ("Left", "PageUp", "Shift" …), sorted
    /// (ledger §356). A key stuck in it is otherwise INVISIBLE: Left and Right
    /// held together cancel to no movement at all, which reads as "the arrows
    /// are dead" with nothing in any log to say why. `editor.viewportState()`
    /// reports this and `flying` beside it so the state can be read from a
    /// script instead of inferred from the camera not moving.
    virtual QStringList heldFlyKeys() const { return QStringList(); }
    /// True while the fly keys are armed — the right mouse button is held.
    virtual bool flying() const { return false; }

    // ---- THE EDITOR'S VR PREVIEW (SPECS/VR_SPEC.md §5 phase 4) ------------
    /// A VR SESSION IS PREVIEWING THIS VIEWPORT'S SCENE, AND THE WEARER IS NOT
    /// THIS CAMERA.
    ///
    /// The desktop viewport stays a full editor while somebody stands in the
    /// same scene with a headset on — its own camera, its own framing, its
    /// gizmos and its selection. The one thing that moves is the SUBJECT of the
    /// fly keys: while this is set, the editor's own gesture (right button +
    /// the arrow cluster + Shift, at the editor's own speed) walks the WEARER
    /// through the world instead of this camera, because a person wearing a
    /// headset cannot see the desktop and the camera they want to move is the
    /// one behind their eyes. Everything else about the camera — orbit, pan,
    /// dolly, F, the axis views — is untouched.
    ///
    /// SET BY THE VR MODULE FOR THE LIFE OF THE SESSION, never derived from "a
    /// VR session exists": the Player's VR mode runs a session on this same
    /// scene (there is one scene), and there the editor's camera is not the
    /// wearer and its fly keys are nobody's.
    /// `step` runs once per SYNCED FRAME — a driver tick and a scripted
    /// `editor.frame()` alike, which is why it is a callback here and not a
    /// signal on the render driver: a script that steps frames itself never
    /// reaches the driver's, and the wearer would stand still through a whole
    /// scripted run (measured on this lane's first cut: the rig's placement
    /// never fired). It is called INSTEAD OF the camera controller's own fly,
    /// in its place in the frame, so the two can never both move somebody.
    ///
    /// A null `step` clears the whole arrangement and the camera has its fly
    /// keys back.
    virtual void setVrPreviewStep(std::function<void()> step) { Q_UNUSED(step); }
    virtual bool vrPreview() const { return false; }
    /// "THIS PREVIEW CANNOT CONTINUE HERE" — the two ways that happens
    /// (VR-4-FIX finding 1; lane MIRROR-LIVE-1 added the second).
    ///
    ///   * THE WORLD IS ABOUT TO GO: called by clearScene() BEFORE the engine
    ///     scene is destroyed — a project close, and the teardown half of a
    ///     project open in place. A VR session renders that engine scene and
    ///     holds a raw pointer to it, so a preview still running when it is
    ///     freed is a use-after-free on the next frame.
    ///   * THE PAGE IS BEING LEFT: called by end(), the editor page's own
    ///     shutdown. THE OWNER'S RULE (2026-09-18): leaving the page that hosts
    ///     a VR session ends that session — the Player has always done it, and
    ///     the editor's preview does it now. A preview left running behind
    ///     another page mirrors into a window nobody is looking at and keeps
    ///     the wearer's fly keys.
    ///
    /// Either way this is where the session's OWNER ends it, properly and by
    /// exactly the path `vr.end()` takes, with the viewport still alive to take
    /// its fly keys back.
    ///
    /// Installed beside the step above and cleared with it. The engine keeps
    /// its own belt (Engine::destroyScene ends a session bound to the scene it
    /// is destroying), so a host that never calls this cannot crash — it just
    /// ends the session less politely.
    virtual void setVrPreviewEnds(std::function<void()> ends) { Q_UNUSED(ends); }
    /// "A world is about to be loaded into me": raises the loading cover and
    /// PRESENTS it before returning, so it is on screen before the load blocks
    /// the thread. `title` names the world (shown under the message). A no-op
    /// for viewports with no on-screen render target.
    virtual void beginSceneLoad(const QString &title = QString()) { Q_UNUSED(title); }
    /// WHAT THE WORLD STILL OWES while it streams in, and what the viewport is
    /// therefore saying about it (SPECS/OPEN_COVER_SPEC.md §2.1/§4, lane
    /// OPEN-COVER-2b). Read by `editor.viewportState()` and by the indicator
    /// line the viewport draws in the HUD's bottom-left corner.
    ///
    /// `shaders` IS NOT A QUEUE, and cannot be: a pipeline state object is
    /// generated when a renderable is first DRAWN, so "how many are left" is
    /// not a number anything in this process knows. It is how many the LAST
    /// DRIVER FRAME built — a rate, zero as soon as a frame draws without
    /// compiling anything, which is exactly the moment the picture stops
    /// changing for that reason. `shadersThisLoad` is the count since this
    /// load began; there is deliberately NO denominator beside it, because the
    /// only number available was the PREVIOUS SESSION's whole total (boot
    /// included), which is a different quantity and read as progress.
    struct StreamingPending {
        unsigned shaders = 0;         ///< compiled by the last driver frame
        unsigned textures = 0;        ///< materials still drawing a fallback
        unsigned gi = 0;              ///< stages of the first lighting arm left
        unsigned shadersThisLoad = 0; ///< compiled since this load began
        unsigned texturesThisLoad = 0;///< the most this load has waited on at once
        /// Anything at all still arriving — `shaders || textures || gi`.
        bool any() const { return shaders || textures || gi; }
    };
    virtual StreamingPending streamingPending() const { return StreamingPending(); }
    /// WHICH COVER IS UP, as a word: "none", "loading" or "noscene". The
    /// loading cover is a preference (services/loadingcover.h) and a preference
    /// whose whole job is drawing a panel needs a reading, or its test has to
    /// photograph the screen. "noscene" is not a preference and always shows.
    virtual QString coverState() const { return QStringLiteral("none"); }
    /// HOW MANY TIMES A COVER HAS BEEN PRESENTED by this viewport, ever. The
    /// preference's whole contract is "was this load covered", and `coverState`
    /// — an instant — cannot answer it after the fact: a warm load is over
    /// before a caller polling from outside the process gets a second reading
    /// in. A caller differences this across a load instead. It counts the
    /// PRESENTS, not the raises, because a cover that was never presented was
    /// never on screen (that is the whole reason `presentCovered` exists).
    virtual qulonglong coversPresented() const { return 0; }
    /// THE INDICATOR LINE this viewport is drawing at the bottom of the frame
    /// while a world streams in, or empty when it is drawing none. The same
    /// argument as `coverState`: a line whose whole job is to be read needs a
    /// reading, or its test has to photograph the screen.
    virtual QString loadingIndicator() const { return QString(); }
    /// THE OTHER END OF beginSceneLoad (SPECS/OPEN_COVER_SPEC.md §2 A): the
    /// world is installed and the page it lives on has been switched to, so
    /// the frames from here on are frames the user can see. The engine builds
    /// a world's FIRST global-illumination arm only after this — the single
    /// longest thing it does on the UI thread, and worth nothing at all while
    /// the load is still running behind a cover.
    ///
    /// The host says it rather than the viewport inferring it: "two frames
    /// have presented" is satisfied by the open runner's own boundary frames
    /// long before the page is shown, and the cover's own state machine is
    /// torn down and rebuilt mid-load (clearScene).
    virtual void endSceneLoad() {}
    /// "Show whatever you have": re-evaluates the cover and presents it
    /// synchronously. Every route onto the editor page calls this — a page
    /// switch reveals the viewport's native window, and until the engine
    /// presents into it the X server shows whatever was there before.
    virtual void coverIfNotPresenting() {}
    /// Why this viewport's ON-SCREEN view could not be created, if it could
    /// not. Empty is the normal answer. Non-empty means the viewport fell back
    /// to an offscreen view: everything except the on-screen pixels still
    /// works, and nothing will EVER present into the widget — which is why the
    /// shell bounces the user back to the Desktop with a toast rather than
    /// leaving them on a page that can only ever be blank
    /// (MainWindow::bounceIfViewportIsDead, STATS_OVERLAY_SPEC §6.4).
    virtual QString viewCreationError() const { return QString(); }
    /// Pushes the bound document into the renderer NOW, without rendering:
    /// the mesh/material/texture uploads that would otherwise happen on the
    /// first frame after the page switch. Called while the loading page is
    /// still on screen so the editor page appears with a world already
    /// uploaded. Optional and always skippable — a viewport with no render
    /// target yet simply does nothing and lets the cover carry the wait.
    virtual void primeSceneSync() { primeSceneGeometry(); primeSceneEnvironment(); }
    /// The two halves of primeSceneSync, so a threaded open can spend them on
    /// SEPARATE event-loop turns (the window keeps pumping between them):
    /// geometry = the mesh/material/texture uploads (SceneMirror::sync), and
    /// environment = sky, world settings and the camera. Calling
    /// primeSceneSync() is exactly calling both, in this order.
    virtual void primeSceneGeometry() {}
    virtual void primeSceneEnvironment() {}
    /// The third half (SHADER_CACHE_SPEC.md §5): compile the shaders the newly
    /// bound world needs, while the loading cover is still up. The engine
    /// generates a shader per renderable on FIRST DRAW, so without this the
    /// first frames after the cover drops are the ones that stutter. Returns
    /// the number of shaders built (0 when there was nothing to do, or when
    /// this viewport has no engine). LENGTHENS a cold open by design.
    virtual unsigned warmUpShaders() { return 0; }
    /// Remembers the PASS SHAPE the editor is drawing this world with — shadows
    /// on/off and the ACHIEVED MSAA sample count — for the NEXT launch's
    /// startup gate, whose tiny offscreen warm-up view is built to match
    /// (SHADER_CACHE_AUDIT F1b): an Hlms permutation is a function of the pass
    /// as much as of the renderable, and the gate has no other way to know what
    /// pass this machine's editor draws.
    ///
    /// Two settings values, written only on change. It is what SURVIVES the
    /// recorded warm-up SET (WARMUPSET-2, 2026-09-21): the set named its
    /// materials by a process-unique datablock name and warmed nothing in the
    /// next process, so it went — the pass shape is a different fact and feeds
    /// a warm-up that does work. No-op with no engine or no world.
    virtual void rememberPassShape() {}

    // ---- lifecycle ----
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual bool isInitialized() = 0;
    virtual void cleanup() = 0;

    /// Scene-scoped teardown for a project swap: drop the document scene and
    /// everything derived from it, but KEEP the render view (native window,
    /// swapchain) alive. cleanup() remains the full teardown. Script sessions
    /// depend on this: they sit permanently on the editor page, so a view
    /// destroyed on project close/open is never recreated by a showEvent and
    /// every engine verb after project.open() used to fail.
    virtual void clearScene() { cleanup(); }
};

#endif // IEDITORVIEWPORT_H
