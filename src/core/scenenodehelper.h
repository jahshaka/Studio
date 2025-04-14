/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENENODEHELPER_H
#define SCENENODEHELPER_H

#include "irisglfwd.h"

#include "irisgl/src/scenegraph/meshnode.h"

#include "constants.h"
#include "core/project.h"
#include "core/database/database.h"

class SceneNodeHelper
{
public:
    static iris::MeshNodePtr createBasicMeshNode(
        const QString &meshPath,
        const QString &meshName,
        const QString &guid
    );
};

#endif