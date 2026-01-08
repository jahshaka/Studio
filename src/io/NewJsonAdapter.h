#ifndef NEWJSONADAPTER_H
#define NEWJSONADAPTER_H

/******************************************************************************
Adapter to convert new JSON scene format -> iris::Scene / iris::SceneNode
Also collects per-node metadata (original JSON) so caller (AssetViewer) can resolve
materials/textures from the new JSON fields (GUIDs, etc.).
******************************************************************************/

#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSharedPointer>
#include <functional>

#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/core/irisutils.h"


namespace NewJsonAdapter {

// Node id type used in AdapterResult
using NodeIdType = qint64;

struct AdapterResult {
    iris::ScenePtr scene;
    QHash<QString, NodeIdType> guidToNodeId;      // manifest GUID -> internal node id
    QHash<NodeIdType, QJsonObject> nodeMetadata; // node id -> original/enriched node JSON
    AdapterResult() : scene(nullptr) {}
};

// Asset resolver: GUID -> filesystem path (or empty)
using AssetResolverFunc = std::function<QString(const QString&)>;

// Public API
bool canHandle(const QJsonObject &rootObj);

// Convert JSON -> iris Scene. guidMap optional (old->new) replacements applied first.
// assetResolver used to resolve GUIDs to paths when available.
AdapterResult convertJsonToScene(const QJsonObject &rootObj,
                                 AssetResolverFunc assetResolver,
                                 const QMap<QString, QString> &guidMap = QMap<QString, QString>());

// Replace GUIDs in QJsonObject (exact match + safe substring fallback)
QJsonObject replaceGuidsInObject(const QJsonObject &obj, const QMap<QString, QString> &guidMap);

} // namespace NewJsonAdapter


#endif // SCENEREADER_H
