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

    /// The ACHIEVED anti-aliasing (MSAA) sample count of the viewport's render
    /// target — the driver may clamp what scene->antiAliasing requested. Only
    /// the engine viewport reports it; the legacy viewport has no MSAA (1).
    virtual int sampleCount() const { return 1; }

    /// Deterministic frame stepping for scripts and tests (editor.frame(n)):
    /// document→engine sync + renderOneFrame, n times, synchronously — the exact
    /// pattern of the headless suites. Only the engine viewport implements it;
    /// the legacy viewport repaints on its own schedule.
    virtual void renderFrames(int n) { Q_UNUSED(n); }

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
