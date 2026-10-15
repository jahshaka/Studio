#include "bridge/offscreenrenderscope.h"
#include "services/framemonitor.h"
#include "bridge/stableoffscreenrender.h"
#include "bridge/sceneworkerthreads.h"
#include "player/engineplayerscene.h"

#include "viewport/freecamerapolicy.h"
#include "viewport/ieditorviewport.h"
#include "bridge/secondarysurfacetonemap.h"

#include <cstdint>
#include <cstring>
#include <string>
#include "irisgl/mirror/scenemirror.h"
#include "player/playback.h"
#include "player/playervr.h"
#include "player/playermousecontroller.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/cameranode.h"

using namespace jahshaka::engine;

EnginePlayerScene::EnginePlayerScene(const std::shared_ptr<Engine> &engine)
    : mEngine(engine)
{
    mPlayback = new PlayBack();
    mPlayback->init();               // the GL-free path: no renderer, no VR hands
    mFrameTimer.start();
}

EnginePlayerScene::~EnginePlayerScene()
{
    release();
    delete mPlayback;
}

void EnginePlayerScene::setEditorScene(Scene *scene, SceneMirror *mirror)
{
    if (mScene == scene && mMirror == mirror) return;
    // The view is bound to the OLD scene; drop that binding before the pointer
    // goes (a View left pointing at a destroyed Scene is the crash this class
    // used to have on the other side of the switch).
    if (mView && mScene && mView->scene() == mScene) mView->setScene(nullptr);
    mScene = scene;
    mMirror = mirror;
    if (mView && mScene) attach(mView);
}

/// THE PAGE THAT CANNOT BIND, ON A BOX WHERE IT ALWAYS CAN
/// (`JAHSHAKA_PLAYER_TEST_NO_ATTACH=1`, SMOKE-FIX-1's fix round, F3).
///
/// WHY A HOOK AT ALL — the same reasoning as the VR lanes' hooks in
/// OgreVrSession.cpp. The refusal path this lane added (a Player page that
/// cannot be bound to the editor's scene refuses, says why, and the window
/// bounces back) is now UNREACHABLE by construction: ensureEngineScene builds
/// the scene for whoever asks, and the only states left that could refuse are
/// an engine with no View at all and a failed view creation — neither of which a
/// suite can produce without breaking the process it needs in order to observe
/// the result. Making the attach answer what a broken one would answer is the
/// only way to test the refusal, the toast's reason reaching `app.space()`, the
/// PlayMode undo and the PLAY START/STOP pairing.
///
/// It overrides nothing else: read once per process (this is a pure "what if",
/// not a lifecycle), and unset — every ordinary run, every gate that does not
/// ask for it — it is one getenv and a compare on a path that runs at a page
/// switch, not per frame.
/// THE SHOT'S SETTLE CAP, in frames: editor.screenshot's (editorapi.cpp
/// kScreenshotSettleFrames), so the two spaces' pictures at rest are the same rule.
static constexpr int kShotSettleCap = 2000;

static bool playerTestNoAttach()
{
    static const bool no = qEnvironmentVariableIntValue("JAHSHAKA_PLAYER_TEST_NO_ATTACH") > 0;
    return no;
}

bool EnginePlayerScene::attach(View *view)
{
    if (playerTestNoAttach()) return false;
    auto engine = mEngine.lock();
    if (!engine || !view || !mScene) return false;
    // IDEMPOTENT AND CHEAP: syncFrame calls this on every frame the page is up,
    // and the work below (a chain rebuild, dropping the mirror's debounce
    // latches) must happen on the BIND, not sixty times a second.
    if (mView == view && view->scene() == mScene) return true;
    if (mView && mView != view && mView->scene() == mScene) {
        // Re-bound to another view (the widget's native window was recreated).
        mView->setScene(nullptr);
    }
    mView = view;
    // THE PLAYER IS A VIEW WITHOUT THE EDITOR'S FURNITURE (ChainDesc::helpers).
    // Set BEFORE the scene bind so the workspace is built once, in its final
    // shape, rather than built and immediately rebuilt.
    mView->setHelpersVisible(false);
    // ONE CONVENTION for the shadow flag (audit F8): the editor viewport sets
    // it true before its first push, and this used to leave it false until
    // buildEnvironment arrived on frame 1 — one guaranteed workspace rebuild per
    // player page, every time. The document's own value follows immediately
    // through buildEnvironment either way.
    mView->setShadows(true);
    if (mView->scene() != mScene) mView->setScene(mScene);
    // Some of what buildEnvironment pushes is PER VIEW (the whole post chain,
    // MSAA, the shadow flag), and the mirror debounces on "already pushed" —
    // which is true, of the OTHER view. Dropping the latches is what makes the
    // player's own chain get built at all.
    if (mMirror) mMirror->invalidateEnvironment();
    return true;
}

