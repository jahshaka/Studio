#include "player/engineplayerview.h"

#include <algorithm>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include "player/engineplayerscene.h"
#include "player/playback.h"
#include "player/playervr.h"
#include "player/playermousecontroller.h"
#include "viewport/enginerenderdriver.h"
#include "bridge/enginehost.h"
#include "services/framemonitor.h"
#include "viewport/ieditorviewport.h"
#include "viewport/keyboardstate.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/scenegraph/cameranode.h"
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

// THE CAMERA THE EDITOR VIEW IS ACTUALLY RENDERING THROUGH (PLAYER-SPAWN-1
// rule 1) — its free explorer, or the scene camera it is PILOTING, which is a
// different node and the one the user is looking through while it lasts
// (EngineSceneViewport::viewCamera). `editorCamera()` above is the explorer's
// own state and stays that: it is what the player's camera IS, and what the
// EditorData round trip saves.
iris::CameraNodePtr EnginePlayerView::editorViewCamera() const
{
    if (!mEditorViewport) return iris::CameraNodePtr();
    if (auto piloted = mEditorViewport->pilotedCamera()) return piloted;
    return mEditorViewport->editorCamera();
}

void EnginePlayerView::setEditorViewport(IEditorViewport *viewport)
{
    mEditorViewport = viewport;
    if (mScene && mScene->playback()) mScene->playback()->setEditorViewport(viewport);
    adoptEditorScene();
}

// ONE SCENE (lane PLAYER-1). The editor viewport builds its engine Scene and its
// SceneMirror in its OWN show event, so they may not exist yet when this view is
// wired up — and the viewport can rebuild neither without going away entirely.
// So this is asked again at every edge that can precede a frame (the wiring, the
// show, the page start, a scripted step), and it is a pair of pointer writes
// when the answer has not changed.
void EnginePlayerView::adoptEditorScene()
{
    if (!mScene) return;
    mScene->setEditorScene(mEditorViewport ? mEditorViewport->engineScene() : nullptr,
                           mEditorViewport ? mEditorViewport->sceneMirror() : nullptr);
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
    adoptEditorScene();
    // A VIEW THAT COULD NOT BE BOUND MUST NOT BE LEFT ENABLED (SMOKE-FIX-1):
    // EngineViewWidget::showEvent has just enabled it unconditionally, and an
    // enabled View with no Scene draws and presents NOTHING — the window then
    // shows whatever the X server had under it, which is how a Player page over
    // a never-shown editor came out as the Desktop page's stale pixels.
    if (view() && !mScene->attach(view())) view()->setEnabled(false);
}

bool EnginePlayerView::start(QString *why)
{
    // ASK FIRST, MUTATE AFTER. The bind is the page's precondition: without it
    // there is nothing to step, nothing to draw and nothing to put back on the
    // way out, so a refusal here leaves the player exactly as it was and the
    // shell can stay on the space the user could see (MainWindow::switchSpace).
    adoptEditorScene();
    if (!view()) {
        if (why)
            *why = viewCreationError().isEmpty()
                       ? tr("the Player's 3D view has not been created yet")
                       : tr("the Player's 3D view could not be created: %1").arg(viewCreationError());
        return false;
    }
    if (!mScene->attach(view())) {
        view()->setEnabled(false);
        if (why)
            *why = tr("the editor's 3D scene is not available yet — the Player draws the editor's "
                      "scene, and the engine has not been able to create it");
        return false;
    }

    mActive = true;
    setFocus();
    // The editor camera may have been replaced since setScene (EditorData load).
    if (mDocument) mScene->setDocument(mDocument, editorCamera());
    mScene->begin();
    // WHEN WE SWITCH, THE PLAYER SHARES THE EDITOR'S VIEWPOINT (PLAYER-SPAWN-1
    // rule 1). AFTER begin(), which is what remembers the pose to put back on
    // the way out, and before the page's own playScene().
    mScene->spawnFrom(editorViewCamera());
    view()->setEnabled(true);
    // THE EXPOSURE HAND-OVER. Auto-exposure is per view and it ADAPTS: a view
    // that has never presented starts from the authored midpoint and walks to
    // the scene's real luminance over the next second — visibly, on the frame
    // the user pressed Play. The editor's view has been looking at this very
    // scene, so its converged multiplier is the right starting point and the
    // Player opens graded.
    //
    // IT SURVIVES THE CHAIN THIS VIEW DOES NOT HAVE YET (lead review round 2).
    // On the FIRST entry this view still carries the passthrough chain, which
    // has no seed pass at all, and the HDR chain is built by frame-1's
    // applyEnvironment a moment later. The engine remembers a value it cannot
    // take yet and spends it on the chain it next builds, before that chain has
    // rendered anything — see View::seedExposureHistory.
    //
    // Not a shared history: after that frame the two views adapt independently,
    // which is what lets them look at different parts of a world. 0 (the editor
    // never presented, no HDR, the fixed grade) leaves the descriptor's own
    // seed in place.
    if (mEditorViewport)
        view()->seedExposureHistory(mEditorViewport->measuredExposureScale());
    return true;
}

