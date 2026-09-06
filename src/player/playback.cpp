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
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicshelper.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/input/inputmap.h"
#include "player/playermousecontroller.h"
#include "services/jahlog.h"
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

	// LATCHED, not per frame (SESSION_LOG_SPEC §8-R2). This used to be a bare
	// irisLog("Controller mismatch!") right here — i.e. a mutex, a formatted
	// QTextStream write, an out->flush() AND a qInfo() to stderr, ONCE PER
	// FRAME for as long as the mismatch held. And it can hold indefinitely:
	// setController() only calls setCamera() when the controller POINTER
	// changes (see above), so once the scene's camera is reassigned underneath
	// it — which the player does, engineplayerscene.cpp `mDocument->setCamera`
	// — nothing re-syncs the controller and the branch is true every frame
	// thereafter. That is a per-frame flushed write on the frame path, which
	// the logging discipline forbids outright.
	//
	// It is NOT deleted, because the condition is a real defect signal (it is
	// adjacent to the 2026-09-05 "can't click anything after play" class); it
	// has simply never been readable. Now it reports the TRANSITION — each
	// distinct controller/camera pair once, and once more when it clears.
	{
		iris::CameraNode *ctrlCam = camController->getCamera().data();
		iris::CameraNode *sceneCam = scene ? scene->camera.data() : nullptr;
		if (ctrlCam != sceneCam) {
			if (!mMismatchLatched || mMismatchController != ctrlCam
			    || mMismatchScene != sceneCam) {
				mMismatchLatched = true;
				mMismatchController = ctrlCam;
				mMismatchScene = sceneCam;
				JAH_LOG(JahLog::render, Warning,
				        QStringLiteral("playback: camera controller is driving a different "
				                       "camera than the scene's (controller %1, scene %2) — "
				                       "player input and the rendered view can disagree")
				            .arg(QString::asprintf("%p", static_cast<void *>(ctrlCam)),
				                 QString::asprintf("%p", static_cast<void *>(sceneCam))));
			}
		} else if (mMismatchLatched) {
			mMismatchLatched = false;
			mMismatchController = nullptr;
			mMismatchScene = nullptr;
			JAH_LOG(JahLog::render, Log,
			        QStringLiteral("playback: camera controller and scene camera agree again"));
		}
	}

	// A paused scene is frozen: the clock does not advance and the document is
	// left exactly as the pause found it.
	if (!_isPaused) {
		animTime += dt;
		scene->updateSceneAnimation(animTime);
		scene->update(dt);
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

	// The Look producer. Raw pixel delta, accumulated until a consumer drains
	// it (consumeLook) — sensitivity and camera-relative meaning belong to
	// possession (Stage 3), not to the producer.
	iris::InputSystem::instance().mouseMoved(float(dir.x()), float(dir.y()));

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
	// Start from a clean input state: a key already down when Play was pressed
	// produced no press edge here, so counting it would walk the character
	// from frame one with nothing the user can release to stop it.
	clearInputState();
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
	// stop, so calling it here would pin the restore point to the pause pose.
}

void PlayBack::stopScene()
{
	_isPlaying = false;
	_isPaused = false;
	// IDEMPOTENT by construction (§8.3 rule 1): clearKeys() on an already-clear
	// state is a no-op, so `editor.stop(); editor.stop();` costs nothing and
	// cannot fault. Everything below this line is guarded for the same reason.
	clearInputState();
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



// The KEYBOARD PRODUCER (AVATAR_LOCOMOTION_SPEC §8.2/§8.3). This is the single
// plug point both play paths pass through: the editor viewport forwards every
// key here while mPlaying (enginesceneviewport.cpp), and EnginePlayerView
// forwards unconditionally. Anything that needs a key in play mode taps in
// HERE and nowhere else.
//
// Auto-repeat is dropped before the InputSystem sees it: an auto-repeat press
// would re-latch Jump under a held key, and an auto-repeat RELEASE would clear
// the held set mid-hold (the same rule EngineSceneViewport::keyPressEvent
// already applies to the fly camera).
void PlayBack::keyPressEvent(QKeyEvent *event)
{
	KeyboardState::keyStates[event->key()] = true;
	if (!event->isAutoRepeat())
		iris::InputSystem::instance().keyPressed(event->key());
	camController->onKeyPressed((Qt::Key)event->key());

}

void PlayBack::keyReleaseEvent(QKeyEvent *event)
{
	KeyboardState::keyStates[event->key()] = false;
	if (!event->isAutoRepeat())
		iris::InputSystem::instance().keyReleased(event->key());
	camController->onKeyReleased((Qt::Key)event->key());
	//camController->keyReleaseEvent(event);
}

void PlayBack::clearInputState()
{
	iris::InputSystem::instance().clearKeys();
}
