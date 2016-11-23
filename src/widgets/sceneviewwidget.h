#ifndef SCENEVIEWWIDGET_H
#define SCENEVIEWWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QSharedPointer>

namespace jah3d
{
    class Scene;
    class ForwardRenderer;
    class Mesh;
    class SceneNode;
    class MeshNode;
    class DefaultMaterial;
    class Viewport;
    class CameraNode;
}

class EditorCameraController;
class QOpenGLShaderProgram;
class CameraControllerBase;
class OrbitalCameraController;

class SceneViewWidget: public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
{
    Q_OBJECT

    CameraControllerBase* camController;
    EditorCameraController* defaultCam;
    OrbitalCameraController* orbitalCam;
public:
    explicit SceneViewWidget(QWidget *parent);

    void setScene(QSharedPointer<jah3d::Scene> scene);
    void setSelectedNode(QSharedPointer<jah3d::SceneNode> sceneNode);

    void setEditorCamera(QSharedPointer<jah3d::CameraNode> camera);

    /**
     * switches to the free editor camera controller
     */
    void setFreeCameraMode();

    /**
     * switches to the arc ball editor camera controller
     */
    void setArcBallCameraMode();


protected:
    void initializeGL();
    bool eventFilter(QObject *obj, QEvent *event);
    void mousePressEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void wheelEvent(QWheelEvent *event);


private slots:
    void paintGL();
    void updateScene();
    void resizeGL(int width, int height);

private:
    void makeObject();
    void renderScene();

    QSharedPointer<jah3d::CameraNode> editorCam;
    QSharedPointer<jah3d::Scene> scene;
    QSharedPointer<jah3d::SceneNode> selectedNode;
    QSharedPointer<jah3d::ForwardRenderer> renderer;

    QPointF prevMousePos;
    bool dragging;

    void initialize();

    jah3d::Viewport* viewport;

signals:
    void initializeGraphics(SceneViewWidget* widget,QOpenGLFunctions_3_2_Core* gl);


};

#endif // SCENEVIEWWIDGET_H