void EnginePlayerScene::forgetView()
{
    // A VR SESSION CANNOT OUTLIVE THE VIEW IT MIRRORS ONTO. The widget's native
    // window is being recreated, so the View this object mirrors the headset's
    // eye onto is about to be freed: the session goes first, while every
    // pointer it holds is still good.
    if (mVr) mVr->end();
    mView = nullptr;
}

void EnginePlayerScene::release()
{
    // Same rule as forgetView, for the ordinary teardown.
    if (mVr) mVr->end();
    // The Scene and the mirror belong to the editor viewport: this object
    // created neither and destroys neither. Only the view binding is ours.
    auto engine = mEngine.lock();
    if (engine && mView && mScene && mView->scene() == mScene) mView->setScene(nullptr);
    mView = nullptr;
}

void EnginePlayerScene::setDocument(iris::ScenePtr scene, iris::CameraNodePtr camera)
{
    mDocument = scene;
    if (mDocument && camera) mDocument->setCamera(camera);
    // NO mirror bind: there is one mirror and the editor viewport owns it, with
    // this very document already bound. Binding it a second time is what used
    // to migrate the document's graph between two scene managers on every page
    // switch (the whole of audit F1).
    //
    // PlayBack hands the document's camera to its controllers here, so the camera
    // must be settled first (above).
    if (mDocument && mDocument->getCamera()) mPlayback->setScene(mDocument);
}

iris::CameraNodePtr EnginePlayerScene::camera() const
{
    return mDocument ? mDocument->getCamera() : iris::CameraNodePtr();
}

iris::CameraNodePtr EnginePlayerScene::renderCamera() const
{
    const iris::CameraNodePtr host = camera();
    return mDocument ? mDocument->renderCamera(host) : host;
}

void EnginePlayerScene::begin()
{
    // Taking the screen back from the editor. One scene now, so there is no
    // binding to fight over — but the post chain, MSAA and the shadow flag are
    // pushed PER VIEW and the mirror's latches say "already pushed" about the
    // editor's view. Dropping them is what gives this view its own chain.
    if (mMirror) mMirror->invalidateEnvironment();
    auto cam = camera();
    if (!cam) return;
    mSavedCamera.transform = cam->getLocalTransform();
    mSavedCamera.angle = cam->angle;
    mSavedCamera.orthoSize = cam->orthoSize;
    mSavedCamera.nearClip = cam->nearClip;
    mSavedCamera.farClip = cam->farClip;
    mSavedCamera.perspective = cam->projMode == iris::CameraProjection::Perspective;
    mHaveSavedCamera = true;
    // The first frame after the page comes up must not be charged the whole
    // time the editor was showing.
    mFrameTimer.restart();
    // force camera update to prevent jumping when switching from the editor
    // to the player (PlayerView::start)
    mPlayback->getMouseController()->captureYawPitchRollFromCamera();
    mPlayback->getMouseController()->updateCameraTransform();
}

