/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "sceneviewwidget.h"
#include <QTimer>

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QtMath>
#include <QDebug>
#include <QMimeData>
#include <QElapsedTimer>

#include "../irisgl/src/irisgl.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/scenegraph/viewernode.h"
#include "../irisgl/src/materials/defaultmaterial.h"
#include "../irisgl/src/graphics/forwardrenderer.h"
#include "../irisgl/src/graphics/mesh.h"
#include "../irisgl/src/geometry/trimesh.h"
#include "../irisgl/src/graphics/texture2d.h"
#include "../irisgl/src/graphics/viewport.h"
#include "../irisgl/src/graphics/renderlist.h"
#include "../irisgl/src/graphics/utils/fullscreenquad.h"
#include "../irisgl/src/vr/vrmanager.h"
#include "../irisgl/src/vr/vrdevice.h"

#include "../editor/cameracontrollerbase.h"
#include "../editor/editorcameracontroller.h"
#include "../editor/orbitalcameracontroller.h"
#include "../editor/editorvrcontroller.h"

#include "../editor/editordata.h"

#include "../editor/translationgizmo.h"
#include "../editor/rotationgizmo.h"
#include "../editor/scalegizmo.h"

#include "../core/keyboardstate.h"

#include "../irisgl/src/graphics/utils/shapehelper.h"
#include "../irisgl/src/materials/colormaterial.h"

SceneViewWidget::SceneViewWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    dragging = false;

    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    //format.setOption();
    format.setSamples(1);

    setFormat(format);
    // needed in order to get mouse events
    setMouseTracking(true);
    // needed in order to get key events
    // http://stackoverflow.com/a/7879484/991834
    setFocusPolicy(Qt::ClickFocus);

    viewport = new iris::Viewport();

    //camController = nullptr;
    defaultCam = new EditorCameraController();
    orbitalCam = new OrbitalCameraController();

    camController = defaultCam;
    prevCamController = defaultCam;
    //camController = orbitalCam;

    editorCam = iris::CameraNode::create();
    editorCam->setLocalPos(QVector3D(0, 5, 14));
    editorCam->setLocalRot(QQuaternion::fromEulerAngles(-5, 0, 0));
    camController->setCamera(editorCam);

    viewportMode = ViewportMode::Editor;

    elapsedTimer = new QElapsedTimer();
    playScene = false;
    animTime = 0.0f;

    sceneFloor = iris::IntersectionHelper::computePlaneND(QVector3D( 100, 0,  100),
                                                          QVector3D(-100, 0,  100),
                                                          QVector3D(   0, 0, -100));

    setAcceptDrops(true);
}

void SceneViewWidget::resetEditorCam()
{
    editorCam->setLocalPos(QVector3D(0, 5, 14));
    editorCam->setLocalRot(QQuaternion::fromEulerAngles(-5, 0, 0));
    camController->setCamera(editorCam);
}

void SceneViewWidget::initialize()
{
    initialH = true;

    translationGizmo = new TranslationGizmo(editorCam);
    translationGizmo->createHandleShader();

    rotationGizmo = new RotationGizmo(editorCam);
    rotationGizmo->createHandleShader();

    scaleGizmo = new ScaleGizmo(editorCam);
    scaleGizmo->createHandleShader();

    viewportGizmo = translationGizmo;
    transformMode = "Global";

    // has to be initialized here since it loads assets
    vrCam = new EditorVrController();

    initLightAssets();
}

void SceneViewWidget::initLightAssets()
{
    pointLightMesh = iris::ShapeHelper::createWireSphere(1.0f);
    lineMat = iris::ColorMaterial::create();
}

iris::MeshPtr SceneViewWidget::createDirLightMesh()
{
    return iris::MeshPtr();
}

void SceneViewWidget::addLightShapesToScene()
{
    QMatrix4x4 mat;

    for(auto light : scene->lights) {
        if ( light->lightType == iris::LightType::Point) {
            mat.setToIdentity();
            mat.translate(light->getGlobalPosition());
            mat.scale(light->distance);
            scene->geometryRenderList->submitMesh(pointLightMesh, lineMat, mat);
        }
    }
}

