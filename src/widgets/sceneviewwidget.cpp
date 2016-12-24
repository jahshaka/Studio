#include "sceneviewwidget.h"
#include <QTimer>

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QtMath>
#include <QDebug>

#include "../irisgl/src/irisgl.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/materials/defaultmaterial.h"
#include "../irisgl/src/graphics/forwardrenderer.h"
#include "../irisgl/src/graphics/mesh.h"
#include "../irisgl/src/geometry/trimesh.h"
#include "../irisgl/src/graphics/texture2d.h"
#include "../irisgl/src/graphics/viewport.h"
#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/graphics/utils/fullscreenquad.h"
#include "../irisgl/src/math/intersectionhelper.h"


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
    format.setSamples(1);

    setFormat(format);
    setMouseTracking(true);
    //installEventFilter(this);

    viewport = new iris::Viewport();

    //camController = nullptr;
    defaultCam = new EditorCameraController();
    orbitalCam = new OrbitalCameraController();
    camController = defaultCam;
    //camController = orbitalCam;

    editorCam = iris::CameraNode::create();
    editorCam->pos = QVector3D(0,5,13);
    //editorCam->pos = QVector3D(0,0,10);
    editorCam->rot = QQuaternion::fromEulerAngles(-15,0,0);
    camController->setCamera(editorCam);

    viewportMode = ViewportMode::Editor;
}

void SceneViewWidget::initialize()
{

    /*
    auto scene = iris::Scene::create();

    auto cam = iris::CameraNode::create();
    cam->pos = QVector3D(0,5,5);
    cam->rot = QQuaternion::fromEulerAngles(-45,0,0);
    //cam->lookAt(QVector3D(0,0,0),QVect);
    scene->setCamera(cam);
    scene->rootNode->addChild(cam);

    //second node
    auto node = iris::MeshNode::create();
    //boxNode->setMesh("app/models/head.obj");
    node->setMesh("app/models/plane.obj");
    node->scale = QVector3D(100,1,100);

    auto m = iris::DefaultMaterial::create();
    node->setMaterial(m);
    m->setDiffuseColor(QColor(255,255,255));
    m->setDiffuseTexture(iris::Texture2D::load("app/content/textures/defaultgrid.png"));
    m->setTextureScale(100);
    scene->rootNode->addChild(node);

    //add test object with basic material
    boxNode = iris::MeshNode::create();
    //boxNode->setMesh("app/models/head.obj");
    //boxNode->setMesh("app/models/box.obj");
    boxNode->setMesh("assets/models/StanfordDragon.obj");

    mat = iris::DefaultMaterial::create();
    boxNode->setMaterial(mat);
    mat->setDiffuseColor(QColor(255,200,200));
    mat->setDiffuseTexture(iris::Texture2D::load("app/content/textures/Artistic Pattern.png"));

    //lighting
    auto light = iris::LightNode::create();
    light->setLightType(iris::LightType::Point);
    light->rot = QQuaternion::fromEulerAngles(45,0,0);
    scene->rootNode->addChild(light);
    //light->pos = QVector3D(5,5,0);
    light->pos = QVector3D(-5,5,3);
    light->intensity = 1;
    light->icon = iris::Texture2D::load("app/icons/bulb.png");


    scene->rootNode->addChild(boxNode);
    //scene->rootNode->addChild(node);
    setScene(scene);
    */

    //camController = new EditorCameraController(cam);
}

void SceneViewWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    this->scene = scene;
    scene->setCamera(editorCam);
    renderer->setScene(scene);

    //remove selected scenenode
    selectedNode.reset();
}

void SceneViewWidget::setSelectedNode(QSharedPointer<iris::SceneNode> sceneNode)
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

    renderer = iris::ForwardRenderer::create(this);

    initialize();
    fsQuad = new iris::FullScreenQuad();

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

    QMatrix4x4 view,proj;

    if(!!renderer && !!scene)
    {
        scene->update(1.0f/60);

        if(viewportMode==ViewportMode::Editor)
            renderer->renderScene(this->context(),viewport,view,proj);
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
    doMeshPicking(scene->getRootNode(),segStart,segEnd,hitList);
    doLightPicking(segStart,segEnd,hitList);

    //Find the closest hit and emit signal
    if(hitList.size()==0)
    {
        emit sceneNodeSelected(iris::SceneNodePtr());//no hits, deselect object
        return;
    }

    //sort by distance to camera then return
    qSort(hitList.begin(),hitList.end(),[](const PickingResult& a,const PickingResult& b)
    {
        return a.distanceFromCameraSqrd>b.distanceFromCameraSqrd;
    });

    emit sceneNodeSelected(hitList.last().hitNode);
}

void SceneViewWidget::doMeshPicking(const QSharedPointer<iris::SceneNode>& sceneNode,const QVector3D& segStart,const QVector3D& segEnd,QList<PickingResult>& hitList)
{

    if(sceneNode->getSceneNodeType()==iris::SceneNodeType::Mesh)
    {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();
        auto triMesh = meshNode->getMesh()->getTriMesh();

        //transform segment to local space
        auto invTransform = meshNode->globalTransform.inverted();
        auto a = invTransform*segStart;
        auto b = invTransform*segEnd;


        QList<iris::TriangleIntersectionResult> results;
        if(int resultCount = triMesh->getSegmentIntersections(a,b,results))
        {
            for(auto triResult:results)
            {
                //convert hit to world space
                auto hitPoint = meshNode->globalTransform*triResult.hitPoint;

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
        doMeshPicking(child,segStart,segEnd,hitList);
    }
}

void SceneViewWidget::doLightPicking(const QVector3D& segStart,const QVector3D& segEnd,QList<PickingResult>& hitList)
{
    const float lightRadius = 0.5f;

    auto rayDir = (segEnd-segStart);
    float segLengthSqrd = rayDir.lengthSquared();
    rayDir.normalize();
    QVector3D hitPoint;
    float t;

    for(auto light:scene->lights)
    {
        if(iris::IntersectionHelper::raySphereIntersects(segStart,rayDir,light->pos,lightRadius,t,hitPoint))
        {
            PickingResult pick;
            pick.hitNode = light.staticCast<iris::SceneNode>();
            pick.hitPoint = hitPoint;
            pick.distanceFromCameraSqrd = (hitPoint-editorCam->getGlobalPosition()).lengthSquared();

            hitList.append(pick);
        }
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