void EnginePlayerView::end()
{
    if (mScene && mScene->vrIfAny() && mScene->vr()->isActive()) qWarning("Jahshaka VR: the Player view is ending (page leave / stop) - the VR session ends with it");
    // THE HEADSET COMES OFF WITH THE PAGE (lead review F3, and this lane's own
    // rule that a session belongs to the RUN). Leaving the Player page used to
    // leave the engine's session pumping at the runtime's cadence behind a page
    // nobody is on: the rig and the camera FROZE (syncFrame returns the moment
    // this flag drops), the render driver stayed in VR pacing, and the mirror
    // went on presenting into a window that is no longer mapped. Ending it here
    // is `player.stop()`'s half of the same statement, made by the page.
    //
    // FIRST, while `mActive` is still true: endPlayerVr re-asserts the view's
    // enabled flag from it, and the line below is what turns it off.
    mActive = false;
    endPlayerVr();
    mScene->end();
    if (view()) view()->setEnabled(false);
}

bool EnginePlayerView::isScenePlaying() { return mScene->isPlaying(); }

void EnginePlayerView::playScene()
{
    // A RUN STARTS WHERE THE EDITOR IS LOOKING (PLAYER-SPAWN-1 rule 1), on the
    // stopped->playing EDGE and nowhere else: `player.play()` on an already
    // running player is documented idempotent, and re-placing the camera under
    // a wearer or a pilot would be anything but. This is the half that covers a
    // scripted play with no page switch; start() above covers the switch
    // itself. A no-op whenever the player already holds the editor's camera,
    // which is every session that is not piloting (EnginePlayerScene::spawnFrom).
    if (!mScene->isPlaying()) mScene->spawnFrom(editorViewCamera());
    mScene->play();
}
void EnginePlayerView::stopScene()      { mScene->stop(); }

QImage EnginePlayerView::takePlayerScreenshot(int width, int height, int grade)
{
    adoptEditorScene();
    // Bind the view lazily: a screenshot may be the FIRST thing a script asks
    // of the player, before the page has ever been shown (the native window is
    // created in showEvent). The engine Scene and its mirror do not need the
    // window — only the on-screen View does — so attach if we can and shoot
    // from the scene either way.
    if (view()) mScene->attach(view());
    return mScene->takeScreenshot(width, height, grade);
}

