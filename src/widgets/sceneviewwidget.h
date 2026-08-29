/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEVIEWWIDGET_H
#define SCENEVIEWWIDGET_H

#include <QOpenGLBuffer>
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLWidget>
#include <QSharedPointer>

#include "irisgl/src/irisglfwd.h"
#include "irisgl/src/math/intersectionhelper.h"

#include "irisgl/src/physics/environment.h"

#include "mainwindow.h"
#include "uimanager.h"

#include "core/project.h"

namespace iris
{
    class CameraNode;
    class DefaultMaterial;
    class ForwardRenderer;
    class FullScreenQuad;
    class Mesh;
    class MeshNode;
    class Scene;
    class SceneNode;
    class Viewport;
	class ContentManager;
}

class CameraControllerBase;
class EditorCameraController;
class EditorData;
#include "../editor/ieditorviewport.h"
class EditorVrController;
class Gizmo;
class OrbitalCameraController;
class OutlinerRenderer;
class QElapsedTimer;
class QOpenGLDebugLogger;
class QOpenGLShaderProgram;
class QTimer;
class RotationGizmo;
class ScaleGizmo;
class ThumbnailGenerator;
class OutlinerRenderer;
class AnimationPath;
class TranslationGizmo;
class ViewerCameraController;
class ViewportGizmo;
class Globals;
class btRigidBody;
class HandGizmoHandler;

class PlayBack;

struct PickingResult
{
    iris::SceneNodePtr hitNode;
    QVector3D hitPoint;

    // this is often used for comparisons so it's not necessary to find the root
    float distanceFromCameraSqrd;
};

class SceneViewWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core, public IEditorViewport
{
	Q_OBJECT

	iris::ContentManagerPtr content;

    CameraControllerBase* prevCamController;
    CameraControllerBase* camController;
    EditorCameraController* defaultCam;
    OrbitalCameraController* orbitalCam;
    ViewerCameraController* viewerCam;
    EditorVrController* vrCam;

    ViewportMode viewportMode;

    QElapsedTimer* elapsedTimer;
    QTimer* timer;

    // for displaying thumbnail of viewer
    iris::CameraNodePtr viewerCamera;
    iris::RenderTargetPtr viewerRT;
    iris::Texture2DPtr viewerTex;
    iris::FullScreenQuad* viewerQuad;

    // for screenshots
    iris::RenderTargetPtr screenshotRT;
    iris::Texture2DPtr screenshotTex;

    // for rendering text
    iris::SpriteBatchPtr spriteBatch;
    iris::FontPtr font;
    float fontSize;

	// vr viewer representation
	iris::MaterialPtr viewerMat;
	iris::MeshPtr viewerMesh;

	iris::ModelPtr handGizmoModel;
	iris::DefaultMaterialPtr handGizmoMaterial;
	HandGizmoHandler* handGizmoHandler;

	PlayBack* playback;
	bool initialized;
	// The signal hub every other part of Studio connects to (IEditorViewport::events()).
	EditorViewportEvents viewportEvents;
public:
	bool showFps;

	// ---- IEditorViewport ----
	QWidget *asWidget() override { return this; }
	EditorViewportEvents *events() override { return &viewportEvents; }
	iris::CameraNodePtr editorCamera() override { return editorCam; }
	// Studio loads meshes and textures into THIS context.
	void beginResourceLoad() override { makeCurrent(); }
	void endResourceLoad() override { doneCurrent(); }

    iris::CameraNodePtr editorCam;

    MainWindow *mainWindow;
    void setMainWindow(MainWindow *window) override {
        mainWindow = window;
    }

    Database *database;
    void setDatabase(Database *window) override {
        database = database;
    }

    ThumbnailGenerator* thumbGen;
	QOpenGLDebugLogger* glDebugger;

    void dragMoveEvent(QDragMoveEvent*);
    void dropEvent(QDropEvent*);
    void dragEnterEvent(QDragEnterEvent*);
	void dragLeaveEvent(QDragLeaveEvent*);

    explicit SceneViewWidget(QWidget *parent = Q_NULLPTR);

    void setScene(iris::ScenePtr scene) override;
    iris::ScenePtr getScene() override;
    void setSelectedNode(iris::SceneNodePtr sceneNode) override;
    void clearSelectedNode() override;

	void enterEditorMode() override;
	void enterPlayerMode() override;

    void setEditorCamera(iris::CameraNodePtr camera) override;
    void resetEditorCam() override;

    // switches to the free editor camera controller
    void setFreeCameraMode() override;

    //switches to the arc ball editor camera controller
    void setArcBallCameraMode() override;
    void setCameraController();

	void focusOnNode(iris::SceneNodePtr sceneNode) override;

    bool isVrSupported() override;
    void setViewportMode(ViewportMode viewportMode) override;
    ViewportMode getViewportMode() override;

    void setGizmoTransformToLocal() override;
    void setGizmoTransformToGlobal() override;

    void addBodyToWorld(btRigidBody *body, const iris::SceneNodePtr &node) override;
    void removeBodyFromWorld(btRigidBody *body) override;
    void removeBodyFromWorld(const QString &guid) override;

    void setGizmoLoc() override;
    void setGizmoRot() override;
    void setGizmoScale() override;
	Gizmo* getActiveGizmo()
	{
		return gizmo;
	}

    void setEditorData(EditorData* data) override;
    EditorData* getEditorData() override;

	void setWindowSpace(WindowSpaces windowSpace) override;

