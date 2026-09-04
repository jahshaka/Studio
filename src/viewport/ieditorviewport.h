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
#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QVector3D>
#include "irisgl/irisglfwd.h"

class QWidget;
class MainWindow;
struct StudioServices;
class Database;
class Project;
class EditorData;
class Gizmo;
class ViewportCover;
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
    void addDroppedMesh(QString path, bool ignore, QVector3D position, QString guid, QString assetName);
    void addDroppedParticleSystem(bool ignore, QVector3D position, QString guid, QString assetName);
    /// A Texture asset dropped on empty space — the shell spawns an image
    /// plane at the drop point (IMAGE_PLANE_SPEC option A).
    void addDroppedImagePlane(QVector3D position, QString guid);
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);
    void updateToolbarButton();
    void changeSkyFromAssetWidget(int index);
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

    // ---- play / physics ----
    virtual void startPlayingScene() = 0;
    virtual void pausePlayingScene() = 0;
    virtual void stopPlayingScene() = 0;
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
    virtual void setShowFps(bool value) = 0;
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

    /// Deterministic frame stepping for scripts and tests (editor.frame(n)):
    /// document→engine sync + renderOneFrame, n times, synchronously — the exact
    /// pattern of the headless suites. Only the engine viewport implements it;
    /// the legacy viewport repaints on its own schedule.
    virtual void renderFrames(int n) { Q_UNUSED(n); }
    /// Steps `n` frames advancing the document's clock by exactly `dt` seconds
    /// each, instead of by however long the wall clock says. A negative `dt`
    /// means "use the wall clock" and is identical to renderFrames(n).
    virtual void renderFrames(int n, float dt) { Q_UNUSED(n); Q_UNUSED(dt); }

    // ---- the "nothing is presenting" cover (src/viewport/viewportcover.h) ----
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
    /// "A world is about to be loaded into me": puts the loading cover up and
    /// PAINTS it before returning, so the grey is on screen before the load
    /// blocks the thread. `title` names the world (shown under the message).
    /// A no-op for viewports that have no cover.
    virtual void beginSceneLoad(const QString &title = QString()) { Q_UNUSED(title); }
    /// The cover widget this viewport drives (owned by the editor page, not by
    /// the viewport). Viewports without one ignore it and stay uncovered.
    virtual void setCover(ViewportCover *) {}
    /// "Show whatever you have": re-evaluates the cover and, if it is up,
    /// paints it synchronously. Every route onto the editor page calls this —
    /// a page switch reveals the viewport's native window, and until the engine
    /// presents into it the X server shows whatever was there before.
    virtual void coverIfNotPresenting() {}
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
