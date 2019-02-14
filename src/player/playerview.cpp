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

	// needed in order to get mouse events
	setMouseTracking(true);
}

void PlayerView::initializeGL()
{
	QOpenGLWidget::initializeGL();
	makeCurrent();
	initializeOpenGLFunctions();

	renderer = iris::ForwardRenderer::create();
	renderer->setScene(scene);

	updateTimer = new QTimer(this);
	connect(updateTimer, &QTimer::timeout, [=]() {
		update();
	});
	updateTimer->start(Constants::FPS_60);

	fpsTimer = new QElapsedTimer();
	fpsTimer->start();
}

void PlayerView::setScene(iris::ScenePtr scene)
{
	this->scene = scene;
	if (renderer)
		renderer->setScene(scene);
}

void PlayerView::end()
void PlayerView::paintGL()
{
	renderScene();
}

void PlayerView::renderScene()
{
	makeCurrent();

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