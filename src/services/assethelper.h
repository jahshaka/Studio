/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETHELPER_H
#define ASSETHELPER_H

#include <QJsonObject>
#include <QJsonArray>

#include "irisgl/irisglfwd.h"
#include "data/constants.h"
#include "data/project.h"
#include "data/database/database.h"

class AssetHelper
{
public:
    /// Rebuilds a library object's materials from its stored definition.
    /// Texture values in the definition are member asset GUIDs (the import
    /// pipeline stores references, not paths); `db` is required to resolve
    /// them to CAS store files before they reach the path-based loaders.
    static void updateNodeMaterial(iris::SceneNodePtr &node, QJsonObject definition,
                                   Database *db);
    static QByteArray makeBlobFromPixmap(const QPixmap &pixmap);
    static QStringList fetchAssetAndAllDependencies(const QString &guid, Database *db);
    static QStringList getChildGuids(const iris::SceneNodePtr &node);
    static ModelTypes getAssetTypeFromExtension(const QString &fileSuffix);
    /// modelStats (optional): filled with AssetMetadata::forModelScene counts
    /// from the aiScene this load already produced — the import-time source
    /// of the per-asset "metadata" properties block.
    /// `extractDir`: where embedded textures / derived maps are written
    /// (import staging). Empty = beside the source — wrong for read-only
    /// sources; the import pipeline always passes a staging dir.
    static iris::SceneNodePtr extractTexturesAndMaterialFromMesh(const QString &filePath,
                                                                 QStringList &textureList,
                                                                 QStringList &texturesFullPath,
                                                                 bool& hasEmbeddedTexture,
                                                                 QJsonObject *modelStats = nullptr,
                                                                 const QString &extractDir = QString());

    /// Process-wide count of extractTexturesAndMaterialFromMesh runs — each
    /// is one full assimp parse of a model file. Instrumentation for the
    /// import suites: the completion tail must never add a second parse on
    /// top of the pipeline's convert (import.async asserts on this).
    static int meshParseCount();
};

#endif