// WHERE THE PLAYER IS LOOKING FROM, AND THROUGH WHAT (PLAYER-SPAWN-1). The one
// read that can tell a run using the scene's armed camera from a run using the
// free viewer the editor handed it — which is the whole of the owner's rule
// seen from a script, and the observable the suites assert on.
//
// WORLD SPACE, deliberately: an armed camera can be parented to anything (a
// socket, an avatar's head), and "where is the player looking from" is a
// question about the world, not about somebody's local frame.
QVariantMap EnginePlayerView::playerCameraReport() const
{
    QVariantMap out;
    if (!mScene) return out;
    const iris::CameraNodePtr cam = mScene->renderCamera();
    if (!cam) return out;                       // no document open yet
    const iris::ScenePtr doc = mScene->document();
    const iris::CameraNodePtr armed = doc ? doc->getActiveCamera() : iris::CameraNodePtr();
    const bool authored = armed && armed == cam;
    const iris::Vec3 pos = cam->getGlobalPosition();
    const iris::Quat rot = cam->getGlobalRotation();
    // The armed camera is a scene NODE and names itself; the free viewer is
    // not in the document at all, so it has no id to give.
    out[QStringLiteral("id")] = authored ? QVariant(cam->getGUID()) : QVariant();
    out[QStringLiteral("name")] = authored ? QVariant(cam->getName()) : QVariant();
    out[QStringLiteral("source")] = authored ? QStringLiteral("active")
                                             : QStringLiteral("viewport");
    out[QStringLiteral("position")] = QVariantMap{ { QStringLiteral("x"), pos.x() },
                                                   { QStringLiteral("y"), pos.y() },
                                                   { QStringLiteral("z"), pos.z() } };
    out[QStringLiteral("rotation")] = QVariantMap{ { QStringLiteral("x"), rot.x() },
                                                   { QStringLiteral("y"), rot.y() },
                                                   { QStringLiteral("z"), rot.z() },
                                                   { QStringLiteral("scalar"), rot.scalar() } };
    out[QStringLiteral("fov")] = cam->angle;
    out[QStringLiteral("projection")] = cam->projMode == iris::CameraProjection::Perspective
                                            ? QStringLiteral("perspective")
                                            : QStringLiteral("orthogonal");
    out[QStringLiteral("orthoSize")] = cam->orthoSize;
    out[QStringLiteral("nearClip")] = cam->nearClip;
    out[QStringLiteral("farClip")] = cam->farClip;
    return out;
}

bool EnginePlayerView::stepPlayerFrames(int n, float dt)
{
    if (!view()) return false;
    adoptEditorScene();
    if (!mScene->attach(view())) return false;
    mScene->stepFrames(n, dt, width(), height());
    // THESE ARE FRAMES ON THE SAME WINDOW (DOUBLE-FRAME-1, the lead's fix-round
    // item 4). `editor.frame` routes here whenever the Player owns the screen
    // (EditorApi::frame -> playerHasTheScreen), so a script stepping frames in
    // play mode draws exactly as the editor's scripted loop does — and the
    // render driver's Live pacing must count those frames too, or the loop adds
    // one of its own in the gap after each verb on top of the one just drawn.
    //
    // ONCE, AFTER THE LOOP, not per frame: `EnginePlayerScene::stepFrames`
    // renders n frames without pumping the event loop, so no tick can fire
    // between them and the only moment that matters is the last frame's end —
    // which is exactly what the pacing clock measures from. (The call cannot
    // live in that TU: it is compiled into two player test targets that link
    // the engine and Qt but nothing of the shell, which is the same reason its
    // own loop does not drain the frame monitor.)
    if (mDriver) mDriver->noteExternalFrame();
    return true;
}

// ---------------------------------------------------------------------------
// THE PLAYER'S VR MODE (SPECS/VR_SPEC.md §4.5, phase 3).
//
// The widget's half is small on purpose: the session, the rig and the mirror
// live in PlayerVr (player/playervr.h), which is driven from the player's own
// frame and is testable without a widget. What belongs HERE is the two things
// only the widget knows — which View the headset mirrors onto, and the render
// DRIVER whose clock the runtime takes over.

// EVERY REFUSAL THAT CAN BE KNOWN BEFORE ANYTHING MOVES, in one place, asked by
// the service before it starts the run (IPlayerHost::canBeginPlayerVr) and
// again by the begin below, which must remain safe on its own.
//
// THE MOST INFORMATIVE ONE FIRST: on a box with no runtime — every box, most
// days — the honest answer is "there is no VR in this process", not "the Player
// page has not been shown", which is a true sentence about an irrelevant fact.
bool EnginePlayerView::canBeginPlayerVr(QString *why) const
{
    const auto no = [why](const QString &text) {
        if (why) *why = text;
        return false;
    };
    if (!mEngine || !mScene) return no(QStringLiteral("this session has no player backend"));
    if (!mEngine->vrAvailable())
        return no(QStringLiteral("VR is not available (%1)")
                      .arg(QString::fromStdString(mEngine->vrInfo().reason)));
    // THE MIRROR IS THIS VIEW, so there has to be one: the player's on-screen
    // View is created by its show event, and a VR session started from a page
    // that has never been shown would have nowhere to put the desktop's
    // picture. The toggle switches to the Player page first, which is exactly
    // what creates it.
    if (!view())
        return no(QStringLiteral("the Player page has not been shown yet — its on-screen "
                                 "view is created when the page opens"));
    return true;
}

