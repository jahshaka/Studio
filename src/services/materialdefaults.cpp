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
                         QStringList *textures, QStringList *newlyPinned)
{
    if (textures) textures->clear();
    if (newlyPinned) newlyPinned->clear();
    if (!defaultfloor::isDefaultFloor(node)) return iris::MaterialPtr();
    QString tileGuid;
    bool tileNew = false;
    auto material = defaultfloor::createMaterial(db, project, &tileGuid, &tileNew);
    if (!tileGuid.isEmpty()) {
        if (textures) textures->append(tileGuid);
        if (newlyPinned && tileNew) newlyPinned->append(tileGuid);
    }
    return material;
}

}   // namespace materialdefaults
