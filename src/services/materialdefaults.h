/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALDEFAULTS_H
#define MATERIALDEFAULTS_H

// A NODE'S OWN DEFAULT MATERIAL — what `material.reset(nodeId)` and the
// material panel's reset restore (owner, 2026-09-12; modelled on Unreal's
// per-slot "reset to default", which falls back to the OBJECT's own default
// rather than to a stored override layer — there is no override layer in the
// document and this adds none).
//
// A node HAS a default only when something provides one. The default floor
// (services/defaultfloor.h) is the first and, today, only provider; every
// other node has none, and a reset of it changes nothing and says so.

#include <QString>
#include <QStringList>

#include "irisgl/irisglfwd.h"
#include "services/defaultfloor.h"

class Database;
class Project;

namespace materialdefaults {

/// Whether `node` has a default material of its own. (Inline, like the name
/// below: the material panel asks without linking the factories.)
inline bool hasDefault(const iris::SceneNodePtr &node)
{
    return defaultfloor::isDefaultFloor(node);
}

/// The provider's name for the UI ("Default Floor"); empty when there is none.
inline QString providerName(const iris::SceneNodePtr &node)
{
    return defaultfloor::isDefaultFloor(node) ? QStringLiteral("Default Floor") : QString();
}

/// A FRESH instance of `node`'s own default material, or null when it has
/// none. `pinnedTextures` receives the texture rows the default pinned into
/// the project (the floor's checker), which the node then uses.
iris::MaterialPtr create(const iris::SceneNodePtr &node, Database *db, Project *project,
                         QStringList *pinnedTextures = nullptr);

}   // namespace materialdefaults

#endif   // MATERIALDEFAULTS_H
