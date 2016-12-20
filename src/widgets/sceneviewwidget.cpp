#include "sceneviewwidget.h"
#include <QTimer>

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QtMath>
#include <QDebug>

#include "../jah3d.h"
#include "../jah3d/scenegraph/meshnode.h"
#include "../jah3d/scenegraph/cameranode.h"
#include "../jah3d/scenegraph/lightnode.h"
#include "../jah3d/materials/defaultmaterial.h"
#include "../jah3d/graphics/forwardrenderer.h"
#include "../jah3d/graphics/mesh.h"
#include "../jah3d/geometry/trimesh.h"
#include "../jah3d/graphics/texture2d.h"
#include "../jah3d/graphics/viewport.h"
#include "../jah3d/graphics/utils/fullscreenquad.h"


#include "../editor/cameracontrollerbase.h"
#include "../editor/editorcameracontroller.h"
#include "../editor/orbitalcameracontroller.h"


SceneViewWidget::SceneViewWidget(QWidget *parent):
    QOpenGLWidget(parent)
{
    dragging = false;

    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    //format.setMajorVersion(3);
    //format.setMinorVersion(3);
    format.setSamples(0);

    setFormat(format);
    setMouseTracking(true);
    //installEventFilter(this);

    viewport = new jah3d::Viewport();

    //camController = nullptr;
    defaultCam = new EditorCameraController();
    orbitalCam = new OrbitalCameraController();
    camController = defaultCam;
    //camController = orbitalCam;

    editorCam = jah3d::CameraNode::create();
    editorCam->pos = QVector3D(0,5,13);
    //editorCam->pos = QVector3D(0,0,10);
    editorCam->rot = QQuaternion::fromEulerAngles(-15,0,0);
    camController->setCamera(editorCam);

    viewportMode = ViewportMode::Editor;
}

void SceneViewWidget::initialize()
{

    /*
    auto scene = jah3d::Scene::create();

    auto cam = jah3d::CameraNode::create();
    cam->pos = QVector3D(0,5,5);
    cam->rot = QQuaternion::fromEulerAngles(-45,0,0);
    //cam->lookAt(QVector3D(0,0,0),QVect);
    scene->setCamera(cam);
    scene->rootNode->addChild(cam);

    //second node
    auto node = jah3d::MeshNode::create();
    //boxNode->setMesh("app/models/head.obj");
    node->setMesh("app/models/plane.obj");
    node->scale = QVector3D(100,1,100);

    auto m = jah3d::DefaultMaterial::create();
    node->setMaterial(m);
    m->setDiffuseColor(QColor(255,255,255));
    m->setDiffuseTexture(jah3d::Texture2D::load("app/content/textures/defaultgrid.png"));
    m->setTextureScale(100);
    scene->rootNode->addChild(node);

    //add test object with basic material
    boxNode = jah3d::MeshNode::create();
    //boxNode->setMesh("app/models/head.obj");
    //boxNode->setMesh("app/models/box.obj");
    boxNode->setMesh("assets/models/StanfordDragon.obj");

    mat = jah3d::DefaultMaterial::create();
    boxNode->setMaterial(mat);
    mat->setDiffuseColor(QColor(255,200,200));
    mat->setDiffuseTexture(jah3d::Texture2D::load("app/content/textures/Artistic Pattern.png"));

    //lighting
    auto light = jah3d::LightNode::create();
    light->setLightType(jah3d::LightType::Point);
    light->rot = QQuaternion::fromEulerAngles(45,0,0);
    scene->rootNode->addChild(light);
    //light->pos = QVector3D(5,5,0);
    light->pos = QVector3D(-5,5,3);
    light->intensity = 1;
    light->icon = jah3d::Texture2D::load("app/icons/bulb.png");


    scene->rootNode->addChild(boxNode);
    //scene->rootNode->addChild(node);
    setScene(scene);
    */

    //camController = new EditorCameraController(cam);
}

void SceneViewWidget::setScene(QSharedPointer<jah3d::Scene> scene)
{
    this->scene = scene;
    scene->setCamera(editorCam);
    renderer->setScene(scene);

    //remove selected scenenode
    selectedNode.reset();
}

void SceneViewWidget::setSelectedNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    selectedNode = sceneNode;
    renderer->setSelectedSceneNode(sceneNode);
}

void SceneViewWidget::clearSelectedNode()
{
    selectedNode.clear();
    renderer->setSelectedSceneNode(selectedNode);
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

    renderer = jah3d::ForwardRenderer::create(this);

    initialize();
    fsQuad = new jah3d::FullScreenQuad();

    emit initializeGraphics(this,this);

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
    //glViewport(0, 0, this->width(),this->height());
    glClearColor(0.3f,0.3f,0.3f,1);
    //glClearColor(1.0f,1.0f,1.0f,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(!!renderer && !!scene)
    {
        scene->update(1.0f/60);

        if(viewportMode==ViewportMode::Editor)
            renderer->renderScene(this->context(),viewport);
        else
            renderer->renderSceneVr(this->context(),viewport);
    }
}

void SceneViewWidget::resizeGL(int width, int height)
{
    glViewport(0,0,width,height);
    //camera->updateView(width,height);
    viewport->width = width;
    viewport->height = height;
}


bool SceneViewWidget::eventFilter(QObject *obj, QEvent *event)
{
    QEvent::Type type = event->type();
    if(type == QEvent::MouseMove)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mouseMoveEvent(evt);
        return false;
    }
    else if(type == QEvent::MouseButtonPress)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mousePressEvent(evt);
        return false;
    }
    else if( type == QEvent::MouseButtonRelease)
    {
        QMouseEvent* evt = static_cast<QMouseEvent*>(event);
        mouseReleaseEvent(evt);
        return false;
    }

    return QWidget::eventFilter(obj,event);
}

void SceneViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    //issue - only fired when mouse is dragged
    QPointF localPos = e->localPos();

    QPointF dir = localPos-prevMousePos;

    if(dragging)
    {

        //camera->rotate(-dir.x(),-dir.y());
        //if(!!boxNode)
        //    boxNode->rot *= QQuaternion::fromEulerAngles(0,dir.x(),0);
    }

    if(camController!=nullptr)
        camController->onMouseMove(-dir.x(),-dir.y());

    prevMousePos = localPos;

    //qDebug()<<prevMousePos;
    //doObjectPicking();
}

void SceneViewWidget::mousePressEvent(QMouseEvent *e)
{
    prevMousePos = e->localPos();
    if(e->button()== Qt::RightButton)
    {
        dragging = true;
    }

    if(e->button()== Qt::LeftButton)
    {
        this->doObjectPicking();
    }

    if(camController!=nullptr)
        camController->onMouseDown(e->button());
}

void SceneViewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if(e->button()== Qt::RightButton)
    {
        dragging = false;
    }

    if(camController!=nullptr)
        camController->onMouseUp(e->button());
}

void SceneViewWidget::wheelEvent(QWheelEvent *event)
{
    //qDebug()<<"wheel event"<<endl;
    if(camController!=nullptr)
        camController->onMouseWheel(event->delta());
}

void SceneViewWidget::doObjectPicking()
{
    editorCam->updateCameraMatrices();

    auto segStart = this->editorCam->pos;
    auto rayDir = editorCam->calculatePickingDirection(viewport->width,viewport->height,prevMousePos);
    auto segEnd = segStart+(rayDir*1000);//todo: remove magic number

    QList<PickingResult> hitList;
    doPicking(scene->getRootNode(),segStart,segEnd,hitList);

    //Find the closest hit and emit signal
    if(hitList.size()==0)
    {
        emit sceneNodeSelected(jah3d::SceneNodePtr());//no hits, deselect object
        return;
    }

    if(hitList.size()==1)
    {
        emit sceneNodeSelected(hitList[0].hitNode);
        return;
    }

    //sort by distance to camera then return
    qSort(hitList.begin(),hitList.end(),[](const PickingResult& a,const PickingResult& b)
    {
        return a.distanceFromCameraSqrd>b.distanceFromCameraSqrd;
    });

    emit sceneNodeSelected(hitList.last().hitNode);
}

void SceneViewWidget::doPicking(const QSharedPointer<jah3d::SceneNode>& sceneNode,const QVector3D& segStart,const QVector3D& segEnd,QList<PickingResult>& hitList)
{

    if(sceneNode->getSceneNodeType()==jah3d::SceneNodeType::Mesh)
    {
        auto meshNode = sceneNode.staticCast<jah3d::MeshNode>();
        auto triMesh = meshNode->getMesh()->getTriMesh();

        //transform segment to local space
        auto invTransform = meshNode->globalTransform.inverted();
        auto a = invTransform*segStart;
        auto b = invTransform*segEnd;

        //auto dist = a.distanceToPoint(b);
        //qDebug()<<"segment distance: " <<dist;

        //if(triMesh->isHitBySegment(segStart,segEnd))
        /*
        QVector3D hitPoint;
        if(triMesh->isHitBySegment(a,b,hitPoint))
        {
            //return sceneNode;
            hitList.append({
                            sceneNode,
                            hitPoint
                        });
        }
        */

        QList<jah3d::TriangleIntersectionResult> results;
        if(int resultCount = triMesh->getSegmentIntersections(a,b,results))
        {
            for(auto triResult:results)
            {
                //convert hit to world space
                auto hitPoint = meshNode->globalTransform*triResult.hitPoint;
                /*
                auto dist = segStart.distanceToPoint(segEnd);
                qDebug()<<"start: "<<segStart;
                qDebug()<<"end: "<<segEnd;
                qDebug()<<"t: "<<triResult.t;
                qDebug()<<"segment distance: " <<dist;
                auto hitPoint = segStart + (segEnd-segStart)*
                        triResult.t;
                qDebug()<<sceneNode->name<<" hit: " <<hitPoint;

                qDebug()<<"cam pos: "<<editorCam->getGlobalPosition();
                */
                PickingResult pick;
                pick.hitNode = sceneNode;
                pick.hitPoint = hitPoint;
                pick.distanceFromCameraSqrd = (hitPoint-editorCam->getGlobalPosition()).lengthSquared();

                hitList.append(pick);
            }
        }
    }

    for(auto child:sceneNode->children)
    {
        doPicking(child,segStart,segEnd,hitList);
    }
}

void SceneViewWidget::setFreeCameraMode()
{
    camController = defaultCam;
    camController->setCamera(editorCam);
    camController->resetMouseStates();
}

void SceneViewWidget::setArcBallCameraMode()
{
    camController = orbitalCam;
    camController->setCamera(editorCam);
    camController->resetMouseStates();
}

bool SceneViewWidget::isVrSupported()
{
    return renderer->isVrSupported();
}

void SceneViewWidget::setViewportMode(ViewportMode viewportMode)
{
    this->viewportMode = viewportMode;
}

ViewportMode SceneViewWidget::getViewportMode()
{
    return viewportMode;
}
