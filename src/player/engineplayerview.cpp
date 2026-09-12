#include "player/engineplayerview.h"

#include <algorithm>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include "player/engineplayerscene.h"
#include "player/playback.h"
#include "player/playermousecontroller.h"
#include "viewport/enginerenderdriver.h"
#include "bridge/enginehost.h"
#include "services/framemonitor.h"
#include "viewport/ieditorviewport.h"
#include "viewport/keyboardstate.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/input/inputmap.h"

void EnginePlayerView::viewAboutToBeDestroyed()
{
    if (mScene) mScene->forgetView();
}

using namespace jahshaka::engine;

EnginePlayerView::EnginePlayerView(const std::shared_ptr<Engine> &engine,
                                   EngineRenderDriver *driver, QWidget *parent)
    : EngineViewWidget(parent), mEngine(engine), mDriver(driver)
{
    mScene.reset(new EnginePlayerScene(engine));
    setMouseTracking(true);                 // PlayerView: needed for mouse events
    setFocusPolicy(Qt::ClickFocus);         // PlayerView: needed for key events
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
    return mEditorViewport ? mEditorViewport->editorCamera() : iris::CameraNodePtr();
}

void EnginePlayerView::setEditorViewport(IEditorViewport *viewport)
{
    mEditorViewport = viewport;
    if (mScene && mScene->playback()) mScene->playback()->setEditorViewport(viewport);
}

void EnginePlayerView::setScene(iris::ScenePtr scene)
{
    mDocument = scene;
    mScene->setDocument(scene, editorCamera());
}

void EnginePlayerView::showEvent(QShowEvent *e)
{
    EngineViewWidget::showEvent(e);
    // The clear colour is the EDITOR VIEWPORT's, to the digit
    // (EngineSceneViewport::showEvent): it is what a scene with no sky shows and
    // what planar reflections clear to, so a different grey here is a visible
    // editor/player mismatch.
    if (!view() && mEngine)
        createView(mEngine, "player-view-" + QString::number(reinterpret_cast<uintptr_t>(this)),
                   Colour(0.10f, 0.11f, 0.14f));
    if (view()) mScene->attach(view());
}

void EnginePlayerView::start()
{
    mActive = true;
    setFocus();
    // The editor camera may have been replaced since setScene (EditorData load).
    if (mDocument) mScene->setDocument(mDocument, editorCamera());
    mScene->begin();
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

QImage EnginePlayerView::takePlayerScreenshot(int width, int height, int grade)
{
    // Bind the view lazily: a screenshot may be the FIRST thing a script asks
    // of the player, before the page has ever been shown (the native window is
    // created in showEvent). The engine Scene and its mirror do not need the
    // window — only the on-screen View does — so attach if we can and shoot
    // from the scene either way.
    if (view()) mScene->attach(view());
    return mScene->takeScreenshot(width, height, grade);
}

bool EnginePlayerView::stepPlayerFrames(int n, float dt)
{
    if (!view()) return false;
    if (!mScene->attach(view())) return false;
    mScene->stepFrames(n, dt, width(), height());
    return true;
}

void EnginePlayerView::syncFrame()
{
    if (!mActive || !view()) return;
    if (!mScene->attach(view())) return;
    // THE FRAME ABOUT TO BE RENDERED IS A PLAYER FRAME (RENDER_LOOP_MONITOR_SPEC
    // §4.2's frame reason). This runs inside the driver's beforeFrame, after the
    // driver's own `Driver` tag and before renderOneFrame, so the last word is
    // the truthful one: a frame whose sync included an ACTIVE player view is
    // the player's. The monitor's SCOPE is the editor for now — this costs one
    // branch and means a capture taken in the player space is not silently
    // labelled as the editor's loop.
    if (framemonitor::active())
        if (auto engine = EngineHost::instance().engine())
            engine->setNextFrameCause(jahshaka::engine::FrameCause::Player);
    mScene->step(-1.0f, width(), height());     // the wall clock, in the scene's timer
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
    // A gameplay key released while another widget had focus never reaches us.
    iris::InputSystem::instance().clearKeys();
}

// THE PLAY-MODE KEY PATH, player-page half (AVATAR_LOCOMOTION_SPEC §8.3).
// ADDED, not amended: this class had no event() override at all, so every
// window-scoped shortcut in MainWindow's registry ate its key before
// keyPressEvent ran — Space in particular, which is `tool.cycle` and is
// page-routed rather than editor-guarded. The editor viewport's override is
// the model; the predicate here is isScenePlaying() (the player page's own
// play flag), so a stopped player page leaves the shortcuts exactly as they
// are today.
bool EnginePlayerView::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride && mScene &&
        iris::gameplayClaimsKey(mScene->isPlaying(), static_cast<QKeyEvent *>(e)->key())) {
        e->accept();
        return true;
    }
    return EngineViewWidget::event(e);
}

EnginePlayerView *createEnginePlayerView(const std::shared_ptr<Engine> &engine,
                                    EngineRenderDriver *driver, QWidget *parent)
{
    return new EnginePlayerView(engine, driver, parent);
}
