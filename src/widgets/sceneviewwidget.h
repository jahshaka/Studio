#ifndef SCENEVIEWWIDGET_H
#define SCENEVIEWWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QSharedPointer>

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

enum class ViewportMode
{
    Editor,
    VR
};

struct PickingResult
{
    QSharedPointer<iris::SceneNode> hitNode;
    QSharedPointer<iris::MeshNode> hitHandle;
    QVector3D hitPoint;

    // this is often used for comparisons so it's not necessary to find the root
    float distanceFromCameraSqrd;
};

class SceneViewWidget: public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
{
    Q_OBJECT

    CameraControllerBase* camController;
    EditorCameraController* defaultCam;
    OrbitalCameraController* orbitalCam;

    ViewportMode viewportMode;
public:
    explicit SceneViewWidget(QWidget *parent);

    void setScene(QSharedPointer<iris::Scene> scene);
    void setSelectedNode(QSharedPointer<iris::SceneNode> sceneNode);
    void clearSelectedNode();

    void setEditorCamera(QSharedPointer<iris::CameraNode> camera);

    /**
     * switches to the free editor camera controller
     */
    void setFreeCameraMode();

    /**
     * switches to the arc ball editor camera controller
     */
    void setArcBallCameraMode();

    bool isVrSupported();
    void setViewportMode(ViewportMode viewportMode);
    ViewportMode getViewportMode();

protected:
    void initializeGL();
    bool eventFilter(QObject *obj, QEvent *event);
    void mousePressEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void wheelEvent(QWheelEvent *event);

    // does raycasting from the mouse's screen position.
    void doObjectPicking(const QPointF& point);
    void doGizmoPicking(const QPointF& point);

private slots:
    void paintGL();
    void updateScene();
    void resizeGL(int width, int height);

private:
    void doLightPicking(const QVector3D& segStart,const QVector3D& segEnd,QList<PickingResult>& hitList);
    // @TODO: use one picking function and pick by mesh type
    void doScenePicking(const QSharedPointer<iris::SceneNode>& sceneNode,
                        const QVector3D& segStart,
                        const QVector3D& segEnd,
                        QList<PickingResult>& hitList);

    void doMeshPicking(const QSharedPointer<iris::SceneNode>& widgetHandles,
                       const QVector3D& segStart,
                       const QVector3D& segEnd,
                       QList<PickingResult>& hitList);

    void makeObject();
    void renderScene();

    QSharedPointer<iris::CameraNode> editorCam;
    QSharedPointer<iris::Scene> scene;
    QSharedPointer<iris::SceneNode> selectedNode;
    QSharedPointer<iris::ForwardRenderer> renderer;

    QPointF prevMousePos;
    bool dragging;

    void initialize();

    QVector3D calculateMouseRay(const QPointF& pos);

    QMatrix4x4 ViewMatrix;
    QMatrix4x4 ProjMatrix;

    GizmoInstance* translationGizmo;
    RotationGizmo* rotationGizmo;
    ScaleGizmo* scaleGizmo;

    GizmoInstance* viewportGizmo;

    iris::Viewport* viewport;
    iris::FullScreenQuad* fsQuad;

signals:
    void initializeGraphics(SceneViewWidget* widget,QOpenGLFunctions_3_2_Core* gl);
    void sceneNodeSelected(QSharedPointer<iris::SceneNode> sceneNode);

};

#endif // SCENEVIEWWIDGET_H
