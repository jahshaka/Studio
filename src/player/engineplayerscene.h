#ifndef ENGINEPLAYERSCENE_H
#define ENGINEPLAYERSCENE_H

// EnginePlayerScene — the player on the engine, minus the widget.
//
// Owns a second engine Scene ("player") and a SceneMirror that pushes the SAME
// iris::ScenePtr document the editor viewport holds (the editor and the player
// share the document; every other module gets its own). Runs PlayBack for
// physics, keyframe animation and the camera controllers exactly as the legacy
// PlayerView does, then mirrors the result and points the View's camera where
// the document's scene camera looks. No GL, no Ogre, no QWidget — so it is
// testable headless with an offscreen View (tests/player). EnginePlayerView
// wraps it.
#include "irisgl/core/math/mat4.h"
#include <memory>
#include <QImage>
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"

class SceneMirror;
class PlayBack;

class EnginePlayerScene
{
public:
    /// Holds the engine weakly, like EngineThumbnailRenderer: it never keeps the
    /// Engine alive and every call checks it is still there.
    explicit EnginePlayerScene(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~EnginePlayerScene();

    /// Creates the player Scene and its mirror WITHOUT binding a view — what a
    /// screenshot of a never-shown player page needs. Idempotent.
    bool ensureScene();
    /// Creates the player Scene and its mirror and binds them to `view` (the View
    /// must already exist: Engine.h, ORDER MATTERS). Idempotent; false if the
    /// engine is gone or the scene could not be created.
    bool attach(jahshaka::engine::View *view);
    /// Destroys the engine Scene and mirror while the Engine is still alive. The
    /// View is the caller's. Safe to call repeatedly; the destructor calls it.
    void release();
    jahshaka::engine::Scene *engineScene() const { return mScene; }
    jahshaka::engine::View *view() const { return mView; }

    /// The document. `camera`, when given, becomes the document's scene camera —
    /// the legacy rule (SceneViewWidget::setScene) is that the play camera IS the
    /// editor camera, and the engine editor viewport does not set it on the
    /// document, so the player does. A null `camera` keeps whatever the document has.
    void setDocument(iris::ScenePtr scene, iris::CameraNodePtr camera = iris::CameraNodePtr());
    iris::ScenePtr document() const { return mDocument; }
    /// The camera the view is driven from (the document's scene camera).
    iris::CameraNodePtr camera() const;

    /// Page shown: remembers the camera transform and primes the mouse controller
    /// so the camera does not jump (PlayerView::start).
    void begin();
    /// Page hidden: restores the camera transform (PlayerView::end).
    void end();

    /// One frame: PlayBack::update (controllers, animation, physics), then
    /// document -> engine, sky -> view, scene camera -> view. `width`/`height`
    /// are the view's pixel size (controller picking + aspect ratio).
    void step(float dt, int width, int height);

    PlayBack *playback() const { return mPlayback; }
    bool isPlaying() const;
    void play();
    void stop();

    /// What the PLAYER is showing, rendered offscreen at the requested size —
    /// the same throwaway-view readback EngineSceneViewport::takeScreenshot
    /// does for the editor, pointed at THIS scene and THIS camera (the
    /// document's scene camera, which is not the editor's viewpoint).
    ///
    /// It exists because the player is a second engine Scene with a second
    /// mirror: a screenshot of the player taken through the editor viewport
    /// would photograph the editor's world state (its sky push, its camera,
    /// its wires) and call it the player. Null QImage when there is nothing to
    /// render (no engine, no scene, no camera).
    /// `grade` is IEditorViewport::ScreenshotGrade as an int (Raw / Tonemap /
    /// Viewport — fix wave 2026-09-07 item 6). An int rather than the enum so
    /// this header keeps not including the editor viewport's.
    QImage takeScreenshot(int width, int height, int grade);

    /// Steps and renders exactly n frames synchronously (editor.frame's
    /// pattern for the player): PlayBack + mirror + renderOneFrame. `dt` < 0
    /// charges wall clock; a fixed dt is what makes a scripted assertion
    /// deterministic. Does nothing without a bound view.
    void stepFrames(int n, float dt, int width, int height);

private:
    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View  *mView  = nullptr;
    jahshaka::engine::Scene *mScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;
    iris::ScenePtr mDocument;
    PlayBack *mPlayback = nullptr;
    iris::Mat4 mSavedCameraMatrix;
    bool mHaveSavedCamera = false;
};

#endif // ENGINEPLAYERSCENE_H