void SceneViewWidget::setScene(iris::ScenePtr scene)
{
    this->scene = scene;
    scene->setCamera(editorCam);
    renderer->setScene(scene);
    vrCam->setScene(scene);

    // remove selected scenenode
    selectedNode.reset();
}

void SceneViewWidget::setSelectedNode(iris::SceneNodePtr sceneNode)
{
    selectedNode = sceneNode;
    renderer->setSelectedSceneNode(sceneNode);

    if (viewportGizmo != nullptr) {
        viewportGizmo->setLastSelectedNode(sceneNode);
    }
}

void SceneViewWidget::clearSelectedNode()
{
    selectedNode.clear();
    renderer->setSelectedSceneNode(selectedNode);
}

void SceneViewWidget::updateScene(bool once)
{
    // update and draw the 3d manipulation gizmo
    if (!!viewportGizmo->getLastSelectedNode()) {
        if (!viewportGizmo->getLastSelectedNode()->isRootNode()) {
            viewportGizmo->updateTransforms(editorCam->getGlobalPosition());
            if (viewportMode != ViewportMode::VR && UiManager::sceneMode == SceneMode::EditMode) {
                viewportGizmo->render(editorCam->viewMatrix, editorCam->projMatrix);
            }
        }
    }
}

void SceneViewWidget::initializeGL()
{
    makeCurrent();

    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    renderer = iris::ForwardRenderer::create();

    initialize();
    fsQuad = new iris::FullScreenQuad();

    emit initializeGraphics(this, this);

    auto timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start();

    this->elapsedTimer->start();
}

void SceneViewWidget::paintGL()
{
    makeCurrent();

    if(iris::VrManager::getDefaultDevice()->isHeadMounted() &&
            viewportMode != ViewportMode::VR)
    {
        // set to vr mode automatically if a headset is detected
        this->setViewportMode(ViewportMode::VR);
    }
    else if(!iris::VrManager::getDefaultDevice()->isHeadMounted() &&
            viewportMode == ViewportMode::VR)
    {
        this->setViewportMode(ViewportMode::Editor);
    }

    renderScene();
}

