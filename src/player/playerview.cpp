/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "playerview.h"

#include <vtkSphereSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkNew.h>
#include <QDebug>
#include <QMouseEvent>

#include "constants.h"
// #include "uimanager.h"
// #include "widgets/sceneviewwidget.h"
// #include "irisgl/Graphics.h"
// #include "irisgl/SceneGraph.h"
// #include "irisgl/Physics.h"
// #include "irisgl/Vr.h"
// #include "irisgl/Content.h"
// #include "playervrcontroller.h"
// #include "playermousecontroller.h"
#include "src/core/keyboardstate.h"
//#include "playback.h"

class VtkPlayBackSim : public QObject
{
public:
    bool playing = false;
    void init(vtkRenderer*) { /* ... */ }
    void setScene(void*) { /* ... */ }
    void renderScene(int, float) { /* ... 模拟物理/动画更新 */ }
    void playScene() { playing = true; qDebug() << "VTK Scene Playing"; }
    void stopScene() { playing = false; qDebug() << "VTK Scene Stopped"; }
    bool isScenePlaying() const { return playing; }

    // 模拟事件传递
    void mousePressEvent(QMouseEvent * evt) {
        qDebug() << "Mouse Pressed handled by Playback sim.";
        // 实际上这里应该调用 VTK 交互器的方法
    }

    void mouseMoveEvent(QMouseEvent * evt) {
        qDebug() << "Mouse Pressed handled by Playback sim.";
        // 实际上这里应该调用 VTK 交互器的方法
    }

    void mouseReleaseEvent(QMouseEvent * evt) {
        qDebug() << "Mouse Pressed handled by Playback sim.";
        // 实际上这里应该调用 VTK 交互器的方法
    }

    void wheelEvent(QWheelEvent * evt) {
        qDebug() << "Mouse Pressed handled by Playback sim.";
        // 实际上这里应该调用 VTK 交互器的方法
    }

    void keyPressEvent(QKeyEvent *event) {

    }

    void keyReleaseEvent(QKeyEvent *event) {

    }
    // ... 其他事件方法
    void getMouseController() {} // 占位符
};

PlayerView::PlayerView(QWidget* parent) :
    QVTKOpenGLNativeWidget(parent)
{
// 	QSurfaceFormat format;
// 	format.setDepthBufferSize(32);
// 	format.setMajorVersion(3);
// 	format.setMinorVersion(2);
// 	format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
// 	format.setProfile(QSurfaceFormat::CoreProfile);
// 	format.setSamples(1);
// 	format.setSwapInterval(0);
// #ifdef QT_DEBUG
// 	format.setOption(QSurfaceFormat::DebugContext);
// #endif
// 	setFormat(format);

    render_window_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    renderer_ = vtkSmartPointer<vtkRenderer>::New();

    this->setRenderWindow(render_window_);
    render_window_->AddRenderer(renderer_);

    playback = new VtkPlayBackSim();

	// needed in order to get mouse events
	setMouseTracking(true);

	// needed in order to get key events http://stackoverflow.com/a/7879484/991834
    setFocusPolicy(Qt::ClickFocus);


    update_timer_ = new QTimer(this);
    connect(update_timer_, &QTimer::timeout, this, &PlayerView::updateRendering);
    // 启动计时器，频率可根据需要调整
    update_timer_->start(Constants::FPS_90); // 约 90 FPS

    fps_timer_ = new QElapsedTimer();
    fps_timer_->start();

    playback->init(renderer_.Get());
}

void PlayerView::setSceneData()
{
    vtkNew<vtkSphereSource> sphereSource;
    sphereSource->SetRadius(1.0);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(sphereSource->GetOutputPort());

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);

    renderer_->AddActor(actor);
    renderer_->ResetCamera(); // 确保相机能看到物体

    playback->setScene(nullptr); // 模拟设置场景
}


void PlayerView::start()
{
	this->setFocus();
    this->renderWindow()->Render();
//	makeCurrent();

    // renderer->regenerateSwapChain();
    // savedCameraMatrix = scene->getCamera()->getLocalTransform();

    // // force camera update to prevent jumping when switching from
    // // the editor to the player
    // playback->getMouseController()->captureYawPitchRollFromCamera();
    // playback->getMouseController()->updateCameraTransform();
}

void PlayerView::end()
{
	//if (playback->isScenePlaying()) {
	//	stopScene();
	//}
//	scene->getCamera()->setLocalTransform(savedCameraMatrix);
}

// void PlayerView::paintGL()
// {
// 	renderScene();
// }

void PlayerView::renderScene()
{
    float elapsedNs = fps_timer_->nsecsElapsed();
    fps_timer_->restart();

    float dt = std::max(0.001f, static_cast<float>(elapsedNs) / (1000.0f * 1000.0f * 1000.0f));
    playback->renderScene(0, dt);

    this->renderWindow()->Render();
}

void PlayerView::mousePressEvent(QMouseEvent * evt)
{
	playback->mousePressEvent(evt);
    QVTKOpenGLNativeWidget::mousePressEvent(evt);
}

void PlayerView::mouseMoveEvent(QMouseEvent * evt)
{
	playback->mouseMoveEvent(evt);
    QVTKOpenGLNativeWidget::mouseMoveEvent(evt);
}

void PlayerView::mouseDoubleClickEvent(QMouseEvent * evt)
{
}

void PlayerView::mouseReleaseEvent(QMouseEvent *evt)
{
	playback->mouseReleaseEvent(evt);
    QVTKOpenGLNativeWidget::mouseReleaseEvent(evt);
}

void PlayerView::wheelEvent(QWheelEvent *event)
{
	playback->wheelEvent(event);
    QVTKOpenGLNativeWidget::wheelEvent(event);
}

PlayerView::~PlayerView()
{

}

bool PlayerView::isScenePlaying()
{
	return playback->isScenePlaying();
}

void PlayerView::playScene()
{
	if (!playback->isScenePlaying())
		playback->playScene();
}


void PlayerView::pause() {}
void PlayerView::stopScene()
{
	if (playback->isScenePlaying())
		playback->stopScene();
}

void PlayerView::updateRendering()
{
    renderScene();
}


void PlayerView::keyPressEvent(QKeyEvent *event)
{
	KeyboardState::keyStates[event->key()] = true;
	playback->keyPressEvent(event);
}

void PlayerView::keyReleaseEvent(QKeyEvent *event)
{
	KeyboardState::keyStates[event->key()] = false;
	playback->keyReleaseEvent(event);
}

void PlayerView::focusOutEvent(QFocusEvent * event)
{
    Q_UNUSED(event);

    KeyboardState::reset();
}

// void PlayerView::resizeEvent(QResizeEvent *event)
// {
//     Q_UNUSED(event);

//     auto vp = iris::Viewport();
//     vp.width = static_cast<int>(width());
//     vp.height = static_cast<int>(height());
// 	vp.pixelRatioScale = devicePixelRatio();
//     playback->getMouseController()->setViewport(vp);

// 	QOpenGLWidget::resizeEvent(event);
// }
