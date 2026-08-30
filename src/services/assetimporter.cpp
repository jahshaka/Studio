/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "assetimporter.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <functional>

#include "../constants.h"
#include "../core/assethelper.h"
#include "../core/project.h"
#include "../globals.h"
#include "../core/database/database.h"
#include "../core/guidmanager.h"
#include "../io/assetmanager.h"
#include "../io/scenewriter.h"
#include "../../irisgl/src/core/irisutils.h"
#include "../../irisgl/src/core/property.h"
#include "../../irisgl/src/materials/custommaterial.h"
#include "../../irisgl/src/scenegraph/meshnode.h"

AssetImporter::Result AssetImporter::importMesh(const QString &filePath, Database *db)
{
    Result result;
    if (!db) { result.error = "no database"; return result; }

    const QFileInfo sourceInfo(filePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        result.error = QStringLiteral("no such file '%1'").arg(filePath);
        return result;
    }
    if (!Constants::MODEL_EXTS.contains(sourceInfo.suffix().toLower())) {
        result.error = QStringLiteral("'%1' is not a mesh file (%2)")
                           .arg(sourceInfo.fileName(), Constants::MODEL_EXTS.join(", "));
        return result;
    }

    // Store layout, exactly like AssetView::importModel: main_guid names the
    // Object row, the on-disk folder and the parent of every member row.
    const QString mainGuid = GUIDManager::generateGUID();
    const QString storeRoot = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), "AssetStore");
    const QString assetFolder = QDir(storeRoot).filePath(mainGuid);
    QDir().mkpath(assetFolder);

    const QString copiedModel = IrisUtils::join(assetFolder, sourceInfo.fileName());
    if (!QFile::copy(filePath, copiedModel)) {
        result.error = QStringLiteral("could not copy '%1' into the asset store").arg(filePath);
        return result;
    }

    // Member row for the mesh file (parent = mainGuid, view_filter = Editor).
    const QString meshGuid = GUIDManager::generateGUID();
    db->createAssetEntry(meshGuid, sourceInfo.fileName(),
                         static_cast<int>(ModelTypes::Mesh), mainGuid,
                         Globals::project->getProjectGuid());

    // Assimp load + texture discovery (no DB rows, no file copies inside).
    QStringList textureNames, texturePaths;
    bool hasEmbedded = false;
    auto node = AssetHelper::extractTexturesAndMaterialFromMesh(copiedModel, textureNames,
                                                                texturePaths, hasEmbedded);
    if (!node) {
        db->deleteAsset(meshGuid);
        QDir(assetFolder).removeRecursively();
        result.error = QStringLiteral("assimp could not read '%1'").arg(sourceInfo.fileName());
        return result;
    }

    // Referenced on-disk textures: copy into the store folder + member rows.
    struct TexEntry { QString fileName, guid; };
    QVector<TexEntry> textures;
    for (const auto &texPath : texturePaths) {
        const QFileInfo texInfo(texPath);
        if (!texInfo.exists()) continue;
        if (std::any_of(textures.begin(), textures.end(),
                        [&](const TexEntry &t) { return t.fileName == texInfo.fileName(); }))
            continue;
        QFile::copy(texPath, IrisUtils::join(assetFolder, texInfo.fileName()));
        TexEntry entry{ texInfo.fileName(), GUIDManager::generateGUID() };
        db->createAssetEntry(entry.guid, entry.fileName,
                             static_cast<int>(ModelTypes::Texture), mainGuid,
                             Globals::project->getProjectGuid());
        textures.append(entry);
    }

    // Guid rewrite (importModel's replacePathsWithGUIDs): the mesh path becomes
    // the Mesh row's guid, every mesh node carries the Object guid, texture
    // material properties become texture guids.
    std::function<void(iris::SceneNodePtr &)> rewrite = [&](iris::SceneNodePtr &n) {
        if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto meshNode = n.staticCast<iris::MeshNode>();
            if (QFileInfo(meshNode->meshPath).fileName() == sourceInfo.fileName())
                meshNode->meshPath = meshGuid;
            meshNode->setGUID(mainGuid);

            auto material = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
            if (material) {
                for (auto prop : material->properties) {
                    if (prop->type != iris::PropertyType::Texture) continue;
                    const QString fileName = QFileInfo(prop->getValue().toString()).fileName();
                    for (const auto &tex : textures) {
                        if (tex.fileName == fileName)
                            material->setValue(prop->name, tex.guid);
                    }
                }
            }
        }
        for (auto &child : n->children) rewrite(child);
    };
    rewrite(node);

    QJsonObject blob;
    SceneWriter::writeSceneNode(blob, node, false);

    // The Object row — the only row with view_filter AssetsView, parent = root.
    db->createAssetEntry(mainGuid, sourceInfo.baseName(),
                         static_cast<int>(ModelTypes::Object), QString(),
                         Globals::project->getProjectGuid(),
                         QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                         QJsonDocument(blob).toJson(), AssetViewFilter::AssetsView);

    for (const auto &tex : textures)
        db->createDependency(static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Texture),
                             mainGuid, tex.guid);
    db->createDependency(static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh),
                         mainGuid, meshGuid);

    // The Mesh row carries no blob (importModel does the same at its tail).
    db->updateAssetAsset(meshGuid, QByteArray());

    // Session registration, like the widget import.
    auto *assetObject = new AssetNodeObject;
    assetObject->fileName = sourceInfo.fileName();
    assetObject->assetGuid = mainGuid;
    assetObject->path = copiedModel;
    assetObject->setValue(QVariant::fromValue(node));
    AssetManager::addAsset(assetObject);

    result.objectGuid = mainGuid;
    result.meshGuid = meshGuid;
    result.node = node;
    return result;
}
