#include "bridge/offscreenrenderscope.h"
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
}

EnginePlayerScene::~EnginePlayerScene()
{
    release();
    delete mPlayback;
}

bool EnginePlayerScene::ensureScene()
{
    auto engine = mEngine.lock();
    if (!engine) return false;
    if (mScene) return true;
    // On-screen and watched at frame rate, exactly like the editor scene
    // (fps audit F3, bridge/sceneworkerthreads.h).
    mScene = engine->createScene("player-" + std::to_string(reinterpret_cast<uintptr_t>(this)),
                                 sceneworkers::count(sceneworkers::Tier::Primary));
    if (!mScene) return false;
    mScene->setAmbient(Colour(0.25f, 0.27f, 0.32f), Colour(0.15f, 0.15f, 0.18f));
    mMirror.reset(new SceneMirror(mScene));
    mMirror->setLightWires(false);           // the player never shows editor wires
    if (mDocument) mMirror->setSource(mDocument);
    return true;
}

bool EnginePlayerScene::attach(View *view)
{
    auto engine = mEngine.lock();
    if (!engine || !view) return false;
    if (mScene && mView == view) return true;
    if (mScene && mView != view) {
        // Re-bound to another view (the widget's native window was recreated).
        if (mView) mView->setScene(nullptr);
    } else if (!mScene) {
        if (!ensureScene()) return false;
    }
    mView = view;
    mView->setScene(mScene);
    // Shadows, ambient, fog, MSAA, GI and the post chain all come from the
    // document, through applyEnvironment in step() — exactly as they do for the
    // editor viewport. Hardcoding any of them here is what made the player and
    // the editor render the same scene differently.
    if (mMirror) mMirror->invalidateEnvironment();
    return true;
}

void EnginePlayerScene::release()
{
    auto engine = mEngine.lock();
    if (mMirror) {
        if (engine && mScene) mMirror->setSource(nullptr);
        mMirror.reset();
    }
    if (engine && mScene) {
        if (mView && mView->scene() == mScene) mView->setScene(nullptr);
        engine->destroyScene(mScene);
    }
    mScene = nullptr;
    mView = nullptr;
}

void EnginePlayerScene::setDocument(iris::ScenePtr scene, iris::CameraNodePtr camera)
{
    mDocument = scene;
    if (mDocument && camera) mDocument->setCamera(camera);
    if (mMirror) mMirror->setSource(mDocument);
    // PlayBack hands the document's camera to its controllers here, so the camera
    // must be settled first (above).
    if (mDocument && mDocument->getCamera()) mPlayback->setScene(mDocument);
}

iris::CameraNodePtr EnginePlayerScene::camera() const
{
    return mDocument ? mDocument->getCamera() : iris::CameraNodePtr();
}

void EnginePlayerScene::begin()
{
    // Taking the screen back from the editor: some of what applyEnvironment
    // pushes is process-wide in the backend (the HlmsPbs GI binding), so the
    // debounce has to be reset or the editor's binding survives into the player.
    if (mMirror) mMirror->invalidateEnvironment();
    auto cam = camera();
    if (!cam) return;
    mSavedCameraMatrix = cam->getLocalTransform();
    mHaveSavedCamera = true;
    // force camera update to prevent jumping when switching from the editor
    // to the player (PlayerView::start)
    mPlayback->getMouseController()->captureYawPitchRollFromCamera();
    mPlayback->getMouseController()->updateCameraTransform();
}

void EnginePlayerScene::end()
{
    auto cam = camera();
    if (cam && mHaveSavedCamera) cam->setLocalTransform(mSavedCameraMatrix);
    mHaveSavedCamera = false;
}

