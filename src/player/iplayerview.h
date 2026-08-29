#ifndef IPLAYERVIEW_H
#define IPLAYERVIEW_H

// IPlayerView — what PlayerWidget and MainWindow actually call on the player's
// view, and nothing more. Two implementations: PlayerView (legacy, a
// QOpenGLWidget on IrisGL) and EnginePlayerView (an EngineViewWidget on
// jahshaka::engine, plan step 11). Neither GL nor the engine leaks through here.
#include "irisgl/src/irisglfwd.h"

class QWidget;

class IPlayerView
{
public:
    virtual ~IPlayerView() = default;

    /// The widget PlayerWidget lays out and focuses.
    virtual QWidget *asWidget() = 0;

    /// The document (shared with the editor viewport — the same iris::ScenePtr).
    virtual void setScene(iris::ScenePtr scene) = 0;

    /// Called when the player page is shown / hidden.
    virtual void start() = 0;
    virtual void end() = 0;

    virtual bool isScenePlaying() = 0;
    virtual void playScene() = 0;
    virtual void stopScene() = 0;
};

#endif // IPLAYERVIEW_H
