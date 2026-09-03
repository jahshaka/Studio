#include "bridge/avatarpreview.h"

#include <algorithm>
#include <QMouseEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include "viewport/enginerenderdriver.h"

using namespace jahshaka::engine;

AvatarPreview::AvatarPreview(const std::shared_ptr<Engine> &engine,
                             EngineRenderDriver *driver, QWidget *parent)
    : EngineViewWidget(parent), mEngine(engine), mDriver(driver)
{
    mScene.reset(new AvatarPreviewScene(engine));
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    mFrameTimer.start();
    if (mDriver)
        connect(mDriver, &EngineRenderDriver::beforeFrame, this, &AvatarPreview::syncFrame);
}

AvatarPreview::~AvatarPreview()
{
    mActive = false;
    mScene->release();      // the Scene goes before the View (Engine.h ordering)
    mScene.reset();
    destroyView();
}

void AvatarPreview::setPreviewModel(avatar::AvatarPreviewModel *model)
{
    mScene->setModel(model);
}

void AvatarPreview::framePreview()
{
    mScene->frameSubject();
}

QImage AvatarPreview::renderPreview(int width, int height)
{
    return mScene->renderImage(width, height);
}

void AvatarPreview::resolvePose()
{
    mScene->resolvePose();
}

void AvatarPreview::showEvent(QShowEvent *e)
{
    EngineViewWidget::showEvent(e);
    if (!view() && mEngine)
        createView(mEngine, "avatar-view-" + QString::number(reinterpret_cast<uintptr_t>(this)),
                   Colour(28 / 255.0f, 30 / 255.0f, 36 / 255.0f));
    if (view()) mScene->attach(view());
    mActive = true;
    mFrameTimer.restart();
}

void AvatarPreview::hideEvent(QHideEvent *e)
{
    EngineViewWidget::hideEvent(e);
    mActive = false;
}

void AvatarPreview::syncFrame()
{
    if (!mActive || !view() || !isVisible()) return;
    if (!mScene->attach(view())) return;
    const float dt = std::max(0.001f, float(mFrameTimer.restart()) / 1000.0f);
    mScene->step(dt, width(), height());
}

// ---- mouse: the preview convention (deltas negated) ----

void AvatarPreview::mousePressEvent(QMouseEvent *e)
{
    e->accept();
    mPrevMousePos = e->position();
    mScene->mouseDown(e->button());
}

void AvatarPreview::mouseMoveEvent(QMouseEvent *e)
{
    e->accept();
    const QPointF pos = e->position();
    const QPointF dir = pos - mPrevMousePos;
    mScene->mouseMove(int(-dir.x()), int(-dir.y()));
    mPrevMousePos = pos;
}

void AvatarPreview::mouseReleaseEvent(QMouseEvent *e)
{
    e->accept();
    mScene->mouseUp(e->button());
}

void AvatarPreview::wheelEvent(QWheelEvent *e)
{
    mScene->wheel(e->angleDelta().y());
    e->accept();
}
