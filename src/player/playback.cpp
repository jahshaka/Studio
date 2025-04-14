#include "playback.h"
#include <QTimer>
#include <QElapsedTimer>
#include "constants.h"
#include "uimanager.h"
#include "widgets/sceneviewwidget.h"
#include "irisgl/Graphics.h"
#include "irisgl/SceneGraph.h"
#include "irisgl/Physics.h"
#include "irisgl/Vr.h"
#include "irisgl/Content.h"
#include "playervrcontroller.h"
#include "playermousecontroller.h"
#include "src/core/keyboardstate.h"

PlayBack::PlayBack()
{
	camController = nullptr;
	vrController = new PlayerVrController();
	mouseController = new PlayerMouseController();
	this->setRestoreCameraTransform(true);
}

void PlayBack::init(iris::ForwardRendererPtr renderer)
{
	this->renderer = renderer;
	renderer->setScene(scene);

	auto content = iris::ContentManager::create(renderer->getGraphicsDevice());
	vrController->loadAssets(content);

	animTime = 0;
}

void PlayBack::setScene(iris::ScenePtr scene)
{
	this->scene = scene;
	if (renderer)
		renderer->setScene(scene);

	vrController->setScene(scene);
	vrController->setCamera(scene->getCamera());
	mouseController->setScene(scene);
	mouseController->setCamera(scene->getCamera());
}

void PlayBack::setController(CameraControllerBase * controller)
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

void PlayBack::setRestoreCameraTransform(bool shouldRestore)
{
	this->shouldRestoreCameraTransform = shouldRestore;
	this->mouseController->setRestoreCameraTransform(shouldRestore);
}

