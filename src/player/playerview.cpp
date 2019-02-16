#include "playerview.h"
#include <QTimer>
#include <QElapsedTimer>
#include "constants.h"
#include "uimanager.h"
#include "widgets/sceneviewwidget.h"
#include "irisgl/Graphics.h"
#include "irisgl/SceneGraph.h"
#include "irisgl/Vr.h"
#include "irisgl/Content.h"
#include "playervrcontroller.h"
#include "playermousecontroller.h"

PlayerView::PlayerView(QWidget* parent) :
	QOpenGLWidget(parent)
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

	camController = nullptr;
	vrController = new PlayerVrController();
	mouseController = new PlayerMouseController();

	//camera = iris::CameraNode::create();

	// needed in order to get mouse events
	setMouseTracking(true);
}                                                       
void PlayerView::initializeGL()
{
	QOpenGLWidget::initializeGL();
	makeCurrent();
	initializeOpenGLFunctions();

	renderer = iris::ForwardRenderer::create(true, true);
	renderer->setScene(scene);

	updateTimer = new QTimer(this);
	connect(updateTimer, &QTimer::timeout, [=]() {
		update();
	});
	updateTimer->start(Constants::FPS_60);

	fpsTimer = new QElapsedTimer();
	fpsTimer->start();

	auto content = iris::ContentManager::create(renderer->getGraphicsDevice());
	vrController->loadAssets(content);
}

void PlayerView::setScene(iris::ScenePtr scene)
{
	this->scene = scene;
	if (renderer)
		renderer->setScene(scene);
	
	vrController->setScene(scene);
	vrController->setCamera(scene->getCamera());
	mouseController->setCamera(scene->getCamera());
}

void PlayerView::setController(CameraControllerBase * controller)
{
	if (controller != camController) {
		// end old one and begin new one
		if (camController)
			camController->end();

		controller->setCamera(scene->getCamera());

		controller->start();

		camController = controller;
	}
}

void PlayerView::start()
{
	makeCurrent();
	//camera = scene->camera;
	//camController->setCamera(scene->getCamera());
	setController(mouseController);
	renderer->regenerateSwapChain();
}

void PlayerView::end()
{
}

void PlayerView::paintGL()
{
	// swap between controllers here
	//auto vrDevice = iris::VrManager::getDefaultDevice();
	auto vrDevice = renderer->getVrDevice();
	if (vrDevice->isHeadMounted()) {
		setController(vrController);
	}
	else {
		setController(mouseController);
	}

	renderScene();
}

void PlayerView::renderScene()
{
	makeCurrent();

	float dt = fpsTimer->nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
	fpsTimer->restart();

	camController->update(dt);

	auto scene = UiManager::sceneViewWidget->getScene();
	//auto renderer = UiManager::sceneViewWidget->getRenderer();

	auto vp = iris::Viewport();
	vp.width = width();
	vp.height = height();
	scene->update(dt);

	//auto vrDevice = iris::VrManager::getDefaultDevice();
	auto vrDevice = renderer->getVrDevice();

	glClearColor(.1f, .1f, .1f, .4f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (vrDevice->isHeadMounted()) {
		renderer->renderSceneVr(dt, &vp, false);
	}
	else {
		renderer->renderScene(dt, &vp);
	}
	//renderer->renderScene(dt, &vp);
}

void PlayerView::mousePressEvent(QMouseEvent * evt)
{
	prevMousePos = evt->localPos();

	if (camController != nullptr) {
		camController->onMouseDown(evt->button());
	}
}

void PlayerView::mouseMoveEvent(QMouseEvent * evt)
{
	QPointF localPos = evt->localPos();
	QPointF dir = localPos - prevMousePos;

	if (camController != nullptr) {
		camController->onMouseMove(-dir.x(), -dir.y());
	}

	prevMousePos = localPos;
}

void PlayerView::mouseDoubleClickEvent(QMouseEvent * evt)
{
}

void PlayerView::mouseReleaseEvent(QMouseEvent *e)
{
	if (camController != nullptr) {
		camController->onMouseUp(e->button());
	}
}

void PlayerView::wheelEvent(QWheelEvent *event)
{
	if (camController != nullptr) {
		camController->onMouseWheel(event->delta());
	}
}

PlayerView::~PlayerView()
{

}

void PlayerView::play()
{
	scene->getPhysicsEnvironment()->simulatePhysics();

	// capture states of all nodes
}


void PlayerView::pause() {}
void PlayerView::stop()
{
	scene->getPhysicsEnvironment()->stopPhysics();

	// reset object positions and states
}