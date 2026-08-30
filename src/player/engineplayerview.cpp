#include "engineplayerview.h"

#include <algorithm>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include "engineplayerscene.h"
#include "playback.h"
#include "playermousecontroller.h"
#include "../widgets/enginerenderdriver.h"
#include "../engine/enginehost.h"
#include "../editor/ieditorviewport.h"
#include "../uimanager.h"
#include "../core/keyboardstate.h"
#include "irisgl/src/graphics/viewport.h"
#include "irisgl/src/scenegraph/scene.h"

using namespace jahshaka::engine;

EnginePlayerView::EnginePlayerView(const std::shared_ptr<Engine> &engine,
                                   EngineRenderDriver *driver, QWidget *parent)
    : EngineViewWidget(parent), mEngine(engine), mDriver(driver)
{
    mScene.reset(new EnginePlayerScene(engine));
    setMouseTracking(true);                 // PlayerView: needed for mouse events
    setFocusPolicy(Qt::ClickFocus);         // PlayerView: needed for key events
    mFrameTimer.start();
    if (mDriver)
        connect(mDriver, &EngineRenderDriver::beforeFrame, this, &EnginePlayerView::syncFrame);
}

EnginePlayerView::~EnginePlayerView()
{
    mActive = false;
    mScene->release();      // the Scene goes before the View (Engine.h ordering)
    mScene.reset();
    destroyView();
}

iris::CameraNodePtr EnginePlayerView::editorCamera() const
{
    return UiManager::sceneViewWidget ? UiManager::sceneViewWidget->editorCamera() : iris::CameraNodePtr();
}

void EnginePlayerView::setScene(iris::ScenePtr scene)
{
    mDocument = scene;
    mScene->setDocument(scene, editorCamera());
}

void EnginePlayerView::showEvent(QShowEvent *e)
{
    EngineViewWidget::showEvent(e);
    if (!view() && mEngine)
        createView(mEngine, "player-view-" + QString::number(reinterpret_cast<uintptr_t>(this)),
                   Colour(0.1f, 0.1f, 0.1f));
    if (view()) mScene->attach(view());
}

void EnginePlayerView::start()
{
    mActive = true;
    setFocus();
    // The editor camera may have been replaced since setScene (EditorData load).
    if (mDocument) mScene->setDocument(mDocument, editorCamera());
    mScene->begin();
    mFrameTimer.restart();
    if (view()) view()->setEnabled(true);
}

void EnginePlayerView::end()
{
    mActive = false;
    mScene->end();
    if (view()) view()->setEnabled(false);
}

bool EnginePlayerView::isScenePlaying() { return mScene->isPlaying(); }
void EnginePlayerView::playScene()      { mScene->play(); }
void EnginePlayerView::stopScene()      { mScene->stop(); }

void EnginePlayerView::syncFrame()
{
    if (!mActive || !view()) return;
    if (!mScene->attach(view())) return;
    const float dt = std::max(0.001f, float(mFrameTimer.restart()) / 1000.0f);
    mScene->step(dt, width(), height());
}

void EnginePlayerView::resizeEvent(QResizeEvent *e)
{
    EngineViewWidget::resizeEvent(e);
    iris::Viewport vp;
    vp.width = width();
    vp.height = height();
    vp.pixelRatioScale = devicePixelRatio();
    mScene->playback()->getMouseController()->setViewport(vp);
}

void EnginePlayerView::mousePressEvent(QMouseEvent *e)
{
    e->accept();   // never let the press propagate to a container filter
    mScene->playback()->mousePressEvent(e);
}

void EnginePlayerView::mouseMoveEvent(QMouseEvent *e)
{
    e->accept();   // never let the press propagate to a container filter
    mScene->playback()->mouseMoveEvent(e);
}

void EnginePlayerView::mouseReleaseEvent(QMouseEvent *e)
{
    e->accept();   // never let the press propagate to a container filter
    mScene->playback()->mouseReleaseEvent(e);
}

void EnginePlayerView::wheelEvent(QWheelEvent *e)
{
    mScene->playback()->wheelEvent(e);
}

void EnginePlayerView::keyPressEvent(QKeyEvent *e)
{
    KeyboardState::keyStates[e->key()] = true;
    mScene->playback()->keyPressEvent(e);
}

void EnginePlayerView::keyReleaseEvent(QKeyEvent *e)
{
    KeyboardState::keyStates[e->key()] = false;
    mScene->playback()->keyReleaseEvent(e);
}

void EnginePlayerView::focusOutEvent(QFocusEvent *)
{
    KeyboardState::reset();
}

EnginePlayerView *createEnginePlayerView(const std::shared_ptr<Engine> &engine,
                                    EngineRenderDriver *driver, QWidget *parent)
{
    return new EnginePlayerView(engine, driver, parent);
}
