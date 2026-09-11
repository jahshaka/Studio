/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/libraryassetnode.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "data/database/database.h"
#include "io/scenereader.h"
#include "services/assetmetadata.h"
#include "services/fitsize.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace libraryasset
{

iris::SceneNodePtr fromLibrary(Database *db, Project *project, const QString &guid)
{
    if (!db || guid.isEmpty()) return iris::SceneNodePtr();

    QJsonObject blob = QJsonDocument::fromJson(db->fetchAssetData(guid)).object();
    if (blob.isEmpty()) return iris::SceneNodePtr();

    SceneReader reader;
    reader.setDatabaseHandle(db);       // mesh + texture guids -> store files
    reader.setProject(project);
    reader.setLibrarySource();          // the STORE asset's own bytes, not a project pin
    iris::SceneNodePtr node = reader.readSceneNode(blob);
    if (!node) return node;             // retired root node type

    // The asset's size, exactly as SceneEditService::addMaterialMesh applies it
    // at every instantiation (services/fitsize.h). ensure() backfills the block
    // for a row imported before the policy landed — one parse, once, ever.
    fitsize::applyFit(node, fitsize::fitScaleOf(AssetMetadata::ensure(db, guid)));
    node->update(0.0f);
    return node;
}

}   // namespace libraryasset
