#include "player/playback.h"
#include <QTimer>
#include <QElapsedTimer>
#include "data/constants.h"
#include "viewport/ieditorviewport.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/charactercontroller.h"
#include "irisgl/document/physics/physicshelper.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "player/playermousecontroller.h"
#include "viewport/keyboardstate.h"

PlayBack::PlayBack()
{
	camController = nullptr;
	mouseController = new PlayerMouseController();
	this->setRestoreCameraTransform(true);
}

void PlayBack::init()
{
	animTime = 0;
}

void PlayBack::setScene(iris::ScenePtr scene)
{
	this->scene = scene;

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

void PlayBack::update(iris::Viewport& viewport, float dt)
{
    // must update the mouse controller's viewport
    // needed for picking
    this->mouseController->setViewport(viewport);

	setController(mouseController);

	camController->update(dt);

	// The editor viewport and the player share one document; fall back to our
	// own pointer when there is no editor viewport (headless tests).
	auto scene = editorViewport ? editorViewport->getScene() : this->scene;

	if (camController->getCamera() != scene->camera)
		irisLog("Controller mismatch!");

	// A paused scene is frozen: the clock does not advance and the document is
	// left exactly as the pause found it.
	if (!_isPaused) {
		animTime += dt;
		scene->updateSceneAnimation(animTime);
		scene->update(dt);

		auto activeViewer = scene->getActiveVrViewer();
		if (_isPlaying && !!activeViewer && activeViewer->isActiveCharacterController()) {
			// The controller can be gone (viewer removed mid-play); the document
			// flag alone never guaranteed one exists.
			if (auto *controller = scene->getPhysicsEnvironment()->getActiveCharacterController())
				activeViewer->setGlobalTransform(controller->getTransform());
		}
	}

	camController->postUpdate(dt);
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
		// Only nodes that were present when play started have an original to go
		// back to; operator[] would have handed a node added mid-play a
		// default-constructed transform — a ZERO scale, i.e. an invisible node.
		const auto trans = nodeTransforms.constFind(node->guid);
		if (trans == nodeTransforms.constEnd()) continue;
		node->setLocalPos(trans->pos);
		node->setLocalRot(trans->rot);
		node->setLocalScale(trans->scale);
	}
	nodeTransforms.clear();
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
	// Resume, never restart: a paused scene keeps its physics world and its
	// pre-play transforms, so re-running the start path would double-add every
	// rigid body and overwrite the originals with the mid-play pose.
	if (_isPaused) { resume(); return; }
	if (_isPlaying) return;

	_isPlaying = true;
	// The DOCUMENT's play flag (CAMERAS_SPEC D6). It is what
	// SceneMirror::applyCamera reads to decide whether the scene's active
	// camera takes the view, and PlayBack is the one place both play paths —
	// editor play-in-place and the player view — pass through. A PAUSED scene
	// stays "playing": the shot must not cut back to the explorer on pause.
	scene->setPlaying(true);
	saveNodeTransforms();
	mouseController->setPlayState(_isPlaying);
	scene->getPhysicsEnvironment()->initializePhysicsWorldFromScene(scene->getRootNode());
	scene->getPhysicsEnvironment()->simulatePhysics();

	if (camController != nullptr) {
		camController->start();
	}

	animTime = 0;
}

void PlayBack::pause()
{
	if (!_isPlaying || _isPaused) return;
	_isPaused = true;
	mouseController->setPlayState(false);
	// Freeze the simulation but keep the world: stopPhysics() only clears the
	// stepping flag, nothing is torn down.
	scene->getPhysicsEnvironment()->stopPhysics();
}

void PlayBack::resume()
{
	if (!_isPlaying || !_isPaused) return;
	_isPaused = false;
	mouseController->setPlayState(true);
	scene->getPhysicsEnvironment()->simulatePhysics();
	// NOT camController->start(): it captures the camera transform to restore on
	// stop, and re-planting the viewer would re-add its character controller.
}

void PlayBack::stopScene()
{
	_isPlaying = false;
	_isPaused = false;
	// A scene switch can arrive with play state still armed against the
	// PREVIOUS project: closeProject tears the old scene down (cleanup()
	// drops its root node) without routing through here, and the next
	// setScene's stopPlayingScene then found scene->getRootNode() null —
	// restoreNodeTransformations walked ->children on null
	// (crash-1788594910.log). Every deref below is against a scene that may
	// be half-dead; guard each, keep the flag resets above unconditional.
	if (mouseController) mouseController->setPlayState(_isPlaying);
	if (!scene) { animTime = 0; return; }
	scene->setPlaying(false);   // back to the explorer (CAMERAS_SPEC D6)
	if (auto env = scene->getPhysicsEnvironment()) {
		env->restartPhysics();
		env->restoreNodeTransformations(scene->getRootNode());
	}
	if (scene->getRootNode())
		restoreNodeTransforms();// it's important that this is here after physics restore

	animTime = 0;
}

PlayerMouseController * PlayBack::getMouseController() const
{
	return mouseController;
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
