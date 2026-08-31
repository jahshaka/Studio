#ifndef HEADLESSASSETVIEWER_H
#define HEADLESSASSETVIEWER_H

// HeadlessAssetViewer — the Assets page's document-only preview stand-in for
// runs where no engine view can exist (--headless scripts, --dump-api-docs).
// Before step 14 this role was played by an unrealized legacy AssetViewer;
// this class keeps only the document surface AssetView exercises headless:
// the assimp SceneSource and the node cache. Nothing renders.
#include <QWidget>
#include "ui/pages/iassetviewer.h"
#include "irisgl/document/scenegraph/meshnode.h"   // iris::SceneSource

class HeadlessAssetViewer : public IAssetViewer
{
public:
    HeadlessAssetViewer(QWidget *parent = nullptr)
    {
        mWidget = new QWidget(parent);
        mSource = new iris::SceneSource();
    }

    ~HeadlessAssetViewer() override { delete mSource; }

    QWidget *asWidget() override { return mWidget; }
    void setDatabase(Database *db) override { Q_UNUSED(db); }

    iris::SceneSource *sceneSource() override { return mSource; }

    void clearScene() override {}
    void changeBackdrop(unsigned int) override {}

    iris::SceneNodePtr cachedAsset(const QString &guid) override { return mCache.value(guid); }
    void addNodeToScene(iris::SceneNodePtr sceneNode, QString guid, bool, bool cache, bool) override
    {
        mLastNode = sceneNode;
        if (cache && !guid.isEmpty()) mCache.insert(guid, sceneNode);
    }
    void cacheCurrentModel(QString guid) override
    {
        if (mLastNode && !guid.isEmpty()) mCache.insert(guid, mLastNode);
    }

    void orientCamera(QVector3D, QVector3D, int) override {}
    QJsonObject getSceneProperties() override { return QJsonObject(); }

    void loadJafModel(QString, QString, bool, bool, bool) override {}
    void loadJafMaterial(QString, bool, bool, bool) override {}
    void loadJafShader(QString, QMap<QString, QString> &, bool, bool, bool) override {}
    void loadJafSky(QString, bool, bool, bool) override {}
    void loadModel(QString, QString, bool, bool, bool) override {}

    QImage takeScreenshot(int, int) override { return QImage(); }

private:
    QWidget *mWidget = nullptr;
    iris::SceneSource *mSource = nullptr;
    iris::SceneNodePtr mLastNode;
    QMap<QString, iris::SceneNodePtr> mCache;
};

#endif // HEADLESSASSETVIEWER_H
