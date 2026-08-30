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
    static void updateNodeMaterial(iris::SceneNodePtr &node, QJsonObject definition);
    static QByteArray makeBlobFromPixmap(const QPixmap &pixmap);
    static QStringList fetchAssetAndAllDependencies(const QString &guid, Database *db);
    static QStringList getChildGuids(const iris::SceneNodePtr &node);
    static ModelTypes getAssetTypeFromExtension(const QString &fileSuffix);
    /// modelStats (optional): filled with AssetMetadata::forModelScene counts
    /// from the aiScene this load already produced — the import-time source
    /// of the per-asset "metadata" properties block.
    static iris::SceneNodePtr extractTexturesAndMaterialFromMesh(const QString &filePath,
                                                                 QStringList &textureList,
                                                                 QStringList &texturesFullPath,
                                                                 bool& hasEmbeddedTexture,
                                                                 QJsonObject *modelStats = nullptr);
};

#endif