void EnginePlayerScene::step(float dt, int width, int height)
{
    if (!mDocument || !mView) return;
    auto cam = camera();
    if (!cam) return;

    iris::Viewport vp;
    vp.width = width;
    vp.height = height;
    vp.pixelRatioScale = 1.0f;
    mPlayback->update(vp, dt);
    // The spring-arm follow camera, same call the editor viewport makes and for
    // the same reason: the document computes the arm, the HOST knows which
    // camera it renders (AVATAR_LOCOMOTION_SPEC §8.5).
    if (mDocument && mDocument->getPossession())
        mDocument->getPossession()->applyToViewCamera(cam);

    cam->setAspectRatio(height > 0 ? float(width) / float(height) : 1.0f);
    if (mMirror) {
        mMirror->sync();
        // THE SAME THREE CALLS THE EDITOR VIEWPORT MAKES, IN THE SAME ORDER
        // (EngineSceneViewport::syncFrame). applyEnvironment is what pushes the
        // World panel — ambient (including the sky's own integral, which
        // applySky has just recorded, so the order is load-bearing), shadows,
        // MSAA, fog, GI, planar reflections and the post-processing chain
        // (HDR + filmic tonemap, bloom, SSAO). Leaving it out gave the player a
        // bare passthrough workspace: the scene's HDR values reached an LDR
        // window untonemapped and clipped to white, while the editor showed the
        // same scene filmic — the owner's "dark in the editor, blown out in the
        // player".
        mMirror->applySky(mView);
        mMirror->applyEnvironment(mView, mEngine.lock().get());
        // The player's fly camera is a FREE camera and takes the wide-aspect
        // FOV cap; applyCamera drops it by itself if the active-camera seam
        // substitutes an AUTHORED camera underneath (a playing scene shooting
        // through its own camera keeps that camera's lens exactly).
        mMirror->applyCamera(cam, mView, freecam::kFreeCameraMaxHorizontalFovDegrees);
    }
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
        step(dt >= 0.0f ? dt : 0.016f, width, height);
        engine->renderOneFrame();
    }
}

QImage EnginePlayerScene::takeScreenshot(int width, int height, int grade)
{
    auto engine = mEngine.lock();
    if (!engine || width <= 0 || height <= 0) return QImage();
    // A screenshot can be the FIRST thing asked of the player — before the page
    // has ever been shown, so before attach() ran. The engine Scene and its
    // mirror need no window (only the on-screen View does), so build them here
    // rather than answering "no player" to a perfectly answerable question.
    if (!ensureScene()) return QImage();
    auto cam = camera();
    if (!cam) return QImage();

    static unsigned serial = 0;
    View *shot = engine->createOffscreenView("player-screenshot-" + std::to_string(++serial),
                                             unsigned(width), unsigned(height),
                                             Colour(0.10f, 0.11f, 0.14f));
    if (!shot) return QImage();
    shot->setScene(mScene);
    if (mMirror) {
        mMirror->sync();
        mMirror->applySky(shot);
        mMirror->applyEnvironment(shot);
        // The camera is copied at the shot's OWN aspect ratio, so a 16:9
        // request off a square page does not photograph a squashed world.
        const float saved = cam->aspectRatio;
        cam->setAspectRatio(height > 0 ? float(width) / float(height) : 1.0f);
        mMirror->applyCamera(cam, shot, freecam::kFreeCameraMaxHorizontalFovDegrees);
        cam->setAspectRatio(saved);
        // The three grades (IEditorViewport::ScreenshotGrade), exactly as the
        // editor's takeScreenshot resolves them — the player is the other
        // space, not another policy.
        if (grade == int(IEditorViewport::ScreenshotGrade::Viewport)) {
            jahshaka::engine::PostFxDesc fx = shot->postFx();
            fx.allowOffscreen = true;
            shot->setPostFx(fx);
        } else if (grade == int(IEditorViewport::ScreenshotGrade::Tonemap)) {
            secondaryfx::apply(shot, true);
        }
    }

    // Quiet the on-screen views for the two forced frames (fps audit F5) —
    // the same scope the editor's screenshot uses.
    OffscreenRenderScope quiet(engine.get());
    // Plus whatever the texture load-request counter still owes
    // (THREADING_ADOPTION_SPEC.md P2 item 4) — bridge/stableoffscreenrender.h.
    renderStableFrames(engine.get());

    Image img;
    QImage result;
    if (shot->readPixels(img) && img.width && img.height) {
        result = QImage(int(img.width), int(img.height), QImage::Format_RGBA8888);
        for (unsigned y = 0; y < img.height; ++y)
            memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    }
    engine->destroyView(shot);
    return result;
}