void SceneViewWidget::renderScene()
{
    glClearColor(.3f, .3f, .3f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float dt = elapsedTimer->nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
    elapsedTimer->restart();

    if (!!renderer && !!scene) {

        this->camController->update(dt);
        if(playScene)
        {
            animTime += dt;
            scene->updateSceneAnimation(animTime);
        }

        scene->update(dt);

        // insert vr head
        if (UiManager::sceneMode != SceneMode::PlayMode || viewportMode != ViewportMode::VR) {
            for (auto view : scene->viewers)
                view->submitRenderItems();
        }
        // todo: ensure it doesnt display these shapes in play mode
        addLightShapesToScene();

        if (viewportMode == ViewportMode::Editor) {
            renderer->renderScene(dt, viewport);
        } else {
            renderer->renderSceneVr(dt, viewport, UiManager::sceneMode == SceneMode::PlayMode);
        }

        this->updateScene();
    }
}

void SceneViewWidget::resizeGL(int width, int height)
{
    // we do an explicit call to glViewport(...) in forwardrenderer
    // with the "good DPI" values so it is not needed here initially
    viewport->pixelRatioScale = devicePixelRatio();
    viewport->width = width;
    viewport->height = height;
}

bool SceneViewWidget::eventFilter(QObject *obj, QEvent *event)
{
    return QWidget::eventFilter(obj, event);
}

QVector3D SceneViewWidget::calculateMouseRay(const QPointF& pos)
{
    float x = pos.x();
    float y = pos.y();

    // viewport -> NDC
    float mousex = (2.0f * x) / this->viewport->width - 1.0f;
    float mousey = (2.0f * y) / this->viewport->height - 1.0f;
    QVector2D NDC = QVector2D(mousex, -mousey);

    // NDC -> HCC
    QVector4D HCC = QVector4D(NDC, -1.0f, 1.0f);

    // HCC -> View Space
    QMatrix4x4 projection_matrix_inverse = this->editorCam->projMatrix.inverted();
    QVector4D eye_coords = projection_matrix_inverse * HCC;
    QVector4D ray_eye = QVector4D(eye_coords.x(), eye_coords.y(), -1.0f, 0.0f);

    // View Space -> World Space
    QMatrix4x4 view_matrix_inverse = this->editorCam->viewMatrix.inverted();
    QVector4D world_coords = view_matrix_inverse * ray_eye;
    QVector3D final_ray_coords = QVector3D(world_coords);

    return final_ray_coords.normalized();
}

bool SceneViewWidget::updateRPI(QVector3D pos, QVector3D r) {
    float t;
    QVector3D q;
    if (iris::IntersectionHelper::intersectSegmentPlane(pos, (r * 1024), sceneFloor, t, q)) {
        Offset = q;
        return true;
    }

    return false;
}

bool SceneViewWidget::doActiveObjectPicking(const QPointF &point)
{
    editorCam->updateCameraMatrices();

    auto segStart = this->editorCam->getLocalPos();
    auto rayDir = this->calculateMouseRay(point) * 1024;
    auto segEnd = segStart + rayDir;

    QList<PickingResult> hitList;
    doScenePicking(scene->getRootNode(), segStart, segEnd, hitList);

    if (hitList.size() == 0) return false;

    if (hitList.size() == 1) {
        hit = hitList.last().hitPoint;
        return true;
    }

    qSort(hitList.begin(), hitList.end(), [](const PickingResult& a, const PickingResult& b) {
        return a.distanceFromCameraSqrd > b.distanceFromCameraSqrd;
    });

    hit = hitList.last().hitPoint;
    return true;
}

void SceneViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    // @ISSUE - only fired when mouse is dragged
    QPointF localPos = e->localPos();
    QPointF dir = localPos - prevMousePos;

    if (e->buttons() == Qt::LeftButton && !!viewportGizmo->currentNode) {
         viewportGizmo->update(editorCam->getLocalPos(), calculateMouseRay(localPos));
    }

    if (camController != nullptr) {
        camController->onMouseMove(-dir.x(), -dir.y());
    }

    prevMousePos = localPos;
}

void SceneViewWidget::mousePressEvent(QMouseEvent *e)
{
    auto lastSelected = selectedNode;
    prevMousePos = e->localPos();

    if (e->button() == Qt::RightButton) {
        dragging = true;
    }

    if (e->button() == Qt::LeftButton) {
        editorCam->updateCameraMatrices();

        if (viewportMode == ViewportMode::Editor) {
            this->doGizmoPicking(e->localPos());

            if (!!selectedNode) {
                viewportGizmo->isGizmoHit(editorCam, e->localPos(), this->calculateMouseRay(e->localPos()));
                viewportGizmo->isHandleHit();
            }

            // temp ---------------------------------
    //        auto translatePlaneNormal = QVector3D(0, 0, 1);
    //        auto pos = editorCam->pos;
    //        auto r = calculateMouseRay(e->localPos() * 1024.f);

    //        translatePlaneD = -QVector3D::dotProduct(translatePlaneNormal, finalHitPoint);

    //        QVector3D ray = (r - pos).normalized();
    //        float nDotR = -QVector3D::dotProduct(translatePlaneNormal, ray);

            // temp temp
    //        if (nDotR != 0.0f) {
    //            float distance = (QVector3D::dotProduct(translatePlaneNormal, pos) + translatePlaneD) / nDotR;
    //            finalHitPoint = ray * distance + pos; // initial hit
    //        }
            // end temp -----------------------------

            // if we don't have a selected node prioritize object picking
            if (selectedNode.isNull()) {
                this->doObjectPicking(e->localPos(), lastSelected);
            }
        }
    }

    if (camController != nullptr) {
        camController->onMouseDown(e->button());
    }
}

void SceneViewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        dragging = false;
    }

    if (e->button() == Qt::LeftButton) {
        // maybe explicitly hard reset stuff related to picking here
        viewportGizmo->onMouseRelease();
    }

    if (camController != nullptr) {
        camController->onMouseUp(e->button());
    }
}

