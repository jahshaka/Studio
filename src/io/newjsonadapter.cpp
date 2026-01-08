#include "NewJsonAdapter.h"

#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QDebug>
#include <QStandardPaths>

namespace NewJsonAdapter {

// ----------------------------- Helpers --------------------------------------

static QString tryGetString(const QJsonObject &o, const QStringList &keys)
{
    for (const QString &k : keys) {
        if (o.contains(k) && o[k].isString()) return o[k].toString();
    }
    return QString();
}

static QVector3D parseVec3(const QJsonObject &obj, const QStringList &possibleKeys, const QVector3D &fallback = QVector3D(0,0,0))
{
    for (const QString &k : possibleKeys) {
        if (!obj.contains(k)) continue;
        const QJsonValue &v = obj[k];
        if (v.isArray()) {
            QJsonArray a = v.toArray();
            if (a.size() >= 3) {
                return QVector3D(static_cast<float>(a[0].toDouble()),
                                 static_cast<float>(a[1].toDouble()),
                                 static_cast<float>(a[2].toDouble()));
            }
        } else if (v.isObject()) {
            QJsonObject o = v.toObject();
            double x = o.contains("x") ? o["x"].toDouble() : 0.0;
            double y = o.contains("y") ? o["y"].toDouble() : 0.0;
            double z = o.contains("z") ? o["z"].toDouble() : 0.0;
            return QVector3D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        }
    }
    return fallback;
}

static QQuaternion parseRotation(const QJsonObject &obj, const QStringList &possibleKeys, const QQuaternion &fallback = QQuaternion())
{
    for (const QString &k : possibleKeys) {
        if (!obj.contains(k)) continue;
        auto v = obj[k];
        if (v.isArray()) {
            QJsonArray a = v.toArray();
            if (a.size() == 4) {
                return QQuaternion(static_cast<float>(a[3].toDouble()), // w
                                   static_cast<float>(a[0].toDouble()), // x
                                   static_cast<float>(a[1].toDouble()), // y
                                   static_cast<float>(a[2].toDouble())); // z
            }
            if (a.size() == 3) {
                return QQuaternion::fromEulerAngles(static_cast<float>(a[0].toDouble()),
                                                    static_cast<float>(a[1].toDouble()),
                                                    static_cast<float>(a[2].toDouble()));
            }
        } else if (v.isObject()) {
            QJsonObject o = v.toObject();
            if (o.contains("w") && o.contains("x") && o.contains("y") && o.contains("z")) {
                return QQuaternion(static_cast<float>(o["w"].toDouble()),
                                   static_cast<float>(o["x"].toDouble()),
                                   static_cast<float>(o["y"].toDouble()),
                                   static_cast<float>(o["z"].toDouble()));
            }
        }
    }
    return fallback;
}

// --------------------- GUID replacement helpers -----------------------------

QJsonObject replaceGuidsInObject(const QJsonObject &obj, const QMap<QString, QString> &guidMap)
{
    std::function<QJsonValue(const QJsonValue&)> replaceRec;
    replaceRec = [&](const QJsonValue &val)->QJsonValue {
        if (val.isString()) {
            QString s = val.toString();
            if (guidMap.contains(s)) return QJsonValue(guidMap.value(s));
            QString out = s;
            for (auto it = guidMap.constBegin(); it != guidMap.constEnd(); ++it) {
                if (out.contains(it.key())) out.replace(it.key(), it.value());
            }
            if (out != s) return QJsonValue(out);
            return val;
        } else if (val.isArray()) {
            QJsonArray in = val.toArray();
            QJsonArray out;
            for (const QJsonValue &v : in) out.append(replaceRec(v));
            return QJsonValue(out);
        } else if (val.isObject()) {
            QJsonObject in = val.toObject();
            QJsonObject out;
            for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
                out.insert(it.key(), replaceRec(it.value()));
            }
            return QJsonValue(out);
        }
        return val;
    };

    QJsonValue rv = replaceRec(QJsonValue(obj));
    if (rv.isObject()) return rv.toObject();
    return QJsonObject();
}

// -------------------- Lookup builders & resolvers ---------------------------

