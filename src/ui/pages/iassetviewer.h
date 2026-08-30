#ifndef IASSETVIEWER_H
#define IASSETVIEWER_H

// IAssetViewer — what AssetView (the Assets page) actually calls on its preview
// viewer, and nothing more. Two implementations: AssetViewer (legacy, a
// QOpenGLWidget with its own ForwardRenderer) and EngineAssetViewer (an
// EngineViewWidget on jahshaka::engine). Neither GL nor the engine leaks through
// here; the document types (iris::) do, because the page hands nodes across.
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector3D>
#include "irisgl/irisglfwd.h"

class QWidget;
class Database;
class Project;
namespace iris { class SceneSource; }

class IAssetViewer
{
public:
    virtual ~IAssetViewer() = default;

    /// The widget AssetView lays out (page 0 of its viewer stack).
    virtual QWidget *asWidget() = 0;
    virtual void setDatabase(Database *db) = 0;
    /// The one live Project (Phase 4: was the Globals::project static). Defaulted
    /// to a no-op: only the engine viewer reads it (relative animation paths).
    virtual void setProject(Project *) {}

    /// The assimp import of the last loadModel() — AssetView reads its textures.
    virtual iris::SceneSource *sceneSource() = 0;

    /// Brackets mesh work AssetView does on the viewer's behalf. The legacy
    /// viewer makes its GL context current here; the engine viewer needs nothing.

    /// Removes the previewed asset (keeps the lights and floor).
    virtual void clearScene() = 0;
    /// 1 = dark, 2 = grey (both floorless, no shadows), 3 = floor + shadows.
    virtual void changeBackdrop(unsigned int id) = 0;

    /// A node previously cached by addNodeToScene(cache) / cacheCurrentModel, or null.
    virtual iris::SceneNodePtr cachedAsset(const QString &guid) = 0;
    virtual void addNodeToScene(iris::SceneNodePtr sceneNode, QString guid = "", bool viewed = false,
                                bool cache = false, bool isOnGround = true) = 0;
    virtual void cacheCurrentModel(QString guid) = 0;

    /// Restores a saved orbit (see getSceneProperties()).
    virtual void orientCamera(QVector3D pos, QVector3D localRot, int distanceFromPivot) = 0;
    virtual QJsonObject getSceneProperties() = 0;

    virtual void loadJafModel(QString path, QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true) = 0;
    virtual void loadJafMaterial(QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true) = 0;
    virtual void loadJafShader(QString guid, QMap<QString, QString> &outGuids, bool firstAdd = true, bool cache = false, bool firstLoad = true) = 0;
    virtual void loadJafSky(QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true) = 0;
    /// Imports a model file (not yet in the library) and previews it.
    virtual void loadModel(QString path, QString guid, bool firstAdd = true, bool cache = false, bool firstLoad = true) = 0;

    /// The RTT preview: the current scene from the current camera at this size.
    virtual QImage takeScreenshot(int width, int height) = 0;
};

#endif // IASSETVIEWER_H
