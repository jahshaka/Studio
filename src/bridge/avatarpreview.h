#ifndef AVATARPREVIEW_H
#define AVATARPREVIEW_H

// AvatarPreview — the Avatar page's centre view on the engine.
//
// An EngineViewWidget wrapping an AvatarPreviewScene: the engine renders the
// loaded rig into this widget's native window from its own engine Scene, which
// mirrors the module's preview document. Studio constructs it and hands it to
// the avatar module through IAvatarPreviewWidget, so the module never links
// engine code. Syncs on EngineRenderDriver::beforeFrame and renders only while
// visible; left/right drag orbits, wheel zooms. Never includes Ogre or GL.
#include <memory>
#include <QElapsedTimer>
#include <QPointF>
#include "bridge/avatarpreviewscene.h"
#include "modules/avatar/avatarpreviewwidget.h"
#include "viewport/engineviewwidget.h"

class EngineRenderDriver;

class AvatarPreview : public EngineViewWidget, public avatar::IAvatarPreviewWidget
{
    Q_OBJECT
public:
    AvatarPreview(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                  EngineRenderDriver *driver, QWidget *parent = nullptr);
    ~AvatarPreview() override;

    // ---- IAvatarPreviewWidget ----
    QWidget *previewWidget() override { return this; }
    void setPreviewModel(avatar::AvatarPreviewModel *model) override;
    void framePreview() override;
    QImage renderPreview(int width, int height) override;
    void resolvePose() override;

    AvatarPreviewScene *previewScene() const { return mScene.get(); }

    /// Steps the transport and pushes document -> engine. Before every frame.
    void syncFrame();

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    EngineRenderDriver *mDriver = nullptr;
    std::unique_ptr<AvatarPreviewScene> mScene;
    QPointF mPrevMousePos;
    QElapsedTimer mFrameTimer;
    bool mActive = false;
};

#endif // AVATARPREVIEW_H
