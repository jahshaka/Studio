#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "bridge/avatarpreviewscene.h"

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "modules/avatar/avatarpreviewmodel.h"
#include "viewport/boneoverlay.h"
#include "viewport/previewframing.h"
#include "viewport/previeworbit.h"

using namespace jahshaka::engine;

AvatarPreviewScene::AvatarPreviewScene(const std::shared_ptr<Engine> &engine)
    : EnginePreviewScene(engine, "avatarpreview", sceneworkers::Tier::Preview)
{
}

AvatarPreviewScene::~AvatarPreviewScene()
{
    // The base destructor cannot run the hooks (see enginepreviewscene.h).
    release();
}

void AvatarPreviewScene::configureScene(Scene *scene)
{
    scene->setAmbient(Colour(0.35f, 0.36f, 0.40f), Colour(0.22f, 0.22f, 0.26f));
    // One planar-reflection slot for the Modern room's floor plate
    // (AVATAR_SPACE_SPEC). Set ONCE at scene creation — changing the
    // budget recompiles PBS shaders, and pushing the same value is free.
    // With no reflector armed (Grid mode, headless) an empty budget slot
    // costs a render target's memory and nothing per-frame.
    PlanarReflectionParams pr;
    pr.budget = 1;
    pr.resolution = 512;
    scene->setPlanarReflections(pr);
}

void AvatarPreviewScene::configureMirror(SceneMirror *mirror)
{
    // A PLAIN WHITE ground grid, so the character stands on something
    // instead of floating in space. Colours and extent are the preview's,
    // not the editor's (the editor keeps its blue-grey ±100 floor); the
    // spacing follows the subject, because a Mixamo character imports
    // ~170 units tall and a 1-unit grid under it is a white sheet.
    mirror->setGridColours(Colour(1.0f, 1.0f, 1.0f, 0.16f),
                           Colour(1.0f, 1.0f, 1.0f, 0.38f));
    applyGrid();
    mOverlay.reset(new BoneOverlay(mScene));
    if (mModel) bindModel(mModel);
}

void AvatarPreviewScene::configureView(View *view)
{
    view->setShadows(false);
}

void AvatarPreviewScene::releaseSubject(bool sceneAlive)
{
    // The pose source captures this scene's mirror; it must not outlive it.
    if (mModel) mModel->setPoseSource(nullptr);
    if (mOverlay) {
        if (sceneAlive) mOverlay->clear();
        mOverlay.reset();
    }
}