void EnginePlayerScene::spawnFrom(const iris::CameraNodePtr &editorViewCamera)
{
    if (!mDocument || !editorViewCamera) return;
    // THE SCENE'S OWN SHOT WINS (the owner's "unless a camera node is
    // present"): with a camera armed, the run renders through it and moving
    // the free viewer would place the VR rig — which anchors on the camera the
    // run RENDERS through — somewhere the picture never was.
    if (mDocument->getActiveCamera()) return;
    auto cam = camera();
    if (!cam) return;
    // THE ORDINARY CASE IS ONE NODE. The play camera has been the editor's own
    // camera since SceneViewWidget, so there is nothing to copy and nothing to
    // restore; only a viewport that is PILOTING a scene camera renders through
    // a camera the player does not already hold.
    if (cam == editorViewCamera) return;

    // WORLD SPACE, through the one setter that takes both: the editor's camera
    // may be parented to anything (a piloted scene camera can ride a socket),
    // and what is being copied is where it is LOOKING FROM, not its place in
    // somebody else's hierarchy.
    cam->setGlobalPosRot(editorViewCamera->getGlobalPosition(),
                         editorViewCamera->getGlobalRotation());
    // ...AND THE LENS THE EDITOR VIEW IS USING — the projection block only
    // (field of view, projection mode, the ortho height and the clip planes).
    // The film back, the focus and the exposure of an authored camera are its
    // own look and belong to it; what the player's free viewer needs is to
    // FRAME what the editor framed.
    cam->angle = editorViewCamera->angle;
    cam->orthoSize = editorViewCamera->orthoSize;
    cam->nearClip = editorViewCamera->nearClip;
    cam->farClip = editorViewCamera->farClip;
    cam->setProjection(editorViewCamera->projMode);
    cam->update(0);
    // The controller is about to fly this camera and holds its own heading:
    // re-read it, or the first flown frame snaps back to where the player was
    // last looking.
    if (mPlayback && mPlayback->getMouseController())
        mPlayback->getMouseController()->captureYawPitchRollFromCamera();
}

void EnginePlayerScene::end()
{
    auto cam = camera();
    if (cam && mHaveSavedCamera) {
        cam->setLocalTransform(mSavedCamera.transform);
        cam->angle = mSavedCamera.angle;
        cam->orthoSize = mSavedCamera.orthoSize;
        cam->nearClip = mSavedCamera.nearClip;
        cam->farClip = mSavedCamera.farClip;
        cam->setProjection(mSavedCamera.perspective ? iris::CameraProjection::Perspective
                                                    : iris::CameraProjection::Orthogonal);
        cam->update(0);
    }
    mHaveSavedCamera = false;
}

void EnginePlayerScene::step(float dt, int width, int height)
{
    if (!mDocument || !mView || !mScene) return;
    // THE MIRROR IS THE EDITOR'S AND IT HOLDS THE DOCUMENT. A frame where the
    // two disagree is a frame between a project open and the host's push of the
    // new document: stepping it would drive this view from a camera whose graph
    // node lives in a scene manager that has just been destroyed. Wait one
    // frame instead — EnginePlayerView::setScene is the push.
    if (mMirror && mMirror->source() != mDocument) return;
    auto cam = camera();
    if (!cam) return;

    // Restarted whether or not the wall was used, for the same reason the
    // editor viewport does it: a scripted step must not be charged to the next
    // free-running frame.
    const float wall = float(double(mFrameTimer.nsecsElapsed()) * 1e-9);
    mFrameTimer.restart();
    iris::Viewport vp;
    vp.width = width;
    vp.height = height;
    vp.pixelRatioScale = 1.0f;
    // THE ONE CLOCK (ENGINEERING_DEBT_SPEC A4.2), the player half: the seconds
    // the document's SimulationClock turned into grid steps this frame are
    // what the engine is told to simulate below.
    const float simulated = mPlayback->update(vp, dt >= 0.0f ? dt : wall);
    // The spring-arm follow camera, same call the editor viewport makes and for
    // the same reason: the document computes the arm, the HOST knows which
    // camera it renders (AVATAR_LOCOMOTION_SPEC §8.5).
    if (mDocument && mDocument->getPossession())
        mDocument->getPossession()->applyToViewCamera(cam);
    // THE WEARER, IF THERE IS ONE (phase 3). AFTER everything that moves the
    // play camera and BEFORE the mirror reads it: the rig is flown, pushed to
    // the engine, and the head's world pose becomes the camera — so the last
    // word on where the camera is belongs to the person wearing the headset.
    // A frame with no session costs one pointer test.
    if (mVr) mVr->step(dt >= 0.0f ? dt : wall, renderCamera());

    cam->setAspectRatio(height > 0 ? float(width) / float(height) : 1.0f);
    if (mMirror) {
        // THE PROJECT'S ANSWER ABOUT THE DEFAULT FLOOR, stated BEFORE the sync
        // (PLAYER-FLOOR-1, owner 2026-09-18). The mirror is the EDITOR'S and
        // serves both views, so each host says what it wants of the shared
        // furniture immediately before its own sync — the same shape
        // pushEditorHelpers has on the editor side. The editor states `false`
        // in its own syncFrame, so a page switch either way is exact and no
        // host inherits the other's answer.
        mMirror->setHideDefaultFloor(mDocument->playerHidesFloor);
        // The Ground plane is an EDITOR widget (WORLD-MODEL-1): the finished
        // thing stands on what the scene holds.
        mMirror->setGroundPlane(false);
        // THE SAME FRAME THE EDITOR VIEWPORT EMITS (EngineSceneViewport::syncFrame; U0 3a's one record): the sky,
        // the environment — what pushes the World panel: ambient (including the sky's own integral, which the sky
        // has just recorded, so the order inside the mirror is load-bearing), shadows, MSAA, fog, GI, planar
        // reflections and the post-processing chain (HDR + filmic tonemap, bloom, SSAO); leaving it out gave the
        // player a bare passthrough workspace (the owner's "dark in the editor, blown out in the player") — and the
        // camera. The player's fly camera is a FREE camera and takes the wide-aspect framing hold; the mirror drops
        // it by itself if the active-camera seam substitutes an AUTHORED camera underneath.
        SceneMirror::FrameViews views;
        {
            SceneMirror::FrameView main;
            main.view = mView;
            main.environment = true;
            main.sky = true;
            main.camera = cam;
            main.framingAspect = freecam::kFreeCameraFramingAspect;
            views.views.push_back(main);
        }
        // ...AND THE HEADSET'S EYES BESIDE IT (lane EYE-GRADE-1): the per-view half only — never a second
        // environment (its scene half counts GI settle frames); the rig owns the eyes' poses.
        if (auto engine = mEngine.lock())
            if (jahshaka::engine::View *eyes = engine->vrView()) {
                SceneMirror::FrameView ev;
                ev.view = eyes;
                ev.postFx = true;
                ev.camera = cam;
                ev.pose = false;
                views.views.push_back(ev);
            }
        mMirror->sync(views);
    }
    if (auto engine = mEngine.lock())
        engine->submitFrame(jahshaka::engine::FrameInput::delta(simulated * mDocument->particleTimeScale));
}

