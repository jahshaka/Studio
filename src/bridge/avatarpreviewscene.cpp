#include "bridge/avatarpreviewscene.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <QQuaternion>

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "modules/avatar/avatarpreviewmodel.h"
#include "viewport/boneoverlay.h"
#include "viewport/previewframing.h"

using namespace jahshaka::engine;

namespace {
float lerp(float a, float b, float t) { return a * (1 - t) + b * t; }
}

AvatarPreviewScene::AvatarPreviewScene(const std::shared_ptr<Engine> &engine)
    : mEngine(engine)
{
}

AvatarPreviewScene::~AvatarPreviewScene()
{
    release();
}

bool AvatarPreviewScene::attach(View *view)
{
    auto engine = mEngine.lock();
    if (!engine || !view) return false;
    if (mScene && mView == view) return true;
    if (mScene && mView != view) {
        if (mView) mView->setScene(nullptr);
    } else if (!mScene) {
        mScene = engine->createScene("avatarpreview-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        if (!mScene) return false;
        mScene->setAmbient(Colour(0.35f, 0.36f, 0.40f), Colour(0.22f, 0.22f, 0.26f));
        // One planar-reflection slot for the Modern room's floor plate
        // (AVATAR_SPACE_SPEC). Set ONCE at scene creation — changing the
        // budget recompiles PBS shaders, and pushing the same value is free.
        // With no reflector armed (Grid mode, headless) an empty budget slot
        // costs a render target's memory and nothing per-frame.
        {
            PlanarReflectionParams pr;
            pr.budget = 1;
            pr.resolution = 512;
            mScene->setPlanarReflections(pr);
        }
        mMirror.reset(new SceneMirror(mScene));
        mMirror->setLightWires(false);          // a preview never shows editor wires
        // A PLAIN WHITE ground grid, so the character stands on something
        // instead of floating in space. Colours and extent are the preview's,
        // not the editor's (the editor keeps its blue-grey ±100 floor); the
        // spacing follows the subject, because a Mixamo character imports
        // ~170 units tall and a 1-unit grid under it is a white sheet.
        mMirror->setGridColours(Colour(1.0f, 1.0f, 1.0f, 0.16f),
                                Colour(1.0f, 1.0f, 1.0f, 0.38f));
        applyGrid();
        mOverlay.reset(new BoneOverlay(mScene));
        if (mModel) bindModel(mModel);
    }
    mView = view;
    mView->setScene(mScene);
    mView->setShadows(false);
    return true;
}

void AvatarPreviewScene::release()
{
    auto engine = mEngine.lock();
    // The pose source captures this scene's mirror; it must not outlive it.
    if (mModel) mModel->setPoseSource(nullptr);
    if (mOverlay) {
        if (engine && mScene) mOverlay->clear();
        mOverlay.reset();
    }
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

void AvatarPreviewScene::bindModel(avatar::AvatarPreviewModel *model)
{
    if (!mMirror) return;
    mMirror->setSource(model ? model->document() : iris::ScenePtr());
    if (!model) return;
    // WHERE THE POSE COMES FROM, since the document stopped computing one.
    // The bone scene nodes still describe the rig's shape and its REST
    // transforms; the pose lives in the engine's SkeletonInstance, and this is
    // the wire that brings it back for the overlay and for avatar.bones().
    SceneMirror *mirror = mMirror.get();
    model->setPoseSource([mirror](QHash<QString, QMatrix4x4> &out) {
        return mirror->boneWorldTransforms(out);
    });
}

void AvatarPreviewScene::setModel(avatar::AvatarPreviewModel *model)
{
    if (mModel && mModel != model) mModel->setPoseSource(nullptr);
    mModel = model;
    bindModel(model);
    if (model) frameSubject();
}

int AvatarPreviewScene::overlaySegments() const
{
    return mOverlay ? mOverlay->visibleSegments() : 0;
}

int AvatarPreviewScene::overlayStubs() const
{
    return mOverlay ? mOverlay->visibleStubs() : 0;
}

int AvatarPreviewScene::overlayJoints() const
{
    return mOverlay ? mOverlay->visibleJoints() : 0;
}

void AvatarPreviewScene::frameSubject()
{
    if (!mModel) return;
    auto camera = mModel->camera();
    if (!camera) return;
    auto fragment = mModel->fragment();

    iris::BoundingSphere bound;
    if (fragment) {
        const iris::AABB aabb = preview::worldBoundingBox(fragment);
        bound = aabb.getMinimalEnclosingSphere();
    }
    if (bound.radius <= 0.0f) { bound.pos = QVector3D(0, 0, 0); bound.radius = 1.0f; }

    mSubjectRadius = bound.radius;
    mPivot = bound.pos;
    mDistFromPivot = preview::framingDistance(bound.radius, camera->angle);
    applyGrid();                                  // the grid follows the subject's scale
    // A Mixamo character imports 138-179 units tall; iris's default farClip is
    // 500 and the framing distance is ~2.9 radii, so without this the subject
    // sits entirely beyond its own far plane and the view renders NOTHING.
    applyClipPlanes();

    mYaw = mTargetYaw = 0.0f;
    mPitch = mTargetPitch = -5.0f;
    updateCameraRot();
}

void AvatarPreviewScene::applyGrid()
{
    if (!mMirror) return;
    // ~8 cells across the subject, and a floor four subjects wide. Both are
    // derived, so a 2-unit test rig and a 179-unit character get the same
    // picture at different scales. No subject, no floor: an empty page would
    // otherwise show a grid framed for a 1-unit subject, edge-on.
    const float spacing = qMax(mSubjectRadius, 0.25f) * 0.25f;
    mMirror->setGridExtent(spacing * 20.0f);
    // The wireframe grid belongs to GRID mode only: in the Modern room it
    // draws at y=0 and shows through the floor's line gaps as a ghost ground
    // (owner sighting, 2026-09-05). sync() re-applies this every frame, so a
    // spaceMode change needs no extra signal.
    const bool modern = mModel && mModel->spaceMode() == avatar::SpaceMode::Modern;
    mMirror->setGrid(mModel && mModel->isLoaded() && !modern, spacing);
}

void AvatarPreviewScene::applyClipPlanes()
{
    if (!mModel) return;
    auto camera = mModel->camera();
    if (!camera) return;
    preview::clipPlanesForFraming(mDistFromPivot, qMax(mSubjectRadius, 1.0f),
                                  camera->nearClip, camera->farClip);
}

void AvatarPreviewScene::updateCameraRot()
{
    if (!mModel) return;
    auto camera = mModel->camera();
    if (!camera) return;
    const auto rot = QQuaternion::fromEulerAngles(mPitch, mYaw, 0);
    const auto localPos = rot.rotatedVector(QVector3D(0, 0, 1));
    camera->setLocalPos(mPivot + localPos * mDistFromPivot);
    camera->setLocalRot(rot);
    camera->update(0);
}

void AvatarPreviewScene::mouseDown(Qt::MouseButton b)
{
    if (b == Qt::LeftButton) mLeftDown = true;
    if (b == Qt::RightButton) mRightDown = true;
    if (b == Qt::MiddleButton) mMiddleDown = true;
}

void AvatarPreviewScene::mouseUp(Qt::MouseButton b)
{
    if (b == Qt::LeftButton) mLeftDown = false;
    if (b == Qt::RightButton) mRightDown = false;
    if (b == Qt::MiddleButton) mMiddleDown = false;
}

void AvatarPreviewScene::mouseMove(int dx, int dy)
{
    if (mLeftDown || mRightDown) orbit(dx * mRotationSpeed, dy * mRotationSpeed);
    if (mMiddleDown && mModel && mModel->camera()) {
        const float dragSpeed = 0.002f * qMax(mSubjectRadius, 1.0f);
        auto dir = mModel->camera()->getLocalRot().rotatedVector(
            QVector3D(dx * dragSpeed, -dy * dragSpeed, 0));
        mPivot += dir;
    }
    updateCameraRot();
}

void AvatarPreviewScene::orbit(float yawDegrees, float pitchDegrees)
{
    mYaw = mTargetYaw + yawDegrees;
    mPitch = mTargetPitch + pitchDegrees;
    mTargetYaw = mYaw;
    mTargetPitch = mPitch;
    updateCameraRot();
}

void AvatarPreviewScene::wheel(int delta)
{
    // Zoom in units of the subject, so a 2-unit rig and a 179-unit character
    // both take the same number of notches to cross the frame.
    mDistFromPivot += -delta * 0.002f * qMax(mSubjectRadius, 0.5f);
    const float minDist = qMax(0.1f, mSubjectRadius * 0.2f);
    if (mDistFromPivot < minDist) mDistFromPivot = minDist;
    applyClipPlanes();
    updateCameraRot();
}

void AvatarPreviewScene::step(float dt, int width, int height)
{
    if (!mModel) return;
    mYaw = lerp(mYaw, mTargetYaw, 0.8f);
    mPitch = lerp(mPitch, mTargetPitch, 0.8f);
    updateCameraRot();

    // ORDER (§0.5.1): pose -> mirror (refreshes global transforms) -> overlay.
    mModel->advance(dt);
    applyGrid();     // cheap (two floats); tracks load/clear without a signal
    auto camera = mModel->camera();
    if (camera) camera->setAspectRatio(height > 0 ? float(width) / float(height) : 1.0f);
    if (mMirror && mView) {
        mMirror->sync();
        mMirror->applySky(mView);
        if (camera) mMirror->applyCamera(camera, mView);
    }
    if (mOverlay) {
        QVector<BoneOverlaySegment> segments;
        if (mModel->skeletonVisible()) {
            for (const auto &s : mModel->boneSegments())
                segments.append(BoneOverlaySegment{ s.from, s.to, s.toAxis, s.toIsLeaf });
        }
        mOverlay->update(segments, mModel->skeletonVisible());
    }
}

void AvatarPreviewScene::resolvePose()
{
    // A bone's transform is resolved by the engine's scene-graph update, i.e.
    // during a render. A verb that READS a pose (avatar.bones, and the overlay
    // through it) therefore has to make sure one has happened since the last
    // setTime — otherwise a script that sets a time and immediately asks for a
    // bone gets the previous frame's answer, which reads as "the clip does
    // nothing". Cheap: one sync and one frame of an already-live view.
    auto engine = mEngine.lock();
    if (!engine || !mScene || !mView || !mMirror || !mModel) return;
    mMirror->sync();
    engine->renderOneFrame();
}

QImage AvatarPreviewScene::toQImage(const Image &img)
{
    QImage result;
    if (img.width && img.height && img.rgba.size() >= size_t(img.width) * img.height * 4u) {
        result = QImage(int(img.width), int(img.height), QImage::Format_RGBA8888);
        for (unsigned y = 0; y < img.height; ++y)
            std::memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    }
    return result;
}

QImage AvatarPreviewScene::renderImage(int width, int height)
{
    auto engine = mEngine.lock();
    if (!engine || !mModel || width <= 0 || height <= 0) return QImage();
    const QColor c = mModel->document()->skyColor;
    View *shot = engine->createOffscreenView(
        "avatar-shot-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + std::to_string(++mShotSerial),
        unsigned(width), unsigned(height), Colour(float(c.redF()), float(c.greenF()), float(c.blueF()), 1.0f));
    if (!shot) return QImage();
    // Not attached anywhere yet (the widget was never shown): the shot view is
    // the first view, which is what lets the scene be created at all (ORDER).
    const bool temporary = !mScene;
    if (temporary && !attach(shot)) { engine->destroyView(shot); return QImage(); }
    shot->setScene(mScene);
    shot->setShadows(false);
    // step() only mirrors when it has a view (`mMirror && mView`), and this
    // function clears mView when it is done with a shot view. Without making
    // the shot the current view for the duration, the SECOND and every later
    // snapshot of a page that is not on screen renders whatever pose was
    // current at the FIRST one — the document advances, the engine never hears
    // about it. (Found by rendering two clips through avatar.snapshot: the
    // second image came back byte-identical to the first.)
    View *const previousView = mView;
    mView = shot;
    step(0.0f, width, height);
    if (mMirror) {
        mMirror->applySky(shot);
        if (mModel->camera()) {
            mModel->camera()->setAspectRatio(float(width) / float(height));
            mModel->camera()->update(0);
            mMirror->applyCamera(mModel->camera(), shot);
        }
    }
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    Image img;
    QImage result;
    if (shot->readPixels(img)) result = toQImage(img);
    shot->setScene(nullptr);
    mView = previousView == shot ? nullptr : previousView;
    engine->destroyView(shot);
    return result;
}
