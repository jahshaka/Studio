#ifndef SCENEVIEWWIDGET_H
#define SCENEVIEWWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
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
}

class QOpenGLShaderProgram;

class SceneViewWidget: public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit SceneViewWidget(QWidget *parent);

    void setScene(QSharedPointer<jah3d::Scene> scene);

protected:
    void initializeGL();
    bool eventFilter(QObject *obj, QEvent *event);
    void mousePressEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);


private slots:
    void paintGL();
    void updateScene();
    void resizeGL(int width, int height);

private:
    void makeObject();
    void renderScene();

    QSharedPointer<jah3d::Scene> scene;
    QSharedPointer<jah3d::ForwardRenderer> renderer;

    QPointF prevMousePos;
    bool dragging;

    int initialized;
    void initialize();

    jah3d::Mesh* mesh;
    QOpenGLShaderProgram* program;
    QSharedPointer<jah3d::MeshNode> boxNode;
    QSharedPointer<jah3d::DefaultMaterial> mat;

};

#endif // SCENEVIEWWIDGET_H