static void buildLookups(const QJsonObject &root,
                         QMap<QString, QJsonObject> &materialById,
                         QMap<QString, QString> &meshToMaterial,
                         QMap<QString, QString> &textureIdToPath)
{
    if (root.contains("materials") && root["materials"].isArray()) {
        for (const QJsonValue &mv : root["materials"].toArray()) {
            if (!mv.isObject()) continue;
            QJsonObject mo = mv.toObject();
            QString mid = mo.value("id").toString();
            if (!mid.isEmpty()) materialById.insert(mid, mo);
        }
    }

    if (root.contains("meshes") && root["meshes"].isArray()) {
        for (const QJsonValue &mv : root["meshes"].toArray()) {
            if (!mv.isObject()) continue;
            QJsonObject mo = mv.toObject();
            QString mid = mo.value("id").toString();
            QString matid = mo.value("material_id").toString();
            if (!mid.isEmpty() && !matid.isEmpty()) meshToMaterial.insert(mid, matid);
        }
    }

    if (root.contains("textures") && root["textures"].isArray()) {
        for (const QJsonValue &tv : root["textures"].toArray()) {
            if (!tv.isObject()) continue;
            QJsonObject to = tv.toObject();
            QString tid = to.value("id").toString();
            QString p = to.value("path").toString();
            if (!tid.isEmpty() && !p.isEmpty()) textureIdToPath.insert(tid, p);
        }
    }
}

static QString normalizePath(const QString &p)
{
    if (p.isEmpty()) return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(p));
}

static QString resolveTextureIdToPath(const QString &texId,
                                      const QMap<QString, QString> &textureIdToPath,
                                      const QString &modelFolder,
                                      const AssetResolverFunc &resolver)
{
    if (texId.isEmpty()) return QString();

    // 1) resolver (project-registered asset)
    if (resolver) {
        QString p = resolver(texId);
        if (!p.isEmpty()) {
            p = normalizePath(p);
            if (QFileInfo::exists(p)) return p;
        }
    }

    // 2) manifest path fallback
    if (textureIdToPath.contains(texId)) {
        QString p = normalizePath(textureIdToPath.value(texId));
        if (p.isEmpty()) return QString();
        if (QFileInfo(p).isAbsolute()) {
            if (QFileInfo::exists(p)) return p;
            // try normalize mixed slashes: already normalized
        } else {
            // relative: modelFolder + p
            if (!modelFolder.isEmpty()) {
                QString candidate = QDir(modelFolder).filePath(p);
                candidate = normalizePath(candidate);
                if (QFileInfo(candidate).exists()) return candidate;
                // also try file name in modelFolder (handles weird path separators)
                QString fn = QFileInfo(p).fileName();
                if (!fn.isEmpty()) {
                    QString candidate2 = QDir(modelFolder).filePath(fn);
                    candidate2 = normalizePath(candidate2);
                    if (QFileInfo(candidate2).exists()) return candidate2;
                }
            }
        }
    }

    return QString();
}

// Resolve all common channels for a given material id into a map of existing absolute paths
static QJsonObject resolveMaterialTextures(const QJsonObject &materialObj,
                                           const QMap<QString, QString> &textureIdToPath,
                                           const QString &modelFolder,
                                           const AssetResolverFunc &resolver)
{
    QJsonObject out;
    struct Channel { const char* field; const char* key; };
    const Channel channels[] = {
        { "base_color_texture", "base_color" },
        { "normal_texture", "normal" },
        { "metallic_texture", "metallic" },
        { "roughness_texture", "roughness" },
        { "emissive_texture", "emissive" }
    };
    for (const Channel &c : channels) {
        QString tid = materialObj.value(QString::fromLatin1(c.field)).toString();
        if (tid.isEmpty()) continue;
        QString p = resolveTextureIdToPath(tid, textureIdToPath, modelFolder, resolver);
        if (!p.isEmpty()) out.insert(QString::fromLatin1(c.key), p);
    }
    return out;
}

// --------------------- Node creation ---------------------------------------

