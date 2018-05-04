/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetwidget.h"
#include "sceneviewwidget.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QMimeData>
#include <QOpenGLDebugLogger>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QtMath>
#include <QTimer>

#include "irisgl/src/graphics/font.h"
#include "irisgl/src/graphics/forwardrenderer.h"
#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/geometry/trimesh.h"
#include "irisgl/src/graphics/renderlist.h"
#include "irisgl/src/graphics/rendertarget.h"
#include "irisgl/src/graphics/shadowmap.h"
#include "irisgl/src/graphics/spritebatch.h"
#include "irisgl/src/graphics/utils/fullscreenquad.h"
#include "irisgl/src/graphics/utils/shapehelper.h"
#include "irisgl/src/graphics/utils/linemeshbuilder.h"
#include "irisgl/src/graphics/viewport.h"
#include "irisgl/src/materials/colormaterial.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/scenegraph/lightnode.h"
#include "irisgl/src/scenegraph/viewernode.h"
#include "irisgl/src/materials/defaultmaterial.h"
#include "irisgl/src/vr/vrdevice.h"
#include "irisgl/src/vr/vrmanager.h"

#include "animationwidget.h"
#include "constants.h"
#include "keyframecurvewidget.h"
#include "mainwindow.h"
#include "uimanager.h"

#include "core/keyboardstate.h"
#include "core/settingsmanager.h"
#include "editor/animationpath.h"
#include "editor/cameracontrollerbase.h"
#include "editor/editordata.h"
#include "editor/editorcameracontroller.h"
#include "editor/editorvrcontroller.h"
#include "editor/gizmo.h"
#include "editor/orbitalcameracontroller.h"
#include "editor/outlinerenderer.h"
#include "editor/rotationgizmo.h"
#include "editor/scalegizmo.h"
#include "editor/thumbnailgenerator.h"
#include "editor/translationgizmo.h"
#include "editor/viewercontroller.h"
#include "editor/viewermaterial.h"

void SceneViewWidget::setShowFps(bool value)
{
	showFps = value;
}

void SceneViewWidget::cleanup()
{
	scene.reset();
	selectedNode.reset();
	translationGizmo->clearSelectedNode();
	rotationGizmo->clearSelectedNode();
	scaleGizmo->clearSelectedNode();

	renderer->setScene(iris::ScenePtr());
	renderer->setSelectedSceneNode(iris::SceneNodePtr());
}

void SceneViewWidget::dragMoveEvent(QDragMoveEvent *event)
{
    QByteArray encoded = event->mimeData()->data("application/x-qabstractitemmodeldatalist");
    QDataStream stream(&encoded, QIODevice::ReadOnly);

    QMap<int, QVariant> roleDataMap;
    while (!stream.atEnd()) stream >> roleDataMap;

 /*   if (roleDataMap.value(0).toInt() != static_cast<int>(ModelTypes::Texture)) {
        savedActiveNode = doActiveObjectPicking(event->posF());
    }*/

	if (roleDataMap.value(0).toInt() == static_cast<int>(ModelTypes::Material)) {
		auto node = doActiveObjectPicking(event->posF());

        // This is to handle overlapping meshes or those close to each other, if we pass
        // over another node while still dragging, switch materials
        if (!!savedActiveNode) {
            if (savedActiveNode != node) {
                savedActiveNode.staticCast<iris::MeshNode>()->setMaterial(originalMaterial);
                savedActiveNode.reset();
                originalMaterial.reset();
                wasHit = false;
            }
        }

		if (!!node && !wasHit) {
			wasHit = true;
			savedActiveNode = node;
			originalMaterial = node.staticCast<iris::MeshNode>()->getMaterial().staticCast<iris::CustomMaterial>();

			// TODO - get this at drag start
			iris::CustomMaterialPtr material;
			QVector<Asset*>::const_iterator iterator = AssetManager::getAssets().constBegin();
			while (iterator != AssetManager::getAssets().constEnd()) {
				if ((*iterator)->assetGuid == roleDataMap.value(3).toString()) {
					material = (*iterator)->getValue().value<iris::CustomMaterialPtr>();
				}
				++iterator;
			}

			if (!!material) node.staticCast<iris::MeshNode>()->setMaterial(material);
		}
		else if (!!node && wasHit) {
			qDebug() << "no node but hit";
		}
		else {
			if (!!savedActiveNode) {
				savedActiveNode.staticCast<iris::MeshNode>()->setMaterial(originalMaterial);
				savedActiveNode.reset();
				originalMaterial.reset();
				wasHit = false;
			}
		}
	}

    if (roleDataMap.value(0).toInt() == static_cast<int>(ModelTypes::Object)) {
        // If we drag unto another object
        if (doActiveObjectPicking(event->posF())) {
            //activeSceneNode->pos = sceneView->hit;
            dragScenePos = hit;
        }
        // If we drag on the "floor"
        else if (updateRPI(editorCam->getLocalPos(), calculateMouseRay(event->posF()))) {
            //activeSceneNode->pos = sceneView->Offset;
            dragScenePos = Offset;
        }
        // Spawn a distance in front of the camera
        else {
            const float spawnDist = 10.0f;
            auto offset = editorCam->getLocalRot().rotatedVector(QVector3D(0, -1.0f, -spawnDist));
            offset += editorCam->getLocalPos();
            //activeSceneNode->pos = offset;
            dragScenePos = offset;
        }
    }
}

