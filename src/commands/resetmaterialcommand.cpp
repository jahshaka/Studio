/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/resetmaterialcommand.h"

#include <QObject>

#include "data/constants.h"
#include "data/database/database.h"
#include "irisgl/document/materials/material.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "services/projectassets.h"
#include "services/projectmembership.h"

namespace {

/// The kinds of edge that mean "this node uses that material / texture" —
/// the only ones a reset clears.
bool isMaterialUse(const DependencyRecord &edge)
{
    return edge.dependeeType == static_cast<int>(ModelTypes::Material)
           || edge.dependeeType == static_cast<int>(ModelTypes::Shader)
           || edge.dependeeType == static_cast<int>(ModelTypes::Texture);
}

}   // namespace

ResetMaterialCommand::ResetMaterialCommand(Database *db, Project *project,
                                           iris::MeshNodePtr meshNode,
                                           iris::MaterialPtr defaultMaterial,
                                           const QStringList &defaultTextures,
                                           const QStringList &newlyPinned)
    : db(db), project(project), meshNode(meshNode),
      oldMaterial(meshNode ? meshNode->getMaterial() : iris::MaterialPtr()),
      defaultMaterial(defaultMaterial), defaultTextures(defaultTextures),
      newlyPinned(newlyPinned)
{
    setText(QObject::tr("Reset Material"));
}

void ResetMaterialCommand::redo()
{
    if (!meshNode) return;
    meshNode->setMaterial(defaultMaterial);

    droppedEdges.clear();
    createdDefaultEdges.clear();
    const QString projectGuid = project ? project->getProjectGuid() : QString();
    if (!db || projectGuid.isEmpty()) return;
    const QString nodeGuid = meshNode->getGUID();

    // The default's own pins, back after an undo took them (the first redo
    // runs right after the default was built, which pinned them already).
    if (!newlyPinnedLive) {
        for (const QString &texture : newlyPinned)
            if (!db->isAssetPinnedBy(projectGuid, texture))
                ProjectAssets::addToProject(texture, db, project, ProjectAssets::AddKind::Binding);
        newlyPinnedLive = true;
    }

    // What the node used: material / shader / texture edges only, minus the
    // default's own textures (those stay — they are what it uses now).
    for (const DependencyRecord &edge : db->fetchNodeDependencies(nodeGuid, projectGuid))
        if (isMaterialUse(edge) && !defaultTextures.contains(edge.dependee))
            droppedEdges.append(edge);
    for (const DependencyRecord &edge : droppedEdges) db->deleteDependency(nodeGuid, edge.dependee);

    // ...and the default's textures are recorded as used, remembering which of
    // those edges THIS redo made, so undo takes back exactly those.
    for (const QString &texture : defaultTextures) {
        if (db->checkIfDependencyExists(nodeGuid, texture)) continue;
        db->createDependency(static_cast<int>(ModelTypes::Object),
                             static_cast<int>(ModelTypes::Texture), nodeGuid, texture, projectGuid);
        createdDefaultEdges.append(texture);
    }
}

void ResetMaterialCommand::undo()
{
    if (!meshNode) return;
    meshNode->setMaterial(oldMaterial);

    const QString projectGuid = project ? project->getProjectGuid() : QString();
    if (!db || projectGuid.isEmpty()) return;
    const QString nodeGuid = meshNode->getGUID();

    for (const QString &texture : createdDefaultEdges) db->deleteDependency(nodeGuid, texture);
    // Every dropped row again (redo deleted by pair, so a duplicated pair went
    // in one delete and comes back as the same number of rows).
    for (const DependencyRecord &edge : droppedEdges)
        db->createDependency(edge.dependerType, edge.dependeeType, nodeGuid, edge.dependee,
                             projectGuid);
    createdDefaultEdges.clear();
    droppedEdges.clear();

    // The pins building the default minted: the project did not have them.
    // Only the pin row goes (never the project-remove path): the library row
    // and its bytes stay, and redo pins it again.
    if (newlyPinnedLive && !newlyPinned.isEmpty()) {
        for (const QString &texture : newlyPinned) db->unpinAsset(projectGuid, texture);
        newlyPinnedLive = false;
        ProjectMembership::instance()->announce(projectGuid);
    }
}