static iris::SceneNodePtr createShallowNode(const QJsonObject &nodeJson,
                                            const QMap<QString, QJsonObject> &materialById,
                                            const QMap<QString, QString> &meshToMaterial,
                                            const QMap<QString, QString> &textureIdToPath,
                                            const QString &modelFolder,
                                            QHash<QString, iris::SceneNodePtr> &guidToNode,
                                            QHash<NodeIdType, QJsonObject> &metaMap,
                                            const AssetResolverFunc &resolver,
                                            const QJsonObject &rootManifest)
{
    QString guid = tryGetString(nodeJson, {"id"});
    QString type = tryGetString(nodeJson, {"type", "nodeType", "class"});
    iris::SceneNodePtr node;

    bool isMesh = (!type.isEmpty() && type.toLower().contains("mesh"));
    bool isLight = (!type.isEmpty() && type.toLower().contains("light"));

    if (isMesh) {
        node = iris::MeshNode::create();
        node->sceneNodeType = iris::SceneNodeType::Mesh;

        QString meshId = tryGetString(nodeJson, {"mesh_id", "meshId", "mesh"});
        QString nodeOverrideMat = tryGetString(nodeJson, {"material_override_id", "materialOverrideId", "material"});

        // Only attempt to resolve meshId as external asset if meshId is NOT in manifest.meshes (i.e., not internal)
        QString meshResolved;
        if (!meshId.isEmpty() && !meshToMaterial.contains(meshId)) {
            if (resolver) meshResolved = resolver(meshId);
        }
        if (!meshResolved.isEmpty() && QFileInfo::exists(meshResolved)) {
            auto mn = node.staticCast<iris::MeshNode>();
            mn->meshPath = normalizePath(meshResolved);
            qDebug() << "NewJsonAdapter: resolved external meshId to file:" << meshId << "->" << mn->meshPath;
        } else {
            // resolve material -> textures
            QString matId;
            if (!nodeOverrideMat.isEmpty()) matId = nodeOverrideMat;
            else if (!meshId.isEmpty() && meshToMaterial.contains(meshId)) matId = meshToMaterial.value(meshId);

            if (!matId.isEmpty() && materialById.contains(matId)) {
                QJsonObject matObj = materialById.value(matId);
                QJsonObject resolved = resolveMaterialTextures(matObj, textureIdToPath, modelFolder, resolver);
                if (!resolved.isEmpty()) {
                    QJsonObject meta = nodeJson;
                    meta.insert(QStringLiteral("__resolved_textures"), resolved);
                    metaMap.insert(static_cast<NodeIdType>(node->getNodeId()), meta);
                    qDebug() << "NewJsonAdapter: resolved textures for node" << guid << ":" << QJsonDocument(resolved).toJson(QJsonDocument::Compact);
                } else {
                    qDebug() << "NewJsonAdapter: no textures resolved for node (will rely on original_index if needed):" << guid;
                }
            } else {
                qDebug() << "NewJsonAdapter: no material found for node (meshId/material override):" << guid << meshId << nodeOverrideMat;
            }
        }
    } else if (isLight) {
        node = iris::SceneNode::create();
        node->sceneNodeType = iris::SceneNodeType::Light;
    } else {
        node = iris::SceneNode::create();
    }

    QString name = tryGetString(nodeJson, {"name", "label", "id"});
    if (!name.isEmpty()) node->setName(name);

    if (nodeJson.contains("transform") && nodeJson["transform"].isObject()) {
        QJsonObject t = nodeJson["transform"].toObject();
        node->setLocalPos(parseVec3(t, {"translation","translate","pos","position"}, node->getLocalPos()));
        node->setLocalRot(parseRotation(t, {"rotation","rot","orientation"}, node->getLocalRot()));
        node->setLocalScale(parseVec3(t, {"scale","s","localScale"}, node->getLocalScale()));
    } else {
        node->setLocalPos(parseVec3(nodeJson, {"translation","pos","position"}, node->getLocalPos()));
        node->setLocalRot(parseRotation(nodeJson, {"rotation","rot","orientation"}, node->getLocalRot()));
        node->setLocalScale(parseVec3(nodeJson, {"scale","s","localScale"}, node->getLocalScale()));
    }

    if (!metaMap.contains(node->getNodeId())) metaMap.insert(node->getNodeId(), nodeJson);
    if (!guid.isEmpty()) guidToNode.insert(guid, node);
    return node;
}

// ----------------------- Public API ---------------------------------------

bool canHandle(const QJsonObject &rootObj)
{
    if (rootObj.contains("nodes") && rootObj["nodes"].isArray()) {
        QJsonArray arr = rootObj["nodes"].toArray();
        if (!arr.isEmpty() && arr[0].isObject()) {
            QJsonObject first = arr[0].toObject();
            if (first.contains("id") && (first.contains("mesh_id") || first.contains("children_ids") || first.contains("material_override_id"))) {
                return true;
            }
        }
    }
    if (rootObj.contains("entities") && rootObj["entities"].isArray()) return true;
    return false;
}

