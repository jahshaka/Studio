#ifndef AVATARPREVIEWSCENE_H
#define AVATARPREVIEWSCENE_H

// AvatarPreviewScene — the Avatar page's centre view on the engine, minus the
// widget (AVATAR_MODULE_SPEC §0.6 D0.2 A).
//
// An EnginePreviewScene (Scene + mirror + View lifecycle and the offscreen
// capture live there) whose mirror pushes the module's preview DOCUMENT, plus
// the shared PreviewOrbit camera framed through previewframing.h and the
// BoneOverlay. The document, the clips, the transport and the toggles all live
// in avatar::AvatarPreviewModel, which has no engine in it — this class is the
// engine half and nothing else.
//
// ORDER IS LOAD-BEARING (§0.5.1): updateSceneAnimation(t) -> mirror.sync()
// (which refreshes the document's global transforms) -> overlay reads those
// globals -> render. Reading the overlay before the sync draws last frame's pose.
//
// (The recorded debt this class opened — "the FOURTH copy of the preview-scene
// pattern", spec §0.12 R0.15 / ENGINEERING_DEBT_SPEC item 6 — is PAID: the
// pattern is EnginePreviewScene now.)
//
// NEVER enables GI: HlmsPbs's VCT/PCC binding is process-wide, so a preview
// scene that turned it on would take it from the editor scene (R0.5).
#include "irisgl/core/math/vec.h"
#include <memory>
#include <QColor>
#include <QImage>
#include <Qt>
#include "irisgl/irisglfwd.h"
#include "bridge/enginepreviewscene.h"   // brings jahshaka/engine/Engine.h
#include "viewport/previeworbit.h"

class SceneMirror;
class BoneOverlay;
namespace avatar { class AvatarPreviewModel; }

class AvatarPreviewScene : public EnginePreviewScene
{
public:
    /// Holds the engine weakly (the preview-scene contract): never keeps it
    /// alive, every call checks it is still there.
    explicit AvatarPreviewScene(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~AvatarPreviewScene() override;

    /// The subject: the module's preview model (its document is what gets
    /// mirrored). Null detaches. The model outlives this class.
    void setModel(avatar::AvatarPreviewModel *model);
    avatar::AvatarPreviewModel *model() const { return mModel; }

    /// Re-frames the orbit on the loaded fragment's bounds. Call after a load —
    /// a Mixamo character is 138-179 units tall and needs the clip planes
    /// previewframing.h computes, or it sits past its own far plane (R0.7).
    void frameSubject();

    // ---- orbit (the shared PreviewOrbit, viewport/previeworbit.h) ----
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

protected:
    void configureScene(jahshaka::engine::Scene *scene) override;
    void configureMirror(SceneMirror *mirror) override;
    void configureView(jahshaka::engine::View *view) override;
    void releaseSubject(bool sceneAlive) override;
    void prepareOffscreen(jahshaka::engine::View *shot, int width, int height) override;

private:
    void updateCameraRot();
    void applyClipPlanes();
    /// Ground grid spacing/extent for the current subject size.
    void applyGrid();
    /// Points the mirror at the model's document AND installs the pose source
    /// that reads the engine's evaluated bones back for the overlay.
    void bindModel(avatar::AvatarPreviewModel *model);

    std::unique_ptr<BoneOverlay> mOverlay;
    avatar::AvatarPreviewModel *mModel = nullptr;

    /// The shared arcball; zoom and pan step scale with the subject (a Mixamo
    /// character is ~170 units tall), which is why those stay here.
    PreviewOrbit mOrbit;
    float mSubjectRadius = 1.0f;
};

#endif // AVATARPREVIEWSCENE_H
