/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialdefaults.h"

#include <QStringList>

#include "irisgl/document/materials/pbrmaterial.h"
#include "services/defaultfloor.h"

namespace materialdefaults {

iris::MaterialPtr create(const iris::SceneNodePtr &node, Database *db, Project *project,
                         QStringList *pinnedTextures)
{
    if (pinnedTextures) pinnedTextures->clear();
    if (!defaultfloor::isDefaultFloor(node)) return iris::MaterialPtr();
    QString tileGuid;
    auto material = defaultfloor::createMaterial(db, project, &tileGuid);
    if (pinnedTextures && !tileGuid.isEmpty()) pinnedTextures->append(tileGuid);
    return material;
}

}   // namespace materialdefaults
