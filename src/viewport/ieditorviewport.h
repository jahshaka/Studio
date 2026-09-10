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
#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include "irisgl/irisglfwd.h"

class QWidget;
class MainWindow;
struct StudioServices;
class Database;
class Project;
class EditorData;
class Gizmo;
enum WindowSpaces : int;      // mainwindow.h
enum class SceneMode;         // playbackservice.h

/// Signals a viewport emits. A separate QObject so the interface itself stays a
/// plain abstract class (QOpenGLWidget and QWidget cannot both be an interface base).
class EditorViewportEvents : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void addPrimitive(QString guid);
    void addDroppedMesh(QString path, bool ignore, iris::Vec3 position, QString guid, QString assetName);
    void addDroppedParticleSystem(bool ignore, iris::Vec3 position, QString guid, QString assetName);
    /// A Texture asset dropped on empty space — the shell spawns an image
    /// plane at the drop point (IMAGE_PLANE_SPEC option A).
    void addDroppedImagePlane(iris::Vec3 position, QString guid);
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);
    void updateToolbarButton();
    /// The camera fly-speed multiplier changed from INSIDE the viewport (the
    /// scroll wheel while flying). The shell shows the toast and re-syncs the
    /// toolbar dropdown; FlySpeedSettings already holds the new value.
    void flySpeedChanged();
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
    virtual void setSelectedNode(iris::SceneNodePtr sceneNode) = 0;
    /// The whole selection SET, primary first (EDITOR_MULTISELECT_SPEC §2.3) —
    /// the outline, the gizmo group and the focus/orbit/floor union read it.
    /// Optional: a headless stand-in viewport has neither outline nor gizmo.
    virtual void setSelectedSet(const QList<iris::SceneNodePtr> &) {}
    virtual void clearSelectedNode() = 0;
    virtual void focusOnNode(iris::SceneNodePtr sceneNode) = 0;
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

    /// The fly speed changed under the viewport's feet — the wheel stepped it
    /// while the camera was flying. The viewport shows the multiplier briefly
    /// and tells the shell so the toolbar dropdown follows. FlySpeedSettings
    /// already holds the new value; this only announces it.
    virtual void onFlySpeedChanged() {}
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

    // ---- play / physics ----
    virtual void startPlayingScene() = 0;
    virtual void pausePlayingScene() = 0;
    virtual void stopPlayingScene() = 0;
    /// The viewport's OWN play flag — the one its event handlers branch on.
    /// This is the truth; PlaybackService's copy delegates here (the two flags
    /// desynced once, 2026-09-05, and every editor click went to the player
    /// controller). Verb surface: editor.playing().
    virtual bool isPlaying() const = 0;
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
    /// A screenshot that looks like the VIEWPORT rather than like a thumbnail
    /// (POST_CHAIN_SPEC.md §7.3). Screenshots render through a throwaway
    /// OFFSCREEN view, and offscreen views deliberately skip the post chain — so
    /// by default a screenshot of an HDR scene comes back ungraded and does not
    /// match what the user is looking at. `postFx` true opts that one view in.
    /// Default implementation ignores it (headless viewports have no chain).
    virtual QImage takeScreenshot(int width, int height, bool postFx) {
        (void)postFx; return takeScreenshot(width, height);
    }

    /// HOW A SCREENSHOT IS GRADED (owner report 2026-09-07, fix wave item 6).
    /// The boolean above says "the whole viewport chain or nothing", and both
    /// answers are wrong for the everyday case: `false` photographs raw linear
    /// radiance, so a bright scene clips to white; `true` drags in the scene's
    /// bloom, AO, SMAA and its ADAPTIVE exposure, which makes the shot depend on
    /// how many frames it happened to render.
    enum class ScreenshotGrade {
        Raw,       ///< no post chain at all — what every pixel suite asserts
        Tonemap,   ///< the deterministic filmic grade only (secondaryfx::apply)
        Viewport,  ///< the scene's full post chain — the old `postFx = true`
    };
    /// Default maps onto the boolean overload, so a viewport that has no chain
    /// (headless) needs no new code.
    virtual QImage takeScreenshot(int width, int height, ScreenshotGrade grade) {
        return takeScreenshot(width, height, grade == ScreenshotGrade::Viewport);
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
        QString mode;          ///< off | instant_radiosity | vct | vct_pcc_hybrid
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
        QVector3D probeRegionMin, probeRegionMax;
        /// What the probe captures RESOLVED to (P3a/P3b). Both document fields
        /// are tri-state with an "auto" that consults the quality dial, and the
        /// shadow half falls back when no shadow node exists — so the request
        /// alone never says what the reflections actually contain.
        bool probeHdr = false, probeShadows = false;
        /// How many probes the renderer re-captures per frame — the RESOLVED
        /// GI update budget (FIX WAVE B1/B2). 0 means GI is PAUSED: every
        /// reflection is frozen until world.refreshGi() asks for more. Every
        /// probe still refreshes within ceil(probeCount / this) frames.
        int  probeUpdatesPerFrame = 0;
        /// Rayon Epic's DYNAMIC PROBES, resolved: extra moved-covering probe
        /// re-captures per frame reserved on top of the budget, and how many
        /// the renderer actually spent on the last frame (0 at rest).
        int  dynamicProbes = 0, dynamicProbeUpdates = 0;
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
        /// What is feeding the probes: true = rasterised captures (world.gi's
        /// ddgiSource resolved to raster and the engine took it), false = voxel.
        bool ifdRaster = false;
    };
    virtual GiStatusInfo giStatus() const { return {}; }

    /// WHAT THE SHADOW ATLAS IS, as opposed to what the scene asked for
    /// (SPECS/SHADOW_TOOLING_SPEC.md §7) — the same reading as giStatus() and
    /// for the same reason: the renderer has a FIXED number of point/spot
    /// shadow maps and silently drops the casters that do not fit, so nothing
    /// downstream could tell "this lamp casts no shadow" from "this lamp's
    /// shadow was dropped". `available` false means there is no engine to ask.
    struct ShadowMapEntry {
        int     slot = 0;        ///< 0 = the directional/PSSM slot, 1..N the focused maps
        QString node;            ///< the light's document guid, empty when it is not ours
        bool    isStatic = false;
        bool    dirty = false;
        bool    pssm = false;
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
        qint64 atlasBytes = 0;
        QVector<ShadowMapEntry> mapped;
        QStringList unmapped;    ///< guids of casters with no map — the silent failures
        int shadowPassesLastFrame = 0;
        int staticMapRendersLastFrame = 0;
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
    /// "A world is about to be loaded into me": raises the loading cover and
    /// PRESENTS it before returning, so it is on screen before the load blocks
    /// the thread. `title` names the world (shown under the message). A no-op
    /// for viewports with no on-screen render target.
    virtual void beginSceneLoad(const QString &title = QString()) { Q_UNUSED(title); }
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
    /// Writes down which shader permutations THIS world uses, for the next
    /// launch's startup warm-up to replay (SHADER_CACHE_SPEC.md §2.7b, audit
    /// F1a). Also remembers the PASS SHAPE the editor is drawing this world
    /// with — shadows on/off and the achieved MSAA sample count — because the
    /// permutations a replay builds depend on the pass as much as on the
    /// renderable, and the startup gate has no other way to know.
    ///
    /// Called on the open path (behind the cover, once the mirror has pushed
    /// geometry and environment) and again when a world is CLOSED — the second
    /// is what the shutdown-only recording missed, and why the owner's recorded
    /// set was 203 bytes. Cheap: it walks the scene's object memory managers
    /// and touches no GPU resource. No-op with no engine or no world.
    virtual void recordWarmUpSet() {}

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
