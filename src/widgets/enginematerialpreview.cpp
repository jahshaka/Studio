#include "enginematerialpreview.h"

#include <algorithm>
#include <QMouseEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include "enginerenderdriver.h"

using namespace jahshaka::engine;

namespace {

PreviewMesh toPreviewMesh(shadergraph::IMaterialPreviewWidget::Model model)
{
    using Model = shadergraph::IMaterialPreviewWidget::Model;
    switch (model) {
    case Model::Sphere:   return PreviewMesh::Sphere;
    case Model::Cube:     return PreviewMesh::Cube;
    case Model::Plane:    return PreviewMesh::Plane;
    case Model::Cylinder: return PreviewMesh::Cylinder;
    case Model::Capsule:  return PreviewMesh::Capsule;
    case Model::Torus:    return PreviewMesh::Torus;
    }
    return PreviewMesh::Sphere;
}

} // namespace

EngineMaterialPreview::EngineMaterialPreview(const std::shared_ptr<Engine> &engine,
                                             EngineRenderDriver *driver, QWidget *parent)
    : EngineViewWidget(parent), mEngine(engine), mDriver(driver)
{
    mScene.reset(new EngineMaterialPreviewScene(engine));
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    mFrameTimer.start();
    if (mDriver)
        connect(mDriver, &EngineRenderDriver::beforeFrame, this, &EngineMaterialPreview::syncFrame);
}

EngineMaterialPreview::~EngineMaterialPreview()
{
    mActive = false;
    mScene->release();      // the Scene goes before the View (Engine.h ordering)
    mScene.reset();
    destroyView();
}

void EngineMaterialPreview::setPreviewMaterial(iris::MaterialPtr material)
{
    mScene->setMaterial(material);
}

void EngineMaterialPreview::setPreviewModel(Model model)
{
    mScene->setPreviewMesh(toPreviewMesh(model));
}

void EngineMaterialPreview::setPreviewBackground(const QColor &colour)
{
    mScene->setBackground(colour);
}

void EngineMaterialPreview::showEvent(QShowEvent *e)
{
    EngineViewWidget::showEvent(e);
    if (!view() && mEngine)
        createView(mEngine, "matpreview-view-" + QString::number(reinterpret_cast<uintptr_t>(this)),
                   Colour(125 / 255.0f, 125 / 255.0f, 125 / 255.0f));
    if (view()) mScene->attach(view());
    mActive = true;
    mFrameTimer.restart();
}

void EngineMaterialPreview::hideEvent(QHideEvent *e)
{
    EngineViewWidget::hideEvent(e);
    mActive = false;
}

void EngineMaterialPreview::syncFrame()
{
    if (!mActive || !view() || !isVisible()) return;
    if (!mScene->attach(view())) return;
    const float dt = std::max(0.001f, float(mFrameTimer.restart()) / 1000.0f);
    mScene->step(dt, width(), height());
}

// ---- mouse: the assets viewer's convention (deltas negated) ----

void EngineMaterialPreview::mousePressEvent(QMouseEvent *e)
{
    e->accept();
    mPrevMousePos = e->position();
    mScene->mouseDown(e->button());
}

void EngineMaterialPreview::mouseMoveEvent(QMouseEvent *e)
{
    e->accept();
    const QPointF pos = e->position();
    const QPointF dir = pos - mPrevMousePos;
    mScene->mouseMove(int(-dir.x()), int(-dir.y()));
    mPrevMousePos = pos;
}

void EngineMaterialPreview::mouseReleaseEvent(QMouseEvent *e)
{
    e->accept();
    mScene->mouseUp(e->button());
}

void EngineMaterialPreview::wheelEvent(QWheelEvent *e)
{
    mScene->wheel(e->angleDelta().y());
    e->accept();
}
