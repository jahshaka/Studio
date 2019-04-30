/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "playerview.h"
#include <QTimer>
#include <QElapsedTimer>
#include "constants.h"
#include "uimanager.h"
#include "widgets/sceneviewwidget.h"
#include "irisgl/Graphics.h"
#include "irisgl/SceneGraph.h"
#include "irisgl/Physics.h"
#include "irisgl/Vr.h"
#include "irisgl/Content.h"
#include "playervrcontroller.h"
#include "playermousecontroller.h"
#include "src/core/keyboardstate.h"
#include "playback.h"

PlayerView::PlayerView(QWidget* parent) :
	QOpenGLWidget(parent)
{
	QSurfaceFormat format;
	format.setDepthBufferSize(32);
	format.setMajorVersion(3);
	format.setMinorVersion(2);
	format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setSamples(1);
	format.setSwapInterval(0);
#ifdef QT_DEBUG
	format.setOption(QSurfaceFormat::DebugContext);
#endif
	setFormat(format);

	playback = new PlayBack();

	// needed in order to get mouse events
	setMouseTracking(true);

	// needed in order to get key events http://stackoverflow.com/a/7879484/991834
	setFocusPolicy(Qt::ClickFocus);
}                                                       
void PlayerView::initializeGL()
{
	QOpenGLWidget::initializeGL();
	makeCurrent();
	initializeOpenGLFunctions();

	renderer = iris::ForwardRenderer::create(true, true);
	renderer->setScene(scene);

	updateTimer = new QTimer(this);
	connect(updateTimer, &QTimer::timeout, [=]() {
		update();
	});
	updateTimer->start(Constants::FPS_90);

	fpsTimer = new QElapsedTimer();
	fpsTimer->start();

	playback->init(renderer);
}

void PlayerView::setScene(iris::ScenePtr scene)
{
	this->scene = scene;
	playback->setScene(scene);
}

void PlayerView::start()
{
	this->setFocus();
	makeCurrent();

	renderer->regenerateSwapChain();
	savedCameraMatrix = scene->getCamera()->getLocalTransform();
}

void PlayerView::end()
{
	//if (playback->isScenePlaying()) {
	//	stopScene();
	//}
	scene->getCamera()->setLocalTransform(savedCameraMatrix);
}

void PlayerView::paintGL()
{
	renderScene();
}

void PlayerView::renderScene()
{
	float dt = fpsTimer->nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
	fpsTimer->restart();

	auto vp = iris::Viewport();
	vp.width = width() * devicePixelRatioF();
	vp.height = height() * devicePixelRatioF();
	
	playback->renderScene(vp, dt);
}

void PlayerView::mousePressEvent(QMouseEvent * evt)
{
	playback->mousePressEvent(evt);
}

void PlayerView::mouseMoveEvent(QMouseEvent * evt)
{
	playback->mouseMoveEvent(evt);
}

void PlayerView::mouseDoubleClickEvent(QMouseEvent * evt)
{
}

void PlayerView::mouseReleaseEvent(QMouseEvent *evt)
{
	playback->mouseReleaseEvent(evt);
}

void PlayerView::wheelEvent(QWheelEvent *event)
{
	playback->wheelEvent(event);
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
	KeyboardState::reset();
}
