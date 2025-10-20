#ifndef PLAYERVIEW_H
#define PLAYERVIEW_H

#include <QVTKOpenGLNativeWidget.h>
#include <vtkSmartPointer.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <QTimer>
#include <QElapsedTimer>

//#include "irisgl/src/irisglfwd.h"

// namespace iris
// {
// 	class CameraNode;
// 	class DefaultMaterial;
// 	class ForwardRenderer;
// 	class FullScreenQuad;
// 	class Mesh;
// 	class MeshNode;
// 	class Scene;
// 	class SceneNode;
// 	class Viewport;
// 	class ContentManager;
// }

class CameraControllerBase;
class PlayerVrController;
class PlayerMouseController;
class QElapsedTimer;
class QTimer;
class PlayBack;

class VtkPlayBackSim;

class PlayerView : public QVTKOpenGLNativeWidget
{
	Q_OBJECT
public:
	explicit PlayerView(QWidget* parent = nullptr);

    void setSceneData();

    // void setScene(iris::ScenePtr scene);
    // void setController(CameraControllerBase* controller);

	// called when the menu item is selected
	void start();
	// called when the menu item is deselected
	void end();

    // void initializeGL();
    // void paintGL();

    void mousePressEvent(QMouseEvent* evt) override;
    void mouseMoveEvent(QMouseEvent* evt) override;
    void mouseDoubleClickEvent(QMouseEvent* evt) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    void focusOutEvent(QFocusEvent *event) override;
 //   void resizeEvent(QResizeEvent* event) override;

	~PlayerView();

	bool isScenePlaying();

public slots:
	void playScene();
	void pause();
	void stopScene();

private slots:
    void updateRendering();

private:
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> render_window_;
    vtkSmartPointer<vtkRenderer> renderer_;


    QTimer* update_timer_ = nullptr;
    QElapsedTimer* fps_timer_ = nullptr;

    VtkPlayBackSim* playback;


    void renderScene();
};

#endif PLAYERVIEW_H
