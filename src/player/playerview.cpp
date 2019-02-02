#include "playerview.h"
#include <QTimer>
#include <QElapsedTimer>
#include "constants.h"
#include "uimanager.h"
#include "widgets/sceneviewwidget.h"
#include "irisgl/Graphics.h"
#include "irisgl/SceneGraph.h"

PlayerView::PlayerView(QWidget* parent) :
	QOpenGLWidget(parent)
{

}

void PlayerView::initializeGL()
{
	QOpenGLWidget::initializeGL();
	makeCurrent();
	initializeOpenGLFunctions();


	updateTimer = new QTimer(this);
	connect(updateTimer, SIGNAL(timeout()), this, SLOT(update()));
	updateTimer->start(Constants::FPS_60);

	fpsTimer = new QElapsedTimer();
	fpsTimer->start();
}

void PlayerView::painGL()
{
	renderScene();
}

void PlayerView::renderScene()
{
	float dt = fpsTimer->nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
	fpsTimer->restart();

	auto scene = UiManager::sceneViewWidget->getScene();
	auto renderer = UiManager::sceneViewWidget->getRenderer();

	auto vp = iris::Viewport();
	vp.width = width();
	vp.height = height();
	scene->update(dt);
	renderer->renderScene(dt, &vp);
}

PlayerView::~PlayerView()
{

}

void PlayerView::play() {}
void PlayerView::pause() {}
void PlayerView::stop() {}