PlayerVr *EnginePlayerScene::vr()
{
    // CREATED ON THE FIRST ASK, never at construction: the object holds nothing
    // but a weak engine pointer and a rig, and a player that is never asked for
    // a headset should not carry even that.
    if (!mVr) mVr.reset(new PlayerVr(mEngine.lock()));
    return mVr.get();
}

bool EnginePlayerScene::isPlaying() const { return mPlayback->isScenePlaying(); }

void EnginePlayerScene::play()
{
    if (!mDocument || !camera()) return;
    if (!mPlayback->isScenePlaying()) mPlayback->playScene();
}

void EnginePlayerScene::stop()
{
    if (mPlayback->isScenePlaying()) mPlayback->stopScene();
}

void EnginePlayerScene::stepFrames(int n, float dt, int width, int height)
{
    auto engine = mEngine.lock();
    if (!engine || !mView || !mScene) return;
    for (int i = 0; i < n; ++i) {
        step(dt, width, height);
        // Scripted player stepping: tag the cause per iteration (the engine
        // consumes it and resets to Driver on every frame), so a capture can
        // tell a player frame from the editor's loop.
        //
        // NO DRAIN HERE, unlike the editor's scripted loop: this TU is compiled
        // into two player test targets that link the engine and Qt but nothing
        // of the shell, and FrameMonitor::noteTickEnd would drag EngineHost,
        // the settings manager and the session log into both. Records still
        // reach the bundle — the stop drains the engine's ring in a loop — and
        // the player is a later extension of the monitor's scope anyway
        // (RENDER_LOOP_MONITOR_SPEC SCOPE).
        if (framemonitor::active())
            engine->submitFrame(jahshaka::engine::FrameInput::withCause(jahshaka::engine::FrameCause::Player));
        engine->renderOneFrame();
    }
}