void SceneViewWidget::wheelEvent(QWheelEvent *event)
{
    if (camController != nullptr) {
        camController->onMouseWheel(event->delta());
    }
}

void SceneViewWidget::keyPressEvent(QKeyEvent *event)
{
    KeyboardState::keyStates[event->key()] = true;
}

void SceneViewWidget::keyReleaseEvent(QKeyEvent *event)
{
    KeyboardState::keyStates[event->key()] = false;
}

void SceneViewWidget::focusOutEvent(QFocusEvent* event)
{
    KeyboardState::reset();
}

/*
 * if @selectRootObject is false then it will return the exact picked object
 * else it will compared the roots of the current and previously selected objects
 * @selectRootObject is usually true for picking using the mouse
 * It's false for when dragging a texture to an object
 */
void SceneViewWidget::doObjectPicking(const QPointF& point, iris::SceneNodePtr lastSelectedNode, bool selectRootObject, bool skipLights, bool skipViewers)
{
    editorCam->updateCameraMatrices();

    auto segStart = this->editorCam->getLocalPos();
    auto rayDir = this->calculateMouseRay(point) * 1024;
    auto segEnd = segStart + rayDir;

    QList<PickingResult> hitList;
    doScenePicking(scene->getRootNode(), segStart, segEnd, hitList);
    if (!skipLights) {
        doLightPicking(segStart, segEnd, hitList);
    }

    if (!skipViewers) {
        doViewerPicking(segStart, segEnd, hitList);
    }

    if (hitList.size() == 0) {
        // no hits, deselect last selected object in viewport and heirarchy
        emit sceneNodeSelected(iris::SceneNodePtr());
        return;
    }

    // if the size of the list is 1 we know it was the only one so return early
//    if (hitList.size() == 1) {
//        viewportGizmo->lastSelectedNode = hitList[0].hitNode;
//        emit sceneNodeSelected(hitList[0].hitNode);
//        return;
//    }

    // sort by distance to camera then return the closest hit node
    qSort(hitList.begin(), hitList.end(), [](const PickingResult& a, const PickingResult& b) {
        return a.distanceFromCameraSqrd > b.distanceFromCameraSqrd;
    });

    auto pickedNode = hitList.last().hitNode;
    iris::SceneNodePtr lastSelectedRoot;

    if (selectRootObject) {
        if (!!lastSelectedNode) {
            lastSelectedRoot = lastSelectedNode;
            while (lastSelectedRoot->isAttached())
                lastSelectedRoot = lastSelectedRoot->parent;
        }

        auto pickedRoot = hitList.last().hitNode;
        while (pickedRoot->isAttached())
            pickedRoot = pickedRoot->parent;



        if (!lastSelectedNode || // if the user clicked away then the root should be reselected
             pickedRoot != lastSelectedRoot)  // if both are under, or is, the same root then pick the actual object
            pickedNode = pickedRoot;// if not then pick the root node
    }

    /*
    auto pickedNode = hitList.last().hitNode;
    // climb tree to object's root (in the case of an imported model)
    iris::SceneNodePtr rootObject;
    if (pickedNode != lastSelectedNode && pickedNode->isAttached()) {
        rootObject = pickedNode;
        // climb the tree to root object
        while (rootObject->isAttached()) {
            rootObject = rootObject->parent;
        }

        // if rootObject is selectedNode, then use the pickedNode, else use the rootObject
        if (rootObject == lastSelectedNode)
            pickedNode = hitList.last().hitNode;
        else
            pickedNode = rootObject;
    }
    */

    //viewportGizmo->lastSelectedNode = hitList.last().hitNode;
    //emit sceneNodeSelected(hitList.last().hitNode);
    viewportGizmo->setLastSelectedNode(pickedNode);
    emit sceneNodeSelected(pickedNode);
}

