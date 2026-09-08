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

#include <QMouseEvent>

#include "constants.h"
#include "src/core/keyboardstate.h"
#include "vtkplaybackmanager.h"

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

    playback_ = new VtkPlaybackManager(this);
    playback_->setRenderer(renderer_.Get());

	// needed in order to get mouse events
	setMouseTracking(true);

	// needed in order to get key events http://stackoverflow.com/a/7879484/991834
    setFocusPolicy(Qt::ClickFocus);


    update_timer_ = new QTimer(this);
    connect(update_timer_, &QTimer::timeout, this, &PlayerView::updateRendering);
    // 启动计时器，频率可根据需要调整
    update_timer_->start(Constants::FPS_90); // 约 90 FPS

}

void PlayerView::setSceneData()
{
    vtkNew<vtkSphereSource> sphereSource;
    sphereSource->SetRadius(1.0);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(sphereSource->GetOutputPort());

    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    renderer_->AddActor(actor);
    playback_->addSceneActor(actor);
    renderer_->ResetCamera(); // 确保相机能看到物体
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
    this->renderWindow()->Render();
}

void PlayerView::mousePressEvent(QMouseEvent * evt)
{
    QVTKOpenGLNativeWidget::mousePressEvent(evt);
}

void PlayerView::mouseMoveEvent(QMouseEvent * evt)
{
    QVTKOpenGLNativeWidget::mouseMoveEvent(evt);
}

void PlayerView::mouseDoubleClickEvent(QMouseEvent * evt)
{
}

void PlayerView::mouseReleaseEvent(QMouseEvent *evt)
{
    QVTKOpenGLNativeWidget::mouseReleaseEvent(evt);
}

void PlayerView::wheelEvent(QWheelEvent *event)
{
    QVTKOpenGLNativeWidget::wheelEvent(event);
}

PlayerView::~PlayerView()
{

}

bool PlayerView::isScenePlaying()
{
    return playback_->isScenePlaying();
}

void PlayerView::playScene()
{
    if (!playback_->isScenePlaying()) {
        playback_->startPlayback();
    }
}


void PlayerView::pause() {}
void PlayerView::stopScene()
{
    if (playback_->isScenePlaying()) {
        playback_->stopPlayback();
    }
}

void PlayerView::updateRendering()
{
    renderScene();
}


void PlayerView::keyPressEvent(QKeyEvent *event)
{
	KeyboardState::keyStates[event->key()] = true;
    QVTKOpenGLNativeWidget::keyPressEvent(event);
}

void PlayerView::keyReleaseEvent(QKeyEvent *event)
{
	KeyboardState::keyStates[event->key()] = false;
    QVTKOpenGLNativeWidget::keyReleaseEvent(event);
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
