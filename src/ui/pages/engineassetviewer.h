#ifndef ENGINEASSETVIEWER_H
#define ENGINEASSETVIEWER_H

// EngineAssetViewer — the Assets page's preview viewer on the engine.
//
// An EngineViewWidget wrapping an EngineAssetScene: the engine renders into this
// widget's native window from the assets Scene, which mirrors the viewer's own
// small preview document. This class does what AssetViewer does around the
// document — read library assets (models, materials, shaders, skies) out of the
// database into iris nodes and materials — and hands them to the scene; the
// mouse orbits. Syncs on EngineRenderDriver::beforeFrame and renders only while
// the page shows it (View::setEnabled). Never includes Ogre or GL.
#include "irisgl/core/math/vec.h"
#include <memory>
#include <QElapsedTimer>
#include <QMap>
#include <QPointF>
#include "viewport/engineviewwidget.h"
#include "ui/pages/iassetviewer.h"
#include "jahshaka/engine/Engine.h"

class EngineRenderDriver;
class EngineAssetScene;
class Project;
class ProgressDialog;
namespace iris { class SceneSource; }

class EngineAssetViewer : public EngineViewWidget, public IAssetViewer
{
    Q_OBJECT
public:
    EngineAssetViewer(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                      EngineRenderDriver *driver, QWidget *parent = nullptr);
    ~EngineAssetViewer() override;

    QWidget *asWidget() override { return this; }
    void setDatabase(Database *db) override { mDb = db; }
    void setProject(Project *project) override { mProject = project; }
    iris::SceneSource *sceneSource() override { return mSource; }
    void clearScene() override;
    void changeBackdrop(unsigned int id) override;
    iris::SceneNodePtr cachedAsset(const QString &guid) override { return mCachedAssets.value(guid); }
    void addNodeToScene(iris::SceneNodePtr sceneNode, QString guid = "", bool viewed = false,
                        bool cache = false, bool isOnGround = true) override;
    void cacheCurrentModel(QString guid) override;
    void orientCamera(iris::Vec3 pos, iris::Vec3 localRot, int distanceFromPivot) override;
    QJsonObject getSceneProperties() override;
    void loadJafModel(QString path, QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true) override;
    void loadJafMaterial(QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true) override;
    void loadJafShader(QString guid, QMap<QString, QString> &outGuids, bool firstAdd = true, bool cache = false, bool firstLoad = true) override;
    void loadJafSky(QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true) override;
    void loadModel(QString path, QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true) override;
    QImage takeScreenshot(int width, int height) override;
    void setLoadFinishedCallback(std::function<void()> callback) override { mLoadFinished = callback; }

    EngineAssetScene *assetScene() const { return mScene.get(); }

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
    /// Database -> document, the AssetViewer::addJaf* readers.
    iris::SceneNodePtr readJafModel(const QString &path, const QString &guid);
    iris::MaterialPtr readJafMaterial(const QString &guid);
    iris::MaterialPtr readJafShader(const QString &guid);
    void applyJafSky(const QString &guid);
    /// The mirror renders PbrMaterial and DefaultMaterial. Default.shader
    /// CustomMaterials (what the readers produce) become a DefaultMaterial with
    /// the same colours and textures; other materials are kept as they are.
    static iris::MaterialPtr mirrorable(iris::MaterialPtr material);
    static void mirrorableMaterials(iris::SceneNodePtr node);
    void showProgress();
    void hideProgress();

    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    EngineRenderDriver *mDriver = nullptr;
    std::unique_ptr<EngineAssetScene> mScene;
    Database *mDb = nullptr;
    Project *mProject = nullptr;   // the live Project (Phase 4: was Globals::project)
    iris::SceneSource *mSource = nullptr;
    ProgressDialog *mProgress = nullptr;
    std::function<void()> mLoadFinished;   // fires in hideProgress (end of every load*)
    QMap<QString, iris::SceneNodePtr> mCachedAssets;
    QPointF mPrevMousePos;
    QElapsedTimer mFrameTimer;
    bool mActive = false;
};

#endif // ENGINEASSETVIEWER_H
