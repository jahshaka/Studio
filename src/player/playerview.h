#ifndef PLAYERVIEW_H
#define PLAYERVIEW_H

#include <QObject>
#include <QPointF>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLWidget>
#include <QSharedPointer>

#include "irisgl/src/irisglfwd.h"

namespace iris
{
	class CameraNode;
	class DefaultMaterial;
	class ForwardRenderer;
	class FullScreenQuad;
	class Mesh;
	class MeshNode;
	class Scene;
	class SceneNode;
	class Viewport;
	class ContentManager;
}

class CameraControllerBase;
class PlayerVrController;
class PlayerMouseController;
class QElapsedTimer;
class QTimer;

class PlayerView : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
{
	Q_OBJECT

	//iris::CameraNodePtr camera;
	CameraControllerBase* camController;
	PlayerVrController* vrController;
	PlayerMouseController* mouseController;

	iris::ForwardRendererPtr renderer;
	iris::ScenePtr scene;

	QTimer* updateTimer;
	QElapsedTimer* fpsTimer;
	QPointF prevMousePos;

public:
	explicit PlayerView(QWidget* parent = nullptr);

	void setScene(iris::ScenePtr scene);
	void setController(CameraControllerBase* controller);

	// called when the menu item is selected
	void start();
	// called when the menu item is deselected
	void end();

	void initializeGL();

	void paintGL();
	void renderScene();

	void mousePressEvent(QMouseEvent* evt);
	void mouseMoveEvent(QMouseEvent* evt);
	void mouseDoubleClickEvent(QMouseEvent* evt);
	void mouseReleaseEvent(QMouseEvent* event);
	void wheelEvent(QWheelEvent *event);

	~PlayerView();

public slots:
	void play();
	void pause();
	void stop();
};

#endif PLAYERVIEW_H