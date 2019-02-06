#ifndef PLAYERVIEW_H
#define PLAYERVIEW_H

#include <QObject>
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

class ViewerCameraController;
class QElapsedTimer;
class QTimer;

class PlayerView : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
{
	Q_OBJECT

	iris::CameraNodePtr editorCam;
	ViewerCameraController* viewerCam;

	iris::ForwardRendererPtr renderer;
	iris::ScenePtr scene;

	QTimer* updateTimer;
	QElapsedTimer* fpsTimer;

public:
	explicit PlayerView(QWidget* parent = nullptr);

	void setScene(iris::ScenePtr scene);

	// called when the menu item is selected
	void enter() {}
	// called when the menu item is deselected
	void exit() {}

	void initializeGL();

	void paintGL();
	void renderScene();

	~PlayerView();

public slots:
	void play();
	void pause();
	void stop();
};

#endif PLAYERVIEW_H