QImage EnginePlayerScene::takeScreenshot(int width, int height, int grade)
{
    auto engine = mEngine.lock();
    if (!engine || width <= 0 || height <= 0) return QImage();
    // A screenshot can be the FIRST thing asked of the player — before the page
    // has ever been shown, so before attach() ran. It needs no window and no
    // player view: the scene is the editor's and it already exists, so the shot
    // is answerable whether or not the page has been up.
    if (!ensureScene()) return QImage();
    auto cam = camera();
    if (!cam) return QImage();

    static unsigned serial = 0;
    View *shot = engine->createOffscreenView("player-screenshot-" + std::to_string(++serial),
                                             unsigned(width), unsigned(height),
                                             Colour(0.10f, 0.11f, 0.14f));
    if (!shot) return QImage();
    shot->setOffscreenContract(jahshaka::engine::OffscreenContract::StillPicture);
    // A PICTURE OF THE PLAYER, so it hides what the player hides: the grid, the
    // wires and icons, the gizmo, the selection shell (ChainDesc::helpers).
    // Before setScene, so the workspace is built once in its final shape.
    shot->setHelpersVisible(false);
    shot->setScene(mScene);
    if (mMirror) {
        // A PLAYER SHOT IS THE PLAYER'S PICTURE (PLAYER-FLOOR-1): the floor the
        // project asked to hide is out of it too, at every grade.
        mMirror->setHideDefaultFloor(mDocument && mDocument->playerHidesFloor);
        mMirror->setGroundPlane(false);
        // The camera is copied at the shot's OWN aspect ratio, so a 16:9 request off a square page does not
        // photograph a squashed world — held for the sync that builds the shot's part of the frame.
        const float saved = cam->aspectRatio;
        cam->setAspectRatio(height > 0 ? float(width) / float(height) : 1.0f);
        SceneMirror::FrameView sv;
        sv.view = shot;
        sv.environment = true;
        sv.sky = true;
        sv.camera = cam;
        sv.framingAspect = freecam::kFreeCameraFramingAspect;
        // The grades (IEditorViewport::ScreenshotGrade), exactly as the editor's takeScreenshot resolves them — the
        // player is the other space, not another policy — on the shot's part of the record.
        sv.finish = [&](jahshaka::engine::FrameInputView &part, jahshaka::engine::View *v) {
            jahshaka::engine::PostFxDesc fx = part.postFx ? *part.postFx : v->pushedPostFx();
            if (grade == int(IEditorViewport::ScreenshotGrade::Scene)) {
                // The player's on-screen view is the one that has been measuring this scene; the shot borrows its
                // exposure (SS1). No view (the player page was never shown) falls back inside applyScene.
                float measured = 0.0f;
                if (mView)
                    if (auto engine = mEngine.lock())   // a REQUEST (U3 stage 3): it maps the meter's ticket
                        engine->request([&](jahshaka::engine::Engine &) { measured = mView->measuredExposureScale(); });
                secondaryfx::applyScene(fx, measured);
            } else if (grade == int(IEditorViewport::ScreenshotGrade::Viewport)) {
                fx.allowOffscreen = true;
            } else if (grade == int(IEditorViewport::ScreenshotGrade::Tonemap)) {
                secondaryfx::apply(fx, true, fx.exposure);
            }
            part.postFx = fx;
        };
        mMirror->sync(SceneMirror::FrameViews{ { sv } });
        cam->setAspectRatio(saved);
    }

    // Quiet the on-screen views for the two forced frames (fps audit F5) —
    // the same scope the editor's screenshot uses.
    OffscreenRenderScope quiet(engine.get());
    // THE PICTURE AT REST, as the editor's shot takes it (PHOTON-GATHER-1d): a
    // hit is lit from the surface cache, which relights over frames after the
    // geometry it describes changes (a hidden floor shown again is recaptured),
    // and the shot view's own history starts young — bridge/stableoffscreenrender.h.
    // THE PICTURE SERVICE (U3 stage 3): the settle, the stable frames and the readback are one request.
    Image img;
    bool read = false;
    takePicture(engine.get(), PictureRequest::stillPicture(mScene, shot, kShotSettleCap), [&](Picture &&p) {
        read = p.ok;
        img = std::move(p.rgba);
    });
    QImage result;
    if (read && img.width && img.height) {
        result = QImage(int(img.width), int(img.height), QImage::Format_RGBA8888);
        for (unsigned y = 0; y < img.height; ++y)
            memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    }
    engine->destroyView(shot);
    // The mirror's per-view latches were just satisfied by a view that no longer
    // exists (buildEnvironment/buildSky above). Drop them so the next on-screen
    // frame — this page's, or the editor's — pushes its own chain again. Cheap:
    // every value below it is idempotent in the engine, and the GI half is a
    // binding re-assert, never a rebuild (SceneMirror::invalidateEnvironment).
    if (mMirror) mMirror->invalidateEnvironment();
    return result;
}
