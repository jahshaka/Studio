/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "thumbnailservice.h"

#include <QDir>

#include "../core/database/database.h"
#include "../core/project.h"
#include "../editor/thumbnailgenerator.h"

ThumbnailService::ThumbnailService(Database *db, Project *project)
    : db(db), project(project)
{
}

void ThumbnailService::refreshObjectThumbnail(const QString &guid)
{
    QString meshGuid = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));
    auto assetName = db->fetchAsset(meshGuid).name;

    ThumbnailGenerator::getSingleton()->requestThumbnail(
        ThumbnailRequestType::ImportedMesh,
        QDir(project->getProjectFolder()).filePath(assetName),
        guid
    );
}
