#include "assetviewer.h"

#include "../core/project.h"
#include "sceneviewwidget.h"

#include "../constants.h"
#include "../globals.h"
#include "../core/keyboardstate.h"

#include <QApplication>
#include <QFileDialog>

AssetViewer::AssetViewer(QWidget *parent) : QOpenGLWidget(parent)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(3, 2);
    format.setSamples(4);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapInterval(0);
    setFormat(format);
    
    //QTimer *timer = new QTimer(this);
    //connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    //timer->start(1000.f / 60.f);
    //
    //connect(this, SIGNAL(frameSwapped()), this, SLOT(update()));

    viewport = new iris::Viewport();
    render = false;

    pdialog = new ProgressDialog();
    pdialog->setWindowModality(Qt::WindowModal);
    pdialog->setRange(0, 100);

    connect(this, &AssetViewer::progressChanged, pdialog, &ProgressDialog::setValue);

    // needed in order to get mouse events
    setMouseTracking(true);
    // needed in order to get key events http://stackoverflow.com/a/7879484/991834
    setFocusPolicy(Qt::ClickFocus);
}

void AssetViewer::paintGL()
{
    makeCurrent();

    if (render) {
        // glViewport(0, 0, this->width() * devicePixelRatio(), this->height() * devicePixelRatio());
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float dt = elapsedTimer.nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
        elapsedTimer.restart();

        if (!!renderer && !!scene) {
            camController->update(dt);
            scene->update(dt);
            renderer->renderScene(dt, viewport);
        }
	}
	else {
		glClearColor(0.18, 0.18, 0.18, 1);
	}

    doneCurrent();
}

void AssetViewer::updateScene()
{

}

void AssetViewer::initializeGL()
{
    QOpenGLWidget::initializeGL();

    initializeOpenGLFunctions();
    gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();

    gl->glEnable(GL_DEPTH_TEST);
    gl->glEnable(GL_CULL_FACE);

    renderer = iris::ForwardRenderer::create(false);
    scene = iris::Scene::create();
    scene->shadowEnabled = true;
    renderer->setScene(scene);

	previewRT = iris::RenderTarget::create(640, 480);
	screenshotTex = iris::Texture2D::create(640, 480);
	previewRT->addTexture(screenshotTex);

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    dlight->setName("Key Light");
    dlight->setLocalRot(QQuaternion::fromEulerAngles(45, 45, 0));
    dlight->intensity = 1;
    dlight->setShadowEnabled(true);
    dlight->shadowMap->shadowType = iris::ShadowMapType::None;
    scene->rootNode->addChild(dlight);

    auto plight = iris::LightNode::create();
    plight->setLightType(iris::LightType::Point);
    plight->setLocalPos(QVector3D(0, 0, -3));
    plight->color = QColor(198, 198, 255);
    plight->intensity = 1;
    plight->setShadowEnabled(true);
    plight->shadowMap->shadowType = iris::ShadowMapType::None;
    scene->rootNode->addChild(plight);

    auto blight = iris::LightNode::create();
    blight->setLightType(iris::LightType::Point);
    blight->setLocalPos(QVector3D(2, 2, 2));
    blight->color = QColor(255, 255, 198);
    blight->intensity = 0.67;
    blight->setShadowEnabled(true);
    blight->shadowMap->shadowType = iris::ShadowMapType::None;
    scene->rootNode->addChild(blight);

    defaultCam = new EditorCameraController();
    orbitalCam = new OrbitalCameraController();

    camera = iris::CameraNode::create();
    camera->setLocalPos(QVector3D(1, 1, 3));
    camera->lookAt(QVector3D(0, 0.5f, 0));

    scene->setCamera(camera);

    scene->setSkyColor(QColor(25, 25, 25));
    scene->setAmbientColor(QColor(255, 255, 255));

    scene->fogEnabled = false;
    scene->shadowEnabled = false;

    camera->update(0);
    scene->update(0);

    //camController->end();
    camController = orbitalCam;
    camController->setCamera(camera);
    // has to be set after setCamera
    orbitalCam->pivot = QVector3D(0, 0, 0);
    orbitalCam->distFromPivot = 5;
    orbitalCam->setRotationSpeed(.5f);
    camController->resetMouseStates();
    camController->start();

    auto timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(Constants::FPS_60);
}

void AssetViewer::wheelEvent(QWheelEvent *event)
{
    if (camController != nullptr) {
        camController->onMouseWheel(event->delta());
    }
}

void AssetViewer::renderObject() {
	render = true;
}

void AssetViewer::mouseMoveEvent(QMouseEvent *e)
{
    // @ISSUE - only fired when mouse is dragged
    QPointF localPos = e->localPos();
    QPointF dir = localPos - prevMousePos;

    if (camController != nullptr) {
        camController->onMouseMove(-dir.x(), -dir.y());
    }

    prevMousePos = localPos;
}

void AssetViewer::mousePressEvent(QMouseEvent *e)
{
    prevMousePos = e->localPos();

    if (camController != nullptr) {
        if (e->button() != Qt::MiddleButton)
            camController->onMouseDown(e->button());
    }
}

void AssetViewer::mouseReleaseEvent(QMouseEvent *e)
{
    if (camController != nullptr) {
        camController->onMouseUp(e->button());
    }
}

void AssetViewer::resetViewerCamera()
{
    camera->setLocalPos(localPos);
    camera->lookAt(lookAt);
    camController->setCamera(camera);
    orbitalCam->pivot = QVector3D(lookAt);
    orbitalCam->distFromPivot = 5;
    orbitalCam->setRotationSpeed(.5f);
    orbitalCam->updateCameraRot();
    camera->update(0);
}

