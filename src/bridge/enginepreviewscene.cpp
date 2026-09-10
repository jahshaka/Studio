#include "bridge/enginepreviewscene.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "irisgl/mirror/scenemirror.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "bridge/offscreenrenderscope.h"
#include "bridge/stableoffscreenrender.h"

using namespace jahshaka::engine;

namespace {
/// Unique per instance: two asset previews (page + dialog) must not name the
/// same engine Scene.
std::string uniqueName(const char *prefix, const void *self)
{
    return std::string(prefix) + "-" + std::to_string(reinterpret_cast<uintptr_t>(self));
}
}   // namespace

EnginePreviewScene::EnginePreviewScene(const std::shared_ptr<Engine> &engine,
                                       const char *namePrefix, sceneworkers::Tier tier)
    : mEngine(engine)
    , mNamePrefix(namePrefix)
    , mWorkerThreads(int(sceneworkers::count(tier)))
{
}

EnginePreviewScene::~EnginePreviewScene()
{
    // A backstop only: by now the subclass half is gone and the hooks resolve
    // to this class's empty ones. Every subclass destructor calls release()
    // first — see the header.
    release();
}

void EnginePreviewScene::adoptView(View *view)
{
    mView = view;
    mOwnsView = view != nullptr;
}

bool EnginePreviewScene::attach(View *view)
{
    auto engine = mEngine.lock();
    if (!engine || !view) return false;
    if (mScene && mView == view) return true;
    if (mScene && mView != view) {
        // Re-bound to another View (a widget's native window was recreated):
        // the Scene moves, it is not rebuilt.
        if (mView) mView->setScene(nullptr);
    } else if (!mScene) {
        // ORDER: the View above already exists, so the Scene can be created.
        mScene = engine->createScene(uniqueName(mNamePrefix, this), unsigned(mWorkerThreads));
        if (!mScene) return false;
        configureScene(mScene);
        mMirror.reset(new SceneMirror(mScene));
        mMirror->setLightWires(false);          // a preview never shows editor wires
        configureMirror(mMirror.get());
    }
    if (mView != view && mOwnsView) {
        // An adopted View was replaced by a caller's: this object no longer
        // owns anything (it never happens today; it must not leak if it does).
        if (mView) { mView->setScene(nullptr); engine->destroyView(mView); }
        mOwnsView = false;
    }
    mView = view;
    mView->setScene(mScene);
    configureView(mView);
    return true;
}

void EnginePreviewScene::release()
{
    auto engine = mEngine.lock();
    // Idempotent all the way down: every branch below is guarded, so a second
    // call (subclass destructor, then ~EnginePreviewScene) does nothing.
    const bool sceneAlive = engine && mScene;
    releaseSubject(sceneAlive);
    if (mMirror) {
        if (sceneAlive) mMirror->setSource(nullptr);
        mMirror.reset();
    }
    if (engine) {
        // Views (workspaces) go before scenes, always.
        if (mView) {
            if (mOwnsView || mView->scene() == mScene) mView->setScene(nullptr);
            if (mOwnsView) engine->destroyView(mView);
        }
        if (mScene) engine->destroyScene(mScene);
    }
    mOwnsView = false;
    mView = nullptr;
    mScene = nullptr;
}

void EnginePreviewScene::pushFrame(const iris::CameraNodePtr &camera, int width, int height)
{
    if (camera) camera->setAspectRatio(height > 0 ? float(width) / float(height) : 1.0f);
    if (mMirror && mView) {
        mMirror->sync();
        mMirror->applySky(mView);
        if (camera) mMirror->applyCamera(camera, mView);
    }
}

QImage EnginePreviewScene::toQImage(const Image &img)
{
    QImage result;
    if (img.width && img.height && img.rgba.size() >= size_t(img.width) * img.height * 4u) {
        result = QImage(int(img.width), int(img.height), QImage::Format_RGBA8888);
        for (unsigned y = 0; y < img.height; ++y)
            std::memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    }
    return result;
}

QImage EnginePreviewScene::renderOffscreen(const char *tag, int width, int height,
                                           const Colour &background, bool shadows)
{
    auto engine = mEngine.lock();
    if (!engine || width <= 0 || height <= 0) return QImage();
    View *shot = engine->createOffscreenView(uniqueName(tag, this) + "-" + std::to_string(++mShotSerial),
                                             unsigned(width), unsigned(height), background);
    if (!shot) return QImage();
    // Nothing attached yet (the widget was never shown): the shot View is the
    // FIRST View, which is what lets the Scene be created (ORDER MATTERS).
    const bool temporary = !mScene;
    if (temporary && !attach(shot)) { engine->destroyView(shot); return QImage(); }
    shot->setScene(mScene);
    shot->setShadows(shadows);
    // The shot is the current View for the duration: a subclass that mirrors
    // through its ordinary per-frame path (the avatar preview does) pushes into
    // THIS view, and the second snapshot of a never-shown page is not a copy of
    // the first.
    View *const previousView = mView;
    mView = shot;
    prepareOffscreen(shot, width, height);
    // The editor does not pay for a preview snapshot (fps audit F5) — see
    // bridge/offscreenrenderscope.h.
    OffscreenRenderScope quiet(engine.get());
    // Two frames, plus whatever the texture load-request counter still owes
    // (THREADING_ADOPTION_SPEC.md P2 item 4) — bridge/stableoffscreenrender.h.
    renderStableFrames(engine.get());
    Image img;
    QImage result;
    if (shot->readPixels(img)) result = toQImage(img);
    shot->setScene(nullptr);
    mView = previousView == shot ? nullptr : previousView;
    engine->destroyView(shot);
    return result;
}