void SceneViewWidget::doGizmoPicking(const QPointF& point)
{
    editorCam->updateCameraMatrices();

    auto segStart = this->editorCam->getLocalPos();
    auto rayDir = this->calculateMouseRay(point) * 1024;
    auto segEnd = segStart + rayDir;

    QList<PickingResult> hitList;
    doMeshPicking(viewportGizmo->getRootNode(), segStart, segEnd, hitList);

    if (hitList.size() == 0) {
        viewportGizmo->setLastSelectedNode(iris::SceneNodePtr());
        viewportGizmo->currentNode = iris::SceneNodePtr();
        emit sceneNodeSelected(iris::SceneNodePtr());
        return;
    }

    qSort(hitList.begin(), hitList.end(), [](const PickingResult& a, const PickingResult& b) {
        return a.distanceFromCameraSqrd > b.distanceFromCameraSqrd;
    });

    viewportGizmo->finalHitPoint = hitList.last().hitPoint;

    viewportGizmo->setPlaneOrientation(hitList.last().hitNode->getName());

    viewportGizmo->currentNode = hitList.last().hitNode;

    viewportGizmo->onMousePress(editorCam->getLocalPos(), this->calculateMouseRay(point) * 1024);
}

void SceneViewWidget::doScenePicking(const QSharedPointer<iris::SceneNode>& sceneNode,
                                     const QVector3D& segStart,
                                     const QVector3D& segEnd,
                                     QList<PickingResult>& hitList)
{
    if ((sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) &&
         sceneNode->isPickable())
    {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();
        auto mesh = meshNode->getMesh();
        if(mesh != nullptr)
        {
            auto triMesh = meshNode->getMesh()->getTriMesh();

            // transform segment to local space
            auto invTransform = meshNode->globalTransform.inverted();
            auto a = invTransform * segStart;
            auto b = invTransform * segEnd;

            QList<iris::TriangleIntersectionResult> results;
            if (int resultCount = triMesh->getSegmentIntersections(a, b, results)) {
                for (auto triResult : results) {
                    // convert hit to world space
                    auto hitPoint = meshNode->globalTransform * triResult.hitPoint;

                    PickingResult pick;
                    pick.hitNode = sceneNode;
                    pick.hitPoint = hitPoint;
                    pick.distanceFromCameraSqrd = (hitPoint - editorCam->getGlobalPosition()).lengthSquared();

                    hitList.append(pick);
                }
            }
        }
    }

    for (auto child : sceneNode->children) {
        doScenePicking(child, segStart, segEnd, hitList);
    }
}

void SceneViewWidget::doMeshPicking(const QSharedPointer<iris::SceneNode>& sceneNode,
                                    const QVector3D& segStart,
                                    const QVector3D& segEnd,
                                    QList<PickingResult>& hitList)
{
    if (sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();
        auto triMesh = meshNode->getMesh()->getTriMesh();

        // transform segment to local space
        auto invTransform = meshNode->globalTransform.inverted();
        auto a = invTransform * segStart;
        auto b = invTransform * segEnd;

        QList<iris::TriangleIntersectionResult> results;
        if (int resultCount = triMesh->getSegmentIntersections(a, b, results)) {
            for (auto triResult : results) {
                // convert hit to world space
                auto hitPoint = meshNode->globalTransform * triResult.hitPoint;

                PickingResult pick;
                pick.hitNode = sceneNode;
                pick.hitPoint = hitPoint;
                pick.distanceFromCameraSqrd = (hitPoint - editorCam->getGlobalPosition()).lengthSquared();

                hitList.append(pick);
            }
        }
    }

    for (auto child : sceneNode->children) {
        doMeshPicking(child, segStart, segEnd, hitList);
    }
}

void SceneViewWidget::doLightPicking(const QVector3D& segStart,
                                     const QVector3D& segEnd,
                                     QList<PickingResult>& hitList)
{
    const float lightRadius = 0.5f;

    auto rayDir = (segEnd-segStart);
    float segLengthSqrd = rayDir.lengthSquared();
    rayDir.normalize();
    QVector3D hitPoint;
    float t;

    for (auto light: scene->lights) {
        if (iris::IntersectionHelper::raySphereIntersects(segStart,
                                                          rayDir,
                                                          light->getLocalPos(),
                                                          lightRadius,
                                                          t, hitPoint))
        {
            PickingResult pick;
            pick.hitNode = light.staticCast<iris::SceneNode>();
            pick.hitPoint = hitPoint;
            pick.distanceFromCameraSqrd = (hitPoint-editorCam->getGlobalPosition()).lengthSquared();

            hitList.append(pick);
        }
    }
}