void AvatarPreviewScene::bindModel(avatar::AvatarPreviewModel *model)
{
    if (!mirror()) return;
    mirror()->setSource(model ? model->document() : iris::ScenePtr());
    if (!model) return;
    // WHERE THE POSE COMES FROM, since the document stopped computing one.
    // The bone scene nodes still describe the rig's shape and its REST
    // transforms; the pose lives in the engine's SkeletonInstance, and this is
    // the wire that brings it back for the overlay and for avatar.bones().
    SceneMirror *const source = mirror();
    model->setPoseSource([source](QHash<QString, iris::Mat4> &out) {
        return source->boneWorldTransforms(out);
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
    if (bound.radius <= 0.0f) { bound.pos = iris::Vec3(0, 0, 0); bound.radius = 1.0f; }

    mSubjectRadius = bound.radius;
    mOrbit.pivot = bound.pos;
    mOrbit.distFromPivot = preview::framingDistance(bound.radius, camera->effectiveFovDegrees());
    applyGrid();                                  // the grid follows the subject's scale
    // A Mixamo character imports 138-179 units tall; iris's default farClip is
    // 500 and the framing distance is ~2.9 radii, so without this the subject
    // sits entirely beyond its own far plane and the view renders NOTHING.
    applyClipPlanes();

    mOrbit.set(0.0f, -5.0f);
    updateCameraRot();
}

void AvatarPreviewScene::applyGrid()
{
    if (!mirror()) return;
    // ~8 cells across the subject, and a floor four subjects wide. Both are
    // derived, so a 2-unit test rig and a 179-unit character get the same
    // picture at different scales. No subject, no floor: an empty page would
    // otherwise show a grid framed for a 1-unit subject, edge-on.
    const float spacing = qMax(mSubjectRadius, 0.25f) * 0.25f;
    mirror()->setGridExtent(spacing * 20.0f);
    // The wireframe grid belongs to GRID mode only: in the Modern room it
    // draws at y=0 and shows through the floor's line gaps as a ghost ground
    // (owner sighting, 2026-09-05). sync() re-applies this every frame, so a
    // spaceMode change needs no extra signal.
    const bool modern = mModel && mModel->spaceMode() == avatar::SpaceMode::Modern;
    mirror()->setGrid(mModel && mModel->isLoaded() && !modern, spacing);
}

void AvatarPreviewScene::applyClipPlanes()
{
    if (!mModel) return;
    auto camera = mModel->camera();
    if (!camera) return;
    preview::clipPlanesForFraming(mOrbit.distFromPivot, qMax(mSubjectRadius, 1.0f),
                                  camera->nearClip, camera->farClip);
}

void AvatarPreviewScene::updateCameraRot()
{
    if (!mModel) return;
    mOrbit.apply(mModel->camera());
}

void AvatarPreviewScene::mouseDown(Qt::MouseButton b) { mOrbit.mouseDown(b); }

void AvatarPreviewScene::mouseUp(Qt::MouseButton b) { mOrbit.mouseUp(b); }

void AvatarPreviewScene::mouseMove(int dx, int dy)
{
    // The PAN step scales with the subject (previeworbit.h keeps the policy at
    // the call site): a 179-unit character needs a bigger drag than a 2-unit rig.
    mOrbit.drag(mModel ? mModel->camera() : iris::CameraNodePtr(), dx, dy,
                0.002f * qMax(mSubjectRadius, 1.0f));
    updateCameraRot();
}

void AvatarPreviewScene::orbit(float yawDegrees, float pitchDegrees)
{
    mOrbit.orbit(yawDegrees, pitchDegrees);
    updateCameraRot();
}

void AvatarPreviewScene::wheel(int delta)
{
    // Zoom in units of the subject, so a 2-unit rig and a 179-unit character
    // both take the same number of notches to cross the frame.
    mOrbit.distFromPivot += -delta * 0.002f * qMax(mSubjectRadius, 0.5f);
    const float minDist = qMax(0.1f, mSubjectRadius * 0.2f);
    if (mOrbit.distFromPivot < minDist) mOrbit.distFromPivot = minDist;
    applyClipPlanes();
    updateCameraRot();
}

void AvatarPreviewScene::step(float dt, int width, int height)
{
    if (!mModel) return;
    mOrbit.advance();
    updateCameraRot();

    // ORDER (§0.5.1): pose -> mirror (refreshes global transforms) -> overlay.
    mModel->advance(dt);
    if (auto e = engine()) e->setFixedFrameDelta(jahshaka::engine::Engine::kDefaultFrameDelta);   // A4.2 S3
    applyGrid();     // cheap (two floats); tracks load/clear without a signal
    pushFrame(mModel->camera(), width, height);
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
    if (!engine || !mScene || !mView || !mirror() || !mModel) return;
    mirror()->sync();
    // NO FRAME AT ALL — just this scene's graph update (THREADING_ADOPTION_SPEC
    // P3). This used to render one frame with every on-screen view disabled,
    // which resolved the pose only because Root::renderOneFrame updated EVERY
    // scene manager whether or not anything drew it. The frame loop is now
    // gated on "an enabled View draws this scene", so that trick resolved
    // nothing the moment the avatar page's own view was the one being quieted
    // (caught by scripting.e2e.avatar: the bone stopped moving between
    // t=0 and t=0.5). Engine::updateScene says what this code always meant,
    // costs strictly less than the old frame, and works whether or not the
    // page is on screen.
    engine->updateScene(mScene);
}

void AvatarPreviewScene::prepareOffscreen(View *shot, int width, int height)
{
    // The base has already made `shot` the current view, which is what step()
    // needs: it only mirrors when it has one. Without that, the SECOND and
    // every later snapshot of a page that is not on screen would render
    // whatever pose was current at the FIRST one — the document advances, the
    // engine never hears about it. (Found by rendering two clips through
    // avatar.snapshot: the second image came back byte-identical to the first.)
    step(0.0f, width, height);
    if (!mirror()) return;
    mirror()->applySky(shot);
    if (mModel && mModel->camera()) {
        mModel->camera()->setAspectRatio(float(width) / float(height));
        mModel->camera()->update(0);
        mirror()->applyCamera(mModel->camera(), shot);
    }
}

QImage AvatarPreviewScene::renderImage(int width, int height)
{
    if (!mModel || !mModel->document()) return QImage();
    const QColor c = mModel->document()->skyColor;
    return renderOffscreen("avatar-shot", width, height,
                           Colour(float(c.redF()), float(c.greenF()), float(c.blueF()), 1.0f), false);
}
