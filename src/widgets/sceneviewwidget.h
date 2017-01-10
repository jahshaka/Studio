#ifndef SCENEVIEWWIDGET_H
#define SCENEVIEWWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QSharedPointer>
#include <QHash>

#include "../irisgl.h"

namespace iris
{
    class Scene;
    class ForwardRenderer;
    class Mesh;
    class SceneNode;
    class MeshNode;
    class DefaultMaterial;
    class Viewport;
    class CameraNode;
    class FullScreenQuad;
}

class EditorCameraController;
class QOpenGLShaderProgram;
class CameraControllerBase;
class OrbitalCameraController;

class GizmoInstance;
class ViewportGizmo;
class TranslationGizmo;
class RotationGizmo;
class ScaleGizmo;

class EditorData;

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

class SceneViewWidget : public QOpenGLWidget,
                        protected QOpenGLFunctions_3_2_Core
{
    Q_OBJECT

    CameraControllerBase* camController;
    EditorCameraController* defaultCam;
    OrbitalCameraController* orbitalCam;

    ViewportMode viewportMode;
public:
    iris::CameraNodePtr editorCam;

    explicit SceneViewWidget(QWidget *parent);

    void setScene(iris::ScenePtr scene);
    void setSelectedNode(iris::SceneNodePtr sceneNode);
    void clearSelectedNode();

    void setEditorCamera(iris::CameraNodePtr camera);

    // switches to the free editor camera controller
    void setFreeCameraMode();

    //switches to the arc ball editor camera controller
    void setArcBallCameraMode();

    bool isVrSupported();
    void setViewportMode(ViewportMode viewportMode);
    ViewportMode getViewportMode();

    void setTransformOrientationLocal();
    void setTransformOrientationGlobal();

    void setGizmoLoc();
    void setGizmoRot();
    void setGizmoScale();

    void setEditorData(EditorData* data);
    EditorData* getEditorData();

protected:
    void initializeGL();
    bool eventFilter(QObject *obj, QEvent *event);
    void mousePressEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void wheelEvent(QWheelEvent *event);
    void keyPressEvent(QKeyEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    void focusOutEvent(QFocusEvent* event);

    // does raycasting from the mouse's screen position.
    void doObjectPicking(const QPointF& point);
    void doGizmoPicking(const QPointF& point);

private slots:
    void paintGL();
    void updateScene(bool once = false);
    void resizeGL(int width, int height);

private:
    void doLightPicking(const QVector3D& segStart,
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


    iris::ScenePtr scene;
    iris::SceneNodePtr selectedNode;
    iris::ForwardRendererPtr renderer;

    QPointF prevMousePos;
    bool dragging;
    bool initialH;

    void initialize();

    QVector3D calculateMouseRay(const QPointF& pos);

    QMatrix4x4 ViewMatrix;
    QMatrix4x4 ProjMatrix;

    GizmoInstance* translationGizmo;
    RotationGizmo* rotationGizmo;
    ScaleGizmo* scaleGizmo;

    GizmoInstance* viewportGizmo;
    QString transformMode;

    iris::Viewport* viewport;
    iris::FullScreenQuad* fsQuad;

signals:
    void initializeGraphics(SceneViewWidget* widget,
                            QOpenGLFunctions_3_2_Core* gl);
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);

};

#endif // SCENEVIEWWIDGET_H