void SceneViewWidget::dropEvent(QDropEvent *event)
{
    QByteArray encoded = event->mimeData()->data("application/x-qabstractitemmodeldatalist");
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QMap<int, QVariant> roleDataMap;
    while (!stream.atEnd()) stream >> roleDataMap;

    qDebug() << roleDataMap.value(0).toInt();
	qDebug() << roleDataMap.value(1).toString();
	qDebug() << roleDataMap.value(2).toString();
	qDebug() << roleDataMap.value(3).toString();

    if (roleDataMap.value(0).toInt() == static_cast<int>(ModelTypes::Object)) {
        auto ppos = dragScenePos;
        emit addDroppedMesh(
                QDir(Globals::project->getProjectFolder()).filePath(roleDataMap.value(2).toString()),
                true, ppos, roleDataMap.value(3).toString(), roleDataMap.value(1).toString()
        );
    }

	if (roleDataMap.value(0).toInt() == static_cast<int>(ModelTypes::Material)) {
		if (!!savedActiveNode) {
			iris::CustomMaterialPtr material;
			QVector<Asset*>::const_iterator iterator = AssetManager::getAssets().constBegin();
			while (iterator != AssetManager::getAssets().constEnd()) {
				if ((*iterator)->assetGuid == roleDataMap.value(3).toString()) {
					material = (*iterator)->getValue().value<iris::CustomMaterialPtr>()->duplicate().staticCast<iris::CustomMaterial>();
				}
				++iterator;
			}

            //if (!!material) savedActiveNode.staticCast<iris::MeshNode>()->setMaterial(material); ???
		}
		else {
			qDebug() << "Empty";
		}

        savedActiveNode.reset();
        originalMaterial.reset();
        wasHit = false;
	}

    if (roleDataMap.value(0).toInt() == static_cast<int>(ModelTypes::Texture)) {
        auto node = doActiveObjectPicking(event->posF());
        if (!!node) {
            auto meshNode = node.staticCast<iris::MeshNode>();
            auto mat = meshNode->getMaterial().staticCast<iris::CustomMaterial>();

            if (!mat->firstTextureSlot().isEmpty()) {
                mat->setValue(mat->firstTextureSlot(), QDir(Globals::project->getProjectFolder()).filePath(roleDataMap.value(1).toString()));
            }
        }
    }
}

void SceneViewWidget::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
        event->acceptProposedAction();
	}
}

