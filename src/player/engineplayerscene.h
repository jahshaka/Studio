#ifndef ENGINEPLAYERSCENE_H
#define ENGINEPLAYERSCENE_H

// EnginePlayerScene — the player on the engine, minus the widget.
//
// ONE SCENE (owner decision 2026-09-14, lane PLAYER-1). The Player page is a
// second VIEW on the EDITOR'S engine Scene, pushed by the EDITOR'S SceneMirror:
// this object owns neither. It owns the PlayBack (physics, keyframe animation,
// the camera controllers) and it owns the per-frame order — PlayBack, then
// document -> engine, then sky/world/camera -> ITS view.
//
// WHY, in one paragraph. It used to create a second engine Scene and a second
// SceneMirror over the same document. A document's graph can only live in one
// Ogre scene manager at a time, so every space switch EVACUATED every engine
// object from the leaving mirror, migrated the tree, and full-walked the
// arriving one — and every derived lighting structure (voxels, probes, lamp
// maps, the irradiance field, the planar arm, the adaptation history) was
// rebuilt from scratch on both sides of the switch. The Player was therefore
// always looking at a COLD scene while the editor was warm, which is the
// mechanism behind "terrible in the Player" (SPECS/audits/
// PLAYER_CHAIN_AUDIT_2026-09-14.md, F1/F2/F5). The process-wide HlmsPbs planar
// and GI bindings were bound by whichever of the two scenes armed last, so
// which space showed correct mirrors depended on history.
//
// WHAT MAKES THE TWO SPACES LOOK DIFFERENT IS NOW THE VIEW, not the scene: the
// Player's View is created with View::setHelpersVisible(false), which takes
// kHelperBit out of every scene pass in that view's workspace — the grid, the
// light/camera/decal wires and icons, the gizmo, the selection shell and the GI
// volume boxes are excluded from the Player's picture and from nothing else.
// The ground's horizon is a BACKDROP (Scene::setNodeBackdrop), not furniture,
// and stays.
//
// No GL, no Ogre, no QWidget — so it is testable headless with an offscreen
// View (tests/player). EnginePlayerView wraps it.
#include "irisgl/core/math/mat4.h"
#include <memory>
#include <QElapsedTimer>
#include <QImage>
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"

class SceneMirror;
class PlayBack;
class PlayerVr;