void SceneViewWidget::doViewerPicking(const QVector3D& segStart,
                                     const QVector3D& segEnd,
                                     QList<PickingResult>& hitList)
{
    const float headRadius = 0.5f;

    auto rayDir = (segEnd-segStart);
    float segLengthSqrd = rayDir.lengthSquared();
    rayDir.normalize();
    QVector3D hitPoint;
    float t;

    for (auto viewer: scene->viewers) {
        if (iris::IntersectionHelper::raySphereIntersects(segStart,
                                                          rayDir,
                                                          viewer->getGlobalPosition(),
                                                          headRadius,
                                                          t, hitPoint))
        {
            PickingResult pick;
            pick.hitNode = viewer.staticCast<iris::SceneNode>();
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

    //return;
    // switch cam to vr mode
    if(viewportMode == ViewportMode::VR)
    {
        prevCamController = camController;
        camController = vrCam;
        camController->setCamera(editorCam);
        camController->resetMouseStates();

    }
    else
    {
        camController = prevCamController;
        camController->setCamera(editorCam);
        camController->resetMouseStates();
    }
}

ViewportMode SceneViewWidget::getViewportMode()
{
    return viewportMode;
}

void SceneViewWidget::setTransformOrientationLocal()
{
    viewportGizmo->setTransformOrientation("Local");
}

void SceneViewWidget::setTransformOrientationGlobal()
{
    viewportGizmo->setTransformOrientation("Global");
}

void SceneViewWidget::hideGizmo()
{
    viewportGizmo->setLastSelectedNode(iris::SceneNodePtr());
}

void SceneViewWidget::setGizmoLoc()
{
    editorCam->updateCameraMatrices();
    transformMode = viewportGizmo->getTransformOrientation();
    viewportGizmo = translationGizmo;
    viewportGizmo->setTransformOrientation(transformMode);
    viewportGizmo->setLastSelectedNode(selectedNode);
}

void SceneViewWidget::setGizmoRot()
{
    editorCam->updateCameraMatrices();
    transformMode = viewportGizmo->getTransformOrientation();
    viewportGizmo = rotationGizmo;
    viewportGizmo->setTransformOrientation(transformMode);
    viewportGizmo->setLastSelectedNode(selectedNode);
}

void SceneViewWidget::setGizmoScale()
{
    editorCam->updateCameraMatrices();
    transformMode = viewportGizmo->getTransformOrientation();
    viewportGizmo = scaleGizmo;
    viewportGizmo->setTransformOrientation(transformMode);
    viewportGizmo->setLastSelectedNode(selectedNode);
}

void SceneViewWidget::setEditorData(EditorData* data)
{
    editorCam = data->editorCamera;
    orbitalCam->distFromPivot = data->distFromPivot;
    scene->setCamera(editorCam);
    camController->setCamera(editorCam);
}

EditorData* SceneViewWidget::getEditorData()
{
    auto data = new EditorData();
    data->editorCamera = editorCam;
    data->distFromPivot = orbitalCam->distFromPivot;

    return data;
}

void SceneViewWidget::startPlayingScene()
{
    playScene = true;
    animTime = 0.0f;
}

void SceneViewWidget::stopPlayingScene()
{
    playScene = false;
    animTime = 0.0f;
    scene->updateSceneAnimation(0.0f);
}

iris::ForwardRendererPtr SceneViewWidget::getRenderer() const
{
    return renderer;
}

void SceneViewWidget::saveFrameBuffer(QString filePath)
{
    auto vp = viewport;
    auto image = this->grabFramebuffer();

    if (image.height() > image.width()) {
        QImage copy;
        copy = image.copy(0, (int) image.height() / 3, 256, 128);
        copy.save(filePath);
    }
    else {
        auto regular = image.scaled(256, 256,
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
        regular.save(filePath);
    }
}
