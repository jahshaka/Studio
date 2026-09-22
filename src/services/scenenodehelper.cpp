/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/scenenodehelper.h"

#include "data/guidmanager.h"
#include "services/primitiveassets.h"

iris::MeshNodePtr SceneNodeHelper::createBasicMeshNode(
    const QString &meshSeed,
    const QString &meshName,
    const QString &meshGuid,
    Database *db
)
{
    iris::MeshNodePtr node = iris::MeshNode::create();
    // THE BAKED ASSET, and the seed path as the document's reference to it.
    node->setMesh(PrimitiveAssets::mesh(meshSeed, db));
    node->meshPath = meshSeed;
    node->meshIndex = 0;
    node->setName(meshName);
    node->setGUID(meshGuid);
    node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->isBuiltIn = true;
    return node;
}