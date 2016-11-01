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
}

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
    void renderSceneVr();

    QSharedPointer<jah3d::Scene> scene;

    QPointF prevMousePos;
    bool dragging;

};

#endif // SCENEVIEWWIDGET_H