void PlayBack::renderScene(iris::Viewport& viewport, float dt)
{
    // must update the mouse controller's viewport
    // needed for picking
    this->mouseController->setViewport(viewport);

	auto vrDevice = renderer->getVrDevice();
	if (vrDevice->isHeadMounted()) {
		setController(vrController);
	}
	else {
		setController(mouseController);
	}

	camController->update(dt);

	auto scene = UiManager::sceneViewWidget->getScene();
	//auto renderer = UiManager::sceneViewWidget->getRenderer();

	if (camController->getCamera() != scene->camera)
		irisLog("Controller mismatch!");

	animTime += dt;
	scene->updateSceneAnimation(animTime);
	scene->update(dt);

	auto activeViewer = scene->getActiveVrViewer();
	if (_isPlaying) {
		if (!!activeViewer && activeViewer->isActiveCharacterController()) {
			activeViewer->setGlobalTransform(scene->getPhysicsEnvironment()->getActiveCharacterController()->getTransform());
		}
	}

	camController->postUpdate(dt);

    glClearColor(.1f, .1f, .1f, .4f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (vrDevice->isHeadMounted()) {
		renderer->renderSceneVr(dt, &viewport, false);
	}
	else {
		renderer->renderScene(dt, &viewport);
	}
	//renderer->renderScene(dt, &vp);
}

void PlayBack::saveNodeTransforms()
{
	for (auto node : scene->nodes) {
		//nodeTransforms.insert(node->guid, node->getLocalTransform());
		nodeTransforms.insert(node->guid, PlayBackNodeTransform(node->getLocalPos(), node->getLocalRot(), node->getLocalScale()));
	}
}

void PlayBack::restoreNodeTransforms()
{
	for (auto node : scene->nodes) {
		//node->setLocalTransform(nodeTransforms[node->guid]);
		const auto trans = nodeTransforms[node->guid];
		node->setLocalPos(trans.pos);
		node->setLocalRot(trans.rot);
		node->setLocalScale(trans.scale);
	}
}

void PlayBack::mousePressEvent(QMouseEvent * evt)
{
	prevMousePos = evt->localPos();

	if (camController != nullptr) {
		camController->onMouseDown(evt->button());
	}
}

void PlayBack::mouseMoveEvent(QMouseEvent * evt)
{
	QPointF localPos = evt->localPos();
	QPointF dir = localPos - prevMousePos;

    if (camController != nullptr) {
        camController->setMousePos(static_cast<int>(localPos.x()), static_cast<int>(localPos.y()));
		camController->onMouseMove(-dir.x(), -dir.y());
	}

	prevMousePos = localPos;
}

void PlayBack::mouseDoubleClickEvent(QMouseEvent * evt)
{
}

void PlayBack::mouseReleaseEvent(QMouseEvent *e)
{
	if (camController != nullptr) {
		camController->onMouseUp(e->button());
	}
}

void PlayBack::wheelEvent(QWheelEvent *event)
{
	if (camController != nullptr) {
        camController->onMouseWheel(event->angleDelta().y());
	}
}

void PlayBack::playScene()
{
	_isPlaying = true;
	saveNodeTransforms();
	vrController->setPlayState(_isPlaying);
	mouseController->setPlayState(_isPlaying);
	scene->getPhysicsEnvironment()->initializePhysicsWorldFromScene(scene->getRootNode());
	scene->getPhysicsEnvironment()->simulatePhysics();

	if (camController != nullptr) {
		camController->start();
	}

	animTime = 0;
}


void PlayBack::pause() {}
void PlayBack::stopScene()
{
	_isPlaying = false;
	vrController->setPlayState(_isPlaying);
	mouseController->setPlayState(_isPlaying);
	scene->getPhysicsEnvironment()->restartPhysics();
	scene->getPhysicsEnvironment()->restoreNodeTransformations(scene->getRootNode());
	restoreNodeTransforms();// it's important that this is here after physics restore

	animTime = 0;
}

PlayerMouseController * PlayBack::getMouseController() const
{
	return mouseController;
}

PlayerVrController * PlayBack::getVrController() const
{
	return vrController;
}


void PlayBack::keyPressEvent(QKeyEvent *event)
{
	KeyboardState::keyStates[event->key()] = true;
	camController->onKeyPressed((Qt::Key)event->key());

	//scene->getPhysicsEnvironment()->onKeyPressed((Qt::Key)event->key());
	if (KeyboardState::isKeyDown(Qt::Key_W)) { scene->getPhysicsEnvironment()->walkForward = 1; }
	if (KeyboardState::isKeyDown(Qt::Key_S)) { scene->getPhysicsEnvironment()->walkBackward = 1; }
	if (KeyboardState::isKeyDown(Qt::Key_A)) { scene->getPhysicsEnvironment()->walkLeft = 1; }
	if (KeyboardState::isKeyDown(Qt::Key_D)) { scene->getPhysicsEnvironment()->walkRight = 1; }
	if (KeyboardState::isKeyDown(Qt::Key_Space)) { scene->getPhysicsEnvironment()->jump = 1; }
}

void PlayBack::keyReleaseEvent(QKeyEvent *event)
{
	KeyboardState::keyStates[event->key()] = false;
	camController->onKeyReleased((Qt::Key)event->key());
	//camController->keyReleaseEvent(event);

	//scene->getPhysicsEnvironment()->keyReleaseEvent((Qt::Key)event->key());
	if (KeyboardState::isKeyUp(Qt::Key_W)) { scene->getPhysicsEnvironment()->walkForward = 0; }
	if (KeyboardState::isKeyUp(Qt::Key_S)) { scene->getPhysicsEnvironment()->walkBackward = 0; }
	if (KeyboardState::isKeyUp(Qt::Key_A)) { scene->getPhysicsEnvironment()->walkLeft = 0; }
	if (KeyboardState::isKeyUp(Qt::Key_D)) { scene->getPhysicsEnvironment()->walkRight = 0; }
	if (KeyboardState::isKeyUp(Qt::Key_Space)) { scene->getPhysicsEnvironment()->jump = 0; }
}
