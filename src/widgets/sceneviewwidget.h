/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

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

#include "irisgl/src/bullet3/src/btBulletDynamicsCommon.h" 

#include "irisgl/src/irisglfwd.h"
#include "irisgl/src/math/intersectionhelper.h"
#include "irisgl/src/physics/physicsproperties.h"

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

class btRigidBody;

enum class ViewportMode
{
    Editor,
    VR
};

struct PickingResult
{
    iris::SceneNodePtr hitNode;
    QVector3D hitPoint;

    // this is often used for comparisons so it's not necessary to find the root
    float distanceFromCameraSqrd;
};

class SceneViewWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
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
    bool showFps;

	// vr viewer representation
	iris::MaterialPtr viewerMat;
	iris::MeshPtr viewerMesh;
public:
    iris::CameraNodePtr editorCam;

    MainWindow *mainWindow;
    void setMainWindow(MainWindow *window) {
        mainWindow = window;
    }

    btRigidBody *activeRigidBody;

    //testing
    //class btTypedConstraint* m_pickedConstraint;
    class btGeneric6DofConstraint* m_pickedConstraint;
    int	m_savedState;
    btVector3 m_oldPickingPos;
    btVector3 m_hitPos;
    btScalar m_oldPickingDist;

    ThumbnailGenerator* thumbGen;
	QOpenGLDebugLogger* glDebugger;

    void dragMoveEvent(QDragMoveEvent*);
    void dropEvent(QDropEvent*);
    void dragEnterEvent(QDragEnterEvent*);

    explicit SceneViewWidget(QWidget *parent = Q_NULLPTR);

    void setScene(iris::ScenePtr scene);
    iris::ScenePtr getScene();
    void setSelectedNode(iris::SceneNodePtr sceneNode);
    void clearSelectedNode();

	void enterEditorMode();
	void enterPlayerMode();

    void setEditorCamera(iris::CameraNodePtr camera);
    void resetEditorCam();

    // switches to the free editor camera controller
    void setFreeCameraMode();

    //switches to the arc ball editor camera controller
    void setArcBallCameraMode();
    void setCameraController();

	void focusOnNode(iris::SceneNodePtr sceneNode);

    bool isVrSupported();
    void setViewportMode(ViewportMode viewportMode);
    ViewportMode getViewportMode();

    void setGizmoTransformToLocal();
    void setGizmoTransformToGlobal();

    void addBodyToWorld(btRigidBody *body, const iris::SceneNodePtr &node);
    void removeBodyFromWorld(btRigidBody *body);
    void removeBodyFromWorld(const QString &guid);

    void addConstraintToWorldFromProperty(const iris::ConstraintProperty &prop);

    void setGizmoLoc();
    void setGizmoRot();
    void setGizmoScale();
	Gizmo* getActiveGizmo()
	{
		return gizmo;
	}

    void setEditorData(EditorData* data);
    EditorData* getEditorData();

	void setWindowSpace(WindowSpaces windowSpace);

    void startPlayingScene();
    void pausePlayingScene();
    void stopPlayingScene();

    iris::ForwardRendererPtr getRenderer() const;

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
    iris::SceneNodePtr doActiveObjectPicking(const QPointF& point);
    void doObjectPicking(
		const QPointF& point,
		iris::SceneNodePtr lastSelectedNode,
		bool selectRootObject = true,
		bool skipLights = false,
		bool skipViewers = false
	);

	QImage takeScreenshot(QSize dimension);
    QImage takeScreenshot(int width=1920, int height=1080);
    bool getShowLightWires() const;
    void setShowLightWires(bool value);
    void toggleDebugDrawFlags(bool value);

	void startPhysicsSimulation();
	void restartPhysicsSimulation();
	void stopPhysicsSimulation();

    void setShowFps(bool value);
	void renderSelectedNode(iris::SceneNodePtr selectedNode);

	void setSceneMode(SceneMode sceneMode);

    void cleanup();
	void setShowPerspeciveLabel(bool val);
	
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
    void doScenePicking(const iris::SceneNodePtr& sceneNode,
                        const QVector3D& segStart,
                        const QVector3D& segEnd,
                        QList<PickingResult>& hitList);

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

    void initLightAssets();
    iris::MeshPtr createDirLightMesh(float radius = 1.0);
    void addLightShapesToScene();

	void addViewerHeadsToScene();

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
    void addPrimitive(QString guid);
    void addDroppedMesh(QString path, bool ignore, QVector3D position, QString guid, QString assetName);
    void addDroppedParticleSystem(bool ignore, QVector3D position, QString guid, QString assetName);
    void initializeGraphics(SceneViewWidget* widget,
                            QOpenGLFunctions_3_2_Core* gl);
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);
	void updateToolbarButton();

};

#endif // SCENEVIEWWIDGET_H
