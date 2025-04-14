/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "scenenodehelper.h"

#include "guidmanager.h"

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