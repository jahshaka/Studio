#include "player/engineplayerscene.h"

#include <cstdint>
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

bool EnginePlayerScene::attach(View *view)
{
    auto engine = mEngine.lock();
    if (!engine || !view) return false;
    if (mScene && mView == view) return true;
    if (mScene && mView != view) {
        // Re-bound to another view (the widget's native window was recreated).
        if (mView) mView->setScene(nullptr);
    } else if (!mScene) {
        mScene = engine->createScene("player-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        if (!mScene) return false;
        mScene->setAmbient(Colour(0.25f, 0.27f, 0.32f), Colour(0.15f, 0.15f, 0.18f));
        mMirror.reset(new SceneMirror(mScene));
        mMirror->setLightWires(false);           // the player never shows editor wires
        if (mDocument) mMirror->setSource(mDocument);
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
        mMirror->applyCamera(cam, mView);
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
