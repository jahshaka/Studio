#include "sceneviewwidget.h"
#include <QTimer>

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QtMath>

#include "../jah3d.h"
#include "../jah3d/scenegraph/meshnode.h"
#include "../jah3d/scenegraph/cameranode.h"
#include "../jah3d/scenegraph/lightnode.h"
#include "../jah3d/materials/defaultmaterial.h"
#include "../jah3d/graphics/forwardrenderer.h"
#include "../jah3d/graphics/mesh.h"
#include "../jah3d/graphics/texture2d.h"


SceneViewWidget::SceneViewWidget(QWidget *parent):
    QOpenGLWidget(parent)
{
    dragging = false;

    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(3);
    //format.setSamples(16);

    setFormat(format);
    installEventFilter(this);

    renderer = jah3d::ForwardRenderer::create(this);
    initialized = 2;
}

void SceneViewWidget::initialize()
{
    auto scene = jah3d::Scene::create();

    auto cam = jah3d::CameraNode::create();
    //cam->lookAt(QVector3D(0,0,0),QVect);
    scene->setCamera(cam);

    //add test object with basic material
    boxNode = jah3d::MeshNode::create();
    boxNode->setMesh("app/models/cube.obj");

    mat = jah3d::DefaultMaterial::create();
    boxNode->setMaterial(mat);
    mat->setDiffuseColor(QColor(255,200,200));
    mat->setDiffuseTexture(jah3d::Texture2D::load("app/content/textures/Artistic Pattern.png"));

    //lighting
    auto light = jah3d::LightNode::create();
    //light->setLightType(jah3d::LightType::Point);
    scene->rootNode->addChild(light);
    light->pos = QVector3D(0,3,0);


    scene->rootNode->addChild(boxNode);
    setScene(scene);

    //initialized = 0;
    initialized = -1;
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
    makeCurrent();

    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    initialize();

    auto timer = new QTimer(this);
    connect(timer,SIGNAL(timeout()),this,SLOT(update()));
    timer->start();
}

void SceneViewWidget::paintGL()
{
    float dt = 1.0f/60.0f;
    //scene->update(dt);
    renderScene();
}

void SceneViewWidget::renderScene()
{
    glViewport(0, 0, this->width(),this->height());
    //glClearColor(0.3f,0.3f,0.3f,1);
    glClearColor(1.0f,1.0f,1.0f,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(initialized==0)
        initialize();

    initialized-=1;

    //scene->render();

    if(!!renderer && !!scene)
    {
        boxNode->pos += QVector3D(0.001f,0,0);
        scene->update(1.0f/60);

        //mat->program->bind();
        //mat->program->setUniformValue("mat_diffuse",QVector3D(1,0,0));
        //mat->program->release();

        //mat->setDiffuseColor(QColor(255,200,200));
        renderer->renderScene(scene);
    }

    /*
    program->bind();
    program->enableAttributeArray(0);
    mesh->vbo->bind();

    auto stride = (3+2+3+3)*sizeof(GLfloat);
    program->setAttributeBuffer(0, GL_FLOAT, 0, 3,stride);
    this->glDrawArrays(GL_TRIANGLES,0,mesh->numFaces*3);
    */

    //this->repaint();
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
        QPointF dir = localPos-prevMousePos;
        //camera->rotate(-dir.x(),-dir.y());
        boxNode->rot *= QQuaternion::fromEulerAngles(0,dir.x(),0);
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