void AssetViewer::loadModel(QString str) {
    pdialog->setLabelText(tr("Importing model..."));
    pdialog->show();
    QApplication::processEvents();
	makeCurrent();
    addMesh(str);
    resetViewerCamera();
	renderObject();
	doneCurrent();
    pdialog->close();
}

void AssetViewer::update() {
	QOpenGLWidget::update();
}

void AssetViewer::resizeGL(int width, int height)
{
    // we do an explicit call to glViewport(...) in forwardrenderer
    // with the "good DPI" values so it is not needed here initially (iKlsR)
    viewport->pixelRatioScale = devicePixelRatio();
    viewport->width = width;
    viewport->height = height;
}

void AssetViewer::addMesh(const QString &path, bool ignore, QVector3D position)
{
	QString filename;
	if (path.isEmpty()) {
		filename = QFileDialog::getOpenFileName(this, "Load Mesh", "Mesh Files (*.obj *.fbx *.3ds *.dae *.c4d *.blend)");
	}
	else {
		filename = path;
	}

	if (filename.isEmpty()) return;

    ssource = new iris::SceneSource();

	auto node = iris::MeshNode::loadAsSceneFragment(filename, [](iris::MeshPtr mesh, iris::MeshMaterialData& data) {
		auto mat = iris::CustomMaterial::create();
		if (mesh->hasSkeleton())
			mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
		else
			mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

		mat->setValue("diffuseColor", QColor(155, 155, 155));
		mat->setValue("specularColor", data.specularColor);
		mat->setValue("ambientColor", QColor(100, 100, 100));
		mat->setValue("emissionColor", data.emissionColor);

		mat->setValue("shininess", data.shininess);

		if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
			mat->setValue("diffuseTexture", data.diffuseTexture);

		if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
			mat->setValue("specularTexture", data.specularTexture);

		if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile())
			mat->setValue("normalTexture", data.normalTexture);

		return mat;
	}, ssource, this);

	// model file may be invalid so null gets returned
	if (!node) return;

	// rename animation sources to relative paths
	auto relPath = QDir(Globals::project->folderPath).relativeFilePath(filename);
	for (auto anim : node->getAnimations()) {
		if (!!anim->skeletalAnimation)
			anim->skeletalAnimation->source = relPath;
	}

	node->setLocalPos(position);

	// todo: load material data
	addNodeToScene(node, ignore);
}

/**
* adds sceneNode directly to the scene's rootNode
* applied default material to mesh if one isnt present
* ignore set to false means we only add it visually, usually to discard it afterw
*/
void AssetViewer::addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore)
{
	if (!scene) {
		// @TODO: set alert that a scene needs to be set before this can be done
		return;
	}

    sceneNode->setLocalPos(QVector3D(0, 0, 0));

	// apply default material to mesh nodes
	if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
		auto meshNode = sceneNode.staticCast<iris::MeshNode>();
		if (!meshNode->getMaterial()) {
			auto mat = iris::CustomMaterial::create();
			mat->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
			meshNode->setMaterial(mat);
		}
	}

    if (scene->rootNode->hasChildren()) {
        for (auto child : scene->rootNode->children) {
            if (child->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                child->removeFromParent();
            }
        }
    }

    scene->rootNode->addChild(sceneNode);

    // fit object in view
    QList<iris::BoundingSphere> spheres;
    getBoundingSpheres(sceneNode, spheres);
    iris::BoundingSphere bound;

    //merge bounding spheres
    if (spheres.count() == 0) {
        bound.pos = QVector3D(0, 0, 0);
        bound.radius = 1;
    }
    else if (spheres.count() == 1) {
        bound = spheres[0];
    }
    else {
        bound.pos = QVector3D(0, 0, 0);
        bound.radius = 1;

        for (auto& sphere : spheres) {
            bound = iris::BoundingSphere::merge(bound, sphere);
        }
    }

    float dist = (bound.radius * 1.2) / qTan(qDegreesToRadians(camera->angle / 2.0f));
    lookAt = bound.pos;
    localPos = QVector3D(0, bound.pos.y(), dist);
    //camera->lookAt(bound.pos);
    //camera->update(0);
}

float AssetViewer::getBoundingRadius(iris::SceneNodePtr node)
{
    auto radius = 0.0f;
    if (node->sceneNodeType == iris::SceneNodeType::Mesh)
        radius = node.staticCast<iris::MeshNode>()->getMesh()->boundingSphere.radius;

    for (auto child : node->children) {
        radius = qMax(radius, getBoundingRadius(child));
    }

    return radius;
}

void AssetViewer::getBoundingSpheres(iris::SceneNodePtr node, QList<iris::BoundingSphere> &spheres)
{
    if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto sphere = node.staticCast<iris::MeshNode>()->getTransformedBoundingSphere();
        spheres.append(sphere);
    }

    for (auto child : node->children) {
        getBoundingSpheres(child, spheres);
    }
}

QImage AssetViewer::takeScreenshot(int width, int height)
{
	makeCurrent();

	previewRT->resize(width, height, true);
    scene->renderSky = false;
	scene->update(0);
    scene->renderSky = true;
	renderer->renderLightBillboards = false;
	renderer->renderSceneToRenderTarget(previewRT, camera, false, false);
	renderer->renderLightBillboards = true;

	auto img = previewRT->toImage();
	doneCurrent();

	return img;
}