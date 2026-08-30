#ifndef ENGINEMATERIALPREVIEW_H
#define ENGINEMATERIALPREVIEW_H

// EngineMaterialPreview — the Effects/Materials Display dock on the engine.
//
// An EngineViewWidget wrapping an EngineMaterialPreviewScene: the engine
// renders the preview primitive into this widget's native window from its own
// engine Scene, which mirrors the preview document. Studio constructs it and
// hands it to the shadergraph module through IMaterialPreviewWidget
// (src/shadergraph/core/materialpreviewwidget.h), so the module never links
// engine code. Syncs on EngineRenderDriver::beforeFrame and renders only
// while visible; left/right drag orbits, wheel zooms. Never includes Ogre or GL.
#include <memory>
#include <QElapsedTimer>
#include <QPointF>
#include "viewport/engineviewwidget.h"
#include "bridge/enginematerialpreviewscene.h"
#include "../shadergraph/core/materialpreviewwidget.h"

class EngineRenderDriver;

class EngineMaterialPreview : public EngineViewWidget, public shadergraph::IMaterialPreviewWidget
{
    Q_OBJECT
public:
    EngineMaterialPreview(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                          EngineRenderDriver *driver, QWidget *parent = nullptr);
    ~EngineMaterialPreview() override;

    // ---- IMaterialPreviewWidget (what the shadergraph module calls) ----
    QWidget *previewWidget() override { return this; }
    void setPreviewMaterial(iris::MaterialPtr material) override;
    void setPreviewModel(Model model) override;
    void setPreviewBackground(const QColor &colour) override;

    EngineMaterialPreviewScene *previewScene() const { return mScene.get(); }

    /// Steps the orbit and pushes document -> engine. Called before every frame.
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
    std::unique_ptr<EngineMaterialPreviewScene> mScene;
    QPointF mPrevMousePos;
    QElapsedTimer mFrameTimer;
    bool mActive = false;
};

#endif // ENGINEMATERIALPREVIEW_H
