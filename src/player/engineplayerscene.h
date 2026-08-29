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
#include <memory>
#include <QMatrix4x4>
#include "irisgl/src/irisglfwd.h"
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

private:
    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View  *mView  = nullptr;
    jahshaka::engine::Scene *mScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;
    iris::ScenePtr mDocument;
    PlayBack *mPlayback = nullptr;
    QMatrix4x4 mSavedCameraMatrix;
    bool mHaveSavedCamera = false;
};

#endif // ENGINEPLAYERSCENE_H
