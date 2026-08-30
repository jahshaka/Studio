#ifndef PLAYBACK_H
#define PLAYBACK_H

#include <QQuaternion>
#include <QObject>
#include <QPointF>
#include <QSharedPointer>
#include <QMatrix4x4>

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
	QVector3D pos, scale;
	QQuaternion rot;

	PlayBackNodeTransform()
	{

	}

	PlayBackNodeTransform(QVector3D pos, QQuaternion rot, QVector3D scale):
		pos(pos), rot(rot), scale(scale)
	{

	}
};

class PlayBack
{
	QMatrix4x4 savedCameraMatrix;
	CameraControllerBase* camController;
	PlayerMouseController* mouseController;

	bool shouldRestoreCameraTransform;

	iris::ScenePtr scene;

	QTimer* updateTimer;
	QElapsedTimer* fpsTimer;
	float animTime;
	QPointF prevMousePos;

	bool _isPlaying = false;
	QMap<QString, PlayBackNodeTransform> nodeTransforms;
public:
	bool isScenePlaying() { return _isPlaying; }

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

	void playScene();
	void pause();
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