bool EnginePlayerView::beginPlayerVr(const QVariantMap &options, QString *error)
{
    if (!canBeginPlayerVr(error)) return false;
    adoptEditorScene();
    if (!mScene->attach(view())) {
        if (error) *error = QStringLiteral("the player has no scene to show yet");
        return false;
    }
    // THE VIEW'S VISIBILITY IS THIS WIDGET'S ANSWER, not the VR mode's (lead
    // review F4): PlayerVr switches this view off for the session and asks HERE
    // what to switch it back to, whenever the session ends — including a
    // session ended from a script or by a lost device, when the page may no
    // longer be up.
    mScene->vr()->setViewRestore([this]() { if (view()) view()->setEnabled(mActive); });
    if (!mScene->vr()->begin(mScene->engineScene(), view(), mScene->renderCamera(),
                             mScene->document(), options, error))
        return false;
    // THE LOOP'S CLOCK IS THE RUNTIME NOW (VR_SPEC §4.3): zero interval, vsync
    // off, and renderOneFrame blocks in xrWaitFrame instead. The driver
    // reconciles with the engine on every tick since VR-2's F5, so this is the
    // fast path rather than the only thing keeping the two in step.
    if (mDriver) mDriver->setVrSessionActive(true);
    return true;
}

void EnginePlayerView::endVrForSceneClose()
{
    if (mScene && mScene->vrIfAny() && mScene->vr()->isActive()) {
        qWarning("Jahshaka VR: the Player's session ends before its scene closes (project close / open in place)");
        endPlayerVr();
    }
}

void EnginePlayerView::endPlayerVr()
{
    if (mScene && mScene->vrIfAny()) mScene->vr()->end();
    // ...and the view goes back to whatever this page is doing NOW. The restore
    // callback above covers the paths PlayerVr notices by itself; this covers
    // the ordinary one and costs nothing when it is already right.
    if (view()) view()->setEnabled(mActive);
    if (mDriver) mDriver->setVrSessionActive(false);
}

bool EnginePlayerView::isPlayerVrActive() const
{
    const PlayerVr *vr = mScene ? mScene->vrIfAny() : nullptr;
    return vr && vr->isActive();
}

QVariantMap EnginePlayerView::playerVrReport() const
{
    const PlayerVr *vr = mScene ? mScene->vrIfAny() : nullptr;
    QVariantMap out = vr ? vr->report() : PlayerVr::idleReport();
    // CAN THIS PROCESS DO VR AT ALL, and why not — answered whether or not a
    // VR mode has ever been created, because that is exactly the question the
    // toolbar icon asks before it decides to be enabled (and the one the
    // tooltip answers when it is not). Fixed at boot: the engine asked the
    // runtime once, before the render system existed.
    out[QStringLiteral("available")] = mEngine && mEngine->vrAvailable();
    out[QStringLiteral("reason")] =
        mEngine ? QString::fromStdString(mEngine->vrInfo().reason)
                : QStringLiteral("no engine is running in this process");
    return out;
}

bool EnginePlayerView::movePlayerVr(const flystep::Keys &keys, float seconds)
{
    return mScene && mScene->vrIfAny() && mScene->vr()->move(keys, seconds);
}

bool EnginePlayerView::recenterPlayerVr()
{
    return mScene && mScene->vrIfAny() && mScene->vr()->recenter();
}

void EnginePlayerView::syncFrame()
{
    if (!mActive || !view()) return;
    // RE-ADOPT FIRST, EVERY FRAME (lead review round 2). The editor's Scene and
    // its mirror are DESTROYED on a project close or an async open — which the
    // user can trigger from this very page — and the viewport nulls its own
    // pointers, so ours would be dangling. Engine::destroyScene detaches every
    // view bound to that scene, so `view()->scene()` is already null here and
    // nothing below would notice: attach() would call setScene() on a freed
    // Scene and step() would sync a freed SceneMirror.
    //
    // adoptEditorScene is null-safe on both sides and a pair of pointer
    // compares when nothing moved, so it belongs at the top of the only edge
    // that did not already have it.
    adoptEditorScene();
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
