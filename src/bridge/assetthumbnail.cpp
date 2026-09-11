/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "bridge/assetthumbnail.h"

#include <QPixmap>

#include "bridge/enginethumbnailrenderer.h"
#include "data/database/database.h"
#include "services/assethelper.h"
#include "services/libraryassetnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace assetthumb
{

QImage renderObject(Database *db, Project *project, const QString &guid,
                    const std::shared_ptr<jahshaka::engine::Engine> &engine, QSize size)
{
    if (!db || !engine || guid.isEmpty()) return QImage();
    iris::SceneNodePtr node = libraryasset::fromLibrary(db, project, guid);
    if (!node) return QImage();

    EngineThumbnailRenderer renderer(engine);
    return renderer.renderNode(node, size);
}

bool storeObject(Database *db, Project *project, const QString &guid,
                 const std::shared_ptr<jahshaka::engine::Engine> &engine, QSize size)
{
    const QImage image = renderObject(db, project, guid, engine, size);
    if (image.isNull()) return false;
    return db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(image)));
}

}   // namespace assetthumb
