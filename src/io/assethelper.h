/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETHELPER_H
#define ASSETHELPER_H

#include <QJsonObject>
#include <QJsonArray>

#include "irisglfwd.h"
#include "constants.h"
#include "core/project.h"

class AssetHelper
{
public:
    static void updateNodeMaterial(iris::SceneNodePtr &node, const QJsonObject definition);
    static QByteArray makeBlobFromPixmap(const QPixmap &pixmap);
    static ModelTypes getAssetTypeFromExtension(const QString &fileSuffix);
    static iris::SceneNodePtr extractTexturesAndMaterialFromMesh(const QString &filePath,
                                                                 QStringList &textureList);
};

#endif