SceneViewWidget::SceneViewWidget(QWidget *parent) : QOpenGLWidget(parent)
{
	QSurfaceFormat format;
	format.setDepthBufferSize(32);
	format.setMajorVersion(3);
	format.setMinorVersion(2);
	format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setSamples(1);
	format.setSwapInterval(0);
#ifdef QT_DEBUG
	format.setOption(QSurfaceFormat::DebugContext);
#endif
	setFormat(format);

    // needed in order to get mouse events
    setMouseTracking(true);

    // needed in order to get key events http://stackoverflow.com/a/7879484/991834
    setFocusPolicy(Qt::ClickFocus);
    setAcceptDrops(true);

    viewport = new iris::Viewport();

    defaultCam = new EditorCameraController(this);
    orbitalCam = new OrbitalCameraController(this);
    viewerCam = new ViewerCameraController();

    camController = defaultCam;
    prevCamController = defaultCam;

    editorCam = iris::CameraNode::create();
    editorCam->setLocalPos(QVector3D(0, 5, 14));
    editorCam->setLocalRot(QQuaternion::fromEulerAngles(-5, 0, 0));
    camController->setCamera(editorCam);

    viewportMode = ViewportMode::Editor;

    elapsedTimer = new QElapsedTimer();
    playScene = false;
    animTime = 0.0f;

    dragging = false;
    showLightWires = false;

    sceneFloor = iris::IntersectionHelper::computePlaneND(QVector3D( 100, 0,  100),
                                                          QVector3D(-100, 0,  100),
                                                          QVector3D(   0, 0, -100));

    setAcceptDrops(true);
    thumbGen = nullptr;

    fontSize = 20;
    showFps = SettingsManager::getDefaultManager()->getValue("show_fps", false).toBool();

	settings = SettingsManager::getDefaultManager();
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
    
    translationGizmo = new TranslationGizmo();
    translationGizmo->loadAssets();

    rotationGizmo = new RotationGizmo();
    rotationGizmo->loadAssets();

    scaleGizmo = new ScaleGizmo();
	scaleGizmo->loadAssets();
    
    transformMode = "Global";
    gizmo = translationGizmo;

    // has to be initialized here since it loads assets
    vrCam = new EditorVrController();

    initLightAssets();
}

bool SceneViewWidget::getShowLightWires() const
{
    return showLightWires;
}

void SceneViewWidget::setShowLightWires(bool value)
{
    showLightWires = value;
}

void SceneViewWidget::initLightAssets()
{
    pointLightMesh = iris::ShapeHelper::createWireSphere(1.0f);
    spotLightMesh = iris::ShapeHelper::createWireCone(1.0f);
    dirLightMesh = createDirLightMesh();
    lineMat = iris::ColorMaterial::create();
}

iris::MeshPtr SceneViewWidget::createDirLightMesh(float baseRadius)
{
    iris::LineMeshBuilder builder;

    int divisions = 36;
    float arcWidth = 360.0f/divisions;

    // XZ plane
    for (int i = 0 ; i < divisions; i++) {
        float angle = i * arcWidth;
        QVector3D a = QVector3D(qSin(qDegreesToRadians(angle)), 0, qCos(qDegreesToRadians(angle))) * baseRadius;

        angle = (i + 1) * arcWidth;
        QVector3D b = QVector3D(qSin(qDegreesToRadians(angle)), 0, qCos(qDegreesToRadians(angle))) * baseRadius;

        builder.addLine(a, b);
    }

    float halfSize = 0.5f;

    builder.addLine(QVector3D(-halfSize, 0, -halfSize), QVector3D(-halfSize, -2, -halfSize));
    builder.addLine(QVector3D(halfSize, 0, -halfSize), QVector3D(halfSize, -2, -halfSize));
    builder.addLine(QVector3D(halfSize, 0, halfSize), QVector3D(halfSize, -2, halfSize));
    builder.addLine(QVector3D(-halfSize, 0, halfSize), QVector3D(-halfSize, -2, halfSize));

    return builder.build();
}

