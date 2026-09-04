#ifndef PLAYBACK_H
#define PLAYBACK_H

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QObject>
#include <QPointF>
#include <QSharedPointer>

#include "irisgl/irisglfwd.h"

namespace iris
{
	class CameraNode;
	class Scene;
	class SceneNode;
	class Viewport;
}

class CameraControllerBase;
class PlayerMouseController;
class QElapsedTimer;
class QTimer;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

struct PlayBackNodeTransform
{
	iris::Vec3 pos, scale;
	iris::Quat rot;

	PlayBackNodeTransform()
	{

	}

	PlayBackNodeTransform(iris::Vec3 pos, iris::Quat rot, iris::Vec3 scale):
		pos(pos), rot(rot), scale(scale)
	{

	}
};

class IEditorViewport;

class PlayBack
{
	/// The editor viewport when one exists (editor and player share the
	/// document); null in headless tests. Was UiManager::sceneViewWidget.
	IEditorViewport* editorViewport = nullptr;

	iris::Mat4 savedCameraMatrix;
	CameraControllerBase* camController;
	PlayerMouseController* mouseController;

	bool shouldRestoreCameraTransform;

	iris::ScenePtr scene;

	QTimer* updateTimer;
	QElapsedTimer* fpsTimer;
	float animTime;
	QPointF prevMousePos;

	bool _isPlaying = false;
	/// Paused is a state of PLAYING, not a stop: the physics world, the saved
	/// pre-play transforms and the animation clock all survive it.
	bool _isPaused = false;
	QMap<QString, PlayBackNodeTransform> nodeTransforms;
public:
	bool isScenePlaying() { return _isPlaying; }
	bool isScenePaused() { return _isPaused; }

	void setEditorViewport(IEditorViewport* viewport) { editorViewport = viewport; }

	PlayBack();
	/// Physics, animation and the camera controllers run here; the engine's
	/// SceneMirror does the drawing.
	void init();

	void setScene(iris::ScenePtr scene);
	void setController(CameraControllerBase* controller);

	void setRestoreCameraTransform(bool shouldRestore);

	/// Simulation step without drawing: controller selection and update, keyframe
	/// animation, physics, character controller.
	void update(iris::Viewport& viewport, float dt);

	void saveNodeTransforms();
	void restoreNodeTransforms();

	/// Enters play mode, or resumes when paused. Idempotent while already
	/// playing: re-entering used to re-save the (mid-play) transforms and add a
	/// second copy of every rigid body.
	void playScene();
	void pause();
	void resume();
	void stopScene();

	PlayerMouseController* getMouseController() const;

	// callbacks from ui
	void mousePressEvent(QMouseEvent* evt);
	void mouseMoveEvent(QMouseEvent* evt);
	void mouseDoubleClickEvent(QMouseEvent* evt);
	void mouseReleaseEvent(QMouseEvent* event);
	void wheelEvent(QWheelEvent *event);

	void keyPressEvent(QKeyEvent *event);
	void keyReleaseEvent(QKeyEvent *event);
};

#endif //PLAYBACK_H