#ifndef AVATARPREVIEWSCENE_H
#define AVATARPREVIEWSCENE_H

// AvatarPreviewScene — the Avatar page's centre view on the engine, minus the
// widget (AVATAR_MODULE_SPEC §0.6 D0.2 A).
//
// Owns its own engine Scene ("avatarpreview"), a SceneMirror pushing the
// module's preview DOCUMENT into it, an orbit camera framed through
// previewframing.h, and the BoneOverlay. The document, the clips, the
// transport and the toggles all live in avatar::AvatarPreviewModel, which has
// no engine in it — this class is the engine half and nothing else.
//
// ORDER IS LOAD-BEARING (§0.5.1): updateSceneAnimation(t) -> mirror.sync()
// (which refreshes the document's global transforms) -> overlay reads those
// globals -> render. Reading the overlay before the sync draws last frame's pose.
//
// RECORDED DEBT: this is the FOURTH copy of the preview-scene pattern
// (EngineAssetScene, EngineMaterialPreviewScene, EnginePlayerScene) — spec
// §0.12 R0.15 / ENGINEERING_DEBT_SPEC. Consolidation is deliberately NOT
// attempted here.
//
// NEVER enables GI: HlmsPbs's VCT/PCC binding is process-wide, so a preview
// scene that turned it on would steal it from the editor scene (R0.5).
#include <memory>
#include <QColor>
#include <QImage>
#include <QVector3D>
#include <Qt>
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"

class SceneMirror;
class BoneOverlay;
namespace avatar { class AvatarPreviewModel; }

class AvatarPreviewScene
{
public:
    /// Holds the engine weakly (the preview-scene contract): never keeps it
    /// alive, every call checks it is still there.
    explicit AvatarPreviewScene(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~AvatarPreviewScene();

    /// Creates the engine Scene, its mirror and the overlay and binds them to
    /// `view` (the View must already exist: Engine.h, ORDER MATTERS).
    bool attach(jahshaka::engine::View *view);
    /// Destroys the engine Scene, mirror and overlay while the Engine is alive.
    void release();

    jahshaka::engine::Scene *engineScene() const { return mScene; }
    jahshaka::engine::View  *view() const { return mView; }

    /// The subject: the module's preview model (its document is what gets
    /// mirrored). Null detaches. The model outlives this class.
    void setModel(avatar::AvatarPreviewModel *model);
    avatar::AvatarPreviewModel *model() const { return mModel; }

    /// Re-frames the orbit on the loaded fragment's bounds. Call after a load —
    /// a Mixamo character is 138-179 units tall and needs the clip planes
    /// previewframing.h computes, or it sits past its own far plane (R0.7).
    void frameSubject();

    // ---- orbit (EngineMaterialPreviewScene's maths) ----
    void mouseDown(Qt::MouseButton b);
    void mouseUp(Qt::MouseButton b);
    void mouseMove(int dx, int dy);
    void wheel(int delta);
    void orbit(float yawDegrees, float pitchDegrees);

    /// One frame, in the order above. `width`/`height` are the view's pixels.
    void step(float dt, int width, int height);

    /// Offscreen render + readback (avatar.snapshot). Works before the widget
    /// has ever been shown: the shot view becomes the first View if needed.
    QImage renderImage(int width, int height);

    /// Makes sure the engine has EVALUATED the current clip time, so a pose
    /// read straight after a setTime is this time's pose and not the last
    /// frame's. One mirror sync and one frame.
    void resolvePose();

    /// What the bone overlay drew last frame — the structural half of the
    /// pixel suite (bones, leaf stubs, joint markers).
    int overlaySegments() const;
    int overlayStubs() const;
    int overlayJoints() const;

private:
    void updateCameraRot();
    void applyClipPlanes();
    /// Ground grid spacing/extent for the current subject size.
    void applyGrid();
    static QImage toQImage(const jahshaka::engine::Image &img);

    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View  *mView  = nullptr;
    jahshaka::engine::Scene *mScene = nullptr;
    /// Points the mirror at the model's document AND installs the pose source
    /// that reads the engine's evaluated bones back for the overlay.
    void bindModel(avatar::AvatarPreviewModel *model);

    std::unique_ptr<SceneMirror> mMirror;
    std::unique_ptr<BoneOverlay> mOverlay;
    avatar::AvatarPreviewModel *mModel = nullptr;
    unsigned mShotSerial = 0;

    // Orbit state
    float mYaw = 0, mPitch = 0, mTargetYaw = 0, mTargetPitch = 0;
    float mRotationSpeed = 0.5f;
    QVector3D mPivot;
    float mDistFromPivot = 4.0f;
    float mSubjectRadius = 1.0f;
    bool mLeftDown = false, mRightDown = false, mMiddleDown = false;
};

#endif // AVATARPREVIEWSCENE_H
