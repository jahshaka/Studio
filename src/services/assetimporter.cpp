/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetimporter.h"

#include <QFileInfo>

#include "data/constants.h"
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
    // NO TYPE HINT, deliberately (it used to pin ModelTypes::Mesh): this entry
    // point means "import this MODEL FILE", and which library type a model
    // file becomes is a property of its CONTENTS, not of the caller's
    // intention. A file with geometry is an Object; a file with animation and
    // no geometry is an Animation asset — pinning the mesh importer here made
    // every Mixamo download "without skin" fail with a message about Draco
    // compression, which was the owner's report.
    //
    // The extension gate keeps the contract narrow: this is still not a
    // general "import anything" door (that is importFile) — an image handed to
    // it is refused rather than quietly filed as a texture.
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (!Constants::MODEL_EXTS.contains(suffix) && !Constants::ANIMATION_EXTS.contains(suffix)) {
        Result refused;
        refused.error = QStringLiteral("'%1' is not a model or animation file (%2)")
                            .arg(QFileInfo(filePath).fileName(),
                                 QStringList(Constants::ANIMATION_EXTS)
                                     .join(QStringLiteral(", ")));
        return refused;
    }
    const ImportResult imported = service.import(request);

    Result result;
    result.objectGuid = imported.assetGuid;
    result.meshGuid = imported.meshGuid;
    result.error = imported.error;
    result.node = imported.node;
    return result;
}

AssetImporter::Result AssetImporter::importFile(const QString &filePath, Database *db,
                                                Project *project, int drawerId, int typeHint)
{
    AssetImportService service(db, project);
    ImportRequest request;
    request.sourcePath = filePath;
    request.drawerId = drawerId;
    request.typeHint = typeHint;
    const ImportResult imported = service.import(request);

    Result result;
    result.objectGuid = imported.assetGuid;
    result.meshGuid = imported.meshGuid;
    result.error = imported.error;
    result.node = imported.node;
    return result;
}
