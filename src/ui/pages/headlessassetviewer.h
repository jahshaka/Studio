#ifndef HEADLESSASSETVIEWER_H
#define HEADLESSASSETVIEWER_H

// HeadlessAssetViewer — the Assets page's document-only preview stand-in for
// runs where no engine view can exist (--headless scripts, --dump-api-docs).
// Before step 14 this role was played by an unrealized legacy AssetViewer;
// this class keeps only the document surface AssetView exercises headless:
// the node cache. Nothing renders.
#include "irisgl/core/math/vec.h"
#include <QWidget>
#include "ui/pages/iassetviewer.h"
#include <QMap>
#include "irisgl/document/scenegraph/scenenode.h"

class HeadlessAssetViewer : public IAssetViewer
{
public:
    HeadlessAssetViewer(QWidget *parent = nullptr)
    {
        mWidget = new QWidget(parent);
    }

    QWidget *asWidget() override { return mWidget; }
    void setDatabase(Database *db) override { Q_UNUSED(db); }

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

    void orientCamera(iris::Vec3, iris::Vec3, int) override {}
    QJsonObject getSceneProperties() override { return QJsonObject(); }

    void loadJafModel(QString, QString, bool, bool, bool) override {}
    void loadJafMaterial(QString, bool, bool, bool) override {}
    void loadJafSky(QString, bool, bool, bool) override {}
    void loadModel(QString, QString, bool, bool, bool) override {}

    QImage takeScreenshot(int, int) override { return QImage(); }

private:
    QWidget *mWidget = nullptr;
    iris::SceneNodePtr mLastNode;
    QMap<QString, iris::SceneNodePtr> mCache;
};

#endif // HEADLESSASSETVIEWER_H
