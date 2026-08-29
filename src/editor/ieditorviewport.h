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
#include "../irisgl/src/irisglfwd.h"

class QWidget;
class MainWindow;
class Database;
class EditorData;
class btRigidBody;
enum WindowSpaces : int;      // mainwindow.h
enum class SceneMode;         // uimanager.h

enum class ViewportMode
{
    Editor,
    VR
};

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
    virtual void setDatabase(Database *db) = 0;

    // ---- document ----
    virtual void setScene(iris::ScenePtr scene) = 0;
    virtual iris::ScenePtr getScene() = 0;
    virtual void setSelectedNode(iris::SceneNodePtr sceneNode) = 0;
    virtual void clearSelectedNode() = 0;
    virtual void focusOnNode(iris::SceneNodePtr sceneNode) = 0;

    // ---- editor camera ----
    virtual iris::CameraNodePtr editorCamera() = 0;
    virtual void setEditorCamera(iris::CameraNodePtr camera) = 0;
    virtual void resetEditorCam() = 0;
    virtual void setFreeCameraMode() = 0;
    virtual void setArcBallCameraMode() = 0;
    virtual void setEditorData(EditorData *data) = 0;
    virtual EditorData *getEditorData() = 0;

    // ---- modes ----
    virtual void setViewportMode(ViewportMode mode) = 0;
    virtual ViewportMode getViewportMode() = 0;
    virtual bool isVrSupported() = 0;
    virtual void setWindowSpace(WindowSpaces windowSpace) = 0;
    virtual void setSceneMode(SceneMode sceneMode) = 0;
    virtual void enterEditorMode() = 0;
    virtual void enterPlayerMode() = 0;

    // ---- gizmos ----
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
    virtual void addBodyToWorld(btRigidBody *body, const iris::SceneNodePtr &node) = 0;
    virtual void removeBodyFromWorld(btRigidBody *body) = 0;
    virtual void removeBodyFromWorld(const QString &guid) = 0;

    // ---- overlays / output ----
    virtual bool getShowLightWires() const = 0;
    virtual void setShowLightWires(bool value) = 0;
    virtual bool getShowDebugDrawFlags() const = 0;
    virtual void setShowDebugDrawFlags(bool value) = 0;
    virtual void setShowFps(bool value) = 0;
    virtual void setShowPerspeciveLabel(bool value) = 0;
    virtual QImage takeScreenshot(int width = 1920, int height = 1080) = 0;
    virtual QImage takeScreenshot(QSize dimension) = 0;

    // ---- lifecycle ----
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual bool isInitialized() = 0;
    virtual void cleanup() = 0;

    // ---- LEGACY ONLY: retire with SceneViewWidget ----
    /// The legacy viewport needs its GL context current while Studio loads meshes and
    /// textures (SceneReader, node creation). The engine viewport needs nothing: the
    /// document is GL-free. Replaces the 40 makeCurrent()/doneCurrent() calls.
    virtual void beginResourceLoad() = 0;
    virtual void endResourceLoad() = 0;
    /// The IrisGL renderer, or null on the engine viewport. Callers must null-check.
    virtual iris::ForwardRendererPtr getRenderer() const = 0;
};

#endif // IEDITORVIEWPORT_H