class EnginePlayerScene
{
public:
    /// Holds the engine weakly, like EngineThumbnailRenderer: it never keeps the
    /// Engine alive and every call checks it is still there.
    explicit EnginePlayerScene(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~EnginePlayerScene();

    /// THE EDITOR'S SCENE AND ITS MIRROR — the one scene the Player draws and
    /// the one mirror that pushes the document into it. Borrowed, never owned:
    /// this object creates no Scene and destroys none.
    ///
    /// Must be set before attach() or step() can do anything. Passing nulls
    /// (the editor viewport has not built its scene yet, or is going away)
    /// leaves the player inert rather than half-bound.
    void setEditorScene(jahshaka::engine::Scene *scene, SceneMirror *mirror);

    /// Is there a scene to draw? (The editor's, through setEditorScene.)
    bool ensureScene() const { return mScene != nullptr; }
    /// Binds `view` to the editor's Scene and takes the editor's furniture out
    /// of it (the View must already exist: Engine.h, ORDER MATTERS). Idempotent;
    /// false if the engine is gone or no editor scene has been handed over.
    bool attach(jahshaka::engine::View *view);
    /// Unbinds this object's view. The Scene and the mirror are the editor's and
    /// are NOT touched. Safe to call repeatedly; the destructor calls it.
    void release();
    /// The widget's View is about to be destroyed (native window recreated):
    /// drop the pointer with NO engine call, so the attach() that follows the
    /// recreation takes the fresh-bind path instead of unbinding freed memory
    /// (the L2 lane's finding, applied here 2026-09-10 — Ogre hands the
    /// replacement View the freed one's address, so the stale unbind landed on
    /// the NEW View: a silently blank player).
    void forgetView();
    jahshaka::engine::Scene *engineScene() const { return mScene; }
    jahshaka::engine::View *view() const { return mView; }

    /// The document. `camera`, when given, becomes the document's scene camera —
    /// the legacy rule (SceneViewWidget::setScene) is that the play camera IS the
    /// editor camera, and the engine editor viewport does not set it on the
    /// document, so the player does. A null `camera` keeps whatever the document has.
    ///
    /// It does NOT bind the document to a mirror: the editor's mirror already
    /// holds it, and there is only one now.
    void setDocument(iris::ScenePtr scene, iris::CameraNodePtr camera = iris::CameraNodePtr());
    iris::ScenePtr document() const { return mDocument; }
    /// The camera the view is driven from (the document's scene camera).
    iris::CameraNodePtr camera() const;
    /// THE CAMERA THE PLAYER ACTUALLY RENDERS THROUGH — `camera()` put through
    /// the document's own active-camera rule (`iris::Scene::renderCamera`,
    /// which the mirror's applyCamera uses for the same question). With an
    /// authored shot armed and nothing possessed, that is the authored camera;
    /// otherwise it is the free one. The VR mode stands the wearer on THIS and
    /// writes the head back to THIS, so the headset and the Player's picture
    /// cannot be looking through two different cameras (lead review F1).
    iris::CameraNodePtr renderCamera() const;

    /// Page shown: remembers the camera's pose AND its lens, and primes the
    /// mouse controller so the camera does not jump (PlayerView::start).
    void begin();
    /// Page hidden: puts the camera's pose and lens back (PlayerView::end).
    void end();

    /// THE PLAYER STARTS WHERE THE EDITOR IS LOOKING (PLAYER-SPAWN-1 rule 1,
    /// owner 2026-09-17: "it should share the editor viewpoint when we switch
    /// unless a camera node is present").
    ///
    /// Called at the two moments a run can begin — the page switch and the
    /// stopped->playing edge — with the camera the EDITOR VIEW is actually
    /// rendering through (its free explorer, or the scene camera it is
    /// piloting). The player's free viewer is placed on that pose and given
    /// that lens, so the first frame of the Player is the frame the editor was
    /// showing, and the VR rig — which anchors on whatever camera the run
    /// renders through — stands the wearer in the same place.
    ///
    /// NOTHING HAPPENS when the scene has an ACTIVE camera: the scene has said
    /// where play looks from, and that is the "unless a camera node is
    /// present" half of the owner's rule. Nothing happens either when the two
    /// cameras are the SAME NODE, which is the ordinary case — the player has
    /// flown the editor's own camera since SceneViewWidget — so this is a
    /// no-op in every session that never pilots.
    void spawnFrom(const iris::CameraNodePtr &editorViewCamera);

    /// One frame: PlayBack::update (controllers, then the document's
    /// simulation clock — animation, physics, avatars), then document ->
    /// engine, sky -> view, scene camera -> view, and the clock's simulated
    /// seconds -> the engine's frame delta. `dt` < 0 charges the wall clock
    /// (the time since the previous step); a fixed dt is what makes a scripted
    /// assertion deterministic. `width`/`height` are the view's pixel size
    /// (controller picking + aspect ratio).
    void step(float dt, int width, int height);

    PlayBack *playback() const { return mPlayback; }

    /// THE PLAYER'S VR MODE (SPECS/VR_SPEC.md §4.5, phase 3) — created on
    /// demand, driven from step(), and never anything at all in a session that
    /// never asks for a headset. Null before the first ask.
    PlayerVr *vr();
    /// The VR mode as it stands, WITHOUT creating one: the read every state
    /// verb makes, on every box, with no runtime.
    const PlayerVr *vrIfAny() const { return mVr.get(); }
    bool isPlaying() const;
    void play();
    void stop();

    /// What the PLAYER is showing, rendered offscreen at the requested size —
    /// the same throwaway-view readback EngineSceneViewport::takeScreenshot
    /// does for the editor, pointed at the document's SCENE camera (which is
    /// not the editor's viewpoint) and with the editor's furniture masked out,
    /// exactly as the on-screen player view has it.
    /// `grade` is IEditorViewport::ScreenshotGrade as an int (Raw / Tonemap /
    /// Viewport — fix wave 2026-09-07 item 6). An int rather than the enum so
    /// this header keeps not including the editor viewport's.
    QImage takeScreenshot(int width, int height, int grade);

    /// Steps and renders exactly n frames synchronously (editor.frame's
    /// pattern for the player): PlayBack + mirror + renderOneFrame, `dt` as
    /// for step(). Does nothing without a bound view.
    void stepFrames(int n, float dt, int width, int height);

private:
    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View  *mView  = nullptr;
    /// The EDITOR's, borrowed (setEditorScene). Never created, never destroyed.
    jahshaka::engine::Scene *mScene = nullptr;
    SceneMirror *mMirror = nullptr;
    iris::ScenePtr mDocument;
    PlayBack *mPlayback = nullptr;
    std::unique_ptr<PlayerVr> mVr;
    /// What the play camera was before the player took it over — restored by
    /// end(). The LENS is in here beside the transform because spawnFrom can
    /// change it (a piloted camera's angle is not the explorer's), and an
    /// explorer left with somebody else's field of view after a visit to the
    /// Player is a viewport that silently zoomed.
    struct CameraState
    {
        iris::Mat4 transform;
        float angle = 45.0f;
        float orthoSize = 10.0f;
        float nearClip = 0.1f;
        float farClip = 1000.0f;
        bool perspective = true;
    };
    CameraState mSavedCamera;
    bool mHaveSavedCamera = false;
    /// The wall clock behind a `dt` < 0 step: time since the previous step.
    QElapsedTimer mFrameTimer;
};

#endif // ENGINEPLAYERSCENE_H
