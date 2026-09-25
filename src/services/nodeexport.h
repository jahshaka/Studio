/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NODEEXPORT_H
#define NODEEXPORT_H

// WHAT A NODE EXPORTS AS — the one rule the scene outliner's Export menu and
// node.exportArchive both read (STUDIO-CRUD-1 item 9). Header-only so the
// outliner's suites, which link no SceneEditService, keep linking.

#include "data/project.h"   // ModelTypes
#include "irisgl/document/scenegraph/scenenode.h"

namespace nodeexport {

/// Object for a mesh or empty that is not a built-in primitive,
/// ParticleSystem for a particle system, Undefined for everything else and
/// for a node that is not exportable at all.
inline ModelTypes typeFor(const iris::SceneNodePtr &node)
{
    if (!node || !node->isExportable()) return ModelTypes::Undefined;
    const auto type = node->getSceneNodeType();
    if ((type == iris::SceneNodeType::Mesh || type == iris::SceneNodeType::Empty)
        && !node->isBuiltIn)
        return ModelTypes::Object;
    if (type == iris::SceneNodeType::ParticleSystem) return ModelTypes::ParticleSystem;
    return ModelTypes::Undefined;
}

}   // namespace nodeexport

#endif // NODEEXPORT_H
