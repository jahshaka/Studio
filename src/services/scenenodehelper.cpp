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

iris::MeshNodePtr SceneNodeHelper::createBasicMeshNode(
    const QString &meshPath,
    const QString &meshName,
    const QString &meshGuid
)
{
    iris::MeshNodePtr node = iris::MeshNode::create();
    node->setMesh(meshPath);
    node->setName(meshName);
    node->setGUID(meshGuid);
    node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->isBuiltIn = true;
    return node;
}