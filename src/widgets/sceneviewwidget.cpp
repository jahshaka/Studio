#include "sceneviewwidget.h"
#include <QTimer>

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QtMath>

#include "../jah3d.h"

SceneViewWidget::SceneViewWidget(QWidget *parent):
    QOpenGLWidget(parent)
{
    dragging = false;

    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(0);
    //format.setSamples(16);

    setFormat(format);
    installEventFilter(this);
}

void SceneViewWidget::setScene(QSharedPointer<jah3d::Scene> scene)
{
    this->scene = scene;
}

void SceneViewWidget::updateScene()
{

}

void SceneViewWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    auto timer = new QTimer(this);
    connect(timer,SIGNAL(timeout()),this,SLOT(update()));
    timer->start(16);//60fps
}

void SceneViewWidget::paintGL()
{
    if(!!scene)
    {
        float dt = 1.0f/60.0f;
        scene->update(dt);
        renderScene();
    }
}

void SceneViewWidget::renderScene()
{
    glViewport(0, 0, this->width(),this->height());
    glClearColor(0.3f,0.3f,0.3f,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    scene->render();
}

void SceneViewWidget::resizeGL(int width, int height)
{
    glViewport(0,0,width,height);
    //camera->updateView(width,height);
}


bool SceneViewWidget::eventFilter(QObject *obj, QEvent *event)
{
    QEvent::Type type = event->type();
    if(type == QEvent::MouseMove)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mouseMoveEvent(evt);
        return true;
    }
    else if(type == QEvent::MouseButtonPress)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mousePressEvent(evt);
        return true;
    }
    else if( type == QEvent::MouseButtonRelease)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mouseReleaseEvent(evt);
        return true;
    }

    return QWidget::eventFilter(obj,event);
}

void SceneViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    //issue - only fired when mouse is dragged
    QPointF localPos = e->windowPos();

    if(dragging)
    {
        //QPointF dir = localPos-prevMousePos;
        //camera->rotate(-dir.x(),-dir.y());
    }

    prevMousePos = localPos;
}

void SceneViewWidget::mousePressEvent(QMouseEvent *e)
{
    prevMousePos = e->windowPos();
    if(e->button()== Qt::RightButton)
    {
        dragging = true;
    }
}

void SceneViewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if(e->button()== Qt::RightButton)
    {
        dragging = false;
    }
}
