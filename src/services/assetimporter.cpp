/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetimporter.h"

#include "data/project.h"
#include "services/import/assetimportservice.h"

// AssetImporter survives as the STABLE FACADE over the one pipeline
// (ASSET_PIPELINE_SPEC §3.2.4: "AssetImporter survives as the seed of the
// service"). Its former bodies moved into MeshImporter/MediaImporter inside
// src/services/import/; these wrappers keep the verb layer's signatures.

AssetImporter::Result AssetImporter::importMesh(const QString &filePath, Database *db,
                                                Project *project)
{
    AssetImportService service(db, project);
    ImportRequest request;
    request.sourcePath = filePath;
    request.typeHint = static_cast<int>(ModelTypes::Mesh);
    const ImportResult imported = service.import(request);

    Result result;
    result.objectGuid = imported.assetGuid;
    result.meshGuid = imported.meshGuid;
    result.error = imported.error;
    result.node = imported.node;
    return result;
}

AssetImporter::Result AssetImporter::importFile(const QString &filePath, Database *db,
                                                Project *project, int drawerId)
{
    AssetImportService service(db, project);
    ImportRequest request;
    request.sourcePath = filePath;
    request.drawerId = drawerId;
    const ImportResult imported = service.import(request);

    Result result;
    result.objectGuid = imported.assetGuid;
    result.meshGuid = imported.meshGuid;
    result.error = imported.error;
    result.node = imported.node;
    return result;
}