AdapterResult convertJsonToScene(const QJsonObject &rootObj,
                                 AssetResolverFunc assetResolver,
                                 const QMap<QString, QString> &guidMap)
{
    AdapterResult res;
    QJsonObject adjustedRoot = rootObj;
    if (!guidMap.isEmpty()) {
        adjustedRoot = replaceGuidsInObject(rootObj, guidMap);
        qDebug() << "NewJsonAdapter: applied GUID mapping replacements (size =" << guidMap.size() << ")";
    }

    QMap<QString, QJsonObject> materialById;
    QMap<QString, QString> meshToMaterial;
    QMap<QString, QString> textureIdToPath;
    buildLookups(adjustedRoot, materialById, meshToMaterial, textureIdToPath);

#ifdef QT_DEBUG
    qDebug() << "NewJsonAdapter: Diagnostic mapping check (mesh -> material -> texture)";
    for (auto it = meshToMaterial.constBegin(); it != meshToMaterial.constEnd(); ++it) {
        QString meshId = it.key();
        QString matId = it.value();
        bool matExists = materialById.contains(matId);
        QString baseTex = matExists ? materialById.value(matId).value("base_color_texture").toString() : QString();
        bool texEntry = !baseTex.isEmpty() && textureIdToPath.contains(baseTex);
        qDebug() << "  mesh" << meshId << "-> material" << matId << " materialExists=" << matExists
                 << " base_color_texture=" << baseTex << " textureEntryExists=" << texEntry;
    }
#endif

    // modelFolder inference
    QString modelFolder;
    QString sourceFile = adjustedRoot.value("source_file").toString();
    if (!sourceFile.isEmpty()) {
        QFileInfo sfInfo(sourceFile);
        if (sfInfo.isAbsolute()) modelFolder = sfInfo.absolutePath();
        else {
            // try package folder inference via adjustedRoot.id
            QString packageGuid;
            if (adjustedRoot.contains("id") && adjustedRoot["id"].isString()) packageGuid = adjustedRoot["id"].toString();
            if (!packageGuid.isEmpty()) {
                QString assetsRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                QString candidateFolder = QDir(assetsRoot).filePath(QStringLiteral("AssetStore") + QDir::separator() + packageGuid);
                if (QDir(candidateFolder).exists()) modelFolder = candidateFolder;
                else if (assetResolver) {
                    QString resolved = assetResolver(packageGuid);
                    if (!resolved.isEmpty()) {
                        QFileInfo rf(resolved);
                        if (rf.isDir()) modelFolder = rf.absoluteFilePath();
                        else modelFolder = rf.absolutePath();
                    }
                }
            }
        }
    }

    res.scene = iris::Scene::create();
    if (!res.scene->rootNode) res.scene->rootNode = iris::SceneNode::create();

    QHash<QString, iris::SceneNodePtr> guidToNode;
    QHash<NodeIdType, QJsonObject> &meta = res.nodeMetadata;

    QJsonArray nodeArray;
    if (adjustedRoot.contains("nodes") && adjustedRoot["nodes"].isArray()) nodeArray = adjustedRoot["nodes"].toArray();
    else if (adjustedRoot.contains("entities") && adjustedRoot["entities"].isArray()) nodeArray = adjustedRoot["entities"].toArray();

    if (!nodeArray.isEmpty()) {
        for (const QJsonValue &v : nodeArray) {
            if (!v.isObject()) continue;
            QJsonObject nobj = v.toObject();
            iris::SceneNodePtr node = createShallowNode(nobj, materialById, meshToMaterial, textureIdToPath, modelFolder, guidToNode, meta, assetResolver, adjustedRoot);
            Q_UNUSED(node);
        }

        for (const QJsonValue &v : nodeArray) {
            if (!v.isObject()) continue;
            QJsonObject nobj = v.toObject();
            QString guid = tryGetString(nobj, {"id"});
            iris::SceneNodePtr node = guidToNode.value(guid, iris::SceneNodePtr());
            if (!node) continue;
            QString parentGuid = tryGetString(nobj, {"parent_id", "parentId", "parent"});
            if (!parentGuid.isEmpty()) {
                iris::SceneNodePtr parentNode = guidToNode.value(parentGuid, iris::SceneNodePtr());
                if (parentNode) parentNode->addChild(node);
                else res.scene->rootNode->addChild(node);
            } else {
                res.scene->rootNode->addChild(node);
            }
            if (!guid.isEmpty()) res.guidToNodeId.insert(guid, static_cast<NodeIdType>(node->getNodeId()));
        }

        res.nodeMetadata = meta;
        return res;
    }

    if (adjustedRoot.contains("children") && adjustedRoot["children"].isArray()) {
        std::function<iris::SceneNodePtr(const QJsonObject&)> createRecursive;
        createRecursive = [&](const QJsonObject &o)->iris::SceneNodePtr {
            iris::SceneNodePtr node = createShallowNode(o, materialById, meshToMaterial, textureIdToPath, modelFolder, guidToNode, meta, assetResolver, adjustedRoot);
            QString guid = tryGetString(o, {"id"});
            if (!guid.isEmpty()) res.guidToNodeId.insert(guid, static_cast<NodeIdType>(node->getNodeId()));
            if (o.contains("children") && o["children"].isArray()) {
                QJsonArray arr = o["children"].toArray();
                for (const QJsonValue &cv : arr) {
                    if (!cv.isObject()) continue;
                    iris::SceneNodePtr child = createRecursive(cv.toObject());
                    if (child) node->addChild(child);
                }
            }
            if (!meta.contains(node->getNodeId())) meta.insert(node->getNodeId(), o);
            return node;
        };

        QJsonArray arr = adjustedRoot["children"].toArray();
        for (const QJsonValue &cv : arr) {
            if (!cv.isObject()) continue;
            iris::SceneNodePtr rootChild = createRecursive(cv.toObject());
            if (rootChild) res.scene->rootNode->addChild(rootChild);
        }

        res.nodeMetadata = meta;
        return res;
    }

    res.nodeMetadata = meta;
    return res;
}

} // namespace NewJsonAdapter
