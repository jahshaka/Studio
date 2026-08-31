/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetimporter.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <functional>

#include "data/constants.h"
#include "services/assethelper.h"
#include "data/project.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"
#include "services/assetmetadata.h"
#include "services/thumbnailmanager.h"
#include "services/videoutils.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"

AssetImporter::Result AssetImporter::importMesh(const QString &filePath, Database *db,
                                               Project *project)
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

    // .obj sidecars: the .mtl files the model names live beside the SOURCE and
    // must follow it into the store, or any later re-read of the copy (metadata
    // backfill, preview rebuilds) loses every material. Textures are copied
    // below through assimp's material references; the .mtl itself is not one.
    if (sourceInfo.suffix().toLower() == QStringLiteral("obj")) {
        QFile obj(filePath);
        if (obj.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!obj.atEnd()) {
                const QString line = QString::fromUtf8(obj.readLine()).trimmed();
                if (!line.startsWith(QStringLiteral("mtllib "))) continue;
                const QFileInfo mtl(sourceInfo.dir(), line.mid(7).trimmed());
                if (mtl.exists() && mtl.isFile())
                    QFile::copy(mtl.absoluteFilePath(),
                                IrisUtils::join(assetFolder, mtl.fileName()));
            }
        }
    }

    // Member row for the mesh file (parent = mainGuid, view_filter = Editor).
    const QString meshGuid = GUIDManager::generateGUID();
    db->createAssetEntry(meshGuid, sourceInfo.fileName(),
                         static_cast<int>(ModelTypes::Mesh), mainGuid,
                         project->getProjectGuid());

    // Assimp load + texture discovery (no DB rows, no file copies inside).
    // modelStats: vertex/triangle/material/texture counts from that same
    // assimp scene — the Object row's "metadata" properties block.
    // Extract from the SOURCE path, not the store copy: sibling files the
    // model references (.mtl, textures) only exist beside the original, so
    // extracting from the lone copy silently dropped every material texture
    // (white previews/thumbnails from assets.import, texture count 0).
    QStringList textureNames, texturePaths;
    bool hasEmbedded = false;
    QJsonObject modelStats;
    auto node = AssetHelper::extractTexturesAndMaterialFromMesh(filePath, textureNames,
                                                                texturePaths, hasEmbedded,
                                                                &modelStats);
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
                             project->getProjectGuid());
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

            // Base MaterialPtr — imported GLBs now carry PbrMaterial, and both
            // it and CustomMaterial expose `properties` + virtual setValue.
            auto material = meshNode->getMaterial();
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
    QJsonObject properties;
    if (!modelStats.isEmpty()) properties["metadata"] = modelStats;
    db->createAssetEntry(mainGuid, sourceInfo.baseName(),
                         static_cast<int>(ModelTypes::Object), QString(),
                         project->getProjectGuid(),
                         QString(), QString(), QByteArray(),
                         QJsonDocument(properties).toJson(), QByteArray(),
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

AssetImporter::Result AssetImporter::importFile(const QString &filePath, Database *db,
                                                Project *project, int drawerId)
{
    Result result;
    if (!db) { result.error = "no database"; return result; }

    const QFileInfo sourceInfo(filePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        result.error = QStringLiteral("no such file '%1'").arg(filePath);
        return result;
    }

    // THE per-type import dispatch (ASSET_DRAWERS_SPEC §3): new library types
    // (Video, …) are one case here, not a new code path.
    const ModelTypes type = AssetHelper::getAssetTypeFromExtension(sourceInfo.suffix().toLower());
    switch (type) {
    case ModelTypes::Mesh:
        result = importMesh(filePath, db, project);
        break;

    case ModelTypes::Texture:
    case ModelTypes::Music:
    case ModelTypes::Video: {
        // One row at the file's guid, view_filter AssetsView — a first-class
        // library asset, not the Editor-filtered ghost the old path made.
        const QString guid = GUIDManager::generateGUID();
        const QString storeRoot = IrisUtils::join(
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), "AssetStore");
        const QString assetFolder = QDir(storeRoot).filePath(guid);
        QDir().mkpath(assetFolder);

        const QString copied = IrisUtils::join(assetFolder, sourceInfo.fileName());
        if (!QFile::copy(filePath, copied)) {
            result.error = QStringLiteral("could not copy '%1' into the asset store").arg(filePath);
            return result;
        }

        // Tile thumbnail: the image itself, a first-second video frame grab
        // (film icon when decode fails — ASSET_MEDIA_SPEC §1), or the music icon.
        QPixmap thumbnail;
        if (type == ModelTypes::Texture) {
            auto thumb = ThumbnailManager::createThumbnail(copied, 256, 256);
            if (thumb && thumb->thumb) thumbnail = QPixmap::fromImage(*thumb->thumb);
        }
        else if (type == ModelTypes::Video) {
            thumbnail = VideoUtils::thumbnailFor(copied);
        }
        else {
            thumbnail = QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png"));
        }

        // Import-time metadata: image header / wav header / video probe
        // + format + byte size.
        const QJsonObject meta = (type == ModelTypes::Texture)
                                     ? AssetMetadata::forImageFile(copied)
                                     : (type == ModelTypes::Video)
                                           ? AssetMetadata::forVideoFile(copied)
                                           : AssetMetadata::forAudioFile(copied);
        db->createAssetEntry(guid, sourceInfo.fileName(), static_cast<int>(type),
                             QString(), project ? project->getProjectGuid() : QString(),
                             QString(), QString(), AssetHelper::makeBlobFromPixmap(thumbnail),
                             QJsonDocument(QJsonObject{ { "metadata", meta } }).toJson(),
                             QByteArray(), QByteArray(),
                             AssetViewFilter::AssetsView);
        result.objectGuid = guid;
        break;
    }

    default:
        result.error = QStringLiteral("'%1' is not an importable library file "
                                      "(models, images, audio or video)").arg(sourceInfo.fileName());
        return result;
    }

    // File it in the requested drawer; rows default to Uncategorized (0).
    if (result.ok() && drawerId > 0) {
        if (db->fetchCollectionSubtree(drawerId).isEmpty())
            result.error = QStringLiteral("imported, but drawer %1 does not exist").arg(drawerId);
        else
            db->switchAssetCollection(drawerId, result.objectGuid);
    }

    return result;
}