void SceneViewWidget::addLightShapesToScene()
{
    QMatrix4x4 mat;

    for (auto light : scene->lights) {
        if (light->lightType == iris::LightType::Point) {
            mat.setToIdentity();
            mat.translate(light->getGlobalPosition());
            mat.scale(light->distance);
            scene->geometryRenderList->submitMesh(pointLightMesh, lineMat, mat);
        }
        else if (light->lightType == iris::LightType::Spot) {
            mat.setToIdentity();
            mat.translate(light->getGlobalPosition());
            mat.rotate(QQuaternion::fromRotationMatrix(light->getGlobalTransform().normalMatrix()));
            auto radius = qCos(qDegreesToRadians(light->spotCutOff - 90)); // 90 is max spot cutoff (180) / 2

            mat.scale(radius, 1.0 - radius, radius);
            mat.scale(light->distance);

            scene->geometryRenderList->submitMesh(spotLightMesh, lineMat, mat);
        }
        else if (light->lightType == iris::LightType::Directional) {
            mat.setToIdentity();
            mat.translate(light->getGlobalPosition());
            mat.rotate(QQuaternion::fromRotationMatrix(light->getGlobalTransform().normalMatrix()));

            scene->geometryRenderList->submitMesh(dirLightMesh, lineMat, mat);
        }
    }
}

