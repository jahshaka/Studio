#include "renderwidget.h"
#include "../graphics/spritebatch.h"
#include "../content/contentmanager.h"
#include <QTimer>
#include <QElapsedTimer>

namespace iris
{

void RenderWidget::start()
{

}

void RenderWidget::update(float dt)
{

}

void RenderWidget::render()
{

}

RenderWidget::RenderWidget(QWidget *parent)
{
    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(1);

    setFormat(format);
    // needed in order to get mouse events
    setMouseTracking(true);

    // needed in order to get key events
    // http://stackoverflow.com/a/7879484/991834
    setFocusPolicy(Qt::ClickFocus);

    elapsedTimer = new QElapsedTimer();
}

void RenderWidget::initializeGL()
{
    makeCurrent();

    initializeOpenGLFunctions();

    device = GraphicsDevice::create();
    content = ContentManager::create(device);
    spriteBatch = SpriteBatch::create(device);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    device->setViewport(this->geometry());

    start();

    auto updateTimer = new QTimer(this);
    connect(updateTimer, SIGNAL(timeout()), this, SLOT(update()));
    updateTimer->start();

    elapsedTimer->start();
    //qDebug()<<"initializing";
}

void RenderWidget::paintGL()
{
    float dt = elapsedTimer->nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
    elapsedTimer->restart();

    update(dt);
    render();
}


}
