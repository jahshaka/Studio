#ifndef PLAYERVIEW_H
#define PLAYERVIEW_H

#include <QObject>
#include <QPointF>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLWidget>
#include <QSharedPointer>
#include <QMatrix4x4>

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
class PlayBack;

class PlayerView : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
{
	Q_OBJECT

	QMatrix4x4 savedCameraMatrix;

	iris::ForwardRendererPtr renderer;
	iris::ScenePtr scene;

	QTimer* updateTimer;
	QElapsedTimer* fpsTimer;

	PlayBack* playback;

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

	void keyPressEvent(QKeyEvent *event);
	void keyReleaseEvent(QKeyEvent *event);

	void focusOutEvent(QFocusEvent *event);

    void resizeEvent(QResizeEvent* event);

	~PlayerView();

	bool isScenePlaying();
public slots:
	void playScene();
	void pause();
	void stopScene();
};

#endif PLAYERVIEW_H
