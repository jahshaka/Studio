#ifndef ENGINEPLAYERVIEW_H
#define ENGINEPLAYERVIEW_H

// EnginePlayerView — the player page's view on the engine (plan step 11).
//
// An EngineViewWidget wrapping an EnginePlayerScene: the engine renders into
// this widget's native window from the player's own engine Scene, which mirrors
// the same document the editor viewport shows. Input goes to PlayBack exactly as
// PlayerView forwards it. Syncs on EngineRenderDriver::beforeFrame and renders
// only while the page is shown (View::setEnabled). Never includes Ogre or GL.
#include <memory>
#include <QElapsedTimer>
#include "../widgets/engineviewwidget.h"
#include "irisgl/src/irisglfwd.h"
#include "jahshaka/engine/Engine.h"

class EngineRenderDriver;
class EnginePlayerScene;

class EnginePlayerView : public EngineViewWidget
{
    Q_OBJECT
public:
    EnginePlayerView(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                     EngineRenderDriver *driver, QWidget *parent = nullptr);
    ~EnginePlayerView() override;

    QWidget *asWidget() { return this; }
    void setScene(iris::ScenePtr scene);
    void start();
    void end();
    bool isScenePlaying();
    void playScene();
    void stopScene();

    EnginePlayerScene *playerScene() const { return mScene.get(); }

    /// Steps the player and pushes document -> engine. Called before every frame.
    void syncFrame();

protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

private:
    /// The editor viewport's camera, when there is one: the play camera is the
    /// editor camera (legacy rule), and the editor and player share the document.
    iris::CameraNodePtr editorCamera() const;

    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    EngineRenderDriver *mDriver = nullptr;
    std::unique_ptr<EnginePlayerScene> mScene;
    iris::ScenePtr mDocument;
    QElapsedTimer mFrameTimer;
    bool mActive = false;
};

#endif // ENGINEPLAYERVIEW_H