void SceneViewWidget::addViewerHeadsToScene()
{
	for (auto viewer : scene->viewers) {
		//if (selectedNode != viewer)
		scene->geometryRenderList->submitMesh(viewerMesh, viewerMat, viewer->getGlobalTransform());
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
    if (!scene)
        return;

    selectedNode = sceneNode;

	if (!!selectedNode && selectedNode->hasActiveAnimation()) {
		animPath->generate(selectedNode, selectedNode->getAnimation());
	}
	else {
		animPath->clearPath();
	}

	if (sceneNode == scene->getRootNode() || !sceneNode) {
		renderer->setSelectedSceneNode(iris::SceneNodePtr());
		gizmo->clearSelectedNode();
	}
	else {
		renderer->setSelectedSceneNode(sceneNode);
		gizmo->setSelectedNode(sceneNode);
	}
}

void SceneViewWidget::clearSelectedNode()
{
    selectedNode.clear();
    renderer->setSelectedSceneNode(selectedNode);
	gizmo->clearSelectedNode();
}

void SceneViewWidget::enterEditorMode()
{

}

void SceneViewWidget::enterPlayerMode()
{
	if (gizmo->isDragging()) {
		gizmo->endDragging();
	}
}

void SceneViewWidget::renderGizmos(bool once)
{
	if (viewportMode != ViewportMode::Editor || UiManager::sceneMode != SceneMode::EditMode)
		return;
	
    auto gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
	if (!!selectedNode) {
		gizmo->updateSize(editorCam);

		QVector3D rayPos, rayDir;
		this->getMousePosAndRay(this->prevMousePos, rayPos, rayDir);
		gizmo->render(renderer->getGraphicsDevice(), rayPos, rayDir, editorCam->viewMatrix, editorCam->projMatrix);
	}
}

void SceneViewWidget::renderSelectedNode(iris::SceneNodePtr selectedNode)
{
	if (viewportMode != ViewportMode::Editor || UiManager::sceneMode != SceneMode::EditMode)
		return;
	outliner->renderOutline(renderer->getGraphicsDevice(), selectedNode, editorCam, qBound(1.f,(float)scene->outlineWidth,2.f),scene->outlineColor);
}

void SceneViewWidget::setSceneMode(SceneMode sceneMode)
{
	// stop animation
	if (sceneMode == SceneMode::EditMode)
	{

	}
	else
	{

	}
}

void SceneViewWidget::initializeGL()
{
    QOpenGLWidget::initializeGL();

    makeCurrent();

    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    renderer = iris::ForwardRenderer::create();
    spriteBatch = iris::SpriteBatch::create(renderer->getGraphicsDevice());
    font = iris::Font::create(renderer->getGraphicsDevice(), fontSize);

    initialize();
    fsQuad = new iris::FullScreenQuad();

    viewerCamera = iris::CameraNode::create();
    viewerRT = iris::RenderTarget::create(500, 500);
    viewerTex = iris::Texture2D::create(500, 500);
    viewerRT->addTexture(viewerTex);
    viewerQuad = new iris::FullScreenQuad();

	auto mat = ViewerMaterial::create();
	mat->setTexture(iris::Texture2D::load(":/assets/models/head.png"));
	viewerMat = mat.staticCast<iris::Material>();
	viewerMesh = iris::Mesh::loadMesh(":/assets/models/head2.obj");

    screenshotRT = iris::RenderTarget::create(500, 500);
    screenshotTex = iris::Texture2D::create(500, 500);
    screenshotRT->addTexture(screenshotTex);

	outliner = new OutlinerRenderer();
	outliner->loadAssets();

    emit initializeGraphics(this, this);

    //thumbGen = new ThumbnialGenerator();
    thumbGen = ThumbnailGenerator::getSingleton();

	animPath = new AnimationPath();

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(Constants::FPS_60);

    this->elapsedTimer->start();

    //auto curveWidget = UiManager::animationWidget->getCurveWidget();
    //connect(curveWidget, SIGNAL(keyChanged(iris::FloatKey*)), this, SLOT(onAnimationKeyChanged(iris::FloatKey*)));

	//initializeOpenGLDebugger();
}

void SceneViewWidget::initializeOpenGLDebugger()
{
	glDebugger = new QOpenGLDebugLogger();
	connect(glDebugger, &QOpenGLDebugLogger::messageLogged, this, [](QOpenGLDebugMessage msg)
	{
		qDebug() << msg;
	});

	if (glDebugger->initialize()) {
		glDebugger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
		glDebugger->enableMessages();
	}
}

void SceneViewWidget::paintGL()
{
    makeCurrent();

    if (iris::VrManager::getDefaultDevice()->isHeadMounted() && viewportMode != ViewportMode::VR) {
        // set to vr mode automatically if a headset is detected
        this->setViewportMode(ViewportMode::VR);
        timer->setInterval(Constants::FPS_90); // 90 fps for vr
    }
    else if (!iris::VrManager::getDefaultDevice()->isHeadMounted() &&
            viewportMode == ViewportMode::VR)
    {
        this->setViewportMode(ViewportMode::Editor);
        timer->setInterval(Constants::FPS_60); // 60 for regular
    }

    renderScene();

}

void SceneViewWidget::renderScene()
{
    glClearColor(.1f, .1f, .1f, .4f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float dt = elapsedTimer->nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
    elapsedTimer->restart();

    if (!!renderer && !!scene) {
        this->camController->update(dt);

        if (playScene) {
            animTime += dt;
            scene->updateSceneAnimation(animTime);
        }

        // hide viewer so it doesnt show up in rt
        bool viewerVisible = true;
/*
        if (!!selectedNode && selectedNode->getSceneNodeType() == iris::SceneNodeType::Viewer) {
            viewerVisible = selectedNode->isVisible();
            selectedNode->hide();
        } else {
            viewerVisible = false;    
        }
		*/
        scene->update(dt);
		//animPath->submit(scene->geometryRenderList);

        // insert vr head
        if ((UiManager::sceneMode == SceneMode::EditMode && viewportMode == ViewportMode::Editor)) {
            renderer->renderLightBillboards = true;
        } else {
            renderer->renderLightBillboards = false;
        }

        // TODO: ensure it doesnt display these shapes in play mode (Nick)
        if (UiManager::sceneMode != SceneMode::PlayMode && showLightWires) addLightShapesToScene();

        // render thumbnail to texture
        if (!playScene && !!selectedNode) {
            if (selectedNode->getSceneNodeType() == iris::SceneNodeType::Viewer) {
                viewerCamera->setLocalTransform(selectedNode->getGlobalTransform());
                viewerCamera->update(0); // update transformation of camera

                // resize render target to fix aspect ratio
                viewerRT->resize(this->width(), this->height(), true);

                renderer->renderSceneToRenderTarget(viewerRT, viewerCamera);
            }
        }

		if (UiManager::sceneMode == SceneMode::EditMode && viewportMode == ViewportMode::Editor)
			addViewerHeadsToScene();

        if (viewportMode == ViewportMode::Editor) {
            renderer->renderScene(dt, viewport);
        } else {
            renderer->renderSceneVr(dt, viewport, UiManager::sceneMode == SceneMode::PlayMode);
        }

        // dont show thumbnail in play mode
        if (!playScene) {
            if (!!selectedNode && selectedNode->getSceneNodeType() == iris::SceneNodeType::Viewer) {
                QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>()->glClear(GL_DEPTH_BUFFER_BIT);
                QMatrix4x4 mat;
                mat.setToIdentity();
                mat.translate(0.75, -0.75, 0);
                mat.scale(0.2, 0.2, 0);
                viewerTex->bind(0);
                viewerQuad->matrix = mat;
                viewerQuad->draw(renderer->getGraphicsDevice());
            }
        }
		
		this->renderSelectedNode(selectedNode);
		this->renderGizmos();
        
    }

    // render fps
    spriteBatch->begin();
    if (showFps) {
        float fps = 1.0 / dt;
        float ms = 1000.f / fps;
        spriteBatch->drawString(font,
                                QString("%1ms (%2fps)")
                                    .arg(QString::number(ms, 'f', 1))
                                    .arg(QString::number(fps, 'f', 1)),
                                QVector2D(8, 8),
                                QColor(255, 255, 255));
    }

//    if (!!scene) {
//        for(auto light : scene->lights) {
//            if (light->lightType == iris::LightType::Spot)
//                spriteBatch->draw(light->shadowMap->shadowTexture,QRect(0,0,200,200),QColor(255,255,255,255));
//        }
//    }
    spriteBatch->end();

}

void SceneViewWidget::resizeGL(int width, int height)
{
    // we do an explicit call to glViewport(...) in forwardrenderer
    // with the "good DPI" values so it is not needed here initially (iKlsR)
    viewport->pixelRatioScale = devicePixelRatio();
    viewport->width = width;
    viewport->height = height;
}

void SceneViewWidget::onAnimationKeyChanged(iris::FloatKey* key)
{
	animPath->generate(selectedNode, selectedNode->getAnimation());
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

iris::SceneNodePtr SceneViewWidget::doActiveObjectPicking(const QPointF &point)
{
    editorCam->updateCameraMatrices();

    auto segStart = this->editorCam->getLocalPos();
    auto rayDir = this->calculateMouseRay(point) * 1024;
    auto segEnd = segStart + rayDir;

    QList<PickingResult> hitList;
    doScenePicking(scene->getRootNode(), segStart, segEnd, hitList);

    if (hitList.size() == 0) return iris::SceneNodePtr();
    if (hitList.size() == 1) return hitList.last().hitNode;

    qSort(hitList.begin(), hitList.end(), [](const PickingResult& a, const PickingResult& b) {
        return a.distanceFromCameraSqrd > b.distanceFromCameraSqrd;
    });

    return hitList.last().hitNode;
}

void SceneViewWidget::mouseMoveEvent(QMouseEvent *e)
{
    // @ISSUE - only fired when mouse is dragged
    QPointF localPos = e->localPos();
    QPointF dir = localPos - prevMousePos;

    if (e->buttons() == Qt::LeftButton && !!selectedNode) {
		if (gizmo->isDragging()) {
			QVector3D rayPos, rayDir;
			this->getMousePosAndRay(e->localPos(), rayPos, rayDir);
			gizmo->drag(rayPos, rayDir);
		}
    }

    if (camController != nullptr) {
        camController->onMouseMove(-dir.x(), -dir.y());
    }

    prevMousePos = localPos;

	gizmo->updateSize(editorCam);
}

void SceneViewWidget::mouseDoubleClickEvent(QMouseEvent * e)
{
	auto lastSelected = selectedNode;

	if (e->button() == Qt::LeftButton) {
		editorCam->updateCameraMatrices();

		if (viewportMode == ViewportMode::Editor && UiManager::sceneMode == SceneMode::EditMode) {
			if (selectedNode.isNull()) {
				// double-click to select object
				if (settings->getValue("mouse_controls", "jahshaka").toString() == "jahshaka") {
					this->doObjectPicking(e->localPos(), lastSelected);
				}
			}
		}
	}
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

        if (viewportMode == ViewportMode::Editor && UiManager::sceneMode == SceneMode::EditMode) {
            this->doGizmoPicking(e->localPos());

            if (!!selectedNode) {
                QVector3D rayPos, rayDir;
                this->getMousePosAndRay(e->localPos(), rayPos, rayDir);
                gizmo->startDragging(rayPos, rayDir);
            }

            // if we don't have a selected node, prioritize object picking
            if (selectedNode.isNull()) {
				if (settings->getValue("mouse_controls", "jahshaka").toString() == "default") {
					this->doObjectPicking(e->localPos(), lastSelected);
				}
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
        if (gizmo->isDragging())
            gizmo->endDragging();
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

	gizmo->updateSize(editorCam);
}

void SceneViewWidget::keyPressEvent(QKeyEvent *event)
{
    KeyboardState::keyStates[event->key()] = true;
	camController->onKeyPressed((Qt::Key)event->key());
}

void SceneViewWidget::keyReleaseEvent(QKeyEvent *event)
{
    KeyboardState::keyStates[event->key()] = false;
	camController->onKeyReleased((Qt::Key)event->key());
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
void SceneViewWidget::doObjectPicking(
	const QPointF& point,
	iris::SceneNodePtr lastSelectedNode,
	bool selectRootObject,
	bool skipLights,
	bool skipViewers)
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

        if (!lastSelectedNode ||            // if the user clicked away then the root should be reselected
            pickedRoot != lastSelectedRoot) // if both are under, or is, the same root then pick the actual object
            pickedNode = pickedRoot;        // if not then pick the root node
    }

    gizmo->setSelectedNode(pickedNode);
    emit sceneNodeSelected(pickedNode);
}

QImage SceneViewWidget::takeScreenshot(QSize dimension)
{
	this->makeCurrent();
	screenshotRT->resize(dimension.width(), dimension.height(), true);
	scene->update(0);
	renderer->renderLightBillboards = false;
	renderer->renderSceneToRenderTarget(screenshotRT, editorCam, false);
	renderer->renderLightBillboards = true;

	auto img = screenshotRT->toImage();
	this->doneCurrent();

	return img;
}


QImage SceneViewWidget::takeScreenshot(int width, int height)
{
    this->makeCurrent();
    screenshotRT->resize(width, height, true);
    scene->update(0);
    renderer->renderLightBillboards = false;
    renderer->renderSceneToRenderTarget(screenshotRT, editorCam, false);
    renderer->renderLightBillboards = true;

    auto img = screenshotRT->toImage();
    this->doneCurrent();

    return img;
}

void SceneViewWidget::doGizmoPicking(const QPointF& point)
{
    editorCam->updateCameraMatrices();

    auto segStart = this->editorCam->getLocalPos();
    auto rayDir = this->calculateMouseRay(point).normalized();// * 1024;

    if (!!selectedNode) {
        gizmo->setSelectedNode(selectedNode);
        if (gizmo != nullptr && gizmo->isHit(segStart, rayDir)) {

        } else {
            emit sceneNodeSelected(iris::SceneNodePtr());
            return;
        }
    }
}

void SceneViewWidget::setCameraController(CameraControllerBase *controller)
{
    camController->end();
    prevCamController = camController;

    camController = controller;
    camController->resetMouseStates();
    camController->setCamera(editorCam);
    camController->start();

}

void SceneViewWidget::restorePreviousCameraController()
{
    camController->end();

    // swap controllers
    auto temp = camController;
    camController = prevCamController;
    prevCamController = temp;

    camController->resetMouseStates();
    camController->setCamera(editorCam);
    camController->start();
}

void SceneViewWidget::getMousePosAndRay(const QPointF& point, QVector3D &rayPos, QVector3D &rayDir)
{
    editorCam->updateCameraMatrices();

    rayPos = this->editorCam->getLocalPos();
    rayDir = this->calculateMouseRay(point).normalized();// * 1024;
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
        if (mesh != nullptr) {
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
    setCameraController(defaultCam);
}

void SceneViewWidget::setArcBallCameraMode()
{
    setCameraController(orbitalCam);
}

void SceneViewWidget::focusOnNode(iris::SceneNodePtr sceneNode)
{
	setCameraController(orbitalCam);
	orbitalCam->focusOnNode(sceneNode);
}

bool SceneViewWidget::isVrSupported()
{
    return renderer->isVrSupported();
}

void SceneViewWidget::setViewportMode(ViewportMode viewportMode)
{
    this->viewportMode = viewportMode;

    // switch cam to vr mode
    if (viewportMode == ViewportMode::VR) {
        prevCamController = camController;
        camController = vrCam;
        camController->setCamera(editorCam);
        camController->resetMouseStates();
    }
    else {
        camController = prevCamController;
        camController->setCamera(editorCam);
        camController->resetMouseStates();
    }
}

ViewportMode SceneViewWidget::getViewportMode()
{
    return viewportMode;
}

void SceneViewWidget::setGizmoTransformToLocal()
{
	translationGizmo->setTransformSpace(GizmoTransformSpace::Local);
	rotationGizmo->setTransformSpace(GizmoTransformSpace::Local);
	scaleGizmo->setTransformSpace(GizmoTransformSpace::Local);
}

void SceneViewWidget::setGizmoTransformToGlobal()
{
	translationGizmo->setTransformSpace(GizmoTransformSpace::Global);
	rotationGizmo->setTransformSpace(GizmoTransformSpace::Global);
	// scaling is only done locally
	scaleGizmo->setTransformSpace(GizmoTransformSpace::Local);
}

void SceneViewWidget::setGizmoLoc()
{
    editorCam->updateCameraMatrices();
	translationGizmo->setSelectedNode(selectedNode);
	gizmo = translationGizmo;
}

void SceneViewWidget::setGizmoRot()
{
    editorCam->updateCameraMatrices();
	rotationGizmo->setSelectedNode(selectedNode);
	gizmo = rotationGizmo;
}

void SceneViewWidget::setGizmoScale()
{
    editorCam->updateCameraMatrices();
	scaleGizmo->setSelectedNode(selectedNode);
	gizmo = scaleGizmo;
}

void SceneViewWidget::setEditorData(EditorData* data)
{
    editorCam = data->editorCamera;
    orbitalCam->distFromPivot = data->distFromPivot;
    scene->setCamera(editorCam);
    camController->setCamera(editorCam);
    showLightWires = data->showLightWires;
}

EditorData* SceneViewWidget::getEditorData()
{
    auto data = new EditorData();
    data->editorCamera = editorCam;
    data->distFromPivot = orbitalCam->distFromPivot;
    data->showLightWires = showLightWires;

    return data;
}

void SceneViewWidget::setWindowSpace(WindowSpaces windowSpace)
{
	this->windowSpace = windowSpace;

	switch (windowSpace)
	{
	case WindowSpaces::EDITOR:
		displayGizmos = true;
		displayLightIcons = true;
		displaySelectionOutline = true;
		break;
	case WindowSpaces::PLAYER:
		displayGizmos = false;
		displayLightIcons = false;
		displaySelectionOutline = false;
		break;
	}
}

void SceneViewWidget::startPlayingScene()
{
    if (!scene)
        return;

    // switch controllers
    if (scene->viewers.count() > 0) {
        viewerCam->setViewer(scene->viewers[0]);
        setCameraController(viewerCam);
    }


    playScene = true;
}

void SceneViewWidget::pausePlayingScene()
{
    playScene = false;
    // time isnt reset
}

void SceneViewWidget::stopPlayingScene()
{
	if (playScene) {
		playScene = false;
		animTime = 0.0f;

		if (!scene)
			return;

		scene->updateSceneAnimation(0.0f);

		restorePreviousCameraController();
	}
}

iris::ForwardRendererPtr SceneViewWidget::getRenderer() const
{
    return renderer;
}