    void startPlayingScene() override;
    void pausePlayingScene() override;
    void stopPlayingScene() override;

    iris::ForwardRendererPtr getRenderer() const override;

    QVector3D calculateMouseRay(const QPointF& pos);
	QVector3D screenSpaceToWoldSpace(const QPointF& pos, float depth);

    void mousePressEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
	void mouseDoubleClickEvent(QMouseEvent* evt);

	iris::SceneNodePtr savedActiveNode;
	iris::CustomMaterialPtr originalMaterial;
	bool wasHit = false;

    float translatePlaneD;
    QVector3D finalHitPoint;
    QVector3D Offset;
    QVector3D hit;
    QVector3D dragScenePos;
    iris::SceneNodePtr activeDragNode;
    bool updateRPI(QVector3D pos, QVector3D r);

	// forcePickable - allows picking of non isPickable() objects
    iris::SceneNodePtr doActiveObjectPicking(const QPointF& point, bool forcePickable = false);
    void doObjectPicking(
		const QPointF& point,
		iris::SceneNodePtr lastSelectedNode,
		bool selectRootObject = true,
		bool skipLights = false,
		bool skipViewers = false
	);

	QImage takeScreenshot(QSize dimension) override;
    QImage takeScreenshot(int width=1920, int height=1080) override;
    bool getShowLightWires() const override;
    void setShowLightWires(bool value) override;
	bool getShowDebugDrawFlags() const override;
	void setShowDebugDrawFlags(bool value) override;
    void toggleDebugDrawFlags(bool value);

	void startPhysicsSimulation() override;
	void restartPhysicsSimulation() override;
	void stopPhysicsSimulation() override;

    void setShowFps(bool value) override;
	void renderSelectedNode(iris::SceneNodePtr selectedNode);

	void setSceneMode(SceneMode sceneMode) override;

    void cleanup() override;
	void setShowPerspeciveLabel(bool val) override;

	void begin() override;
	void end() override;

	bool isInitialized() override { return initialized; }
	
protected:
    void initializeGL();
	void initializeOpenGLDebugger();
    bool eventFilter(QObject *obj, QEvent *event);
    void mouseReleaseEvent(QMouseEvent* event);
    void wheelEvent(QWheelEvent *event);
    void keyPressEvent(QKeyEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    void focusOutEvent(QFocusEvent* event);

    // does raycasting from the mouse's screen position.
    void doGizmoPicking(const QPointF& point);
    void setCameraController(CameraControllerBase* controller);
    void restorePreviousCameraController();

    void getMousePosAndRay(const QPointF& point, QVector3D& rayPos, QVector3D& rayDir);

private slots:
    void paintGL();
    void renderGizmos(bool once = false);
    void resizeGL(int width, int height);
	void onAnimationKeyChanged(iris::FloatKey* key);


private:
    void doLightPicking(const QVector3D& segStart,
                        const QVector3D& segEnd,
                        QList<PickingResult>& hitList);

    void doViewerPicking(const QVector3D& segStart,
                        const QVector3D& segEnd,
                        QList<PickingResult>& hitList);

    // @TODO: use one picking function and pick by mesh type
	// @forcePickable - if the scenenode has isPickable() set to false, it will
	// still be picked
    void doScenePicking(const iris::SceneNodePtr& sceneNode,
                        const QVector3D& segStart,
                        const QVector3D& segEnd,
                        QList<PickingResult>& hitList,
						bool forcePickable = false);

    void doMeshPicking(const iris::SceneNodePtr& widgetHandles,
                       const QVector3D& segStart,
                       const QVector3D& segEnd,
                       QList<PickingResult>& hitList);

    void makeObject();
    void renderScene();
	void renderCameraUi(iris::SpriteBatchPtr batch);


    iris::ScenePtr scene;
    iris::SceneNodePtr selectedNode;
    iris::ForwardRendererPtr renderer;

    QPointF prevMousePos;
    bool dragging;
    bool initialH;

    void initialize();

	Gizmo* gizmo;
    TranslationGizmo* translationGizmo;
    RotationGizmo* rotationGizmo;
    ScaleGizmo* scaleGizmo;

    QString transformMode;

    iris::Viewport* viewport;
    iris::FullScreenQuad* fsQuad;
	OutlinerRenderer* outliner;

    bool playScene;
    iris::Plane sceneFloor;
    float animTime;

    iris::MeshPtr pointLightMesh;
    iris::MeshPtr dirLightMesh;
    iris::MeshPtr spotLightMesh;
    iris::MaterialPtr lineMat;

    bool showLightWires;
	bool showDebugDrawFlags;

    void initLightAssets();
    iris::MeshPtr createDirLightMesh(float radius = 1.0);
    void addLightShapesToScene();

	void addViewerHeadsToScene();
	void addGrabGizmosToScene();

	WindowSpaces windowSpace;
	bool displayGizmos;
	bool displayLightIcons;
	bool displaySelectionOutline;

	AnimationPath* animPath;
	SettingsManager* settings;

	bool showPerspevtiveLabel;
	QString cameraView;
	QString cameraOrientation;
	QString checkView();
	int offset;

signals:
    // Legacy-only: hands the IrisGL renderer to the post-process UI. Everything else
    // (drops, selection, toolbar, sky) is emitted through events() so that callers
    // connect to the interface, not to this widget.
    void initializeGraphics(SceneViewWidget* widget,
                            QOpenGLFunctions_3_2_Core* gl);

};

#endif // SCENEVIEWWIDGET_H
